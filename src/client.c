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
    
    printf("Connected to server at %s:%d\n", ip, port);
    
    // main client loop
    char input[MAX_INPUT_SIZE];
    char output[MAX_OUTPUT_SIZE];
    
    while (1) {
        // display prompt and get user input
        printf("$ ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        // remove trailing newline
        input[strcspn(input, "\n")] = 0;
        
        // check if user wants to exit
        if (strcmp(input, "exit") == 0) {
            // send exit command to server before closing
            send(client_socket, input, strlen(input), 0);
            
            // receive any remaining output from server
            ssize_t bytes_received = recv(client_socket, output, sizeof(output) - 1, 0);
            if (bytes_received > 0) {
                output[bytes_received] = '\0';
                printf("%s", output);
            }
            // close the socket and exit the loop
            break;
        }

        if (strlen(input) == 0) {
            continue;  // skip sending empty input to server
        }
        
        // send command to server, including empty commands
        if (send(client_socket, input, strlen(input), 0) < 0) {
            perror("send");
            continue;
        }
        
        // receive response from server
        ssize_t bytes_received = recv(client_socket, output, sizeof(output) - 1, 0);
        if (bytes_received < 0) {
            printf("Error receiving data from server\n");
            perror("recv");
            break;
        }

        // check if server closed the connection
        if (bytes_received == 0) {
            printf("Server closed connection\n");
            break;
        }
        
        // display the output
        output[bytes_received] = '\0';
        printf("%s", output);
    }
    // close the client socket
    close(client_socket);
}