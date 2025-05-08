#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include "demo.h"

void run_demo(int n, int client_socket, int client_id, int task_id) {
    char buffer[128];
    
    // loop n times, simulate work with sleep
    for (int i = 1; i <= n; i++) {
        snprintf(buffer, sizeof(buffer), "Demo %d/%d\n", i, n);
        
        // send the current iteration to the client
        if (client_socket > 0) {
            send(client_socket, buffer, strlen(buffer), 0);
        }
        
        // print to server console
        printf("Task #%d (Client #%d): %s", task_id, client_id, buffer);
        
        // simulate work with sleep (1 second per iteration)
        sleep(1);
    }
}