#define _POSIX_C_SOURCE 200809L  // Add this for strdup
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
#include <sys/socket.h>  // Add this for send function

// define scheduler constants
#define FIRST_ROUND_QUANTUM 3  // quantum for first round (in seconds)
#define OTHER_ROUNDS_QUANTUM 7 // quantum for subsequent rounds (in seconds)
#define MAX_TASKS 100          // maximum number of tasks in the queue

// global scheduler variables
static task_queue_t *task_queue = NULL;
static pthread_t scheduler_thread;
static int scheduler_running = 0;
static int next_task_id = 1;

// color codes for output
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_RESET   "\033[0m"

// initialize the scheduler
void scheduler_init(void) {
    // allocate the task queue
    task_queue = malloc(sizeof(task_queue_t));
    if (!task_queue) {
        perror("failed to allocate task queue");
        exit(EXIT_FAILURE);
    }
    
    // initialize task queue fields
    task_queue->tasks = malloc(MAX_TASKS * sizeof(task_t *));
    if (!task_queue->tasks) {
        perror("failed to allocate tasks array");
        free(task_queue);
        exit(EXIT_FAILURE);
    }
    
    task_queue->capacity = MAX_TASKS;
    task_queue->size = 0;
    task_queue->current_round = 1;
    
    // initialize synchronization primitives
    if (pthread_mutex_init(&task_queue->lock, NULL) != 0) {
        perror("mutex init failed");
        free(task_queue->tasks);
        free(task_queue);
        exit(EXIT_FAILURE);
    }
    
    if (pthread_cond_init(&task_queue->not_empty, NULL) != 0) {
        perror("condition variable init failed");
        pthread_mutex_destroy(&task_queue->lock);
        free(task_queue->tasks);
        free(task_queue);
        exit(EXIT_FAILURE);
    }
}

// clean up scheduler resources
void scheduler_cleanup(void) {
    if (task_queue) {
        // free all tasks
        for (int i = 0; i < task_queue->size; i++) {
            if (task_queue->tasks[i]) {
                free(task_queue->tasks[i]->command);
                free(task_queue->tasks[i]);
            }
        }
        
        // destroy synchronization primitives
        pthread_mutex_destroy(&task_queue->lock);
        pthread_cond_destroy(&task_queue->not_empty);
        
        // free the tasks array and queue
        free(task_queue->tasks);
        free(task_queue);
        task_queue = NULL;
    }
}

// add a task to the queue
void scheduler_add_task(int client_id, int client_socket, const char *command, int type, int exec_time) {
    pthread_mutex_lock(&task_queue->lock);
    
    // check if we have space for a new task
    if (task_queue->size >= task_queue->capacity) {
        printf("task queue is full, cannot add more tasks\n");
        pthread_mutex_unlock(&task_queue->lock);
        return;
    }
    
    // create a new task
    task_t *task = malloc(sizeof(task_t));
    if (!task) {
        perror("failed to allocate task");
        pthread_mutex_unlock(&task_queue->lock);
        return;
    }
    
    // initialize task fields
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
    task->bytes_sent = 0;  // Initialize bytes sent to 0
    
    // add the task to the queue
    task_queue->tasks[task_queue->size++] = task;
    
    // print task creation message
    if (type == TASK_SHELL_COMMAND) {
        printf("[%d]>>> %s\n", client_id, command);
        printf("[%d]--- " COLOR_GREEN "created" COLOR_RESET " (-1)\n", client_id);
    } else {
        printf("[%d]>>> %s\n", client_id, command);
        printf("[%d]--- " COLOR_GREEN "created" COLOR_RESET " (%d)\n", client_id, exec_time);
    }
    
    // signal that the queue is not empty
    pthread_cond_signal(&task_queue->not_empty);
    pthread_mutex_unlock(&task_queue->lock);
}

