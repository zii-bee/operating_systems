#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"

// defining defaults
#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    
    // Check if port is provided as command-line argument
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Using default port %d.\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }
    
    printf("Server running on %s:%d\n", DEFAULT_IP, port);
    start_server(port);
    
    return 0;
}