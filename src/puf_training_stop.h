#ifndef PUF_TRAINING_STOP_H
#define PUF_TRAINING_STOP_H

#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t puf_training_stop_signal = 0;

static void puf_training_stop_handler(int sig) {
    if (puf_training_stop_signal) _exit(128 + sig);
    int saved_errno = errno;
    puf_training_stop_signal = sig;
    const char message[] = "\nStop requested: finishing the current update and saving policy. "
        "Signal again to force exit without waiting.\n";
    // No CUDA, allocation, stdio, locks, or checkpoint I/O in the handler.
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    errno = saved_errno;
}

struct PufTrainingStop {
    bool enabled;
    struct sigaction old_int, old_term;
    explicit PufTrainingStop(bool enable) : enabled(enable) {
        puf_training_stop_signal = 0;
        if (!enabled) return;
        struct sigaction action = {};
        action.sa_handler = puf_training_stop_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if (sigaction(SIGINT, &action, &old_int) ||
                sigaction(SIGTERM, &action, &old_term)) {
            perror("install training stop handler");
            exit(1);
        }
    }
    bool requested() const { return enabled && puf_training_stop_signal != 0; }
    ~PufTrainingStop() {
        if (enabled) {
            sigaction(SIGINT, &old_int, NULL);
            sigaction(SIGTERM, &old_term, NULL);
        }
    }
};

#endif
