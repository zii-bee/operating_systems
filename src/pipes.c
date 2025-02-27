#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "pipes.h"
#include "parser.h"
#include "redirection.h"

// Maximum number of commands in a pipeline
#define MAX_COMMANDS 10

// Helper: Trim leading/trailing whitespace
char *trim_whitespace(char *str) {
    while(*str == ' ') str++;
    char *end = str + strlen(str) - 1;
    while(end > str && (*end == ' ' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

void execute_pipeline(const char *input) {
    // Duplicate input to avoid modifying the original string.
    char *input_copy = strdup(input);
    char *commands[MAX_COMMANDS];
    int num_commands = 0;

    // Split input on the pipe symbol.
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
            // If not the first command, redirect stdin to the previous pipe's read end.
            if (i > 0) {
                if (dup2(pipefds[(i - 1) * 2], 0) < 0) {
                    perror("dup2 stdin");
                    exit(EXIT_FAILURE);
                }
            }
            // If not the last command, redirect stdout to the current pipe's write end.
            if (i < num_commands - 1) {
                if (dup2(pipefds[i * 2 + 1], 1) < 0) {
                    perror("dup2 stdout");
                    exit(EXIT_FAILURE);
                }
            }
            // Close all pipe file descriptors in the child.
            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefds[j]);
            }

            // Parse the individual command.
            Command *cmd = parse_command(commands[i]);
            if (!cmd) {
                fprintf(stderr, "Parsing error in pipeline command.\n");
                exit(EXIT_FAILURE);
            }

            // Handle any redirections for this command.
            if (cmd->input_file) {
                if (redirect_input(cmd->input_file) != 0)
                    exit(EXIT_FAILURE);
            }
            if (cmd->output_file) {
                if (redirect_output(cmd->output_file) != 0)
                    exit(EXIT_FAILURE);
            }
            if (cmd->error_file) {
                if (redirect_error(cmd->error_file) != 0)
                    exit(EXIT_FAILURE);
            }

            if (execvp(cmd->args[0], cmd->args) < 0) {
                perror("execvp");
                free_command(cmd);
                exit(EXIT_FAILURE);
            }
        }
    }
    // Parent: Close all pipe file descriptors.
    for (int i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefds[i]);
    }
    // Wait for all child processes.
    for (int i = 0; i < num_commands; i++) {
        wait(NULL);
    }
    free(input_copy);
}
