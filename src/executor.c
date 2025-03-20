#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include "executor.h"
#include "redirection.h"

#define COLOR_GREEN "\033[1;32m"

void execute_command(Command *cmd) {
    // check if the command is a built-in command
    if (handle_builtin_command(cmd)) {
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        // child process: set up redirections if specified
        if (cmd->input_file) {
            if (redirect_input(cmd->input_file) != 0)
                exit(EXIT_FAILURE);
        }
        if (cmd->output_file) { // redirect output to a file
            if (redirect_output(cmd->output_file) != 0)
                exit(EXIT_FAILURE);
        }
        if (cmd->error_file) { // redirect error output to a file
            if (redirect_error(cmd->error_file) != 0)
                exit(EXIT_FAILURE);
        }
        if (execvp(cmd->args[0], cmd->args) < 0) {
            // execvp only returns if an error occurs
            if (errno == ENOENT) { // command not found
                fprintf(stderr, "Command not found: \"" COLOR_GREEN "%s" COLOR_GREEN "\"\n", cmd->args[0]);
            } else {
                perror("execvp");
            }
            free_command(cmd);
            exit(EXIT_FAILURE);
        }
    } else {
        // parent process: wait for the child to finish
        int status;
        waitpid(pid, &status, 0);
    }
}

// handle built-in commands
int handle_builtin_command(Command *cmd) {
    if (strcmp(cmd->args[0], "cd") == 0) {
        // Change directory
        if (cmd->args[1] == NULL) {
            // No arguments, change to home directory
            chdir(getenv("HOME"));
        } else {
            if (chdir(cmd->args[1]) != 0) {
                perror("cd");
            }
        }
        return 1; // Command was handled
    }
    return 0; // Not a built-in command
}