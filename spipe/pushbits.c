#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "noeintr.h"
#include "warnp.h"

#include "pushbits.h"

struct push {
	uint8_t buf[BUFSIZ];
	int in;
	int out;
	pthread_t thr;
};

/* Bit-pushing thread. */
static void *
workthread(void * cookie)
{
	struct push * P = cookie;
	ssize_t readlen;

	/* Infinite loop unless we hit EOF or an error. */
	do {
		/* Read data and die on error. */
		if ((readlen = read(P->in, P->buf, BUFSIZ)) == -1) {
			if (errno == EINTR)
				continue;
			warnp("Error reading");
			exit(1);
		}

		/* If we hit EOF, exit the loop. */
		if (readlen == 0)
			break;

		/* Write the data back out. */
		if (noeintr_write(P->out, &P->buf, readlen) != readlen) {
			warnp("Error writing");
			exit(1);
		}
	} while (1);

	/* Close the descriptor we hit EOF on. */
	close(P->in);

	/*
	 * Try to shut down the descriptor we're writing to.  Ignore ENOTSOCK,
	 * since it might, indeed, not be a socket.
	 */
	if (shutdown(P->out, SHUT_WR)) {
		if (errno != ENOTSOCK) {
			warnp("Error shutting down socket");
			exit(1);
		}
	}

	/* We're done. */
	return (NULL);
}

/**
 * pushbits(in, out):
 * Create a thread which copies data from ${in} to ${out}.
 */
void *
pushbits(int in, int out)
{
	struct push * P;
	int rc;

	/* Allocate structure. */
	if ((P = malloc(sizeof(struct push))) == NULL)
		goto err0;
	P->in = in;
	P->out = out;

	/* Create thread. */
	if ((rc = pthread_create(&P->thr, NULL, workthread, P)) != 0) {
		warn0("pthread_create: %s", strerror(rc));
		goto err1;
	}

	/* Success! */
	return (P);

err1:
	free(P);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * pushbits_free(pushbits_cookie):
 * Free memory associated with the ${pushbits_cookie}.
 */
void
pushbits_free(void * cookie)
{
	struct push * P = cookie;

	/* Behave consistently with free(NULL). */
	if (P == NULL)
		return;

	/* Wait for thread to exit. */
	pthread_join(P->thr, NULL);

	/* Clean up memory. */
	free(P);
}

/**
 * pushbits_cancel(push_cookie):
 * Cancel the thread in ${push_cookie} and free associated memory.
 */
void
pushbits_cancel_free(void * cookie)
{
	struct push * P = cookie;

	/* Send cancellation request to the thread. */
	if (pthread_cancel(P->thr)) {
		warn0("Could not schedule thread cancellation");
		exit(1);
	}

	/* Free memory. */
	pushbits_free(P);
}
