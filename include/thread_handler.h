#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include <pthread.h>
#include <netinet/in.h>

// structure to hold client connection information
typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
    int client_id;
} client_info;

// function declarations
void *handle_client(void *arg);
void handle_command(int client_socket, const char *command, int client_id);

#endif // THREAD_HANDLER_H