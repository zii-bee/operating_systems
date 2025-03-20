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

// Color constants
#define COLOR_RESET "\033[0m"
#define COLOR_BLUE "\033[1;34m"
#define COLOR_GREY "\033[90m"
#define COLOR_GREEN "\033[1;32m"

// maximum size of input buffer
#define MAX_INPUT_SIZE 1024
#define BACKLOG 5 // maximum number of pending connections

void execute_shell_command(int client_socket, const char *input) {

    printf(COLOR_GREY "[EXECUTING]" COLOR_RESET " Executing command: \"" COLOR_GREEN "%s" COLOR_RESET "\"\n", input);
    
    // redirect stdout and stderr to capture the output
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    
    // create pipes for capturing stdout and stderr
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }
    
    // redirect stdout and stderr to the pipe
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    
    // execute the command using existing shell implementation
    if (strchr(input, '|')) {
        if (strstr(input, "||") != NULL) {
            fprintf(stderr, COLOR_GREY "[ERROR]" COLOR_RESET " Error: Empty command between pipes.\n");
        } else {
            execute_pipeline(input);
        }
    } else {
        // parse and execute a single command
        Command *cmd = parse_command(input);
        if (cmd) {
            execute_command(cmd);
            free_command(cmd);
        } else {
            fprintf(stderr, COLOR_GREY "[ERROR]" COLOR_RESET " Parsing error.\n");
        }
    }
    
    // flush the streams
    fflush(stdout);
    fflush(stderr);
    
    // restore original stdout and stderr
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stderr_backup);
    
    // read the output from the pipe
    char buffer[MAX_INPUT_SIZE];
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);
    
    // if there's output, send it to the client
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0'; // null-terminate the buffer
        // check if the output contains an error message
        int is_error = (strstr(buffer, "Error:") != NULL || 
                        strstr(buffer, "not found") != NULL ||
                        strstr(buffer, ": missing operand") != NULL ||
                        strstr(buffer, "Parsing error") != NULL);
        
        if (is_error) { // if it's an error, send the error message to the client
            // log error message with closing quote fixed
            printf(COLOR_GREY "[ERROR]" COLOR_RESET " %s", buffer);
            printf(COLOR_GREY "[OUTPUT]" COLOR_RESET " Sending error message to client: \" " COLOR_GREEN "%s" COLOR_RESET "\"", buffer);
            send(client_socket, buffer, bytes_read, 0);
        } else if (strcmp(input, "ls") == 0){ // ls output is formatted differently because no newline between items
            char processed_buffer[MAX_INPUT_SIZE];
            int j = 0;
            
            // replace newlines with spaces because client output on regular ls giving newlines between items
            for (int i = 0; i < bytes_read; i++) {
                if (buffer[i] == '\n' && i < bytes_read - 1) {
                    processed_buffer[j++] = ' ';
                } else {
                    processed_buffer[j++] = buffer[i]; // copy the character as is
                }
            }
            processed_buffer[j] = '\0'; // null-terminate the processed buffer
            
            // send the processed output
            printf(COLOR_GREY "[OUTPUT]" COLOR_RESET " Sending output to client:\n%s", processed_buffer);
            send(client_socket, processed_buffer, j, 0);
        }
        else {
            // log normal output
            printf(COLOR_GREY "[OUTPUT]" COLOR_RESET " Sending output to client:\n%s", buffer);
            send(client_socket, buffer, bytes_read, 0);
        }
    // if there's no output, send an empty response
    } else {
        printf(COLOR_GREY "[OUTPUT]" COLOR_RESET " Sending empty response (command had no output)\n");
        send(client_socket, "", 1, 0); // send an empty response
    }
}

void start_server(int port) {
    // create socket variables
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    // check if socket creation was successful
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_socket); // close the socket before exiting
        exit(EXIT_FAILURE);
    }
    
    // configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // accept connections from any address
    server_addr.sin_port = htons(port); // set the port number
    
    // bind socket to address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    // listen for connections
    if (listen(server_socket, BACKLOG) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    // print server info with "for" in blue
    printf(COLOR_GREY "[INFO]" COLOR_RESET " Server started, waiting " COLOR_BLUE "for" COLOR_RESET " client connections...\n");
    
    while (1) {
        // accept connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        // get client IP and port
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        printf(COLOR_GREY "[INFO]" COLOR_RESET " Client connected: %s:%d\n", client_ip, client_port);
        
        // handle client communication
        char input[MAX_INPUT_SIZE];
        ssize_t bytes_received;

        // receive data from client
        while ((bytes_received = recv(client_socket, input, sizeof(input) - 1, 0)) > 0) {
            input[bytes_received] = '\0';
            
            // check if client wants to exit
            if (strcmp(input, "exit") == 0) {
                printf(COLOR_GREY "[INFO]" COLOR_RESET " Client %s:%d requested to exit\n", client_ip, client_port);
                send(client_socket, "Goodbye!\n", 9, 0);
                printf(COLOR_GREY "[INFO]" COLOR_RESET " Client disconnected: %s:%d\n", client_ip, client_port);
                break;
            }
            
            // log the commands and actions on the server side, with "from" in blue and command in green
            printf(COLOR_GREY "[RECEIVED]" COLOR_RESET " Received command: \"" COLOR_GREEN "%s" COLOR_RESET "\" " COLOR_BLUE "from" COLOR_RESET " client %d:%s: \n", input, client_port, client_ip);
            
            // execute the command
            execute_shell_command(client_socket, input);
            
        }
        // check if there was an error receiving data
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf(COLOR_GREY "[INFO]" COLOR_RESET " Client disconnected: %s:%d\n", client_ip, client_port);
            } else {
                perror("recv");
                printf(COLOR_GREY "[ERROR]" COLOR_RESET " Error receiving " COLOR_BLUE "from" COLOR_RESET " client %s:%d\n", client_ip, client_port);
            }
        }
        // close the client socket
        close(client_socket);
    }
    // close the server socket
    close(server_socket);
}
