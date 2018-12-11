// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

/*
  (terminal-1)
  $ ./relay -b ipc://@/tmp/ipc{1,2,3}
  broker start!

  (terminal-2)
  $ ./relay -r ipc://@/tmp/ipc1
  2 5 8

  (terminal-3)
  $ ./relay -r ipc://@/tmp/ipc2
  3 6 9

  (terminal-4)
  $ ./relay -s ipc://@/tmp/ipc3
  relay start!
  1 4 7 10
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
	int n = *((int *)data);
	fprintf(stderr, "%d ", n++);
	rteipc_send(ctx, &n, 4);
	usleep(500 * 1000);
}

static void process(int starter, const char *uri)
{
	int ctx, count = 1;

	rteipc_init(NULL);
	ctx = rteipc_connect(uri, read_cb, NULL);
	if (ctx < 0) {
		fprintf(stderr, "Failed to connect %s\n", uri);
		return;
	}

	if (starter) {
		printf("relay start!\n");
		fprintf(stderr, "%d ", count++);
		rteipc_send(ctx, &count, 4);
	}
	rteipc_dispatch();
	rteipc_shutdown();
}

static void broker(const char *uri1, const char *uri2, const char *uri3)
{
	int ep1, ep2, ep3;

	printf("broker start!\n");

	rteipc_init(NULL);
	ep1 = rteipc_ep_open(uri1);
	ep2 = rteipc_ep_open(uri2);
	ep3 = rteipc_ep_open(uri3);
	if (ep1 < 0 || ep2 < 0 || ep3 < 0) {
		fprintf(stderr, "Failed to open endpoints\n");
		return;
	}

	rteipc_ep_route(ep1, ep2, RTEIPC_FORWARD);
	rteipc_ep_route(ep1, ep3, RTEIPC_REVERSE);
	rteipc_ep_route(ep2, ep3, RTEIPC_FORWARD);
	rteipc_dispatch();
	rteipc_shutdown();
}

int main(int argc, char **argv)
{
	if (argc == 5 && !strcmp(argv[1], "-b")) {
		broker(argv[2], argv[3], argv[4]);
	} else if (argc == 3 && !strcmp(argv[1], "-s")) {
		process(1, argv[2]);
	} else if (argc == 3 && !strcmp(argv[1], "-r")) {
		process(0, argv[2]);
	} else {
		fprintf(stderr, "Usage: ./relay -b <uri1> <uri2> <uri3>\n"
				"       ./relay -s|r <uri>\n");
	}

	return 0;
}
