#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "scheduler.h"

static void handle_signal(int signal) {
    printf("\nReceived signal %d, shutting down...\n", signal);
    
    // stop the scheduler and clean up
    scheduler_stop();
    scheduler_cleanup();
    
    exit(EXIT_SUCCESS);
}

void setup_signal_handlers(void) {
    // register signal handlers for proper program termination
    signal(SIGINT, handle_signal);  // handle Ctrl+C
    signal(SIGTERM, handle_signal); // handle termination signal
}