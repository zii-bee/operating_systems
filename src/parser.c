#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define MAX_TOKENS 64

Command* parse_command(const char *input) {
    Command *cmd = malloc(sizeof(Command));
    if (!cmd) {
        perror("malloc");
        return NULL;
    }
    cmd->args = malloc(MAX_TOKENS * sizeof(char*));
    if (!cmd->args) {
        perror("malloc");
        free(cmd);
        return NULL;
    }

    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;
    cmd->pipe_count = 0;

    char *input_copy = strdup(input);
    char *token = strtok(input_copy, " ");
    int arg_index = 0;

    while (token != NULL && arg_index < MAX_TOKENS - 1) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " ");
            if (token)
                cmd->input_file = strdup(token);
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " ");
            if (token)
                cmd->output_file = strdup(token);
        } else if (strcmp(token, "2>") == 0) {
            token = strtok(NULL, " ");
            if (token)
                cmd->error_file = strdup(token);
        } else if (strcmp(token, "|") == 0) {
            // pipecounter
            cmd->pipe_count++;
        } else {
            cmd->args[arg_index++] = strdup(token);
        }
        token = strtok(NULL, " ");
    }
    if (arg_index == 0) {
        // no command specified
        fprintf(stderr, "Error: No command specified.\n");
        free_command(cmd);
        return NULL;
    }
    cmd->args[arg_index] = NULL;
    free(input_copy);
    return cmd;
}

void free_command(Command *cmd) {
    if (!cmd) return;
    if (cmd->args) {
        for (int i = 0; cmd->args[i] != NULL; i++) {
            free(cmd->args[i]);
        }
        free(cmd->args);
    }
    if (cmd->input_file) free(cmd->input_file);
    if (cmd->output_file) free(cmd->output_file);
    if (cmd->error_file) free(cmd->error_file);
    free(cmd);
}
