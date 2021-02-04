// Copyright (c) 2018 - 2021 Ryosuke Saito All rights reserved.
// MIT licensed

/*
 * A port is a special endpoint that can be bound to any other like a normal
 * endpoint except it has no backend (i.e., it won't read/write any data
 * from/to the backend).
 * While the constraint that an endpoint can be bound to only one at the same
 * time gives its simplicity, but often inconvenient. For that, the switch and
 * port are introduced. In other words, using them makes it as if an endpoint
 * can be bound together more than one.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "rteipc.h"
#include "ep.h"
#include "message.h"
#include "table.h"


#define MAX_NR_SW		DESC_BIT_WIDTH

#define node_to_port(n) \
	(struct rteipc_port *)((char *)(n) - (char *)&((struct rteipc_port *)0)->entry)

extern __thread struct event_base *__base;

struct rteipc_ep_ops port_ops;

struct rteipc_sw {
	int id;
	list_t port_list;
	rteipc_filter_cb filter;
};

struct rteipc_port {
	char *key;
	struct rteipc_ep *ep;
	struct rteipc_sw *sw;
	node_t entry;
};

static dtbl_t sw_tbl = DTBL_INITIALIZER(MAX_NR_SW);


static void port_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct rteipc_port *src = self->data;
	struct rteipc_sw *sw = src->sw;
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len;
	int ret;
	node_t *n;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		list_each(&sw->port_list, n, {
			struct rteipc_port *dst = node_to_port(n);
			if (src != dst && sw->filter(src->key, dst->key,
						msg, len)) {
				rteipc_buffer(dst->ep->bev, msg, len);
			}
		})
		free(msg);
	}
}

static void port_close(struct rteipc_ep *self)
{
	struct rteipc_port *port = self->data;
	struct rteipc_sw *sw = port->sw;
	list_remove(&sw->port_list, &port->entry);
	free(port->key);
	free(port);
}

int rteipc_filter(int desc, rteipc_filter_cb filter)
{
	struct rteipc_sw *sw;

	if (!(sw = dtbl_get(&sw_tbl, desc))) {
		fprintf(stderr, "Invalid switch id\n");
		return -1;
	}
	sw->filter = filter;
	return 0;
}

int rteipc_port(int desc, const char *key)
{
	struct rteipc_sw *sw;
	struct rteipc_port *port;
	struct rteipc_ep *ep;
	int id;

	if (!(sw = dtbl_get(&sw_tbl, desc))) {
		fprintf(stderr, "Invalid switch id\n");
		return -1;
	}

	if (!(port = calloc(1, sizeof(*port)))) {
		fprintf(stderr, "Failed to allocate memory for port\n");
		return -1;
	}
	if (key)
		port->key = calloc(1, strlen(key) ?: 1);

	if (!port->key) {
		fprintf(stderr, "Failed to allocate memory for port\n");
		return -1;
	}

	if (strlen(key))
		memcpy(port->key, key, strlen(key));

	/* setup custom endpoint */
	if (!(ep = allocate_endpoint(EP_TEMPLATE)))
		goto out_port;

	port->ep = ep;
	port->sw = sw;
	ep->data = port;
	ep->ops = &port_ops;

	if ((id = register_endpoint(ep)) < 0)
		goto out_ep;

	list_push(&sw->port_list, &port->entry);
	return id;

out_ep:
	destroy_endpoint(ep);
out_port:
	free(port->key);
	free(port);
	return -1;
}

int rteipc_sw(void)
{
	struct rteipc_sw *sw;
	int desc;

	sw = malloc(sizeof(*sw));
	if (!sw) {
		fprintf(stderr, "Failed to create sw\n");
		return -1;
	}

	list_init(&sw->port_list);

	desc = dtbl_set(&sw_tbl, sw);
	if (desc < 0) {
		fprintf(stderr, "Failed to register sw\n");
		free(sw);
		return -1;
	}
	sw->id = desc;

	return desc;
}

struct rteipc_ep_ops port_ops = {
	.on_data = port_on_data,
	.open = NULL,
	.close = port_close,
};
