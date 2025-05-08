#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "scheduler.h"
#include "parser.h"
#include "executor.h"
#include "pipes.h"

// Define colors for console output
#define COLOR_BLUE "\033[1;34m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_RED "\033[1;31m"
#define COLOR_RESET "\033[0m"

// Maximum size of input and output buffers
#define MAX_INPUT_SIZE 1024
#define MAX_OUTPUT_SIZE 4096

// Quantum times for different rounds
#define QUANTUM_FIRST_ROUND 3  // 3 seconds for first round
#define QUANTUM_OTHER_ROUNDS 7 // 7 seconds for subsequent rounds

// Global variables
static TaskQueue task_queue;
static pthread_t scheduler_thread;
static volatile bool scheduler_running = false;
static int task_id_counter = 0;
static pthread_mutex_t task_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static Task *current_running_task = NULL;
static pthread_mutex_t running_task_mutex = PTHREAD_MUTEX_INITIALIZER;
static int last_selected_task_id = -1;

// Function to get a new unique task ID
static int get_next_task_id() {
    pthread_mutex_lock(&task_id_mutex);
    int id = ++task_id_counter;
    pthread_mutex_unlock(&task_id_mutex);
    return id;
}

// Initialize the scheduler system
void scheduler_init() {
    task_queue.head = NULL;
    task_queue.tail = NULL;
    task_queue.size = 0;
    pthread_mutex_init(&task_queue.mutex, NULL);
    scheduler_running = false;
    current_running_task = NULL;
    last_selected_task_id = -1;
}

// Clean up the scheduler system
void scheduler_cleanup() {
    pthread_mutex_lock(&task_queue.mutex);
    
    // Free all tasks in the queue
    Task *current = task_queue.head;
    while (current != NULL) {
        Task *next = current->next;
        free(current->command);
        free(current);
        current = next;
    }
    
    task_queue.head = NULL;
    task_queue.tail = NULL;
    task_queue.size = 0;
    
    pthread_mutex_unlock(&task_queue.mutex);
    pthread_mutex_destroy(&task_queue.mutex);
}

// Add a new task to the queue
Task* scheduler_add_task(int client_id, int client_socket, const char *command, 
                        const char *client_ip, int client_port) {
    // Create a new task
    Task *task = (Task *)malloc(sizeof(Task));
    if (!task) {
        perror("malloc");
        return NULL;
    }
    
    // Initialize task fields
    task->id = get_next_task_id();
    task->client_id = client_id;
    task->client_socket = client_socket;
    task->command = strdup(command);
    task->state = TASK_WAITING;
    task->round = 0;
    task->arrival_time = time(NULL);
    task->start_time = 0;
    task->completion_time = 0;
    task->next = NULL;
    strncpy(task->client_ip, client_ip, sizeof(task->client_ip) - 1);
    task->client_ip[sizeof(task->client_ip) - 1] = '\0'; // Ensure null termination
    task->client_port = client_port;
    
    // Determine if it's a program command with execution time
    int execution_time = 0;
    if (is_program_command(command, &execution_time)) {
        task->type = TASK_PROGRAM;
        task->burst_time = execution_time;
        task->remaining_time = execution_time;
        printf(COLOR_BLUE "[SCHEDULER] [Client #%d - %s:%d] Added program task #%d with burst time %d seconds\n" COLOR_RESET,
               client_id, client_ip, client_port, task->id, execution_time);
    } else {
        // Shell commands are always executed in one go
        task->type = TASK_SHELL_COMMAND;
        task->burst_time = -1;  // Special value for shell commands
        task->remaining_time = -1;
        printf(COLOR_BLUE "[SCHEDULER] [Client #%d - %s:%d] Added shell command task #%d\n" COLOR_RESET,
               client_id, client_ip, client_port, task->id);
    }
    
    // Add the task to the queue
    pthread_mutex_lock(&task_queue.mutex);
    
    if (task_queue.tail == NULL) {
        // Queue is empty
        task_queue.head = task;
        task_queue.tail = task;
    } else {
        // Add to the end of the queue
        task_queue.tail->next = task;
        task_queue.tail = task;
    }
    
    task_queue.size++;
    pthread_mutex_unlock(&task_queue.mutex);
    
    return task;
}

// Get the quantum time for the current round
int get_quantum_for_round(int round) {
    return (round == 0) ? QUANTUM_FIRST_ROUND : QUANTUM_OTHER_ROUNDS;
}