// get the next task to execute based on the scheduling algorithm
task_t *scheduler_get_next_task(void) {
    pthread_mutex_lock(&task_queue->lock);
    
    // wait until there is at least one task in the queue
    while (task_queue->size == 0) {
        pthread_cond_wait(&task_queue->not_empty, &task_queue->lock);
    }
    
    task_t *selected_task = NULL;
    
    // first, check for shell commands which have highest priority
    for (int i = 0; i < task_queue->size; i++) {
        if (task_queue->tasks[i]->type == TASK_SHELL_COMMAND && 
            task_queue->tasks[i]->state == TASK_STATE_WAITING) {
            selected_task = task_queue->tasks[i];
            break;
        }
    }
    
    // if no shell command, use SJRF (Shortest Job Remaining First)
    if (!selected_task) {
        int shortest_time = -1;
        
        for (int i = 0; i < task_queue->size; i++) {
            task_t *task = task_queue->tasks[i];
            
            // only consider waiting tasks
            if (task->state == TASK_STATE_WAITING) {
                // avoid selecting the same task consecutively unless it's the only one
                if (task->last_executed && task_queue->size > 1) {
                    continue;
                }
                
                // select the task with shortest remaining time
                if (shortest_time == -1 || task->remaining_time < shortest_time) {
                    shortest_time = task->remaining_time;
                    selected_task = task;
                }
                // if remaining times are equal, use FCFS (first come, first served)
                else if (task->remaining_time == shortest_time && 
                         task->arrival_time < selected_task->arrival_time) {
                    selected_task = task;
                }
            }
        }
    }
    
    // if we found a task, mark it as running
    if (selected_task) {
        selected_task->state = TASK_STATE_RUNNING;
        
        // reset last_executed flag for all tasks
        for (int i = 0; i < task_queue->size; i++) {
            task_queue->tasks[i]->last_executed = 0;
        }
        
        // mark the selected task as last executed
        selected_task->last_executed = 1;
        
        // print status message
        printf("[%d]--- " COLOR_GREEN "started" COLOR_RESET " ", selected_task->client_id);
        
        if (selected_task->type == TASK_SHELL_COMMAND) {
            printf("(-1)\n");
        } else {
            printf("(%d)\n", selected_task->remaining_time);
        }
    }
    
    pthread_mutex_unlock(&task_queue->lock);
    return selected_task;
}

// update a task's remaining time and status
void scheduler_update_task(task_t *task, int time_executed) {
    pthread_mutex_lock(&task_queue->lock);
    
    if (task->type == TASK_PROGRAM) {
        task->remaining_time -= time_executed;
        
        // if the task was preempted
        if (task->remaining_time > 0) {
            task->state = TASK_STATE_WAITING;
            task->round++;
            task->preempted = 1;
            
            // print waiting status
            printf("[%d]--- " COLOR_YELLOW "waiting" COLOR_RESET " (%d)\n", 
                   task->client_id, task->remaining_time);
        }
    }
    
    pthread_mutex_unlock(&task_queue->lock);
}

// mark a task as completed and remove it from the queue
void scheduler_complete_task(task_t *task) {
    pthread_mutex_lock(&task_queue->lock);
    
    // find the task in the queue
    int task_index = -1;
    for (int i = 0; i < task_queue->size; i++) {
        if (task_queue->tasks[i] == task) {
            task_index = i;
            break;
        }
    }
    
    if (task_index != -1) {
        // mark the task as completed
        task->state = TASK_STATE_COMPLETED;
        
        // print completion message
        printf("[%d]--- " COLOR_RED "ended" COLOR_RESET " ", task->client_id);
        
        if (task->type == TASK_SHELL_COMMAND) {
            printf("(-1)\n");
        } else {
            printf("(%d)\n", task->remaining_time);
        }
        
        // remove the task from the queue by shifting all subsequent tasks
        free(task->command);
        free(task);
        
        for (int i = task_index; i < task_queue->size - 1; i++) {
            task_queue->tasks[i] = task_queue->tasks[i + 1];
        }
        
        task_queue->size--;
    }
    
    pthread_mutex_unlock(&task_queue->lock);
}

