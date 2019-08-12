//
// Copyright 2019 Staysail Systems, Inc.
// Provided under the MIT License. See LICENSE.txt.
//

// This attempts to set up a simple echo test.
//
// Usage: nng-loadtest server <url> [<count>]
//        nng-loadtest client <url> [<count> [<delay in msec>]]
//
// Server will run <count> contexts in parallel.  Defaults to 1024.
// Note that this may have a different value than the client <count>, but there
// is no point in making it larger than the total number of all client counts.
//
// Client will start up <count> contexts (default is just 1), each of which
// will attempt to perform requests and wait for responses.  The optional delay
// injects a randomized delay of between delay/2 delay mseconds before starting
// each new request attempt.  This can help with server loading.  The default
// delay of zero msec will tend to pound the server.
//
// The client will open a new pipe for each count.  While this isn't strictly
// required, doing so allows for maximum scaling (which is what we want to
// test for.)
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>

struct server_state {
	int      state;
	nng_ctx  ctx;
	nng_aio *aio;
	nng_msg *msg;
};

static void
server_cb(void *a)
{
	struct server_state *s = a;
	int                  rv;
	unsigned int         when;

	switch (s->state) {
	case 0:

		s->state = 1;
		nng_ctx_recv(s->ctx, s->aio);
		return;
	case 1:
		if ((rv = nng_aio_result(s->aio)) != 0) {
			fprintf(stderr, "ctx_recv: %s\n", nng_strerror(rv));
			exit(1);
		}

		s->msg = nng_aio_get_msg(s->aio);
		if ((rv = nng_msg_trim_u32(s->msg, &when)) != 0) {
			// bad message, just ignore it.
			nng_msg_free(s->msg);
			s->msg = NULL;
			nng_ctx_recv(s->ctx, s->aio);
			return;
		}
		s->state = 2;
		nng_sleep_aio(when, s->aio);
		return;

	case 2:
		s->state = 3;
		nng_aio_set_msg(s->aio, s->msg);
		s->msg = NULL;
		nng_ctx_send(s->ctx, s->aio);
		return;

	case 3:
		if ((rv = nng_aio_result(s->aio)) != 0) {
			fprintf(stderr, "ctx_send: %s\n", nng_strerror(rv));
			exit(1);
		}

		s->state = 1;
		nng_ctx_recv(s->ctx, s->aio);
		return;
	}
}

static int
server(const char *url, int count)
{
	struct server_state *states;
	int                  rv;
	nng_socket           sock;

	if (count < 0) {
		count = 1;
	}
	if ((states = calloc(count, sizeof(struct server_state))) == NULL) {
		fprintf(stderr, "calloc failed\n");
		exit(1);
	}
	if ((rv = nng_rep0_open(&sock)) != 0) {
		fprintf(stderr, "rep0_open: %s\n", nng_strerror(rv));
		exit(1);
	}
	if ((rv = nng_listen(sock, url, NULL, 0)) != 0) {
		fprintf(stderr, "listen: %s\n", nng_strerror(rv));
		exit(1);
	}

	// Create <count> worker contexts.
	for (int i = 0; i < count; i++) {
		struct server_state *sp = &states[i];

		if ((rv = nng_aio_alloc(&sp->aio, server_cb, sp)) != 0) {
			fprintf(stderr, "aio_alloc: %s\n", nng_strerror(rv));
			exit(1);
		}
		if ((rv = nng_ctx_open(&sp->ctx, sock)) != 0) {
			fprintf(stderr, "ctx_open: %s\n", nng_strerror(rv));
			exit(1);
		}
	}

	// Now start them all.
	for (int i = 0; i < count; i++) {
		server_cb(&states[i]);
	}

	for (;;) {
		// This could also be sleep, or pause, if those were portable.
		// This wakes up once every minute.
		nng_msleep(60000);
	}
}

struct client_state {
	int      state;
	nng_ctx  ctx;
	nng_aio *aio;
	nng_msg *msg;
	int      dly; // max delay, will randomize
};

