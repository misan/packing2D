#include "SignalHandler.h"
#include <signal.h>
#include <iostream>

// Define and initialize the global flag.
std::atomic<bool> g_interrupt_received(false);

// The actual signal handler function.
void signal_handler_func(int signum) {
    if (signum == SIGINT) {
        g_interrupt_received = true;
    }
}

// Function to set up the signal handler for SIGINT.
void setup_signal_handler() {
    signal(SIGINT, signal_handler_func);
}
