#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

// setting defaults to run for client
#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    char ip_address[16];
    
    // use default IP unless specified
    strcpy(ip_address, DEFAULT_IP);
    
    // check if IP address is provided as command-line argument
    if (argc > 1) {
        strcpy(ip_address, argv[1]);
    }
    
    // check if port is provided as command-line argument
    if (argc > 2) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Using default port %d.\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }
    
    printf("Client connecting to %s:%d\n", ip_address, port);
    start_client(ip_address, port);
    
    return 0;
}