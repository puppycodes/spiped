#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "asprintf.h"
#include "events.h"
#include "network.h"
#include "sock.h"
#include "warnp.h"

#include "simple_server.h"

#define BUFLEN 8192

struct accept_state {
	int s;
	int conndone;
	int shutdown_requested;
	size_t nconn;
	size_t nconn_max;
	void * accept_cookie;
	struct conn_list_node * conn_cookies;
	void * caller_cookie;
	int (* callback_nc_message)(void *, uint8_t *, size_t);
};

/* Doubly linked list. */
struct conn_list_node {
	/* General "dispatch"-level info. */
	struct conn_list_node * prev;
	struct conn_list_node * next;
	struct accept_state * A;

	/* Reading a network message. */
	int sock_read;
	uint8_t buf[BUFLEN];
	void * network_read_cookie;
};

static int callback_read(void *, ssize_t);
static int callback_gotconn(void *, int);
static int conndied(struct conn_list_node *);
static int doaccept(struct accept_state *);
static int drop(struct conn_list_node *);

/* Non-blocking accept, if we can have more connections. */
static int
doaccept(struct accept_state * A)
{
	int rc = 0;

	/* If we can, accept a new connection. */
	if ((A->nconn < A->nconn_max) && (A->accept_cookie == NULL) &&
	    !A->shutdown_requested) {
		if ((A->accept_cookie =
		    network_accept(A->s, callback_gotconn, A)) == NULL)
			rc = -1;
	}

	/* Return success/fail status. */
	return (rc);
}

/* A connection has closed.  Accept more if necessary. */
static int
conndied(struct conn_list_node * node_ptr)
{
	struct accept_state * A = node_ptr->A;

	/* We should always have a non-empty list of conn_cookies. */
	assert(A->conn_cookies != NULL);

	/* We've lost a connection. */
	A->nconn -= 1;

	/* Remove the closed connection from the list of conn_cookies. */
	if (node_ptr == A->conn_cookies) {
		/* Closed conn_cookie is first in the list. */
		A->conn_cookies = node_ptr->next;
		if (node_ptr->next != NULL)
			node_ptr->next->prev = NULL;
	} else {
		/* Closed conn_cookie is in the middle of list. */
		assert(node_ptr->prev != NULL);
		node_ptr->prev->next = node_ptr->next;
		if (node_ptr->next != NULL)
			node_ptr->next->prev = node_ptr->prev;
	}

	/* Clean up the now-unused node. */
	free(node_ptr);

	/* If requested to do so, indicate that all connections are closed. */
	if (A->shutdown_requested && (A->nconn == 0))
		A->conndone = 1;

	/* Maybe accept more connections. */
	return (doaccept(A));
}