static void
client_cb(void *a)
{
	struct client_state *s = a;
	int                  rv;
	int                  dly;

	switch (s->state) {
	case 0:
		if ((rv = nng_msg_alloc(&s->msg, 0)) != 0) {
			fprintf(stderr, "msg_alloc: %s\n", nng_strerror(rv));
			exit(1);
		}
		if (s->dly != 0) {
			// range is between dly/2 and dly.
			dly = (rand() % (s->dly / 2)) + s->dly / 2;
		} else {
			dly = 0;
		}
		if ((rv = nng_msg_append_u32(s->msg, dly)) != 0) {
			fprintf(stderr, "msg_append: %s\n", nng_strerror(rv));
			exit(1);
		}
		nng_aio_set_msg(s->aio, s->msg);
		s->msg   = NULL;
		s->state = 1;
		nng_ctx_send(s->ctx, s->aio);
		break;

	case 1:
		if ((rv = nng_aio_result(s->aio)) != 0) {
			fprintf(stderr, "send: %s\n", nng_strerror(rv));
			exit(1);
		}
		s->state = 2;
		nng_ctx_recv(s->ctx, s->aio);
		break;

	case 2:
		if ((rv = nng_aio_result(s->aio)) != 0) {
			fprintf(stderr, "recv: %s\n", nng_strerror(rv));
			exit(1);
		}
		s->msg = nng_aio_get_msg(s->aio);
		nng_msg_free(s->msg);
		s->msg   = NULL;
		s->state = 0;
		fprintf(stdout, ".");
		fflush(stdout);
		client_cb(s);
		break;
	}
}

static void
client(const char *url, int count, int delay)
{
	struct client_state *states;
	int                  rv;
	nng_socket           sock;

	if (count < 0) {
		count = 1;
	}
	if ((states = calloc(count, sizeof(struct client_state))) == NULL) {
		fprintf(stderr, "calloc failed\n");
		exit(1);
	}
	if ((rv = nng_req0_open(&sock)) != 0) {
		fprintf(stderr, "req0_open: %s\n", nng_strerror(rv));
		exit(1);
	}

	// Create <count> worker contexts.
	for (int i = 0; i < count; i++) {
		struct client_state *sp = &states[i];

		// Intentionally, we dial separately for each socket.
		if ((rv = nng_dial(sock, url, NULL, 0)) != 0) {
			fprintf(stderr, "dial: %s\n", nng_strerror(rv));
			exit(1);
		}

		if ((rv = nng_aio_alloc(&sp->aio, client_cb, sp)) != 0) {
			fprintf(stderr, "aio_alloc: %s\n", nng_strerror(rv));
			exit(1);
		}
		if ((rv = nng_ctx_open(&sp->ctx, sock)) != 0) {
			fprintf(stderr, "ctx_open: %s\n", nng_strerror(rv));
			exit(1);
		}
	}

	// Now start them all.
	for (int i = 0; i < count; i++) {
		client_cb(&states[i]);
	}

	for (;;) {
		// This could also be sleep, or pause, if those were portable.
		// This wakes up once every minute.
		nng_msleep(60000);
	}
}

static void
usage(const char *n)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s server <url> [<count>]\n", n);
	fprintf(stderr, "  %s client <url> [<count> [<delay(ms)>]]\n", n);
	exit(1);
}

int
main(int argc, char **argv)
{

	if (argc < 2) {
		usage(argv[0]);
	}
	if (strcmp(argv[1], "server") == 0) {
		int count = 0;
		switch (argc) {
		case 3:
			count = 1;
			break;
		case 4:
			if ((count = atoi(argv[3])) < 1) {
				fprintf(stderr, "count must be positive\n");
				exit(1);
			}
			break;
		default:
			usage(argv[0]);
			break;
		}
		server(argv[2], count);
		exit(0);
	}

	if (strcmp(argv[1], "client") == 0) {
		int count = 0;
		int delay = 0;

		switch (argc) {
		case 3:
			count = 1;
			delay = 0;
			break;
		case 4:
			if ((count = atoi(argv[3])) < 1) {
				fprintf(stderr, "count must be positive\n");
				exit(1);
			}
			delay = 0;
			break;
		case 5:
			if ((count = atoi(argv[3])) < 1) {
				fprintf(stderr, "count must be positive\n");
				exit(1);
			}

			if ((delay = atoi(argv[4])) < 1) {
				fprintf(stderr, "delay must be positive\n");
				exit(1);
			}
			break;
		default:
			usage(argv[0]);
			break;
		}
		client(argv[2], count, delay);
		exit(0);
	}

	usage(argv[0]);
}
