#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

void execute_command(Command *cmd);
int handle_builtin_command(Command *cmd);

#endif // EXECUTOR_H
