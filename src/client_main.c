#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"

int main(int argc, char *argv[]) {
    start_client(DEFAULT_IP, DEFAULT_PORT);
    return 0;
}

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "client.h"

// // setting defaults to run for client
// #define DEFAULT_PORT 8080
// #define DEFAULT_IP "127.0.0.1"
// #define MAX_INPUT_SIZE 20

// int main(int argc, char *argv[]) {
//     int port;
//     char port_input[MAX_INPUT_SIZE];
//     char ip_address[MAX_INPUT_SIZE];
    
//     // use default IP unless specified
//     strcpy(ip_address, DEFAULT_IP);
    
//     // check if IP address is provided as command-line argument
//     if (argc > 1) {
//         strcpy(ip_address, argv[1]);
//     }
    
//     // display the IP address we're using
//     printf("Client connecting to IP: %s\n", ip_address);
    
//     // ask for port number
//     printf("PORT: ");
//     if (fgets(port_input, sizeof(port_input), stdin) == NULL) {
//         fprintf(stderr, "Error reading port number.\n");
//         return EXIT_FAILURE;
//     }
    
//     // remove trailing newline
//     port_input[strcspn(port_input, "\n")] = '\0';
    
//     // check if the user entered a port number
//     if (strlen(port_input) == 0) {
//         // use default port
//         port = DEFAULT_PORT;
//         printf("Using default port: %d\n", port);
//     } else {
//         // convert input to integer
//         port = atoi(port_input);
//         if (port <= 0 || port > 65535) {
//             fprintf(stderr, "Invalid port number. Using default port %d.\n", DEFAULT_PORT);
//             port = DEFAULT_PORT;
//         }
//     }
    
//     // printf("Connecting to server at %s:%d...\n", ip_address, port);
//     start_client(ip_address, port);
    
//     return 0;
// }