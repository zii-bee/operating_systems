#ifndef PARSER_H
#define PARSER_H

typedef struct Command {
    char **args;           // NULL-terminated array of arguments
    char *input_file;      // input redirection file
    char *output_file;     // output redirection file
    char *error_file;      // error redirection file
    int pipe_count;        // number of pipes detected n
} Command;

Command* parse_command(const char *input);
void free_command(Command *cmd);

#endif // PARSER_H
