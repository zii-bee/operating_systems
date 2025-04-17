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
#include "server.h"
#include "thread_handler.h"

// Maximum size of input buffer
#define MAX_INPUT_SIZE 1024
#define BACKLOG 5 // Maximum number of pending connections

// Global thread counter
static int thread_count = 0;
pthread_mutex_t thread_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

void start_server(int port) {
    // Create socket variables
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    // Check if socket creation was successful
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_socket); // Close the socket before exiting
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections from any address
    server_addr.sin_port = htons(port); // Set the port number
    
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
    
    printf("[INFO] Server started, waiting for client connections...\n");
    
    // Main server loop to accept and handle client connections
    while (1) {
        // Accept connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        // Get client IP and port
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        
        // Increment thread counter (protected by mutex)
        pthread_mutex_lock(&thread_counter_mutex);
        int client_id = ++thread_count;
        pthread_mutex_unlock(&thread_counter_mutex);
        
        printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n", 
               client_id, client_ip, client_port, client_id);
        
        // Create a new thread to handle the client
        pthread_t thread_id;
        client_info *info = malloc(sizeof(client_info));
        if (info == NULL) {
            perror("malloc");
            close(client_socket);
            continue;
        }
        
        info->client_socket = client_socket;
        info->client_addr = client_addr;
        info->client_id = client_id;
        
        if (pthread_create(&thread_id, NULL, handle_client, (void *)info) != 0) {
            perror("pthread_create");
            free(info);
            close(client_socket);
            continue;
        }
        
        // Detach the thread so it cleans up automatically when done
        pthread_detach(thread_id);
    }
    
    // Close the server socket (this code will never be reached in normal operation)
    close(server_socket);
}