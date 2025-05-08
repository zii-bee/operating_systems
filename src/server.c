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
#include "scheduler.h"
#include "signal_handling.h"

// Maximum size of input buffer
#define MAX_INPUT_SIZE 1024
#define BACKLOG 5 // Maximum number of pending connections

// Global thread counter
static int thread_count = 0;
pthread_mutex_t thread_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

void start_server(int port) {
    // setup signal handlers for proper termination
    setup_signal_handlers();
    
    // initialize the scheduler
    scheduler_init();
    scheduler_start();
    
    // create socket variables
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        scheduler_stop();
        scheduler_cleanup();
        exit(EXIT_FAILURE);
    }
    
    // set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_socket);
        scheduler_stop();
        scheduler_cleanup();
        exit(EXIT_FAILURE);
    }
    
    // configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // bind socket to address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        scheduler_stop();
        scheduler_cleanup();
        exit(EXIT_FAILURE);
    }
    
    // listen for connections
    if (listen(server_socket, BACKLOG) < 0) {
        perror("listen");
        close(server_socket);
        scheduler_stop();
        scheduler_cleanup();
        exit(EXIT_FAILURE);
    }
    
    printf("| Hello, Server Started |\n");
    
    // main server loop
    while (1) {
        // accept client connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        // get client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        
        // assign client ID
        pthread_mutex_lock(&thread_counter_mutex);
        int client_id = ++thread_count;
        pthread_mutex_unlock(&thread_counter_mutex);
        
        printf("[%d]<<< client connected\n", client_id);
        
        // create thread to handle this client
        pthread_t thread_id;
        client_info *info = malloc(sizeof(client_info));
        if (!info) {
            perror("malloc");
            close(client_socket);
            continue;
        }
        
        info->client_socket = client_socket;
        info->client_addr = client_addr;
        info->client_id = client_id;
        
        if (pthread_create(&thread_id, NULL, handle_client, info) != 0) {
            perror("pthread_create");
            free(info);
            close(client_socket);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    // this will never be reached in normal operation
    close(server_socket);
    scheduler_stop();
    scheduler_cleanup();
}