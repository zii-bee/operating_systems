#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include <pthread.h>
#include <netinet/in.h>

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
    int client_id;  // Added client_id to track client number
} client_info;

void *handle_client(void *arg);
void handle_command(int client_socket, const char *command, int client_id, char *client_ip, int client_port);

#endif // THREAD_HANDLER_H