// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "rteipc.h"
#include "ep.h"


extern __thread struct event_base *__base;

extern struct rteipc_ep_ops ep_ipc;
extern struct rteipc_ep_ops ep_tty;

static struct rteipc_ep *__ep_table[MAX_NR_EP];
static int __ep_index;
static pthread_mutex_t ep_tbl_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct rteipc_ep_ops *ep_ops_list[] = {
	&ep_ipc,
	&ep_tty,
};


static inline struct rteipc_ep *ep_get(int idx)
{
	struct rteipc_ep *ep;

	pthread_mutex_lock(&ep_tbl_mutex);

	if (idx < MAX_NR_EP) {
		ep = __ep_table[idx];
		pthread_mutex_unlock(&ep_tbl_mutex);
		return ep;
	}

	pthread_mutex_unlock(&ep_tbl_mutex);
	return NULL;
}

static inline int ep_register(struct rteipc_ep *ep)
{
	int ret;

	pthread_mutex_lock(&ep_tbl_mutex);

	if (__ep_index < MAX_NR_EP) {
		ret = __ep_index;
		__ep_table[__ep_index++] = ep;
		pthread_mutex_unlock(&ep_tbl_mutex);
		return ret;
	}

	pthread_mutex_unlock(&ep_tbl_mutex);
	fprintf(stderr, "Endpoint limit(max=%d) exceeded\n", MAX_NR_EP);
	return -1;
}

static inline struct rteipc_ep *ep_new(int type)
{
	struct rteipc_ep *ep;
	struct rteipc_ep_ops *ops = ep_ops_list[type];

	ep = malloc(sizeof(*ep));
	if (ep) {
		ep->base = __base;
		ep->ops = ops;
		ep->data = NULL;
	}
	return ep;
}

int rteipc_ep_route(int efd1, int efd2, int flag)
{
	struct rteipc_ep *ep1;
	struct rteipc_ep *ep2;
	struct bufferevent *pair[2];
	struct bufferevent *ep1_up, *ep1_dn, *ep2_up, *ep2_dn;

	if (!(flag & RTEIPC_FORWARD) && !(flag & RTEIPC_REVERSE)) {
		fprintf(stderr, "Invalid route direction\n");
		return -1;
	}

	ep1 = ep_get(efd1);
	ep2 = ep_get(efd2);

	if (!ep1 || !ep2) {
		fprintf(stderr, "Invalid endpoint is specified\n");
		return -1;
	}

	if (bufferevent_pair_new(__base, 0, pair)) {
		fprintf(stderr, "Failed to allocate memory for socket pair\n");
		return -1;
	}

	if (flag == RTEIPC_BIDIRECTIONAL) {
		ep1_up = ep1_dn = pair[0];
		ep2_up = ep2_dn = pair[1];
	} else if (flag == RTEIPC_FORWARD) {
		ep1_up = pair[0];
		ep2_dn = pair[1];
		ep1_dn = ep2_up = NULL;
	} else if (flag == RTEIPC_REVERSE) {
		ep1_dn = pair[0];
		ep2_up = pair[1];
		ep1_up = ep2_dn = NULL;
	}
	ep1->ops->route(ep1, RTEIPC_ROUTE_ADD, ep1_up, ep1_dn);
	ep2->ops->route(ep2, RTEIPC_ROUTE_ADD, ep2_up, ep2_dn);

	return 0;
}

int rteipc_ep_open(const char *uri)
{
	char protocol[16], path[128];
	struct rteipc_ep *ep;
	struct rteipc_ep_ops *ops;
	int type;

	sscanf(uri, "%[^:]://%99[^:]", protocol, path);

	if (!strcmp(protocol, "ipc"))
		type = RTEIPC_IPC;
	else if (!strcmp(protocol, "tty"))
		type = RTEIPC_TTY;
	else
		return -1;

	ep = ep_new(type);
	ops = ep->ops;
	if (ops->bind(ep, path))
		return -1;

	return ep_register(ep);
}
