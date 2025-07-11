#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "scheduler.h"
#include "parser.h"
#include "executor.h"
#include "pipes.h"
#include <sys/socket.h>

#define FIRST_ROUND_QUANTUM 3
#define OTHER_ROUNDS_QUANTUM 7
#define MAX_TASKS 100
#define BUFFER_SIZE 4096

// global variables
static task_queue_t *task_queue = NULL;
static pthread_t scheduler_thread;
static int scheduler_running = 0;
static int next_task_id = 1;

// color definitions
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_RESET   "\033[0m"

// forward declarations of internal functions
static void print_task_summary(void);
static void send_to_client(task_t *task, const char *output, int send_prompt);
void *scheduler_thread_func(void *arg);

// prints the blue summary of tasks in format [client_id]-[remaining_time]
static void print_task_summary(void) {
    pthread_mutex_lock(&task_queue->lock);
    printf(COLOR_BLUE);
    printf("[");
    for (int i = 0; i < task_queue->size; i++) {
        printf("[%d]-[%d]", task_queue->tasks[i]->client_id, 
               task_queue->tasks[i]->remaining_time);
        if (i < task_queue->size - 1) printf("-");
    }
    printf("]\n" COLOR_RESET);
    pthread_mutex_unlock(&task_queue->lock);
}

// sends output to client and tracks bytes sent
static void send_to_client(task_t *task, const char *output, int send_prompt) {
    if (!output || !task) return;
    ssize_t bytes_to_send = strlen(output);
    ssize_t sent_bytes = send(task->client_socket, output, bytes_to_send, 0);
    
    if (sent_bytes > 0) {
        task->bytes_sent += sent_bytes;
    }
    
    if (send_prompt) {
        const char *prompt = "$ ";
        send(task->client_socket, prompt, strlen(prompt), 0);
    }
}

// initialize the scheduler
void scheduler_init(void) {
    task_queue = malloc(sizeof(task_queue_t));
    if (!task_queue) {
        perror("malloc failed for task queue");
        exit(EXIT_FAILURE);
    }
    
    task_queue->tasks = malloc(MAX_TASKS * sizeof(task_t *));
    if (!task_queue->tasks) {
        free(task_queue);
        perror("malloc failed for tasks array");
        exit(EXIT_FAILURE);
    }
    
    task_queue->capacity = MAX_TASKS;
    task_queue->size = 0;
    task_queue->current_round = 1;
    
    if (pthread_mutex_init(&task_queue->lock, NULL) != 0) {
        free(task_queue->tasks);
        free(task_queue);
        perror("mutex init failed");
        exit(EXIT_FAILURE);
    }
    
    if (pthread_cond_init(&task_queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&task_queue->lock);
        free(task_queue->tasks);
        free(task_queue);
        perror("condition variable init failed");
        exit(EXIT_FAILURE);
    }
    
    scheduler_start();
}

// clean up scheduler resources
void scheduler_cleanup(void) {
    if (task_queue) {
        for (int i = 0; i < task_queue->size; i++) {
            if (task_queue->tasks[i]) {
                free(task_queue->tasks[i]->command);
                free(task_queue->tasks[i]);
            }
        }
        pthread_mutex_destroy(&task_queue->lock);
        pthread_cond_destroy(&task_queue->not_empty);
        free(task_queue->tasks);
        free(task_queue);
        task_queue = NULL;
    }
}

// start the scheduler thread
void scheduler_start(void) {
    scheduler_running = 1;
    if (pthread_create(&scheduler_thread, NULL, scheduler_thread_func, NULL) != 0) {
        perror("failed to create scheduler thread");
        exit(EXIT_FAILURE);
    }
}

// stop the scheduler thread
void scheduler_stop(void) {
    scheduler_running = 0;
    pthread_cond_signal(&task_queue->not_empty);
    pthread_join(scheduler_thread, NULL);
}

