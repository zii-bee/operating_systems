#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "executor.h"
#include "redirection.h"

void execute_command(Command *cmd) {
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
    } else {
        // parent process: wait for the child to finish
        int status;
        waitpid(pid, &status, 0);
    }
}
