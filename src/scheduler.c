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

// quantum timing for round robin scheduling
#define FIRST_ROUND_QUANTUM 3   // first round gets 3 seconds
#define OTHER_ROUNDS_QUANTUM 7  // subsequent rounds get 7 seconds
#define MAX_TASKS 100
#define BUFFER_SIZE 4096

// global scheduler variables
static task_queue_t *task_queue = NULL;
static pthread_t scheduler_thread;
static int scheduler_running = 0;
static int next_task_id = 1;

// color codes for output formatting
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_RESET   "\033[0m"

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

// get next task based on scheduling algorithm
task_t *scheduler_get_next_task(void) {
    pthread_mutex_lock(&task_queue->lock);
    
    while (task_queue->size == 0) {
        pthread_cond_wait(&task_queue->not_empty, &task_queue->lock);
    }
    
    task_t *selected_task = NULL;
    
    // part 1: shell commands get highest priority (always run first)
    for (int i = 0; i < task_queue->size; i++) {
        if (task_queue->tasks[i]->type == TASK_SHELL_COMMAND && 
            task_queue->tasks[i]->state == TASK_STATE_WAITING) {
            selected_task = task_queue->tasks[i];
            break;
        }
    }
    
    // part 2: sjrf (shortest job remaining first) implementation
    if (!selected_task) {
        int shortest_time = -1;
        for (int i = 0; i < task_queue->size; i++) {
            task_t *task = task_queue->tasks[i];
            if (task->state == TASK_STATE_WAITING) {
                // implement consecutive restriction (part of rr)
                if (task->last_executed && task_queue->size > 1) continue;
                
                // select shortest remaining time
                if (shortest_time == -1 || task->remaining_time < shortest_time) {
                    shortest_time = task->remaining_time;
                    selected_task = task;
                }
            }
        }
    }
    
    if (selected_task) {
        selected_task->state = TASK_STATE_RUNNING;
        // reset last_executed flags (part of rr implementation)
        for (int i = 0; i < task_queue->size; i++) {
            task_queue->tasks[i]->last_executed = 0;
        }
        selected_task->last_executed = 1;
        
        printf("[%d]--- " COLOR_GREEN "started" COLOR_RESET " (%d)\n", 
               selected_task->client_id, 
               selected_task->type == TASK_SHELL_COMMAND ? -1 : selected_task->remaining_time);
    }
    
    pthread_mutex_unlock(&task_queue->lock);
    return selected_task;
}

// update task state after execution
void scheduler_update_task(task_t *task, int time_executed) {
    pthread_mutex_lock(&task_queue->lock);
    
    if (task->type == TASK_PROGRAM) {
        task->remaining_time -= time_executed;
        if (task->remaining_time > 0) {
            // task still has work - move to waiting state
            task->state = TASK_STATE_WAITING;
            task->round++;  // increment round for rr quantum
            task->preempted = 1;
            printf("[%d]--- " COLOR_YELLOW "waiting" COLOR_RESET " (%d)\n", 
                   task->client_id, task->remaining_time);
        }
    }
    
    pthread_mutex_unlock(&task_queue->lock);
}

// main scheduler thread implementation
void *scheduler_thread_func(void *arg) {
    while (scheduler_running) {
        task_t *task = scheduler_get_next_task();
        if (!task) continue;

        // implement rr quantum based on round number
        int quantum = (task->round == 1) ? FIRST_ROUND_QUANTUM : OTHER_ROUNDS_QUANTUM;

        if (task->type == TASK_SHELL_COMMAND) {
            // handle shell commands by capturing output
            int stdout_backup = dup(STDOUT_FILENO);
            int stderr_backup = dup(STDERR_FILENO);
            int pipefd[2];
            pipe(pipefd);
            
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            
            Command *cmd = parse_command(task->command);
            if (cmd) {
                execute_command(cmd);
                free_command(cmd);
            }
            
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
            // handle program execution with quantum time slicing
            int time_to_execute = (task->remaining_time < quantum) ? 
                                 task->remaining_time : quantum;
            
            if (task->preempted) {
                printf("[%d]--- " COLOR_BLUE "running" COLOR_RESET " (%d)\n", 
                      task->client_id, task->remaining_time);
                task->preempted = 0;
            }
            
            // simulate program execution with output
            char buffer[BUFFER_SIZE] = {0};
            for (int i = 0; i < time_to_execute; i++) {
                char line[64];
                snprintf(line, sizeof(line), "Demo %d/%d\n", 
                        task->total_time - task->remaining_time + i + 1, 
                        task->total_time);
                strcat(buffer, line);
                sleep(1);
            }
            
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
                task_queue->tasks[i]->bytes_sent = sent_bytes; // Just set it directly
                
                // Print the bytes sent here immediately
                printf("[%d]<<< %zu bytes sent\n", client_id, sent_bytes);
                break;
            }
        }
        pthread_mutex_unlock(&task_queue->lock);
    }
    
    // Add a prompt
    const char *prompt = "$ ";
    sent_bytes = send(client_socket, prompt, strlen(prompt), 0);
    if (sent_bytes > 0) {
        // We don't need to track these bytes for the display
        // as they are just the prompt, not the actual content
    }
    
    // after all iterations complete, print the summary
    print_queue_summary();
}