// add a task to the scheduler queue
void scheduler_add_task(int client_id, int client_socket, const char *command, int type, int exec_time) {
    pthread_mutex_lock(&task_queue->lock);
    
    if (task_queue->size >= task_queue->capacity) {
        printf("task queue is full\n");
        pthread_mutex_unlock(&task_queue->lock);
        return;
    }
    
    task_t *task = malloc(sizeof(task_t));
    if (!task) {
        perror("malloc failed");
        pthread_mutex_unlock(&task_queue->lock);
        return;
    }
    // initialize task properties
    task->id = next_task_id++;
    task->client_id = client_id;
    task->client_socket = client_socket;
    task->type = type;
    task->command = strdup(command);
    task->total_time = exec_time;
    task->remaining_time = exec_time;
    task->state = TASK_STATE_WAITING;
    task->round = 1;
    task->last_executed = 0;
    task->arrival_time = time(NULL);
    task->preempted = 0;
    task->bytes_sent = 0;
    // add task to queue
    task_queue->tasks[task_queue->size++] = task;
    
    printf("[%d]>>> %s\n", client_id, command);
    printf("[%d]--- " COLOR_GREEN "created" COLOR_RESET " (%d)\n", 
           client_id, type == TASK_SHELL_COMMAND ? -1 : exec_time);
    // notify scheduler that a new task is available
    pthread_cond_signal(&task_queue->not_empty);
    pthread_mutex_unlock(&task_queue->lock);
}

// get next task based on scheduling algorithm
task_t *scheduler_get_next_task(void) {
    pthread_mutex_lock(&task_queue->lock);
    // wait until there is a task available
    while (task_queue->size == 0) {
        pthread_cond_wait(&task_queue->not_empty, &task_queue->lock);
    }
    
    task_t *selected_task = NULL;
    
    // first priority: shell commands get highest priority
    for (int i = 0; i < task_queue->size; i++) {
        if (task_queue->tasks[i]->type == TASK_SHELL_COMMAND && 
            task_queue->tasks[i]->state == TASK_STATE_WAITING) {
            selected_task = task_queue->tasks[i];
            break;
        }
    }
    
    // second priority: shortest remaining time first (sjrf)
    if (!selected_task) {
        int shortest_time = -1;
        for (int i = 0; i < task_queue->size; i++) {
            task_t *task = task_queue->tasks[i];
            if (task->state == TASK_STATE_WAITING) {
                // prevent consecutive execution unless it's the only task
                if (task->last_executed && task_queue->size > 1) continue;
                
                if (shortest_time == -1 || task->remaining_time < shortest_time) {
                    shortest_time = task->remaining_time;
                    selected_task = task;
                }
            }
        }
    }
    // third priority: round robin for remaining tasks
    if (selected_task) {
        selected_task->state = TASK_STATE_RUNNING;
        for (int i = 0; i < task_queue->size; i++) {
            task_queue->tasks[i]->last_executed = 0;
        }
        selected_task->last_executed = 1;
        
        printf("[%d]--- " COLOR_GREEN "started" COLOR_RESET " (%d)\n", 
               selected_task->client_id, 
               selected_task->type == TASK_SHELL_COMMAND ? -1 : selected_task->remaining_time);
    }
    // remove the selected task from the queue
    pthread_mutex_unlock(&task_queue->lock);
    return selected_task;
}

// mark a task as completed and remove it from queue
void scheduler_complete_task(task_t *task) {
    pthread_mutex_lock(&task_queue->lock);
    
    int task_index = -1;
    for (int i = 0; i < task_queue->size; i++) {
        if (task_queue->tasks[i] == task) {
            task_index = i;
            break;
        }
    }
    
    if (task_index != -1) {
        task->state = TASK_STATE_COMPLETED;
        printf("[%d]--- " COLOR_RED "ended" COLOR_RESET " (%d)\n", 
               task->client_id, task->type == TASK_SHELL_COMMAND ? -1 : task->remaining_time);
        
        free(task->command);
        free(task);
        
        for (int i = task_index; i < task_queue->size - 1; i++) {
            task_queue->tasks[i] = task_queue->tasks[i + 1];
        }
        task_queue->size--;
    }
    
    pthread_mutex_unlock(&task_queue->lock);
}

