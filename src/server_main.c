#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"

#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"
#define MAX_INPUT_SIZE 20

int main(int argc, char *argv[]) {
    int port;
    char port_input[MAX_INPUT_SIZE];
    
    // Display the default IP address
    printf("Server running on IP: %s\n", DEFAULT_IP);
    
    // Ask for port number
    printf("PORT: ");
    if (fgets(port_input, sizeof(port_input), stdin) == NULL) {
        fprintf(stderr, "Error reading port number.\n");
        return EXIT_FAILURE;
    }
    
    // Remove trailing newline
    port_input[strcspn(port_input, "\n")] = '\0';
    
    // Check if the user entered a port number
    if (strlen(port_input) == 0) {
        // Use default port
        port = DEFAULT_PORT;
        printf("Using default port: %d\n", port);
    } else {
        // Convert input to integer
        port = atoi(port_input);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Using default port %d.\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }
    
    printf("Starting server on %s:%d...\n", DEFAULT_IP, port);
    start_server(port);
    
    return 0;
}