#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "events.h"
#include "network.h"
#include "sock.h"
#include "warnp.h"

struct senddata {
	char * buffer;
	int socket;
	int conndone;
};

/* Forward declarations. */
static int send_input(void *);
static int callback_wrote(void * cookie, ssize_t lenwrit);
static int callback_connected(void * cookie, int socket);

/* Send data from stdin to a socket, or close the connection. */
static int
send_input(void * cookie)
{
	struct senddata * send = (struct senddata *)cookie;
	size_t len = 0;
	ssize_t read;

	/* Ensure that the buffer is clear. */
	if (send->buffer != NULL) {
		free(send->buffer);
		send->buffer = NULL;
	}

	/* Read data from stdin. */
	if ((read = getline(&send->buffer, &len, stdin)) != -1) {
		/* Send data to server. */
		if (network_write(send->socket, (uint8_t *)send->buffer,
		    (size_t)read, (size_t)read, callback_wrote, cookie)
		    == NULL) {
			warn0("network_write failure");
			goto err0;
		};
	} else {
		/* Close connection without sending anything. */
		send->conndone = 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (1);
}

/* Finished writing data; look for more from stdin. */
static int
callback_wrote(void * cookie, ssize_t lenwrit)
{

	(void)lenwrit; /* UNUSED */

	return send_input(cookie);
}

/* Got a connection; look for data from stdin. */
static int
callback_connected(void * cookie, int socket)
{
	struct senddata * send = (struct senddata *)cookie;

	/* Record socket for future use. */
	send->socket = socket;

	return send_input(cookie);
}

int
main(int argc, char ** argv)
{
	/* Command-line parameter. */
	const char * addr;

	/* Working variables. */
	struct sock_addr ** sas_t;
	void * connect_cookie = NULL;
	struct senddata send_allocated;
	struct senddata * send = &send_allocated;

	WARNP_INIT;

	/* Parse command-line arguments. */
	if (argc < 2) {
		fprintf(stderr, "%s ADDRESS\n", argv[0]);
		goto err0;
	}
	addr = argv[1];

	/* Initialize cookie. */
	send->buffer = NULL;
	send->conndone = 0;

	/* Resolve target address. */
	if ((sas_t = sock_resolve(addr)) == NULL) {
		warnp("Error resolving socket address: %s", addr);
		goto err1;
	}
	if (sas_t[0] == NULL) {
		warn0("No addresses found for %s", addr);
		goto err2;
	}

	if ((connect_cookie = network_connect(sas_t, callback_connected,
	    send)) == NULL) {
		warn0("Error connecting");
		goto err2;
	}

	/* Loop until we die. */
	if (events_spin(&send->conndone)) {
		warnp("Error running event loop");
		goto err3;
	}

	/* Clean up. */
	events_shutdown();
	sock_addr_freelist(sas_t);
	if (send->buffer != NULL) {
		free(send->buffer);
		send->buffer = NULL;
	}

	/* Success! */
	exit(0);

err3:
	network_connect_cancel(connect_cookie);
err2:
	sock_addr_freelist(sas_t);
err1:
	if (send->buffer != NULL) {
		free(send->buffer);
		send->buffer = NULL;
	}
err0:
	/* Failure! */
	exit(1);
}
