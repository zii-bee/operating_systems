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

#define MAX_INPUT_SIZE 1024

// Execute commands sent by clients (copied from server.c and modified)
void execute_shell_command(int client_socket, const char *input, char *client_ip, int client_port) {
    // Print incoming command message
    printf("Incoming message from %s:%d: \"%s\"\n", client_ip, client_port, input);
    
    // Redirect stdout and stderr to capture the output
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    
    // Create pipes for capturing stdout and stderr
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }
    
    // Redirect stdout and stderr to the pipe
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    
    // Execute the command using existing shell implementation
    if (strchr(input, '|')) {
        if (strstr(input, "||") != NULL) {
            fprintf(stderr, "Error: Empty command between pipes.\n");
        } else {
            execute_pipeline(input);
        }
    } else {
        // Parse and execute a single command
        Command *cmd = parse_command(input);
        if (cmd) {
            execute_command(cmd);
            free_command(cmd);
        } else {
            fprintf(stderr, "Parsing error.\n");
        }
    }
    
    // Flush the streams
    fflush(stdout);
    fflush(stderr);
    
    // Restore original stdout and stderr
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stderr_backup);
    
    // Read the output from the pipe
    char buffer[MAX_INPUT_SIZE];
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);
    
    // If there's output, send it to the client
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0'; // Null-terminate the buffer
        
        // Print outgoing message
        printf("Outgoing message to %s:%d: \"%s\"\n", client_ip, client_port, 
               buffer[bytes_read-1] == '\n' ? buffer : strcat(buffer, "\n"));
        
        send(client_socket, buffer, bytes_read, 0);
    } else {
        // If there's no output, send an empty response
        printf("Outgoing message to %s:%d: \"\"\n", client_ip, client_port);
        send(client_socket, "\n", 1, 0); // Send a newline for empty response
    }
}

// Thread handler function for each client
void *handle_client(void *arg) {
    client_info *info = (client_info *)arg;
    int client_socket = info->client_socket;
    struct sockaddr_in client_addr = info->client_addr;
    
    // Get client IP and port
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    
    // Handle client communication
    char input[MAX_INPUT_SIZE];
    ssize_t bytes_received;
    
    // Receive data from client
    while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
        input[bytes_received] = '\0';
        
        // Check if client wants to exit
        if (strcmp(input, "exit") == 0) {
            printf("Incoming message from %s:%d: \"exit\"\n", client_ip, client_port);
            char goodbye[] = "Goodbye!\n";
            printf("Outgoing message to %s:%d: \"Goodbye!\"\n", client_ip, client_port);
            send(client_socket, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // Execute the command
        execute_shell_command(client_socket, input, client_ip, client_port);
    }
    
    // Check if there was an error receiving data
    if (bytes_received < 0) {
        perror("recv");
    }
    
    // Close the client socket
    close(client_socket);
    free(info);
    
    return NULL;
}