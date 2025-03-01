#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "pipes.h"
#include "parser.h"
#include "redirection.h"

// max commands in a pipeline
#define MAX_COMMANDS 10

// helper: Trim leading/trailing whitespace
char *trim_whitespace(char *str) {
    if (!str || *str == '\0') return str;  // check for empty string
    
    while(*str == ' ') str++;
    char *end = str + strlen(str) - 1;
    while(end > str && (*end == ' ' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

void execute_pipeline(const char *input) {
    // duplicate input to avoid modifying the original string.
    char *input_copy = strdup(input);
    char *commands[MAX_COMMANDS];
    int num_commands = 0;

    // split input on the pipe symbol.
    char *token = strtok(input_copy, "|");
    while (token != NULL && num_commands < MAX_COMMANDS) {
        commands[num_commands++] = trim_whitespace(token);
        token = strtok(NULL, "|");
    }

    int pipefds[2 * (num_commands - 1)];
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            free(input_copy);
            return;
        }
    }

    for (int i = 0; i < num_commands; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(input_copy);
            return;
        }
        if (pid == 0) {
            // if not the first command, redirect stdin to the previous pipe's read end.
            if (i > 0) {
                if (dup2(pipefds[(i - 1) * 2], 0) < 0) {
                    perror("dup2 stdin");
                    exit(EXIT_FAILURE);
                }
            }
            // if not the last command, redirect stdout to the current pipe's write end.
            if (i < num_commands - 1) {
                if (dup2(pipefds[i * 2 + 1], 1) < 0) {
                    perror("dup2 stdout");
                    exit(EXIT_FAILURE);
                }
            }
            // close all pipe file descriptors in the child
            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefds[j]);
            }

            // parse the individual command
            Command *cmd = parse_command(commands[i]);
            if (!cmd) {
                fprintf(stderr, "Parsing error in pipeline command.\n");
                exit(EXIT_FAILURE);
            }

            // handle redirects
            if (cmd->input_file) {
                if (redirect_input(cmd->input_file) != 0)
                    free_command(cmd);
                    exit(EXIT_FAILURE);
            }
            if (cmd->output_file) {
                if (redirect_output(cmd->output_file) != 0)
                    free_command(cmd);
                    exit(EXIT_FAILURE);
            }
            if (cmd->error_file) {
                if (redirect_error(cmd->error_file) != 0)
                    free_command(cmd);
                    exit(EXIT_FAILURE);
            }

            if (execvp(cmd->args[0], cmd->args) < 0) {
                perror("execvp");
                free_command(cmd);
                exit(EXIT_FAILURE);
            }
        }
    }
    // parent: Close all pipe file descriptors.
    for (int i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefds[i]);
    }
    // wait for all child processes.
    for (int i = 0; i < num_commands; i++) {
        wait(NULL);
    }
    free(input_copy);
}
