#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <pthread.h>
#include <time.h>

// task types
#define TASK_SHELL_COMMAND 1
#define TASK_PROGRAM 2

// task states
#define TASK_STATE_WAITING 0
#define TASK_STATE_RUNNING 1
#define TASK_STATE_COMPLETED 2

// scheduler algorithm selection
#define SCHED_RR 1
#define SCHED_SJRF 2

typedef struct {
    int id;                   // unique task id
    int client_id;            // client that submitted this task
    int client_socket;        // socket to send results back to
    int type;                 // shell command or program
    char *command;            // the command to execute
    int total_time;           // total execution time (for programs)
    int remaining_time;       // remaining execution time
    int state;                // current state of the task
    int round;                // current round number for this task
    int last_executed;        // flag to avoid consecutive execution
    time_t arrival_time;      // when the task was submitted
    int preempted;            // whether this task was preempted
} task_t;

typedef struct {
    task_t **tasks;           // array of task pointers
    int capacity;             // maximum number of tasks
    int size;                 // current number of tasks
    int current_round;        // current scheduling round
    pthread_mutex_t lock;     // mutex to protect the queue
    pthread_cond_t not_empty; // condition variable for queue not empty
} task_queue_t;

// initialize the scheduler
void scheduler_init(void);

// clean up the scheduler
void scheduler_cleanup(void);

// add a task to the queue
void scheduler_add_task(int client_id, int client_socket, const char *command, int type, int time);

// get the next task to execute based on the scheduling algorithm
task_t *scheduler_get_next_task(void);

// update a task's remaining time and status
void scheduler_update_task(task_t *task, int time_executed);

// mark a task as completed and remove it from the queue
void scheduler_complete_task(task_t *task);

// remove all tasks for a specific client
void scheduler_remove_client_tasks(int client_id);

// start the scheduler thread
void scheduler_start(void);

// stop the scheduler thread
void scheduler_stop(void);

// estimate execution time for a command
int estimate_execution_time(const char *command);

// demo program execution
void execute_demo_program(const char *command, int client_socket, int n, int client_id);

#endif // SCHEDULER_H