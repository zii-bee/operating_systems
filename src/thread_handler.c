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
#define MAX_OUTPUT_SIZE 4096

// Execute commands sent by clients
void execute_shell_command(int client_socket, const char *input, char *client_ip, int client_port, int client_id) {
    // Print received message
    printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n", 
           client_id, client_ip, client_port, input);
    
    // Print executing message
    printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n", 
           client_id, client_ip, client_port, input);
    
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
    char buffer[MAX_OUTPUT_SIZE];
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);
    
    // If there's output, send it to the client
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0'; // Null-terminate the buffer
        
        // Check if the output contains an error message
        int is_error = (strstr(buffer, "Error:") != NULL || 
                        strstr(buffer, "not found") != NULL ||
                        strstr(buffer, ": missing operand") != NULL ||
                        strstr(buffer, "Parsing error") != NULL);
        
        if (is_error) {
            // Print error message
            printf("[ERROR] [Client #%d - %s:%d] %s", 
                  client_id, client_ip, client_port, buffer);
                  
            // Print output message
            printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n\"%s\"\n", 
                  client_id, client_ip, client_port, buffer);
                  
            send(client_socket, buffer, bytes_read, 0);
        } 
        else if (strcmp(input, "ls") == 0) {
            // For 'ls' command, perform special formatting to match Linux terminal
            char processed_buffer[MAX_OUTPUT_SIZE];
            int j = 0;
            
            // Replace newlines with spaces (except for the last one)
            for (int i = 0; i < bytes_read; i++) {
                if (buffer[i] == '\n' && i < bytes_read - 1) {
                    processed_buffer[j++] = ' ';
                } else {
                    processed_buffer[j++] = buffer[i];
                }
            }
            
            // Ensure we end with a newline
            if (j > 0 && processed_buffer[j-1] != '\n') {
                processed_buffer[j++] = '\n';
            }
            
            processed_buffer[j] = '\0';
            
            // Print output message
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s", 
                  client_id, client_ip, client_port, processed_buffer);
                  
            // Send the processed output to client
            send(client_socket, processed_buffer, j, 0);
        } 
        else {
            // Normal output
            // Print output message
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s", 
                  client_id, client_ip, client_port, 
                  buffer[bytes_read-1] == '\n' ? buffer : strcat(buffer, "\n"));
                  
            // Ensure output ends with a newline for consistency
            int needs_newline = (bytes_read > 0 && buffer[bytes_read-1] != '\n');
            
            // Send the output to client
            send(client_socket, buffer, bytes_read, 0);
            
            // Add a newline if needed
            if (needs_newline) {
                send(client_socket, "\n", 1, 0);
            }
        }
    } else {
        // If there's no output, send an empty response with just a newline
        printf("[OUTPUT] [Client #%d - %s:%d] Sending empty response (command had no output)\n", 
              client_id, client_ip, client_port);
        send(client_socket, "\n", 1, 0);
    }
}

// Thread handler function for each client
void *handle_client(void *arg) {
    client_info *info = (client_info *)arg;
    int client_socket = info->client_socket;
    struct sockaddr_in client_addr = info->client_addr;
    int client_id = info->client_id;
    
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
            printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"exit\"\n", 
                   client_id, client_ip, client_port);
            char goodbye[] = "Disconnected from Server.\n";
            printf("[OUTPUT] [Client #%d - %s:%d] Sending response: \"Disconnected from Server.\"\n", 
                   client_id, client_ip, client_port);
            send(client_socket, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // Execute the command
        execute_shell_command(client_socket, input, client_ip, client_port, client_id);
    }
    
    // Check if there was an error receiving data
    if (bytes_received < 0) {
        perror("recv");
        printf("[ERROR] [Client #%d - %s:%d] Error receiving data\n", 
              client_id, client_ip, client_port);
    }
    
    printf("[INFO] Client #%d disconnected from %s:%d\n", client_id, client_ip, client_port);
    
    // Close the client socket
    close(client_socket);
    free(info);
    
    return NULL;
}