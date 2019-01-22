// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

/*
  (terminal-1)
  $ ./broadcast -b ipc://@/tmp/ipc{1,2,3}
  broker start!

  (terminal-2)
  $ ./broadcast -p ipc://@/tmp/ipc1 foo
  bar
  baz

  (terminal-3)
  $ ./broadcast -p ipc://@/tmp/ipc2 bar
  foo
  baz

  (terminal-4)
  $ ./broadcast -p ipc://@/tmp/ipc3 baz
  foo
  bar
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/event.h>
#include "rteipc.h"


static int sw_ep[3];

static void read_cb(int ctx, void *data, size_t len, void *arg)
{
	printf("%.*s\n", len, data);
}

static void process(const char *uri, const char *msg)
{
	int ctx;

	rteipc_init(NULL);
	ctx = rteipc_connect(uri);
	if (ctx < 0) {
		fprintf(stderr, "Failed to connect %s\n", uri);
		return;
	}
	rteipc_setcb(ctx, read_cb, NULL, NULL, 0);
	rteipc_send(ctx, msg, strlen(msg));
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}

static void swep_cb(int sw, int from_ep, void *data, size_t len, void *arg)
{
	int to_ep, i;

	for (i = 0; i < 3; i++) {
		to_ep = sw_ep[i];
		if (to_ep == from_ep)
			continue;
		rteipc_sw_xfer(sw, to_ep, data, len);
	}
}

static void broker(const char *uri1, const char *uri2, const char *uri3)
{
	int sw, swep, i;
	int ep[3];

	printf("broker start!\n");

	rteipc_init(NULL);
	ep[0] = rteipc_ep_open(uri1);
	ep[1] = rteipc_ep_open(uri2);
	ep[2] = rteipc_ep_open(uri3);
	if (ep[0] < 0 || ep[1] < 0 || ep[2] < 0) {
		fprintf(stderr, "Failed to open endpoints\n");
		return;
	}

	/* create a switch instance */
	sw = rteipc_sw();
	rteipc_sw_setcb(sw, swep_cb, NULL, 0);
	for (i = 0; i < 3; i++) {
		sw_ep[i] = rteipc_sw_ep_open(sw);
		rteipc_ep_bind(ep[i], sw_ep[i]);
	}

	rteipc_dispatch(NULL);
	rteipc_shutdown();
}

int main(int argc, char **argv)
{
	if (argc == 5 && !strcmp(argv[1], "-b")) {
		broker(argv[2], argv[3], argv[4]);
	} else if (argc == 4 && !strcmp(argv[1], "-p")) {
		process(argv[2], argv[3]);
	} else {
		fprintf(stderr, "Usage: ./broadcast -b <uri1> <uri2> <uri3>\n"
				"       ./broadcast -p <uri> <string>\n");
	}

	return 0;
}
