#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // default number of iterations
    int n = 5;
    
    // check if a specific number of iterations was provided
    if (argc > 1) {
        n = atoi(argv[1]);
        if (n <= 0) {
            printf("Invalid number of iterations. Using default of 5.\n");
            n = 5;
        }
    }
    
    // loop n times, simulating work with sleep
    for (int i = 1; i <= n; i++) {
        printf("Demo %d/%d\n", i, n);
        fflush(stdout); // ensure output is sent immediately
        
        // sleep for 1 second to simulate work
        sleep(1);
    }
    
    return 0;
}