// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "rteipc.h"
#include "table.h"
#include "ep.h"


extern __thread struct event_base *__base;

extern struct rteipc_ep_ops ep_ipc;
extern struct rteipc_ep_ops ep_tty;
extern struct rteipc_ep_ops ep_gpio;

static struct rteipc_ep_ops *ep_ops_list[] = {
	[RTEIPC_IPC]  = &ep_ipc,
	[RTEIPC_TTY]  = &ep_tty,
	[RTEIPC_GPIO] = &ep_gpio,
};

static dtbl_t ep_tbl = DTBL_INITIALIZER(MAX_NR_EP);
static pthread_mutex_t ep_mutex = PTHREAD_MUTEX_INITIALIZER;


static void ep_read_cb(struct bufferevent *bev, void *arg)
{
	int id = (intptr_t)arg;
	struct rteipc_ep *ep;

	pthread_mutex_lock(&ep_mutex);

	ep = dtbl_get(&ep_tbl, id);
	if (ep && ep->ops->on_data)
		ep->ops->on_data(ep, bev);

	pthread_mutex_unlock(&ep_mutex);
}

static int do_ep_bind(int a, int b, int bind)
{
	struct rteipc_ep *ea, *eb;
	struct bufferevent *pair[2];

	pthread_mutex_lock(&ep_mutex);

	ea = dtbl_get(&ep_tbl, a);
	eb = dtbl_get(&ep_tbl, b);

	if (!ea || !eb) {
		fprintf(stderr, "Invalid endpoint is specified\n");
		goto err;
	}

	if (bind) {
		if (ea->bev || eb->bev) {
			fprintf(stderr, "One of endpoints is busy\n");
			goto err;
		}
		if (bufferevent_pair_new(__base, 0, pair)) {
			fprintf(stderr, "Failed to allocate a socket pair\n");
			goto err;
		}
		ea->bev = pair[0];
		eb->bev = pair[1];
		bufferevent_setcb(ea->bev, ep_read_cb, NULL, NULL,
					(void *)(intptr_t)a);
		bufferevent_setcb(eb->bev, ep_read_cb, NULL, NULL,
					(void *)(intptr_t)b);
		bufferevent_enable(ea->bev, EV_READ);
		bufferevent_enable(eb->bev, EV_READ);
	} else {
		if (!ea->bev || !eb->bev ||
		    ea->bev != bufferevent_pair_get_partner(eb->bev)
		) {
			fprintf(stderr,
				"endpoints are not bound\n");
			goto err;
		}
		bufferevent_free(ea->bev);
		bufferevent_free(eb->bev);
		ea->bev = NULL;
		eb->bev = NULL;
	}

	pthread_mutex_unlock(&ep_mutex);
	return 0;

err:
	pthread_mutex_unlock(&ep_mutex);
	return -1;
}

int rteipc_ep_bind(int a, int b)
{
	return do_ep_bind(a, b, 1);
}

int rteipc_ep_unbind(int a, int b)
{
	return do_ep_bind(a, b, 0);
}

int rteipc_ep_open(const char *uri)
{
	char protocol[16], path[128];
	struct rteipc_ep *ep;
	int type, id;

	sscanf(uri, "%[^:]://%99[^\n]", protocol, path);

	if (!strcmp(protocol, "ipc")) {
		type = RTEIPC_IPC;
	} else if (!strcmp(protocol, "tty")) {
		type = RTEIPC_TTY;
	} else if (!strcmp(protocol, "gpio")) {
		type = RTEIPC_GPIO;
	} else {
		fprintf(stderr, "Unknown protocol:%s\n", protocol);
		return -1;
	}

	ep = malloc(sizeof(*ep));
	if (!ep) {
		fprintf(stderr, "Failed to allocate memory for ep\n");
		return -1;
	}
	ep->base = __base;
	ep->ops = ep_ops_list[type];
	ep->bev = NULL;
	ep->data = NULL;

	pthread_mutex_lock(&ep_mutex);

	id = dtbl_set(&ep_tbl, ep);

	if (id < 0) {
		fprintf(stderr, "Failed to register ep\n");
		goto free_ep;
	}

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
	pthread_mutex_unlock(&ep_mutex);
	return -1;
}

void rteipc_ep_close(int id)
{
	struct rteipc_ep *ep;

	pthread_mutex_lock(&ep_mutex);

	ep = dtbl_get(&ep_tbl, id);
	if (ep) {
		dtbl_del(&ep_tbl, id);
		ep->ops->close(ep);
		if (ep->bev) {
			bufferevent_free(bufferevent_pair_get_partner(
						ep->bev));
			bufferevent_free(ep->bev);
		}
		free(ep);
	}

	pthread_mutex_unlock(&ep_mutex);
}
