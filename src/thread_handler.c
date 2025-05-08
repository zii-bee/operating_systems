#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "parser.h"
#include "executor.h"
#include "pipes.h"
#include "thread_handler.h"
#include "scheduler.h"

#define MAX_INPUT_SIZE 1024
#define MAX_OUTPUT_SIZE 4096

// color codes for output
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_RESET   "\033[0m"

// Update handle_command function in src/thread_handler.c
void handle_command(int client_socket, const char *command, int client_id, char *client_ip, int client_port) {
    int is_program = 0;
    int execution_time = -1;
    
    // Check if this is a demo program
    if (strncmp(command, "./demo", 6) == 0 || strncmp(command, "demo", 4) == 0) {
        is_program = 1;
        sscanf(command, "%*s %d", &execution_time);
        if (execution_time <= 0) {
            execution_time = 5;
        }
    }
    
    // Add task to scheduler - it will handle the output and prompts
    scheduler_add_task(client_id, client_socket, command, 
                      is_program ? TASK_PROGRAM : TASK_SHELL_COMMAND, 
                      execution_time);
}

void *handle_client(void *arg) {
    client_info *info = (client_info *)arg;
    int client_socket = info->client_socket;
    struct sockaddr_in client_addr = info->client_addr;
    int client_id = info->client_id;
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    
    char input[MAX_INPUT_SIZE];
    ssize_t bytes_received;
    
    // Send initial prompt only
    const char *prompt = "$ ";
    send(client_socket, prompt, strlen(prompt), 0);
    
    while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
        input[bytes_received] = '\0';
        
        if (strcmp(input, "exit") == 0) {
            printf("[%d]>>> exit\n", client_id);
            const char *goodbye = "Disconnected from server.\n";
            send(client_socket, goodbye, strlen(goodbye), 0);
            break;
        }
        
        handle_command(client_socket, input, client_id, client_ip, client_port);
    }
    
    if (bytes_received < 0) {
        perror("recv");
    }
    
    printf("[%d]>>> disconnected\n", client_id);
    scheduler_remove_client_tasks(client_id);
    close(client_socket);
    free(info);
    
    return NULL;
}