/* Handle an incoming connection. */
static int
callback_gotconn(void * cookie, int s)
{
	struct accept_state * A = cookie;
	struct conn_list_node * node_new;

	/* This accept is no longer in progress. */
	A->accept_cookie = NULL;

	/* If we got a -1 descriptor, something went seriously wrong. */
	if (s == -1) {
		warnp("network_accept failed");
		goto err0;
	}

	/* We have gained a connection. */
	A->nconn += 1;

	/* Create new conn_list_node. */
	if ((node_new = malloc(sizeof(struct conn_list_node))) == NULL)
		goto err0;
	node_new->prev = NULL;
	node_new->next = NULL;
	node_new->A = A;
	node_new->sock_read = s;

	/* Schedule reading from this connection. */
	if ((node_new->network_read_cookie = network_read(node_new->sock_read,
	    node_new->buf, BUFLEN, 0, callback_read, node_new)) == NULL)
		goto err1;

	/* Link node_new to the beginning of the conn_cookies list. */
	if (A->conn_cookies != NULL) {
		node_new->next = A->conn_cookies;
		node_new->next->prev = node_new;
	}

	/* Insert node_new to the beginning of the conn_cookies list. */
	A->conn_cookies = node_new;

	/* Accept another connection if we can. */
	if (doaccept(A))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(node_new);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_read(void * cookie, ssize_t lenread)
{
	struct conn_list_node * R = cookie;
	struct accept_state * A = R->A;

	/* Cookie is no longer valid. */
	R->network_read_cookie = NULL;

	/* If we have a message. */
	if (lenread > 0) {
		/* Handle it with the parent code. */
		A->callback_nc_message(A->caller_cookie, R->buf,
		    (size_t)lenread);

		/* We can re-use R. */
		R->network_read_cookie = network_read(R->sock_read, R->buf,
		    BUFLEN, 0, callback_read, R);
	} else if (lenread == 0) {
		drop(R);
	} else {
		warn0("Failed to read from network");
		A->conndone = 1;
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Drop connection. */
static int
drop(struct conn_list_node * node_ptr)
{

	/* If we still have an active read cookie, cancel it. */
	if (node_ptr->network_read_cookie != NULL)
		network_read_cancel(node_ptr->network_read_cookie);

	/* Close the incoming connection. */       
	if (close(node_ptr->sock_read) == -1)
		goto err0;

	/* Clean up the node. */
	conndied(node_ptr);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * simple_server_init(port, nconn_max, callback, caller_cookie):
 * Initializes a server which accepts up to ${nconn_max}
 * connections to port ${port}.  When it receives a message, it
 * calls ${callback} and passes it the ${caller_cookie}, along
 * with the message.  Return a cookie which should be passed to
 * other functions in this header.
 */
void *
simple_server_init(const uint16_t port, const size_t nconn_max,
    int (* callback_nc_message)(void *, uint8_t *, size_t),
    void * caller_cookie)
{
	struct accept_state * A;
	char * addr;
	struct sock_addr ** sas;
	int sock;

	/* Create an address string suitable for sock_resolve. */
	if (asprintf(&addr, "[0.0.0.0]:%" PRIu16, port) == -1) {
		warn0("Failed to allocate memory");
		goto err0;
	}

	/* Resolve the address. */
	if ((sas = sock_resolve(addr)) == NULL) {
		warn0("sock_resolve");
		goto err1;
	}

	/* Create a socket, bind it, mark it as listening. */
	if ((sock = sock_listener(sas[0])) == -1) {
		warn0("sock_listener");
		goto err2;
	}

	/* Bake a cookie for the server. */
	if ((A = malloc(sizeof(struct accept_state))) == NULL)
		goto err3;
	A->s = sock;
	A->conndone = 0;
	A->shutdown_requested = 0;
	A->nconn = 0;
	A->nconn_max = nconn_max;
	A->accept_cookie = NULL;
	A->conn_cookies = NULL;
	A->callback_nc_message = callback_nc_message;
	A->caller_cookie = caller_cookie;

	/* Accept a connection. */
	if (doaccept(A))
		goto err4;

	/* Clean up. */
	sock_addr_freelist(sas);
	free(addr);

	/* Success! */
	return (A);

err4:
	free(A);
err3:
	close(sock);
err2:
	sock_addr_freelist(sas);
err1:
	free(addr);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * simple_server_run(cookie):
 * Runs an event loop until the server associated with ${cookie} quits.
 */
int
simple_server_run(void * cookie)
{
	struct accept_state * A = cookie;

	/* Loop until we die. */
	if (events_spin(&A->conndone)) {
		warnp("Error running event loop");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * simple_server_shutdown(cookie):
 * Stops and frees memory associated with the ${cookie}.
 */
void
simple_server_shutdown(void * cookie)
{
	struct accept_state * A = cookie;
	struct conn_list_node * node_ptr;

	/* Cancel any further accepts. */
	if (A->accept_cookie != NULL)
		network_accept_cancel(A->accept_cookie);

	/*
	 * Shut down any open connections.  drop() will call
	 * conndied(), which removes the relevant conn_list_node
	 * from the list of conn_cookies.
	 */
	while (A->conn_cookies != NULL) {
		/* Remove nodes from the list. */
		node_ptr = A->conn_cookies;
		drop(A->conn_cookies);
		/*
		 * Force the clang static analyzer to realize that
		 * the A->conn_cookies pointer changed.
 		 */
		assert(node_ptr != A->conn_cookies);
	}

	/* Close socket and free memory. */
	close(A->s);
	free(A);
}

/**
 * simple_server_request_shutdown(cookie):
 * Requests a shutdown: Stop accepting new connections and notify once
 * every existing connection ended.
 */
void
simple_server_request_shutdown(void * cookie)
{
	struct accept_state * A = cookie;

	/* Signal that we're shutting down. */
	A->shutdown_requested = 1;

	/* Cancel any further accepts. */
	if (A->accept_cookie != NULL) {
		network_accept_cancel(A->accept_cookie);
		A->accept_cookie = NULL;
	}

	/* If no connections are open... */
	if (A->nconn == 0) {
		/* Indicate that all connections are closed. */
		A->conndone = 1;
	}
}
