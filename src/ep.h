// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_EP_H
#define _RTEIPC_EP_H

#include <event2/event.h>
#include <event2/bufferevent.h>

#define MAX_NR_EP		(2 * DESC_BIT_WIDTH)

#define EP_IPC		0
#define EP_TTY		1
#define EP_GPIO		2
#define EP_SPI		3


/*
 * An endpoint is a core design of rteipc, and it's a representation of and an
 * interface with a process, file or peripheral (e.g., gpio, tty). Each
 * endpoint can be bound to any other but only one in the same time.
 *
 * The data stream is transferred between each endpoint bound together, and the
 * only way for a process to read/write the data from/to an endpoint is to
 * connect to the EP_IPC endpoint only with which a process can talk.
 *
 * For example, if a process want to read/write data from/to EP_TTY, should
 * connect to EP_IPC and read/write data through it.
 *
 *        EP_TTY                                    EP_IPC
 *    +-------------+                           +-------------+
 *    |  rteipc_ep  |                           |  rteipc_ep  |
 *    |             |                           |             |
 *    |     .bev <--|--- Paired bufferevents ---|--> .bev     |
 *    |     .ops <--|----------+    +-----------|--> .ops     |
 *    |             |          |    |           |             |
 *    +-------------+          |    |           +-------------+
 *                             |    |
 *                  read/write |    | read/write
 *     '/dev/tty*' <-----------+    +-----------> 'Unix socket' <---+
 *                                                                  |
 *                                                       read/write |
 *                          +---------+                             |
 *                          | Process |<----------------------------+
 *                          +---------+
 *
 * The data format is determined by and depends on each endpoint.
 */

struct rteipc_ep;

struct rteipc_ep_ops {
	int (*open)(struct rteipc_ep *self, const char *path);
	void (*close)(struct rteipc_ep *self);
	void (*on_data)(struct rteipc_ep *self, struct bufferevent *bev);
};

struct rteipc_ep {
	struct event_base *base;
	struct bufferevent *bev;
	struct rteipc_ep_ops *ops;
	void *data;
};

int register_endpoint(struct rteipc_ep *ep);

void unregister_endpoint(struct rteipc_ep *ep);

int bind_endpoint(struct rteipc_ep *lh, struct rteipc_ep *rh,
	bufferevent_data_cb readcb, bufferevent_data_cb writecb,
	bufferevent_event_cb eventcb);

void unbind_endpoint(struct rteipc_ep *ep);

struct rteipc_ep *find_endpoint(int desc);

struct rteipc_ep *allocate_endpoint(int type);

void destroy_endpoint(struct rteipc_ep *ep);

#endif /* _RTEIPC_EP_H */