// Get the next task to execute based on scheduling algorithm
Task* scheduler_get_next_task() {
    pthread_mutex_lock(&task_queue.mutex);
    
    if (task_queue.head == NULL) {
        // Queue is empty
        pthread_mutex_unlock(&task_queue.mutex);
        return NULL;
    }
    
    // First, check for shell commands (highest priority)
    Task *current = task_queue.head;
    Task *shell_command = NULL;
    
    while (current != NULL) {
        if (current->type == TASK_SHELL_COMMAND && current->state == TASK_WAITING) {
            shell_command = current;
            break;
        }
        current = current->next;
    }
    
    if (shell_command != NULL) {
        pthread_mutex_unlock(&task_queue.mutex);
        return shell_command;
    }
    
    // No shell commands, use SJRF for program tasks
    Task *shortest_job = NULL;
    int shortest_time = INT_MAX;
    
    current = task_queue.head;
    while (current != NULL) {
        if (current->type == TASK_PROGRAM && 
            current->state == TASK_WAITING && 
            current->remaining_time < shortest_time &&
            current->id != last_selected_task_id) {
            shortest_job = current;
            shortest_time = current->remaining_time;
        }
        current = current->next;
    }
    
    // If no eligible task found with SJRF (or all have been selected last time),
    // try again without the last_selected_task_id constraint
    if (shortest_job == NULL) {
        current = task_queue.head;
        while (current != NULL) {
            if (current->type == TASK_PROGRAM && 
                current->state == TASK_WAITING && 
                current->remaining_time < shortest_time) {
                shortest_job = current;
                shortest_time = current->remaining_time;
            }
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&task_queue.mutex);
    return shortest_job;
}

// Update task state after execution
void scheduler_update_task(Task *task, int executed_time) {
    pthread_mutex_lock(&task_queue.mutex);
    
    if (task->type == TASK_SHELL_COMMAND) {
        // Shell commands complete in one execution
        task->state = TASK_COMPLETED;
        task->completion_time = time(NULL);
        printf(COLOR_GREEN "[SCHEDULER] Task #%d (Shell Command) from Client #%d completed\n" COLOR_RESET,
               task->id, task->client_id);
    } else {
        // Update remaining time for program tasks
        task->remaining_time -= executed_time;
        task->round++;
        
        if (task->remaining_time <= 0) {
            // Task completed
            task->state = TASK_COMPLETED;
            task->completion_time = time(NULL);
            printf(COLOR_GREEN "[SCHEDULER] Task #%d (Program) from Client #%d completed after %d rounds\n" COLOR_RESET,
                   task->id, task->client_id, task->round);
        } else {
            // Task needs more time
            task->state = TASK_WAITING;
            printf(COLOR_YELLOW "[SCHEDULER] Task #%d (Program) from Client #%d preempted. Remaining time: %d seconds\n" COLOR_RESET,
                   task->id, task->client_id, task->remaining_time);
        }
    }
    
    pthread_mutex_unlock(&task_queue.mutex);
}

// Remove a task from the queue
void scheduler_remove_task(Task *task) {
    pthread_mutex_lock(&task_queue.mutex);
    
    if (task_queue.head == NULL) {
        pthread_mutex_unlock(&task_queue.mutex);
        return;
    }
    
    // Special case: removing the head
    if (task_queue.head == task) {
        task_queue.head = task->next;
        if (task_queue.head == NULL) {
            task_queue.tail = NULL;
        }
    } else {
        // Find the task in the queue
        Task *prev = task_queue.head;
        while (prev != NULL && prev->next != task) {
            prev = prev->next;
        }
        
        if (prev != NULL) {
            prev->next = task->next;
            if (prev->next == NULL) {
                task_queue.tail = prev;
            }
        }
    }
    
    task_queue.size--;
    
    printf(COLOR_RED "[SCHEDULER] Removed Task #%d from Client #%d from the queue\n" COLOR_RESET,
           task->id, task->client_id);
    
    // Free the task memory
    free(task->command);
    free(task);
    
    pthread_mutex_unlock(&task_queue.mutex);
}

// Remove all tasks for a specific client
void scheduler_remove_client_tasks(int client_id) {
    pthread_mutex_lock(&task_queue.mutex);
    
    Task *current = task_queue.head;
    Task *prev = NULL;
    
    while (current != NULL) {
        if (current->client_id == client_id) {
            Task *to_remove = current;
            
            // Update the list pointers
            if (prev == NULL) {
                // Removing the head
                task_queue.head = current->next;
                if (task_queue.head == NULL) {
                    task_queue.tail = NULL;
                }
            } else {
                prev->next = current->next;
                if (prev->next == NULL) {
                    task_queue.tail = prev;
                }
            }
            
            current = current->next;
            
            // Free the task memory
            printf(COLOR_RED "[SCHEDULER] Removed Task #%d from Client #%d (client disconnected)\n" COLOR_RESET,
                   to_remove->id, to_remove->client_id);
            free(to_remove->command);
            free(to_remove);
            
            task_queue.size--;
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&task_queue.mutex);
}

// Check if a command is a program with execution time parameter
bool is_program_command(const char *command, int *execution_time) {
    // Check if the command starts with "./demo" or "demo"
    if (strncmp(command, "./demo", 6) == 0 || 
        (strncmp(command, "demo", 4) == 0 && (command[4] == ' ' || command[4] == '\0'))) {
        // Extract the execution time parameter
        char *token, *str, *tofree;
        tofree = str = strdup(command);
        
        // Get the first token (command name)
        token = strtok(str, " ");
        if (token != NULL) {
            // Get the second token (execution time)
            token = strtok(NULL, " ");
            if (token != NULL) {
                *execution_time = atoi(token);
                free(tofree);
                return (*execution_time > 0);
            }
        }
        
        free(tofree);
    }
    
    return false;
}

// Execute a shell command task
void execute_shell_command_task(Task *task) {
    printf(COLOR_BLUE "[SCHEDULER] [Client #%d - %s:%d] Executing shell command: \"%s\"\n" COLOR_RESET,
           task->client_id, task->client_ip, task->client_port, task->command);
    
    // Save the current stdout and stderr
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    
    // Create a pipe to capture the command's output
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }
    
    // Redirect stdout and stderr to our pipe
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    
    // Execute the command
    if (strchr(task->command, '|')) {
        // Handle pipeline
        if (strstr(task->command, "||") == NULL) {
            execute_pipeline(task->command);
        } else {
            fprintf(stderr, "Error: Empty command between pipes.\n");
        }
    } else {
        // Handle single command
        Command *cmd = parse_command(task->command);
        if (cmd) {
            execute_command(cmd);
            free_command(cmd);
        } else {
            fprintf(stderr, "Parsing error.\n");
        }
    }
    
    // Flush output
    fflush(stdout);
    fflush(stderr);
    
    // Restore original stdout and stderr
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stderr_backup);
    
    // Read the captured output
    char buffer[MAX_OUTPUT_SIZE];
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        // Check if the output contains error messages
        int is_error = (strstr(buffer, "Error:") != NULL || 
                        strstr(buffer, "not found") != NULL ||
                        strstr(buffer, ": missing operand") != NULL ||
                        strstr(buffer, "Parsing error") != NULL);
        
        if (is_error) {
            printf(COLOR_RED "[ERROR] [Client #%d - %s:%d] %s" COLOR_RESET, 
                   task->client_id, task->client_ip, task->client_port, buffer);
            send(task->client_socket, buffer, bytes_read, 0);
        } else {
            // Format and send the output
            printf(COLOR_GREEN "[OUTPUT] [Client #%d - %s:%d] Command output:\n%s\n" COLOR_RESET, 
                   task->client_id, task->client_ip, task->client_port, buffer);
            send(task->client_socket, buffer, bytes_read, 0);
            
            // Add a newline if needed
            if (bytes_read > 0 && buffer[bytes_read-1] != '\n') {
                send(task->client_socket, "\n", 1, 0);
            }
        }
    } else {
        // No output
        printf(COLOR_BLUE "[OUTPUT] [Client #%d - %s:%d] Command had no output\n" COLOR_RESET, 
               task->client_id, task->client_ip, task->client_port);
        send(task->client_socket, "", 1, 0);
    }
}

