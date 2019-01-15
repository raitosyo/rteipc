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
extern struct rteipc_ep_ops ep_gpio;

static struct rteipc_ep *__ep_table[MAX_NR_EP];
static int __ep_index;
static pthread_mutex_t ep_tbl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ep_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct rteipc_ep_ops *ep_ops_list[] = {
	[RTEIPC_IPC]  = &ep_ipc,
	[RTEIPC_TTY]  = &ep_tty,
	[RTEIPC_GPIO] = &ep_gpio,
};


static inline struct rteipc_ep *ep_get(int eid)
{
	struct rteipc_ep *ret;

	if (eid >= MAX_NR_EP)
		return NULL;

	pthread_mutex_lock(&ep_tbl_mutex);
	ret = __ep_table[eid];
	pthread_mutex_unlock(&ep_tbl_mutex);
	return ret;
}

static inline int __find_next_ep(int start)
{
	int i;

	for (i = start; i < start + MAX_NR_EP; i++) {
		if (__ep_table[i % MAX_NR_EP] == NULL)
			return (i % MAX_NR_EP);
	}
	return -1;
}

static int ep_register(struct rteipc_ep *ep)
{
	int eid;

	pthread_mutex_lock(&ep_tbl_mutex);

	eid = __find_next_ep(__ep_index);
	if (eid < 0) {
		pthread_mutex_unlock(&ep_tbl_mutex);
		fprintf(stderr, "Endpoint limit(max=%d) exceeded\n",
				MAX_NR_EP);
		return -1;
	}

	__ep_table[eid] = ep;
	__ep_index = eid + 1;

	pthread_mutex_unlock(&ep_tbl_mutex);
	return eid;
}

static void ep_unregister(int eid)
{
	int i;

	if (eid >= MAX_NR_EP) {
		fprintf(stderr, "Invalid connection id is specified\n");
		return;
	}

	pthread_mutex_lock(&ep_tbl_mutex);
	__ep_table[eid] = NULL;
	pthread_mutex_unlock(&ep_tbl_mutex);
}

int rteipc_ep_route(int eid1, int eid2, int flag)
{
	struct rteipc_ep *ep1;
	struct rteipc_ep *ep2;
	struct bufferevent *pair[2];
	struct bufferevent *ep1_up, *ep1_dn, *ep2_up, *ep2_dn;
	int ret = 0;

	if (!(flag & RTEIPC_FORWARD) && !(flag & RTEIPC_REVERSE)) {
		fprintf(stderr, "Invalid route direction\n");
		return -1;
	}

	pthread_mutex_lock(&ep_mutex);

	ep1 = ep_get(eid1);
	ep2 = ep_get(eid2);

	if (!ep1 || !ep2) {
		fprintf(stderr, "Invalid endpoint is specified\n");
		ret = -1;
		goto out;
	}

	if (bufferevent_pair_new(__base, 0, pair)) {
		fprintf(stderr, "Failed to allocate memory for socket pair\n");
		ret = -1;
		goto out;
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

out:
	pthread_mutex_unlock(&ep_mutex);
	return ret;
}

int rteipc_ep_open(const char *uri)
{
	char protocol[16], path[128];
	struct rteipc_ep *ep;
	int type, eid;

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
	ep->data = NULL;

	eid = ep_register(ep);
	if (eid < 0) {
		fprintf(stderr, "Failed to register ep\n");
		goto free_ep;
	}

	if (ep->ops->bind(ep, path)) {
		fprintf(stderr, "Failed to bind ep\n");
		goto unreg_ep;
	}

	return eid;

unreg_ep:
	ep_unregister(eid);
free_ep:
	free(ep);
	return -1;
}

void rteipc_ep_close(int eid)
{
	struct rteipc_ep *ep;

	pthread_mutex_lock(&ep_mutex);

	ep = ep_get(eid);
	if (ep) {
		ep_unregister(eid);
		ep->ops->unbind(ep);
		free(ep);
	}

	pthread_mutex_unlock(&ep_mutex);
}
