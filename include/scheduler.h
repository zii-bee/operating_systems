#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

// Task states
typedef enum {
    TASK_WAITING,   // Task is waiting to be executed
    TASK_RUNNING,   // Task is currently running
    TASK_COMPLETED, // Task has completed execution
    TASK_TERMINATED // Task was terminated (e.g., client disconnected)
} TaskState;

// Task types
typedef enum {
    TASK_SHELL_COMMAND, // Shell command (executes in one go)
    TASK_PROGRAM        // Program that may need multiple rounds of execution
} TaskType;

// Task structure to hold information about each task
typedef struct Task {
    int id;                // Unique task ID
    int client_id;         // Client ID that submitted this task
    int client_socket;     // Client socket to send results back
    char *command;         // Original command string
    TaskType type;         // Type of task
    TaskState state;       // Current state of the task
    int burst_time;        // Total execution time needed (for programs)
    int remaining_time;    // Remaining execution time
    int round;             // Current execution round
    time_t arrival_time;   // When the task was added to the queue
    time_t start_time;     // When the task first started execution
    time_t completion_time; // When the task completed
    struct Task *next;     // Pointer to next task in the queue
    char client_ip[16];    // Client IP address
    int client_port;       // Client port
} Task;

// Queue structure to manage tasks
typedef struct {
    Task *head;           // Head of the task queue
    Task *tail;           // Tail of the task queue
    int size;             // Current size of the queue
    pthread_mutex_t mutex; // Mutex for thread safety
} TaskQueue;

// Initialize the scheduler system
void scheduler_init();

// Clean up the scheduler system
void scheduler_cleanup();

// Add a new task to the queue
Task* scheduler_add_task(int client_id, int client_socket, const char *command, 
                        const char *client_ip, int client_port);

// Get the next task to execute based on scheduling algorithm
Task* scheduler_get_next_task();

// Update task state after execution
void scheduler_update_task(Task *task, int executed_time);

// Remove a task from the queue
void scheduler_remove_task(Task *task);

// Remove all tasks for a specific client
void scheduler_remove_client_tasks(int client_id);

// Start the scheduler thread
void scheduler_start();

// Stop the scheduler thread
void scheduler_stop();

// Check if a command is a program with execution time parameter
bool is_program_command(const char *command, int *execution_time);

// Execute a task for a specific time slice
void execute_task(Task *task, int time_slice);

// Print the current state of the task queue
void print_task_queue();

// Get the quantum time for the current round
int get_quantum_for_round(int round);

#endif // SCHEDULER_H
