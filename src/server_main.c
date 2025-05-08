#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"

#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"

int main(void) {
    printf("| Server Started on %s:%d |\n", DEFAULT_IP, DEFAULT_PORT);
    start_server(DEFAULT_PORT);
    return 0;
}