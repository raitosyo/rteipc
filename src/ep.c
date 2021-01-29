// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "rteipc.h"
#include "table.h"
#include "ep.h"


extern __thread struct event_base *__base;

extern struct rteipc_ep_ops ipc_ops;
extern struct rteipc_ep_ops tty_ops;
extern struct rteipc_ep_ops gpio_ops;
extern struct rteipc_ep_ops spi_ops;

struct ep_core {
	int id;
	int partner;
	struct rteipc_ep *ep;
};

static struct rteipc_ep_ops *ep_ops_list[] = {
	[EP_IPC]  = &ipc_ops,
	[EP_TTY]  = &tty_ops,
	[EP_GPIO] = &gpio_ops,
	[EP_SPI] = &spi_ops,
};

static dtbl_t ep_tbl = DTBL_INITIALIZER(MAX_NR_EP);
static pthread_mutex_t ep_mutex = PTHREAD_MUTEX_INITIALIZER;


static void ep_read_cb(struct bufferevent *bev, void *arg)
{
	int id = (intptr_t)arg;
	struct ep_core *core;
	struct rteipc_ep *ep;

	pthread_mutex_lock(&ep_mutex);

	core = dtbl_get(&ep_tbl, id);
	ep = core->ep;
	if (ep && ep->ops->on_data)
		ep->ops->on_data(ep, bev);

	pthread_mutex_unlock(&ep_mutex);
}

static int __do_ep_bind(int a, int b, int bind)
{
	struct ep_core *ca, *cb;
	struct rteipc_ep *ea, *eb;
	struct bufferevent *pair[2];

	ca = dtbl_get(&ep_tbl, a);
	cb = dtbl_get(&ep_tbl, b);
	if (!ca || !cb) {
		fprintf(stderr, "Invalid endpoint is specified\n");
		return -1;
	}

	ea = ca->ep;
	eb = cb->ep;
	if (bind) {
		if (ea->bev || eb->bev) {
			fprintf(stderr, "One of endpoints is busy\n");
			return -1;
		}
		if (bufferevent_pair_new(__base, 0, pair)) {
			fprintf(stderr, "Failed to allocate a socket pair\n");
			return -1;
		}
		ea->bev = pair[0];
		eb->bev = pair[1];
		bufferevent_setcb(ea->bev, ep_read_cb, NULL, NULL,
					(void *)(intptr_t)a);
		bufferevent_setcb(eb->bev, ep_read_cb, NULL, NULL,
					(void *)(intptr_t)b);
		bufferevent_enable(ea->bev, EV_READ);
		bufferevent_enable(eb->bev, EV_READ);
		ca->partner = cb->id;
		cb->partner = ca->id;
	} else {
		if (!ea->bev || !eb->bev ||
		    ea->bev != bufferevent_pair_get_partner(eb->bev)
		) {
			fprintf(stderr,
				"endpoints are not bound\n");
			return -1;
		}
		bufferevent_free(ea->bev);
		bufferevent_free(eb->bev);
		ea->bev = NULL;
		eb->bev = NULL;
		ca->partner = -1;
		cb->partner = -1;
	}
	return 0;
}

int rteipc_ep_bind(int a, int b)
{
	int ret;

	pthread_mutex_lock(&ep_mutex);

	ret = __do_ep_bind(a, b, 1);

	pthread_mutex_unlock(&ep_mutex);
	return ret;
}

int rteipc_ep_unbind(int a, int b)
{
	int ret;

	pthread_mutex_lock(&ep_mutex);

	ret = __do_ep_bind(a, b, 0);

	pthread_mutex_unlock(&ep_mutex);
	return ret;
}

int rteipc_ep_open(const char *uri)
{
	char protocol[16], path[128];
	struct ep_core *core;
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
	} else {
		fprintf(stderr, "Unknown protocol:%s\n", protocol);
		return -1;
	}

	core = malloc(sizeof(*core));
	if (!core) {
		fprintf(stderr, "Failed to allocate memory for core\n");
		return -1;
	}
	core->partner = -1;

	ep = malloc(sizeof(*ep));
	if (!ep) {
		fprintf(stderr, "Failed to allocate memory for ep\n");
		goto free_core;
	}
	ep->base = __base;
	ep->ops = ep_ops_list[type];
	ep->bev = NULL;
	ep->data = NULL;
	core->ep = ep;

	pthread_mutex_lock(&ep_mutex);

	id = dtbl_set(&ep_tbl, core);
	if (id < 0) {
		fprintf(stderr, "Failed to register ep\n");
		goto free_ep;
	}
	core->id = id;

	if (ep->ops->open(ep, path)) {
		fprintf(stderr, "Failed to open ep\n");
		goto unreg_ep;
	}

	pthread_mutex_unlock(&ep_mutex);
	return id;

unreg_ep:
	dtbl_del(&ep_tbl, id);
free_ep:
	free(ep);
free_core:
	free(core);
	pthread_mutex_unlock(&ep_mutex);
	return -1;
}

void rteipc_ep_close(int id)
{
	struct ep_core *core;
	struct rteipc_ep *ep;

	pthread_mutex_lock(&ep_mutex);

	core = dtbl_get(&ep_tbl, id);
	if (core && core->ep) {
		dtbl_del(&ep_tbl, core->id);
		ep = core->ep;
		ep->ops->close(ep);
		if (core->partner >= 0)
			__do_ep_bind(core->id, core->partner, 0);
		free(ep);
		free(core);
	}

	pthread_mutex_unlock(&ep_mutex);
}
