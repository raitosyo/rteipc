// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include "rteipc.h"
#include "ep.h"


static void read_cb(struct bufferevent *bev, void *arg)
{
	struct rteipc_ep *ep = arg;
	if (ep && ep->ops->on_data)
		ep->ops->on_data(ep, bev);
}

int rteipc_bind(int lh, int rh)
{
	return bind_endpoint(find_endpoint(lh), find_endpoint(rh), read_cb,
			NULL, NULL);
}

void rteipc_unbind(int id)
{
	unbind_endpoint(find_endpoint(id));
}

int rteipc_open(const char *uri)
{
	char protocol[16], path[128];
	struct rteipc_ep *ep;
	int type, id;

	sscanf(uri, "%[^:]://%99[^\n]", protocol, path);

	if (!strcmp(protocol, "ipc")) {
		type = EP_IPC;
	} else if (!strcmp(protocol, "tty")) {
		type = EP_TTY;
	} else if (!strcmp(protocol, "gpio")) {
		type = EP_GPIO;
	} else if (!strcmp(protocol, "spi")) {
		type = EP_SPI;
	} else if (!strcmp(protocol, "i2c")) {
		type = EP_I2C;
	} else {
		fprintf(stderr, "Unknown protocol:%s\n", protocol);
		return -1;
	}

	if (!(ep = allocate_endpoint(type)))
		return -1;

	if ((id = register_endpoint(ep)) < 0)
		goto out_put;

	if (ep->ops->open(ep, path)) {
		fprintf(stderr, "Failed to open ep\n");
		goto out_unreg;
	}
	return id;

out_unreg:
	unregister_endpoint(ep);
out_put:
	destroy_endpoint(ep);
	return -1;
}

void rteipc_close(int id)
{
	struct ep_core *core;
	struct rteipc_ep *ep;

	ep = find_endpoint(id);
	if (!ep) {
		fprintf(stderr, "Can not close an invalid open endpoint\n");
		return;
	}

	if (ep->ops && ep->ops->close)
		ep->ops->close(ep);

	unregister_endpoint(ep);
	destroy_endpoint(ep);
}
