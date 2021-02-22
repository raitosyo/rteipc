// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "rteipc.h"

static void read_tty(int ctx, void *data, size_t len, void *arg)
{
	/* Print serial output from TTY device */
	printf("%.*s", len, data);
}

static void write_tty(evutil_socket_t fd, short what, void *arg)
{
	int ctx = (intptr_t)arg;
	char msg[256];
	size_t len;

	len = read(fd, msg, sizeof(msg));
	rteipc_send(ctx, msg, len);
}

void main(int argc, char **argv)
{
	const char *ipc = "ipc://@/sample_tty";
	struct event_base *base = event_base_new();
	struct event *ev;
	int ctx;

	if (argc != 2) {
		fprintf(stderr, "Usage: ./sample_tty <uri>\n"
				"  (example: # ./sample_tty tty:///dev/ttyS0,115200)\n");
		return;
	}

	rteipc_init(base);

	if (rteipc_bind(rteipc_open(ipc), rteipc_open(argv[1]))) {
		fprintf(stderr, "Failed to bind %s\n", argv[1]);
		return;
	}

	if ((ctx = rteipc_connect(ipc)) < 0) {
		fprintf(stderr, "Failed to connect %s\n", ipc);
		return;
	}

	/* Redirect stdin to TTY device */
	ev = event_new(base, STDIN_FILENO, EV_READ | EV_PERSIST,
				write_tty, NULL);
	event_add(ev, (void *)(intptr_t)ctx);

	rteipc_setcb(ctx, read_tty, NULL, NULL, 0);
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}