// Execute a program task for a specific time slice
void execute_program_task(Task *task, int time_slice) {
    // If this is the first time running this task
    if (task->start_time == 0) {
        task->start_time = time(NULL);
    }
    
    printf(COLOR_BLUE "[SCHEDULER] [Client #%d - %s:%d] Executing program task #%d for %d seconds (remaining: %d)\n" COLOR_RESET,
           task->client_id, task->client_ip, task->client_port, task->id, 
           time_slice, task->remaining_time);
    
    // Determine how many iterations to run
    int iterations = (time_slice < task->remaining_time) ? time_slice : task->remaining_time;
    
    // Create a message to send to the client
    char message[MAX_OUTPUT_SIZE];
    snprintf(message, sizeof(message), 
             "[Task #%d] Executing for %d seconds (Round %d, Remaining: %d)\n",
             task->id, iterations, task->round + 1, task->remaining_time);
    send(task->client_socket, message, strlen(message), 0);
    
    // Simulate the program execution
    for (int i = 1; i <= iterations; i++) {
        // Check if we should continue execution
        if (!scheduler_running || task->state == TASK_TERMINATED) {
            break;
        }
        
        // Send progress to the client
        snprintf(message, sizeof(message), "[Task #%d] Iteration %d of %d\n", 
                 task->id, i, task->remaining_time);
        send(task->client_socket, message, strlen(message), 0);
        
        // Sleep to simulate work
        sleep(1);
    }
    
    // Return the actual time executed
    scheduler_update_task(task, iterations);
    
    // If the task completed, send a completion message
    if (task->state == TASK_COMPLETED) {
        snprintf(message, sizeof(message), "[Task #%d] Completed execution after %ld seconds\n",
                 task->id, (long)(task->completion_time - task->start_time));
        send(task->client_socket, message, strlen(message), 0);
    }
}

