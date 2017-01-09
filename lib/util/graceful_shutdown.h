#ifndef GRACEFUL_SHUTDOWN_H
#define GRACEFUL_SHUTDOWN_H

/**
 * graceful_shutdown_register():
 * Initializes a signal handler for SIGTERM.
 */
int graceful_shutdown_initialize(void);

/**
 * graceful_shutdown_monitor(callback, caller_cookie):
 * Starts a continuous 1-second timer which checks if SIGTERM was given; if
 * detected, calls ${callback} and gives it the ${caller_cookie}.
 */
int graceful_shutdown_monitor(int (*)(void *), void *);

#endif
