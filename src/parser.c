#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

#define MAX_TOKENS 64

// parse a command while handling quotes and redirection with care
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

    // create a duplicate of the input so we can modify it safely
    char *input_copy = strdup(input);
    if (!input_copy) {
        perror("strdup");
        free(cmd->args);
        free(cmd);
        return NULL;
    }

    int arg_index = 0;
    char *curr = input_copy;
    int in_quotes = 0;
    char quote_char = '\0';
    char token[1024] = {0};
    int token_index = 0;

    // loop through each character in the input until we reach the end or fill our tokens
    while (*curr != '\0' && arg_index < MAX_TOKENS - 1) {
        // check if we hit a quote and it isn't escaped
        if ((*curr == '"' || *curr == '\'') && (curr == input_copy || *(curr-1) != '\\')) {
            if (!in_quotes) {
                in_quotes = 1;
                quote_char = *curr;
                curr++;
                continue;
            } else if (*curr == quote_char) {
                in_quotes = 0;
                curr++;
                continue;
            }
        }

        // if we're inside quotes or encountering non-space characters, build the current token
        if (in_quotes || (!isspace(*curr) && *curr != '\0')) {
            token[token_index++] = *curr++;
            continue;
        }

        // reached a delimiter (space or end), so finish processing the token we've built
        if (token_index > 0) {
            token[token_index] = '\0';
            token_index = 0;

            // check if token is a redirection operator and handle accordingly
            if (strcmp(token, "<") == 0) {
                // move past any extra spaces to reach the filename
                while (isspace(*curr)) curr++;

                // capture the filename for input redirection, accounting for quotes if needed
                char filename[1024] = {0};
                int f_index = 0;
                
                // if the filename is quoted, extract everything within the quotes
                if (*curr == '"' || *curr == '\'') {
                    quote_char = *curr++;
                    while (*curr && *curr != quote_char) {
                        filename[f_index++] = *curr++;
                    }
                    if (*curr == quote_char) curr++; // skip the closing quote
                } else {
                    // if not quoted, grab characters until the next space
                    while (*curr && !isspace(*curr)) {
                        filename[f_index++] = *curr++;
                    }
                }
                
                filename[f_index] = '\0';
                cmd->input_file = strdup(filename);
            } else if (strcmp(token, ">") == 0) {
                // skip spaces before the output filename starts
                while (isspace(*curr)) curr++;

                // capture the filename for output redirection, supporting quoted filenames
                char filename[1024] = {0};
                int f_index = 0;
                
                // if filename is enclosed in quotes, process accordingly
                if (*curr == '"' || *curr == '\'') {
                    quote_char = *curr++;
                    while (*curr && *curr != quote_char) {
                        filename[f_index++] = *curr++;
                    }
                    if (*curr == quote_char) curr++; // skip the closing quote
                } else {
                    while (*curr && !isspace(*curr)) {
                        filename[f_index++] = *curr++;
                    }
                }
                
                filename[f_index] = '\0';
                cmd->output_file = strdup(filename);
            } else if (strcmp(token, "2>") == 0) {
                // skip any extra spaces to locate the error redirection filename
                while (isspace(*curr)) curr++;

                // capture the filename for error redirection, handling quotes if present
                char filename[1024] = {0};
                int f_index = 0;
                
                // if the filename is quoted, extract the content within the quotes
                if (*curr == '"' || *curr == '\'') {
                    quote_char = *curr++;
                    while (*curr && *curr != quote_char) {
                        filename[f_index++] = *curr++;
                    }
                    if (*curr == quote_char) curr++; // skip over the closing quote
                } else {
                    while (*curr && !isspace(*curr)) {
                        filename[f_index++] = *curr++;
                    }
                }
                
                filename[f_index] = '\0';
                cmd->error_file = strdup(filename);
            } else if (strcmp(token, "|") == 0) {
                // found a pipe symbol, so just increase our pipe count for later processing
                cmd->pipe_count++;
            } else {
                // it's a normal argument, so add it to our list of command arguments
                cmd->args[arg_index++] = strdup(token);
            }
        }

        // skip any spaces between tokens to get ready for the next token
        while (isspace(*curr)) curr++;
    }

    // if we exit the loop while still inside a quote, that's an error because quotes didn't match
    if (in_quotes) {
        fprintf(stderr, "Error: Unmatched quotes.\n");
        free_command(cmd);
        free(input_copy);
        return NULL;
    }

    // if there's any token data left at the end, add it to our command arguments
    if (token_index > 0) {
        token[token_index] = '\0';
        cmd->args[arg_index++] = strdup(token);
    }

    // mark the end of our arguments list with a null pointer for execvp compatibility
    cmd->args[arg_index] = NULL;

    // if no arguments were added, then no command was provided, which is an error
    if (arg_index == 0) {
        fprintf(stderr, "Error: No command specified.\n");
        free_command(cmd);
        free(input_copy);
        return NULL;
    }

    // free our temporary duplicate of the input, as it's no longer needed
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
