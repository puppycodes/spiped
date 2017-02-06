#ifndef SIMPLE_SERVER_H
#define SIMPLE_SERVER_H

#include <stdint.h>

/**
 * simple_server_init(port, nconn_max, callback, caller_cookie):
 * Initializes a server which accepts up to ${nconn_max}
 * connections to port ${port}.  When it receives a message, it
 * calls ${callback} and passes it the ${caller_cookie}, along
 * with the message.  Return a cookie which should be passed to
 * other functions in this header.
 */
void * simple_server_init(const uint16_t, const size_t,
    int (*)(void *, uint8_t *, size_t), void *);

/**
 * simple_server_run(cookie):
 * Runs an event loop until the server associated with ${cookie} quits.
 */
int simple_server_run(void *);

/**
 * simple_server_shutdown(cookie):
 * Stops and frees memory associated with the ${cookie}.
 */
void simple_server_shutdown(void *);

/**
 * simple_server_request_shutdown(cookie):
 * Requests a shutdown: Stop accepting new connections and notify once
 * every existing connection ended.
 */
void simple_server_request_shutdown(void *);

#endif
