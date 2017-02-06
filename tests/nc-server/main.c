#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "events.h"
#include "warnp.h"

#include "graceful_shutdown.h"
#include "simple_server.h"

#define MAX_CONNECTIONS 2

struct nc_cookie {
	FILE * out;
};

/* Forward definition. */
static int callback_snc_response(void * cookie, uint8_t * buf, size_t buflen);
static int callback_begin_shutdown(void * C);

/* A client sent a message. */
static int
callback_snc_response(void * cookie, uint8_t * buf, size_t buflen)
{
	struct nc_cookie * C = cookie;

	/* Write buffer to the previously-opened file. */
	if (fwrite(buf, sizeof(uint8_t), buflen, C->out) != buflen)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_begin_shutdown(void * C)
{

	simple_server_request_shutdown(C);

	/* Success! */
	return 0;
}

int
main(int argc, char ** argv)
{
	struct nc_cookie * C;
	void * S;
	unsigned long port_long;
	uint16_t port;
	const char * filename;

	(void)argc; /* UNUSED */

	WARNP_INIT;

	/* Parse command-line arguments. */
	if (argc < 3) {
		fprintf(stderr, "usage: %s PORT FILENAME\n", argv[0]);
		goto err0;
	}

	/* Parse port number. */
	errno = 0;
	port_long = strtoul(argv[1], NULL, 0);
	if (errno || port_long > UINT16_MAX)
		goto err0;
	port = (uint16_t)port_long;

	/* Get output filename. */
	filename = argv[2];

	/* Register a handler for SIGTERM. */
	if (graceful_shutdown_initialize())
		goto err0;

	/* Initialize our cookie. */
	if ((C = malloc(sizeof(struct nc_cookie))) == NULL)
		goto err0;

	/* Open the output file; can be /dev/null. */
	if ((C->out = fopen(filename, "w+b")) == NULL) {
		warnp("fopen");
		goto err1;
	}

	/* Initialize the server. */
	if ((S = simple_server_init(port, MAX_CONNECTIONS,
	    &callback_snc_response, C)) == NULL)
		goto err2;

	/* Begin checking for any shutdown requests. */
	if (graceful_shutdown_monitor(&callback_begin_shutdown, S))
		goto err3;

	/* Run until SIGTERM or an error. */
	if (simple_server_run(S))
		goto err3;

	/* Write the output file. */
	if (fclose(C->out) != 0) {
		warnp("fclose");
		goto err3;
	}

	/* Clean up. */
	simple_server_shutdown(S);
	events_shutdown();
	free(C);

	/* Success! */
	exit(0);

err3:
	simple_server_shutdown(S);
	events_shutdown();
err2:
	fclose(C->out);
err1:
	free(C);
err0:
	/* Failure! */
	exit(1);
}
