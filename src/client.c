#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "client.h"

// maximum size of input buffer
#define MAX_INPUT_SIZE 1024
#define MAX_OUTPUT_SIZE 4096 // maximum size of output buffer

// Update client.c's start_client function to better handle prompts
void start_client(const char *ip, int port) {
    // create socket variables
    int client_socket;
    struct sockaddr_in server_addr;
    
    // create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // convert IP address from string to binary form
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address format\n");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    // connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to a server\n");
    
    // set up for select()
    fd_set read_fds;
    int max_fd = (client_socket > STDIN_FILENO) ? client_socket : STDIN_FILENO;
    
    // main client loop
    char input[MAX_INPUT_SIZE];
    char output[MAX_OUTPUT_SIZE];
    int waiting_for_prompt = 1;  // Start by waiting for the first prompt
    
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(client_socket, &read_fds);
        
        // wait for input from either stdin or the server
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }
        
        // check for input from the server
        if (FD_ISSET(client_socket, &read_fds)) {
            memset(output, 0, sizeof(output));
            ssize_t bytes_received = recv(client_socket, output, sizeof(output) - 1, 0);
            
            if (bytes_received <= 0) {
                // connection closed or error
                if (bytes_received < 0) {
                    perror("recv");
                }
                printf("Server closed connection\n");
                break;
            }
            
            // display the output
            output[bytes_received] = '\0';
            
            // Check if the last character(s) in the output are the prompt
            if (bytes_received >= 2 && 
                output[bytes_received - 2] == '$' && 
                output[bytes_received - 1] == ' ') {
                // Remove the prompt from output and print separately
                output[bytes_received - 2] = '\0';
                printf("%s", output);
                printf("$ ");
                fflush(stdout);
                waiting_for_prompt = 0;
            } else {
                // Just print the output as-is
                printf("%s", output);
                fflush(stdout);
            }
        }
        
        // check for input from stdin (only if we're not waiting for a prompt)
        if (FD_ISSET(STDIN_FILENO, &read_fds) && !waiting_for_prompt) {
            if (!fgets(input, sizeof(input), stdin)) {
                break;
            }
            
            // remove trailing newline
            input[strcspn(input, "\n")] = 0;
            
            // check if user wants to exit
            if (strcmp(input, "exit") == 0) {
                // send exit command to server
                send(client_socket, input, strlen(input), 0);
                waiting_for_prompt = 1;  // Wait for the server's goodbye message
                continue;
            }
            
            if (strlen(input) == 0) {
                // Skip empty input, but still show the prompt
                printf("$ ");
                fflush(stdout);
                continue;
            }
            
            // send command to server
            if (send(client_socket, input, strlen(input), 0) < 0) {
                perror("send");
                continue;
            }
            
            // After sending a command, we're waiting for a response (which should end with a prompt)
            waiting_for_prompt = 1;
        }
    }
    
    // close the client socket
    close(client_socket);
}