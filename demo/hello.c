// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

/*
  (terminal-1)
  $ ./hello -b ipc://@/tmp/ipc{1,2}
  broker start!

  (terminal-2)
  $ ./hello -p ipc://@/tmp/ipc1 foo
  bar

  (terminal-3)
  $ ./hello -p ipc://@/tmp/ipc2 bar
  foo
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/event.h>
#include "rteipc.h"


static void read_cb(int ctx, void *data, size_t len, void *arg)
{
	struct event_base *base = arg;
	printf("%.*s\n", len, data);
	event_base_loopbreak(base);
}

static void process(const char *uri, const char *msg)
{
	struct event_base *base = event_base_new();
	int ctx;

	rteipc_init(base);
	ctx = rteipc_connect(uri);
	if (ctx < 0) {
		fprintf(stderr, "Failed to connect %s\n", uri);
		return;
	}
	rteipc_setcb(ctx, read_cb, NULL, base, 0);
	rteipc_send(ctx, msg, strlen(msg));
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}

static void broker(const char *uri1, const char *uri2)
{
	int ep1, ep2;

	printf("broker start!\n");

	rteipc_init(NULL);
	ep1 = rteipc_ep_open(uri1);
	ep2 = rteipc_ep_open(uri2);
	if (ep1 < 0 || ep2 < 0) {
		fprintf(stderr, "Failed to open endpoints\n");
		return;
	}
	rteipc_ep_route(ep1, ep2, RTEIPC_ROUTE_ADD);
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "-b")) {
		broker(argv[2], argv[3]);
	} else if (argc == 4 && !strcmp(argv[1], "-p")) {
		process(argv[2], argv[3]);
	} else {
		fprintf(stderr, "Usage: ./hello -b <uri1> <uri2>\n"
				"       ./hello -p <uri> <string>\n");
	}

	return 0;
}