// Execute a task for a specific time slice
void execute_task(Task *task, int time_slice) {
    // Set the current running task
    pthread_mutex_lock(&running_task_mutex);
    current_running_task = task;
    pthread_mutex_unlock(&running_task_mutex);
    
    // Update task state to running
    pthread_mutex_lock(&task_queue.mutex);
    task->state = TASK_RUNNING;
    pthread_mutex_unlock(&task_queue.mutex);
    
    // Execute based on task type
    if (task->type == TASK_SHELL_COMMAND) {
        execute_shell_command_task(task);
    } else {
        execute_program_task(task, time_slice);
    }
    
    // Update the last selected task ID
    last_selected_task_id = task->id;
    
    // Clear the current running task
    pthread_mutex_lock(&running_task_mutex);
    current_running_task = NULL;
    pthread_mutex_unlock(&running_task_mutex);
}

// Print the current state of the task queue
void print_task_queue() {
    pthread_mutex_lock(&task_queue.mutex);
    
    printf(COLOR_BLUE "\n===== TASK QUEUE STATUS =====\n" COLOR_RESET);
    printf(COLOR_BLUE "Total tasks in queue: %d\n" COLOR_RESET, task_queue.size);
    
    if (task_queue.size > 0) {
        printf(COLOR_BLUE "ID\tClient\tType\t\tState\t\tRemaining\tRound\n" COLOR_RESET);
        Task *current = task_queue.head;
        while (current != NULL) {
            const char *type_str = (current->type == TASK_SHELL_COMMAND) ? "Shell Command" : "Program";
            const char *state_str;
            
            switch (current->state) {
                case TASK_WAITING: state_str = "Waiting"; break;
                case TASK_RUNNING: state_str = "Running"; break;
                case TASK_COMPLETED: state_str = "Completed"; break;
                case TASK_TERMINATED: state_str = "Terminated"; break;
                default: state_str = "Unknown"; break;
            }
            
            printf(COLOR_BLUE "%d\t%d\t%s\t%s\t\t%d\t\t%d\n" COLOR_RESET,
                   current->id, current->client_id, type_str, state_str,
                   current->remaining_time, current->round);
            
            current = current->next;
        }
    }
    
    printf(COLOR_BLUE "============================\n\n" COLOR_RESET);
    
    pthread_mutex_unlock(&task_queue.mutex);
}

// Main scheduler loop
void *scheduler_loop(void *arg) {
    printf(COLOR_GREEN "[SCHEDULER] Scheduler thread started\n" COLOR_RESET);
    
    while (scheduler_running) {
        // Get the next task to execute
        Task *task = scheduler_get_next_task();
        
        if (task != NULL) {
            // Determine the time slice for this task
            int time_slice = get_quantum_for_round(task->round);
            
            // Execute the task
            execute_task(task, time_slice);
            
            // If the task is completed, remove it from the queue
            if (task->state == TASK_COMPLETED) {
                scheduler_remove_task(task);
            }
            
            // Print the current state of the queue
            print_task_queue();
        } else {
            // No tasks to execute, sleep for a short time
            usleep(100000); // 100ms
        }
    }
    
    printf(COLOR_GREEN "[SCHEDULER] Scheduler thread stopped\n" COLOR_RESET);
    return NULL;
}

// Start the scheduler thread
void scheduler_start() {
    if (!scheduler_running) {
        scheduler_running = true;
        if (pthread_create(&scheduler_thread, NULL, scheduler_loop, NULL) != 0) {
            perror("pthread_create");
            scheduler_running = false;
        }
    }
}

// Stop the scheduler thread
void scheduler_stop() {
    if (scheduler_running) {
        scheduler_running = false;
        pthread_join(scheduler_thread, NULL);
    }
}
