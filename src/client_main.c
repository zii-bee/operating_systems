#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"

int main(void) {
    start_client(DEFAULT_IP, DEFAULT_PORT);
    return 0;
}