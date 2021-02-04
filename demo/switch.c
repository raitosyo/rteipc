// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

/*
  (terminal-1)
  $ ./switch -b ipc://@/tmp/ipc{1,2,3}
  broker start!

  (terminal-2)
  $ ./switch -p ipc://@/tmp/ipc1 foo
  bar
  baz

  (terminal-3)
  $ ./switch -p ipc://@/tmp/ipc2 bar
  foo
  baz

  (terminal-4)
  $ ./switch -p ipc://@/tmp/ipc3 baz
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

static bool sw_filter(const char *src, const char *dst, void *data, size_t len)
{
	return true;
}

static void broker(const char *uri1, const char *uri2, const char *uri3)
{
	int sw, swep, i;
	int ep[3];

	printf("broker start!\n");

	rteipc_init(NULL);

	/* create a switch instance */
	sw = rteipc_sw();
	if (rteipc_ep_bind(rteipc_port(sw, "ipc1"), rteipc_ep_open(uri1)) ||
	    rteipc_ep_bind(rteipc_port(sw, "ipc2"), rteipc_ep_open(uri2)) ||
	    rteipc_ep_bind(rteipc_port(sw, "ipc3"), rteipc_ep_open(uri3))) {
		fprintf(stderr, "Failed to open endpoints\n");
		return;
	}
	rteipc_filter(sw, sw_filter);

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
		fprintf(stderr, "Usage: ./switch -b <uri1> <uri2> <uri3>\n"
				"       ./switch -p <uri> <string>\n");
	}

	return 0;
}
