// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include "rteipc.h"
#include "ep.h"


struct ep_to_str {
	int type;
	char *name;
};

static const struct ep_to_str name_tbl[] = {
	{EP_IPC,      "IPC"},
	{EP_TTY,      "TTY"},
	{EP_GPIO,     "GPIO"},
	{EP_SPI,      "SPI"},
	{EP_I2C,      "I2C"},
	{EP_SYSFS,    "SYSFS"},
	{EP_INET,     "INET"},
};

static inline const char *type_to_str(int type)
{
	int i;
	for (i = 0; i < (sizeof(name_tbl) / sizeof(name_tbl[0])); i++) {
		if (name_tbl[i].type == type)
			return name_tbl[i].name;
	}
	return "UNKNOWN";
}

static void read_cb(struct bufferevent *bev, void *arg)
{
	struct rteipc_ep *ep = arg;
	if (ep && ep->ops->on_data)
		ep->ops->on_data(ep, bev);
}

int rteipc_bind(int lh, int rh)
{
	struct rteipc_ep *le, *re;

	if (!(le = find_endpoint(lh)) || !(re = find_endpoint(rh))) {
		fprintf(stderr, "Invalid endpoint specified\n");
		return -1;
	}

	if (le == re) {
		fprintf(stderr, "Cannot bind an endpoint to self\n");
		return -1;
	}

	if (!ep_compatible(le, re)) {
		fprintf(stderr, "Not compatible endpoints: %s and %s\n",
				type_to_str(le->type), type_to_str(re->type));
		return -1;
	}

	return bind_endpoint(le, re, read_cb, NULL, NULL);
}

void rteipc_unbind(int id)
{
	struct rteipc_ep *ep;

	if (!(ep = find_endpoint(id))) {
		fprintf(stderr, "Invalid endpoint specified\n");
		return;
	}
	unbind_endpoint(ep);
}

int rteipc_open(const char *uri)
{
	char protocol[16] = {0}, path[128] = {0};
	struct rteipc_ep *ep;
	int type, id;

	sscanf(uri, "%[^:]://%99[^\n]", protocol, path);

	if (!strstr(uri, "://")) {
		/* No protocol specified, treated as loopback name */
		type = EP_LOOP;
		strncpy(path, uri, sizeof(path));
	} else if (!strcmp(protocol, "ipc")) {
		type = EP_IPC;
	} else if (!strcmp(protocol, "inet")) {
		type = EP_INET;
	} else if (!strcmp(protocol, "tty")) {
		type = EP_TTY;
	} else if (!strcmp(protocol, "gpio")) {
		type = EP_GPIO;
	} else if (!strcmp(protocol, "spi")) {
		type = EP_SPI;
	} else if (!strcmp(protocol, "i2c")) {
		type = EP_I2C;
	} else if (!strcmp(protocol, "sysfs")) {
		type = EP_SYSFS;
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