// remove all tasks for a specific client
void scheduler_remove_client_tasks(int client_id) {
    pthread_mutex_lock(&task_queue->lock);
    
    int i = 0;
    while (i < task_queue->size) {
        if (task_queue->tasks[i]->client_id == client_id) {
            // free task resources
            free(task_queue->tasks[i]->command);
            free(task_queue->tasks[i]);
            
            // shift remaining tasks
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

// main scheduler thread function
void *scheduler_thread_func(void *arg) {
    (void)arg; // unused parameter
    
    while (scheduler_running) {
        // get the next task to execute
        task_t *task = scheduler_get_next_task();
        
        if (task) {
            // determine the quantum based on the round
            int quantum = (task->round == 1) ? FIRST_ROUND_QUANTUM : OTHER_ROUNDS_QUANTUM;
            
            // execute the task based on its type
            if (task->type == TASK_SHELL_COMMAND) {
                // for shell commands, we need to capture output to send back to client
                
                // save the current stdout and stderr
                int stdout_backup = dup(STDOUT_FILENO);
                int stderr_backup = dup(STDERR_FILENO);
                
                // create a pipe for capturing command output
                int pipefd[2];
                if (pipe(pipefd) == -1) {
                    perror("pipe");
                    scheduler_complete_task(task);
                    continue;
                }
                
                // redirect stdout and stderr to the pipe
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]); // close the write end in this process
                
                // parse and execute the command
                Command *cmd = parse_command(task->command);
                if (cmd) {
                    execute_command(cmd);
                    free_command(cmd);
                }
                
                // flush to ensure all output goes to the pipe
                fflush(stdout);
                fflush(stderr);
                
                // restore the original stdout and stderr
                dup2(stdout_backup, STDOUT_FILENO);
                dup2(stderr_backup, STDERR_FILENO);
                close(stdout_backup);
                close(stderr_backup);
                
                // read the captured output
                char buffer[4096] = {0};
                ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
                close(pipefd[0]);
                
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    
                    // send output back to the client
                    ssize_t sent_bytes = send(task->client_socket, buffer, bytes_read, 0);
                    if (sent_bytes < 0) {
                        perror("send");
                    } else {
                        task->bytes_sent += sent_bytes;
                    }
                    
                    // Send an additional newline and prompt
                    const char *prompt = "\n$ ";
                    sent_bytes = send(task->client_socket, prompt, strlen(prompt), 0);
                    if (sent_bytes > 0) {
                        task->bytes_sent += sent_bytes;
                    }
                } else {
                    // If no output, at least send a prompt back to client
                    const char *prompt = "$ ";
                    ssize_t sent_bytes = send(task->client_socket, prompt, strlen(prompt), 0);
                    if (sent_bytes > 0) {
                        task->bytes_sent += sent_bytes;
                    }
                }
                
                // Print the bytes sent
                printf("[%d]<<< %zu bytes sent\n", task->client_id, task->bytes_sent);
                
                // mark the task as completed
                scheduler_complete_task(task);
            } else if (task->type == TASK_PROGRAM) {
                // for programs, execute for the quantum or until completion
                int time_to_execute = (task->remaining_time < quantum) ? 
                                     task->remaining_time : quantum;
                
                // print running status
                if (task->preempted) {
                    printf("[%d]--- " COLOR_BLUE "running" COLOR_RESET " (%d)\n", 
                          task->client_id, task->remaining_time);
                    task->preempted = 0;
                }
                
                // simulate execution time with sleep
                sleep(time_to_execute);
                
                // update the task's status
                scheduler_update_task(task, time_to_execute);
                
                // if the task is completed
                if (task->remaining_time <= 0) {
                    // Print the bytes sent - this should have been tracked when the demo output was sent
                    printf("[%d]<<< %zu bytes sent\n", task->client_id, task->bytes_sent);
                    
                    // Send prompt back to the client
                    const char *prompt = "$ ";
                    send(task->client_socket, prompt, strlen(prompt), 0);
                    
                    // Mark the task as complete
                    scheduler_complete_task(task);
                }
            }
        }
    }
    
    return NULL;
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
    pthread_cond_signal(&task_queue->not_empty); // wake up the thread if it's waiting
    pthread_join(scheduler_thread, NULL);
}

