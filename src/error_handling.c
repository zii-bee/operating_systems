#include <stdio.h>
#include <stdlib.h>
#include "error_handling.h"

void handle_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
}
