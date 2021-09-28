// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

/*
 * An endpoint is a core design of rteipc, and it's a representation of and an
 * interface with a process, file, or peripheral (e.g., gpio, tty). Each
 * endpoint can be bound to any other but only one at the same time.
 *
 * The data stream is transferred between each endpoint bound together, and the
 * only way for a process to read/write the data from/to an endpoint is to
 * connect to the EP_IPC endpoint only with which a process can talk.
 *
 * For example, if a process wants to read/write data from/to EP_TTY, should
 * connect to EP_IPC and read/write data through it.
 *
 *        EP_TTY                                    EP_IPC
 *    +-------------+                           +-------------+
 *    |  rteipc_ep  |                           |  rteipc_ep  |
 *    |             |                           |             |
 *    |     .bev <--|------- Data Stream -------|--> .bev     |
 *    |     .ops <--|----------+    +-----------|--> .ops     |
 *    |             |          |    |           |             |
 *    +-------------+          |    |           +-------------+
 *                             |    |
 *                  read/write |    | read/write
 *       Backend  <------------+    +------------>  Backend  <------+
 *      (/dev/tty*)                              (Unix Socket)      |
 *                                                                  |
 *                                                       read/write |
 *                          +---------+                             |
 *                          | Process |<----------------------------+
 *                          +---------+
 *
 * The data format in the stream is determined by each endpoint.
 */
#ifndef _RTEIPC_EP_H
#define _RTEIPC_EP_H

#include <event2/event.h>
#include <event2/bufferevent.h>

#define MAX_NR_EP		(2 * DESC_BIT_WIDTH)

#define EP_TEMPLATE	0
#define EP_IPC		1
#define EP_TTY		2
#define EP_GPIO		3
#define EP_SPI		4
#define EP_I2C		5
#define EP_SYSFS	6
#define EP_INET		7  /* EP_INET is implemented as an EP_IPC extention */

#define COMPAT_ANY		(~0)
#define COMPAT_IPC		((1 << EP_IPC)|(1 << EP_INET))
#define COMPAT_TTY		(1 << EP_TTY)
#define COMPAT_GPIO		(1 << EP_GPIO)
#define COMPAT_SPI		(1 << EP_SPI)
#define COMPAT_I2C		(1 << EP_I2C)
#define COMPAT_SYSFS		(1 << EP_SYSFS)

#define COMPATIBLE_WITH(name, mask)                   \
	static inline int name##_compatible(int val)  \
	{                                             \
		return !!((1 << val) & (mask));       \
	}

struct rteipc_ep;

struct rteipc_ep_ops {
	int (*open)(struct rteipc_ep *self, const char *path);
	void (*close)(struct rteipc_ep *self);
	void (*on_data)(struct rteipc_ep *self, struct bufferevent *bev);
	int (*compatible)(int type);
};

struct rteipc_ep {
	int type;
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

struct rteipc_ep *get_partner_endpoint(struct rteipc_ep *ep);

static inline int ep_compatible(struct rteipc_ep *lh, struct rteipc_ep *rh)
{
	if (!lh->ops->compatible || !rh->ops->compatible)
		return 0;

	if ((lh->ops->compatible(rh->type) &&
			rh->ops->compatible(lh->type)))
		return 2;  /* bidirectional */

	if ((lh->ops->compatible(rh->type) ||
			rh->ops->compatible(lh->type)))
		return 1;  /* single-directional */

	return 0;  /* Not compatible */
}

#endif /* _RTEIPC_EP_H */
