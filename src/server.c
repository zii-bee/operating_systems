#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "parser.h"
#include "executor.h"
#include "pipes.h"
#include "server.h"

#define MAX_INPUT_SIZE 1024
#define BACKLOG 5

void execute_shell_command(int client_socket, const char *input) {
    // Check for empty input or only whitespace
    if (!input || strlen(input) == 0 || strspn(input, " \t\n") == strlen(input)) {
        // Send a newline character as a response to empty input
        send(client_socket, "\n", 1, 0);
        return;
    }

    printf("Executing command: \"%s\"\n", input);
    
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
    
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        // Print the output for server logs
        printf("Command output:\n%s", buffer);
        // Send the output to the client
        send(client_socket, buffer, bytes_read, 0);
        printf("Sent %zd bytes of response\n", bytes_read);
    } else {
        // Send a newline as a response when there's no output
        const char *empty_response = "";
        send(client_socket, empty_response, 0, 0);
    }
}

void start_server(int port) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind socket to address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_socket, BACKLOG) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("Server started. Listening on port %d...\n", port);
    
    while (1) {
        // Accept connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        
        printf("======================================\n");
        printf("New client connected: %s:%d\n", client_ip, client_port);
        printf("======================================\n");
        
        // Handle client communication
        char input[MAX_INPUT_SIZE];
        ssize_t bytes_received;
        
        while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
            input[bytes_received] = '\0';
            
            // Check if client wants to exit
            if (strcmp(input, "exit") == 0) {
                printf("Client %s:%d requested to exit\n", client_ip, client_port);
                send(client_socket, "Goodbye!\n", 9, 0);
                printf("Client disconnected: %s:%d\n", client_ip, client_port);
                printf("======================================\n");
                break;
            }
            
            // Log the command regardless of whether it's empty or not
            printf("--------------------------------------\n");
            printf("Command received from %s:%d: \"%s\"\n", client_ip, client_port, input);
            printf("Processing command...\n");
            
            // Execute the command
            execute_shell_command(client_socket, input);
            
            printf("Response sent to %s:%d\n", client_ip, client_port);
            printf("--------------------------------------\n");
        }
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("Client disconnected: %s:%d\n", client_ip, client_port);
            } else {
                perror("recv");
                printf("Error receiving from client %s:%d\n", client_ip, client_port);
            }
            printf("======================================\n");
        }
        
        close(client_socket);
    }
    
    close(server_socket);
}