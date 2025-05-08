#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "thread_handler.h"
#include "scheduler.h"

#define MAX_INPUT_SIZE 1024
#define MAX_OUTPUT_SIZE 4096

// handles commands received from clients
void handle_command(int client_socket, const char *command, int client_id, char *client_ip, int client_port) {
    // handle empty commands by just sending prompt back
    if (!command || strlen(command) == 0) {
        const char *prompt = "$ ";
        send(client_socket, prompt, strlen(prompt), 0);
        return;
    }

    int is_program = 0;
    int execution_time = -1;
    
    // check if command is a demo program execution
    if (strncmp(command, "./demo", 6) == 0 || strncmp(command, "demo", 4) == 0) {
        is_program = 1;
        sscanf(command, "%*s %d", &execution_time);
        if (execution_time <= 0) {
            execution_time = 5;  // default execution time
        }
    }
    
    // add task to scheduler queue - scheduler handles execution and output
    scheduler_add_task(client_id, client_socket, command, 
                      is_program ? TASK_PROGRAM : TASK_SHELL_COMMAND, 
                      execution_time);
}

// handles individual client connections in separate threads
void *handle_client(void *arg) {
    client_info *info = (client_info *)arg;
    int client_socket = info->client_socket;
    int client_id = info->client_id;
    
    char input[MAX_INPUT_SIZE];
    ssize_t bytes_received;
    
    // send initial prompt to new client
    const char *prompt = "$ ";
    send(client_socket, prompt, strlen(prompt), 0);
    
    // main client communication loop
    while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
        input[bytes_received] = '\0';
        
        // handle exit command
        if (strcmp(input, "exit") == 0) {
            printf("[%d]>>> exit\n", client_id);
            const char *goodbye = "Disconnected from server.\n";
            send(client_socket, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // process received command
        handle_command(client_socket, input, client_id, 
                      inet_ntoa(info->client_addr.sin_addr),
                      ntohs(info->client_addr.sin_port));
    }
    
    // cleanup when client disconnects
    printf("[%d]>>> disconnected\n", client_id);
    scheduler_remove_client_tasks(client_id);
    close(client_socket);
    free(info);
    
    return NULL;
}