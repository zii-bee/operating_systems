#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Demo program to simulate a process that runs for N seconds
 * Usage: ./demo N
 * Where N is the number of seconds the program should run
 */
int main(int argc, char *argv[]) {
    // Check if the number of seconds is provided
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <seconds>\n", argv[0]);
        return 1;
    }
    
    // Parse the number of seconds
    int seconds = atoi(argv[1]);
    if (seconds <= 0) {
        fprintf(stderr, "Error: Number of seconds must be positive\n");
        return 1;
    }
    
    printf("Starting demo process that will run for %d seconds\n", seconds);
    
    // Simulate work by sleeping and printing progress
    for (int i = 1; i <= seconds; i++) {
        printf("Demo process: Iteration %d of %d\n", i, seconds);
        fflush(stdout); // Ensure output is immediately visible
        sleep(1);       // Sleep for 1 second
    }
    
    printf("Demo process completed after %d seconds\n", seconds);
    return 0;
}
