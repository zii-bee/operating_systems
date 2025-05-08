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

// handle a command from a client
void handle_command(int client_socket, const char *command, int client_id, char *client_ip, int client_port) {
    // check if this is a program command or shell command
    int is_program = 0;
    int execution_time = -1;
    
    // check if this is a demo program
    if (strncmp(command, "./demo", 6) == 0 || strncmp(command, "demo", 4) == 0) {
        is_program = 1;
        
        // extract the n value
        sscanf(command, "%*s %d", &execution_time);
        if (execution_time <= 0) {
            execution_time = 5; // default if not specified
        }
    }
    
    if (is_program) {
        // add to scheduler as a program task
        scheduler_add_task(client_id, client_socket, command, TASK_PROGRAM, execution_time);
        
        // prepare response for client
        char response[4096] = {0};
        for (int i = 1; i <= execution_time; i++) {
            char line[64];
            snprintf(line, sizeof(line), "Demo %d/%d\n", i, execution_time);
            strcat(response, line);
        }
        
        // send the response to the client
        if (send(client_socket, response, strlen(response), 0) < 0) {
            perror("send");
        }
    } else {
        // this is a shell command, add it to the scheduler
        scheduler_add_task(client_id, client_socket, command, TASK_SHELL_COMMAND, -1);
    }
}

// thread function to handle a client connection
void *handle_client(void *arg) {
    // extract client information
    client_info *info = (client_info *)arg;
    int client_socket = info->client_socket;
    struct sockaddr_in client_addr = info->client_addr;
    int client_id = info->client_id;
    
    // get client IP and port
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    
    // buffer for incoming commands
    char input[MAX_INPUT_SIZE];
    ssize_t bytes_received;
    
    // main client loop
    while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
        // null-terminate the received data
        input[bytes_received] = '\0';
        
        // check for exit command
        if (strcmp(input, "exit") == 0) {
            printf("[%d]>>> exit\n", client_id);
            
            // send a goodbye message
            char goodbye[] = "Disconnected from server.\n";
            send(client_socket, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // handle the command through scheduler
        handle_command(client_socket, input, client_id, client_ip, client_port);
    }
    
    // client disconnected or error occurred
    if (bytes_received < 0) {
        perror("recv");
        printf("[ERROR] [Client #%d - %s:%d] Error receiving data\n", 
              client_id, client_ip, client_port);
    }
    
    // clean up client resources
    printf("[%d]>>> disconnected\n", client_id);
    
    // remove any pending tasks for this client
    scheduler_remove_client_tasks(client_id);
    
    // close the socket and free the client info
    close(client_socket);
    free(info);
    
    return NULL;
}