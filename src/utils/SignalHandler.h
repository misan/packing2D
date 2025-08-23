#pragma once

#include <atomic>
#include <csignal>
#include <iostream>

// Use inline for the global variable to prevent multiple definition errors (C++17).
inline std::atomic<bool> g_interrupt_received(false);

// The actual signal handler function.
inline void signal_handler_func(int signum) {
    if (signum == SIGINT) {
        g_interrupt_received = true;
    }
}

// Function to set up the signal handler for SIGINT.
inline void setup_signal_handler() {
    // Use a static flag to ensure the handler is only set up once.
    static bool handler_is_setup = false;
    if (!handler_is_setup) {
        signal(SIGINT, signal_handler_func);
        handler_is_setup = true;
    }
}