// remove all tasks belonging to a specific client
void scheduler_remove_client_tasks(int client_id) {
    pthread_mutex_lock(&task_queue->lock);
    
    int i = 0;
    while (i < task_queue->size) {
        if (task_queue->tasks[i]->client_id == client_id) {
            free(task_queue->tasks[i]->command);
            free(task_queue->tasks[i]);
            
            for (int j = i; j < task_queue->size - 1; j++) {
                task_queue->tasks[j] = task_queue->tasks[j + 1];
            }
            task_queue->size--;
        } else {
            i++;
        }
    }
    
    pthread_mutex_unlock(&task_queue->lock);
}

// update task state after execution
void scheduler_update_task(task_t *task, int time_executed) {
    pthread_mutex_lock(&task_queue->lock);
    
    if (task->type == TASK_PROGRAM) {
        task->remaining_time -= time_executed;
        if (task->remaining_time > 0) {
            task->state = TASK_STATE_WAITING;
            task->round++;
            task->preempted = 1;
            printf("[%d]--- " COLOR_YELLOW "waiting" COLOR_RESET " (%d)\n", 
                   task->client_id, task->remaining_time);
        }
    }
    
    pthread_mutex_unlock(&task_queue->lock);
}

// main scheduler thread implementation
void *scheduler_thread_func(void *arg) {
    (void)arg; // unused parameter
    
    while (scheduler_running) {
        // wait for tasks to be available
        task_t *task = scheduler_get_next_task();
        if (!task) continue;

        int quantum = (task->round == 1) ? FIRST_ROUND_QUANTUM : OTHER_ROUNDS_QUANTUM;
        // handle shell commands and programs differently
        if (task->type == TASK_SHELL_COMMAND) {
            int stdout_backup = dup(STDOUT_FILENO);
            int stderr_backup = dup(STDERR_FILENO);
            int pipefd[2];
            pipe(pipefd);
             // redirect stdout and stderr to the pipe
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            
            Command *cmd = parse_command(task->command);
            if (cmd) {
                execute_command(cmd);
                free_command(cmd);
            }
            // flush the pipe to ensure all output is sent
            fflush(stdout);
            fflush(stderr);
            
            dup2(stdout_backup, STDOUT_FILENO);
            dup2(stderr_backup, STDERR_FILENO);
            close(stdout_backup);
            close(stderr_backup);
            
            char buffer[BUFFER_SIZE] = {0};
            ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
            close(pipefd[0]);
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                send_to_client(task, buffer, 1);
            } else {
                send_to_client(task, "", 1);
            }
            
            printf("[%d]<<< %zu bytes sent\n", task->client_id, task->bytes_sent);
            scheduler_complete_task(task);
            print_task_summary();
            
        } else if (task->type == TASK_PROGRAM) {
            int time_to_execute = (task->remaining_time < quantum) ? 
                                 task->remaining_time : quantum;
            
            if (task->preempted) {
                printf("[%d]--- " COLOR_BLUE "running" COLOR_RESET " (%d)\n", 
                      task->client_id, task->remaining_time);
                task->preempted = 0;
            }
            // simulate program execution by sleeping
            char buffer[BUFFER_SIZE] = {0};
            for (int i = 0; i < time_to_execute; i++) {
                char line[64];
                snprintf(line, sizeof(line), "Demo %d/%d\n", 
                        task->total_time - task->remaining_time + i + 1, 
                        task->total_time);
                strcat(buffer, line);
                sleep(1);
            }
            // send the output to the client
            send_to_client(task, buffer, 0);
            scheduler_update_task(task, time_to_execute);
            
            if (task->remaining_time <= 0) {
                printf("[%d]<<< %zu bytes sent\n", task->client_id, task->bytes_sent);
                send_to_client(task, "", 1);
                scheduler_complete_task(task);
                print_task_summary();
            }
        }
    }
    return NULL;
}