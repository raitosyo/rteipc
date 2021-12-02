// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "rteipc.h"

static void read_tty(const char *name, void *data, size_t len, void *arg)
{
	/* Print serial output from TTY device */
	printf("%.*s", len, data);
}

static void write_tty(evutil_socket_t fd, short what, void *arg)
{
	char msg[256];
	size_t len;

	len = read(fd, msg, sizeof(msg));
	rteipc_xfer("lo", msg, len);
}

void main(int argc, char **argv)
{
	struct event_base *base = event_base_new();
	struct event *ev;

	if (argc != 2 || argv[1] != strstr(argv[1], "tty://")) {
		fprintf(stderr, "Usage: %s <uri>\n"
				"  (example: # ./sample_tty tty:///dev/ttyS0,115200)\n",
				argv[0]);
		return;
	}

	rteipc_init(base);

	if (rteipc_bind(rteipc_open("lo"), rteipc_open(argv[1]))) {
		fprintf(stderr, "Failed to bind %s\n", argv[1]);
		return;
	}

	/* Redirect stdin to TTY device */
	ev = event_new(base, STDIN_FILENO, EV_READ | EV_PERSIST,
				write_tty, NULL);
	event_add(ev, NULL);

	rteipc_xfer_setcb("lo", read_tty, NULL);
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}
