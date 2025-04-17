#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include <pthread.h>

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
} client_info;

void *handle_client(void *arg);

#endif // THREAD_HANDLER_H