// estimate execution time for a command
int estimate_execution_time(const char *command) {
    // check if this is a ./demo command with a specific time
    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char *token = strtok(cmd_copy, " ");
    if (token && (strcmp(token, "./demo") == 0 || strcmp(token, "demo") == 0)) {
        token = strtok(NULL, " ");
        if (token) {
            return atoi(token);
        }
    }
    
    // default value for unknown commands
    return 5;
}

// print the queue status summary (blue highlighted line)
void print_queue_summary(void) {
    pthread_mutex_lock(&task_queue->lock);
    
    printf(COLOR_BLUE);
    printf("[");
    
    // print each task's client ID and remaining time
    for (int i = 0; i < task_queue->size; i++) {
        task_t *task = task_queue->tasks[i];
        printf("[%d]-[%d]", task->client_id, task->remaining_time);
        if (i < task_queue->size - 1) {
            printf("-");
        }
    }
    
    printf("]\n");
    printf(COLOR_RESET);
    
    pthread_mutex_unlock(&task_queue->lock);
}

// execute demo program which simulates work
void execute_demo_program(const char *command, int client_socket, int n, int client_id) {
    // extract the value of n from the command if not provided
    int iterations = n;
    if (iterations <= 0) {
        char cmd_copy[256];
        strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';
        
        char *token = strtok(cmd_copy, " ");
        token = strtok(NULL, " "); // get the second part which should be n
        if (token) {
            iterations = atoi(token);
        } else {
            iterations = 5; // default value
        }
    }
    
    // add the task to the scheduler
    scheduler_add_task(client_id, client_socket, command, TASK_PROGRAM, iterations);
    
    // prepare response for client
    char response[4096] = {0};
    for (int i = 1; i <= iterations; i++) {
        char line[64];
        snprintf(line, sizeof(line), "Demo %d/%d\n", i, iterations);
        strcat(response, line);
    }
    
    // calculate bytes to send
    size_t bytes_to_send = strlen(response);
    
    // send the response to the client
    ssize_t sent_bytes = send(client_socket, response, bytes_to_send, 0);
    if (sent_bytes < 0) {
        perror("send");
    } else {
        // Update the task's bytes_sent value
        pthread_mutex_lock(&task_queue->lock);
        for (int i = 0; i < task_queue->size; i++) {
            if (task_queue->tasks[i]->client_id == client_id && 
                strcmp(task_queue->tasks[i]->command, command) == 0) {
                task_queue->tasks[i]->bytes_sent += sent_bytes;
                break;
            }
        }
        pthread_mutex_unlock(&task_queue->lock);
    }
    
    // Add a prompt
    const char *prompt = "$ ";
    sent_bytes = send(client_socket, prompt, strlen(prompt), 0);
    if (sent_bytes > 0) {
        // Update the bytes sent for this task
        pthread_mutex_lock(&task_queue->lock);
        for (int i = 0; i < task_queue->size; i++) {
            if (task_queue->tasks[i]->client_id == client_id && 
                strcmp(task_queue->tasks[i]->command, command) == 0) {
                task_queue->tasks[i]->bytes_sent += sent_bytes;
                break;
            }
        }
        pthread_mutex_unlock(&task_queue->lock);
    }
    
    // after all iterations complete, print the summary
    print_queue_summary();
}