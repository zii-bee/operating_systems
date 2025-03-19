#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "parser.h"
#include "executor.h"
#include "pipes.h"
#include "server.h"
#include "client.h"

#define MAX_INPUT_SIZE 1024
#define DEFAULT_PORT 8080

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s             - Run in local shell mode\n", program_name);
    fprintf(stderr, "  %s -s [port]   - Run in server mode (default port: %d)\n", program_name, DEFAULT_PORT);
    fprintf(stderr, "  %s -c ip [port] - Run in client mode (default port: %d)\n", program_name, DEFAULT_PORT);
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    if (argc > 1) {
        // Server mode
        if (strcmp(argv[1], "-s") == 0) {
            int port = DEFAULT_PORT;
            if (argc > 2) {
                port = atoi(argv[2]);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Invalid port number. Using default port %d.\n", DEFAULT_PORT);
                    port = DEFAULT_PORT;
                }
            }
            start_server(port);
            return 0;
        }
        // Client mode
        else if (strcmp(argv[1], "-c") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Missing server IP address.\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            const char *ip = argv[2];
            int port = DEFAULT_PORT;
            if (argc > 3) {
                port = atoi(argv[3]);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Invalid port number. Using default port %d.\n", DEFAULT_PORT);
                    port = DEFAULT_PORT;
                }
            }
            start_client(ip, port);
            return 0;
        }
        else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    
    // Local shell mode (original code)
    char input[MAX_INPUT_SIZE];

    while (1) {
        printf("$ ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // remove trailing newline
        input[strcspn(input, "\n")] = 0;
        if(strcmp(input, "exit") == 0) {
            break;
        }

        // if the input contains a pipe, delegate to the pipeline executor.
        if (strchr(input, '|')) {
            if (strstr(input, "||") != NULL) {
                fprintf(stderr, "Error: Empty command between pipes.\n");
                continue;
            }
            execute_pipeline(input);
            continue;
        }

        // parse a single command
        Command *cmd = parse_command(input);
        if (!cmd) {
            fprintf(stderr, "Parsing error.\n");
            continue;
        }

        // execute the parsed command
        execute_command(cmd);
        free_command(cmd);
    }

    return 0;
}