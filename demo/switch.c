// Copyright (c) 2018 - 2021 Ryosuke Saito All rights reserved.
// MIT licensed

/*
  (terminal-1)
  $ ./switch -b ipc://@/tmp/ipc{1,2,3}
  broker start!

  (terminal-2)
  $ ./switch -p ipc://@/tmp/ipc1
  Greetings from ipc://@/tmp/ipc2
  Greetings from ipc://@/tmp/ipc3
  (Then, type)
  hello from ipc1

  (terminal-3)
  $ ./switch -p ipc://@/tmp/ipc2
  GREETINGS FROM IPC://@/TMP/IPC1
  Greetings from ipc://@/tmp/ipc3
  HELLO FROM IPC1

  (terminal-4)
  $ ./switch -p ipc://@/tmp/ipc3
  GREETINGS FROM IPC://@/TMP/IPC1
  Greetings from ipc://@/tmp/ipc2
  HELLO FROM IPC1
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "rteipc.h"


static void read_cb(int ctx, void *data, size_t len, void *arg)
{
	printf("%.*s", len, data);
}

static void write_cb(evutil_socket_t fd, short what, void *arg)
{
	int ctx = (intptr_t)arg;
	char msg[256];
	size_t len;

	len = read(fd, msg, sizeof(msg));
	rteipc_send(ctx, msg, len);
}

static void process(const char *uri)
{
	struct event_base *base = event_base_new();
	struct event *ev;
	int ctx;
	char *fmt = "Greetings from %s\n";
	char msg[256];

	rteipc_init(base);
	ctx = rteipc_connect(uri);
	if (ctx < 0) {
		fprintf(stderr, "Failed to connect %s\n", uri);
		return;
	}
	rteipc_setcb(ctx, read_cb, NULL, NULL, 0);

	/* Redirect stdin to IPC */
	ev = event_new(base, STDIN_FILENO, EV_READ | EV_PERSIST,
				write_cb, NULL);
	event_add(ev, (void *)(intptr_t)ctx);

	sprintf(msg, fmt, uri);
	rteipc_send(ctx, msg, strlen(msg));
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}

void p1_cb(int sw, void *data, size_t len)
{
	char *c = data;
	int i;

	// convert lowercase string to uppercase on "p1".
	for (i = 0; i < len; i++)
		c[i] = toupper(c[i]);

	// broadcast uppercase string to all the other ports
	rteipc_xfer(sw, "p2", data, len);
	rteipc_xfer(sw, "p3", data, len);
}

static void broker(const char *uri1, const char *uri2, const char *uri3)
{
	int sw;

	printf("broker start!\n");

	rteipc_init(NULL);

	/* Create a switch instance */
	sw = rteipc_sw();

	/*
	 * Create ports that belong to the switch, bind each port and endpoint.
	 * "p1", "p2", "p3" are names of the ports by which each port is
	 * identified.
	 */
	if (rteipc_bind(rteipc_port(sw, "p1"), rteipc_open(uri1)) ||
	    rteipc_bind(rteipc_port(sw, "p2"), rteipc_open(uri2)) ||
	    rteipc_bind(rteipc_port(sw, "p3"), rteipc_open(uri3))) {
		fprintf(stderr, "Failed to open endpoints\n");
		return;
	}

	/*
	 * Switch will send all the data received on a port to all the other
	 * ports. If you want to change this behavior, set your own handler for
	 * the switch by rteipc_sw_setcb() or for the port by
	 * rteipc_port_setcb().
	 *
	 * In this example, data received from "p1" will be converted to
	 * uppercase strings, then sent to all the other ports. Data received
	 * from "p2" and "p3" will be handled by the default switch handler
	 * (i.e. broadcast).
	 */
	rteipc_port_setcb(sw, "p1", p1_cb);

	rteipc_dispatch(NULL);
	rteipc_shutdown();
}

int main(int argc, char **argv)
{
	if (argc == 5 && !strcmp(argv[1], "-b")) {
		broker(argv[2], argv[3], argv[4]);
	} else if (argc == 3 && !strcmp(argv[1], "-p")) {
		process(argv[2]);
	} else {
		fprintf(stderr, "Usage: ./switch -b <uri1> <uri2> <uri3>\n"
				"       ./switch -p <uri>\n");
	}

	return 0;
}
