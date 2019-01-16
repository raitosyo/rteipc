// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_EP_H
#define _RTEIPC_EP_H

#include <event2/event.h>
#include <event2/bufferevent.h>

#define MAX_NR_EP		(2 * DESC_BIT_WIDTH)

#define RTEIPC_IPC		0
#define RTEIPC_TTY		1
#define RTEIPC_GPIO		2

#define RTEIPC_ROUTE_ADD	0
#define RTEIPC_ROUTE_DEL	1

struct rteipc_ep;

struct rteipc_ep_ops {
	int (*bind)(struct rteipc_ep *self, const char *path);
	void (*unbind)(struct rteipc_ep *self);
	int (*route)(struct rteipc_ep *self, int what,
			struct bufferevent *up, struct bufferevent *down);
};

struct rteipc_ep {
	struct event_base *base;
	struct rteipc_ep_ops *ops;
	void *data;
};

#endif /* _RTEIPC_EP_H */
