// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "rteipc.h"
#include "list.h"
#include "ep.h"


#define MAX_NR_SW		16

#define list_node_to_sep(n) \
	(struct sw_ep *)((char *)(n) - (char *)&((struct sw_ep *)0)->entry)

extern __thread struct event_base *__base;

struct sw_ep {
	int ep_id;
	int ctx;
	struct rteipc_sw *parent;
	node_t entry;
};

struct rteipc_sw {
	rteipc_sw_cb handler;
	void *data;
	short flag;
	list_t ep_list;
};

static struct rteipc_sw *__sw_table[MAX_NR_SW];
static int __sw_index;
static pthread_mutex_t sw_tbl_mutex = PTHREAD_MUTEX_INITIALIZER;


static inline struct rteipc_sw *sw_get(int sw_id)
{
	struct rteipc_sw *ret;

	if (sw_id >= MAX_NR_SW)
		return NULL;

	pthread_mutex_lock(&sw_tbl_mutex);
	ret = __sw_table[sw_id];
	pthread_mutex_unlock(&sw_tbl_mutex);
	return ret;
}

static inline int __find_sw(struct rteipc_sw *sw)
{
	int i;

	for (i = 0; i < MAX_NR_SW; i++) {
		if (__sw_table[i] == sw)
			break;
	}
	if (i == MAX_NR_SW)
		return -1;
	return i;
}

static inline int __find_next_sw(int start)
{
	int i;

	for (i = start; i < start + MAX_NR_SW; i++) {
		if (__sw_table[i % MAX_NR_SW] == NULL)
			return (i % MAX_NR_SW);
	}
	return -1;
}

static int sw_register(struct rteipc_sw *sw)
{
	int sid;

	pthread_mutex_lock(&sw_tbl_mutex);

	sid = __find_next_sw(__sw_index);
	if (sid < 0) {
		pthread_mutex_unlock(&sw_tbl_mutex);
		fprintf(stderr, "Switch limit(max=%d) exceeded\n",
				MAX_NR_SW);
		return -1;
	}

	__sw_table[sid] = sw;
	__sw_index = sid + 1;

	pthread_mutex_unlock(&sw_tbl_mutex);
	return sid;
}

static inline struct sw_ep *find_sw_ep(int sw_id, int ep_id)
{
	struct rteipc_sw *sw = NULL;
	struct sw_ep *sep = NULL;
	node_t *n;
	int i;

	pthread_mutex_lock(&sw_tbl_mutex);

	if (sw_id < MAX_NR_SW)
		sw = __sw_table[sw_id];

	if (!sw)
		goto out;

	list_each(&sw->ep_list, n, {
		struct sw_ep *e = list_node_to_sep(n);
		if (e->ep_id == ep_id) {
			sep = e;
			break;
		}
	})

out:
	pthread_mutex_unlock(&sw_tbl_mutex);
	return sep;
}

int rteipc_sw_evxfer(int sw_id, int ep_id, struct evbuffer *buf)
{
	struct sw_ep *sep = find_sw_ep(sw_id, ep_id);

	if (!sep) {
		fprintf(stderr, "Invalid sw-ep=%d is specified\n", ep_id);
		return -1;
	}

	return rteipc_evsend(sep->ctx, buf);
}

int rteipc_sw_xfer(int sw_id, int ep_id, const void *data, size_t len)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	evbuffer_add(buf, data, len);
	ret = rteipc_sw_evxfer(sw_id, ep_id, buf);
	evbuffer_free(buf);
	return ret;
}

static void err_cb(int ctx, short events, void *arg)
{
	struct sw_ep *sep = arg;

	fprintf(stderr, "switch fatal error occurred\n");
	rteipc_ep_close(sep->ep_id);
	memset(sep, 0, sizeof(*sep));
}

static void data_cb(int ctx, void *data, size_t len, void *arg)
{
	struct sw_ep *sep = arg;
	struct rteipc_sw *sw = sep->parent;
	int sid;

	pthread_mutex_lock(&sw_tbl_mutex);
	sid = __find_sw(sw);
	pthread_mutex_unlock(&sw_tbl_mutex);

	if (sw->handler)
		sw->handler(sid, sep->ep_id, data, len, sw->data);
}

static void rand_fname(char *out, size_t len)
{
	static const char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				  "abcdefghijklmnopqrstuvwxyz"
				  "0123456789._-";
	int i, end = len - 1;

	for (i = 0; i < end; ++i)
		out[i] = set[rand() % (sizeof(set) - 1)];
	out[end] = 0;
}

int rteipc_sw_setcb(int sw_id, rteipc_sw_cb handler, void *arg, short flag)
{
	struct rteipc_sw *sw = sw_get(sw_id);

	if (!sw) {
		fprintf(stderr, "Invalid switch id:%d\n", sw_id);
		return -1;
	}

	sw->handler = handler;
	sw->data = arg;
	sw->flag = flag;
	return 0;
}

int rteipc_sw_ep_open(int sw_id)
{
	const char *fmt = "ipc://@rteipc-sw%02d-%s";
	char path[32] = {0}, fname[7];
	struct rteipc_sw *sw = NULL;
	struct sw_ep *sep;
	struct bufferevent *bev;
	struct sockaddr_un addr;
	int ret;

	pthread_mutex_lock(&sw_tbl_mutex);

	if (sw_id < MAX_NR_SW)
		sw = __sw_table[sw_id];

	if (!sw) {
		pthread_mutex_unlock(&sw_tbl_mutex);
		fprintf(stderr, "Invalid switch id is specified\n");
		return -1;
	}

	rand_fname(fname, sizeof(fname));
	snprintf(path, sizeof(path), fmt, sw_id, fname);

	ret = rteipc_ep_open(path);
	if (ret < 0) {
		fprintf(stderr, "Failed to create ep=%s\n", path);
		goto out;
	}

	sep = malloc(sizeof(*sep));
	sep->ep_id = ret;
	sep->parent = sw;
	sep->ctx = rteipc_connect(path);
	if (sep->ctx < 0) {
		fprintf(stderr, "Failed to connect to ep=%s\n", path);
		free(sep);
		ret = -1;
		goto out;
	}

	list_push(&sw->ep_list, &sep->entry);
	rteipc_setcb(sep->ctx, data_cb, err_cb, sep, 0);
out:
	pthread_mutex_unlock(&sw_tbl_mutex);
	return ret;
}

int rteipc_sw(void)
{
	struct rteipc_sw *sw;
	int ret;

	sw = malloc(sizeof(*sw));
	if (!sw) {
		fprintf(stderr, "Failed to create switch\n");
		return -1;
	}

	sw->handler = NULL;
	sw->data = NULL;
	sw->flag = 0;
	list_init(&sw->ep_list);

	ret = sw_register(sw);
	if (ret < 0) {
		free(sw);
		return -1;
	}
	return ret;
}
