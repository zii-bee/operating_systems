#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "executor.h"
#include "pipes.h"

#define MAX_INPUT_SIZE 1024

int main() {
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
