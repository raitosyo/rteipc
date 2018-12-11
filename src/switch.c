// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "rteipc.h"
#include "ep.h"


#define MAX_NR_SW		16

extern __thread struct event_base *__base;

struct rteipc_sw {
	int sw_id;
	int connections;
	struct ep_connection {
		int ep_id;
		int ctx;
		rteipc_sw_handler handler;
		void *data;
		struct rteipc_sw *parent;
	} cn_list[MAX_NR_EP];
};

static struct rteipc_sw *__sw_table[MAX_NR_SW];
static int __sw_index;
static pthread_mutex_t sw_tbl_mutex = PTHREAD_MUTEX_INITIALIZER;


static inline int sw_register(struct rteipc_sw *sw)
{
	int ret;

	pthread_mutex_lock(&sw_tbl_mutex);

	if (__sw_index < MAX_NR_SW) {
		ret = __sw_index;
		__sw_table[__sw_index++] = sw;
		pthread_mutex_unlock(&sw_tbl_mutex);
		return ret;
	}

	pthread_mutex_unlock(&sw_tbl_mutex);
	fprintf(stderr, "Switch limit(max=%d) exceeded\n", MAX_NR_SW);
	return -1;
}

static inline struct ep_connection *find_connection(int sw_id, int ep_id)
{
	struct rteipc_sw *sw = NULL;
	struct ep_connection *cn;
	int i;

	pthread_mutex_lock(&sw_tbl_mutex);

	if (sw_id < MAX_NR_SW)
		sw = __sw_table[sw_id];

	if (!sw)
		goto out;

	for (i = 0; i < sw->connections; i++) {
		if (sw->cn_list[i].ep_id == ep_id) {
			cn = &sw->cn_list[i];
			pthread_mutex_unlock(&sw_tbl_mutex);
			return cn;
		}
	}

out:
	pthread_mutex_unlock(&sw_tbl_mutex);
	return NULL;
}

int rteipc_sw_evxfer(int sw_id, int ep_id, struct evbuffer *buf)
{
	struct ep_connection *cn = find_connection(sw_id, ep_id);

	if (!cn) {
		fprintf(stderr, "Invalid sw-ep=%d is specified\n", ep_id);
		return -1;
	}

	return rteipc_evsend(cn->ctx, buf);
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

static void data_cb(int ctx, void *data, size_t len, void *arg)
{
	struct ep_connection *cn = arg;
	struct rteipc_sw *sw = cn->parent;
	cn->handler(sw->sw_id, cn->ep_id, data, len, cn->data);
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

int rteipc_sw_ep_open(int sw_id, rteipc_sw_handler handler, void *arg)
{
	const char *fmt = "ipc://@rteipc-sw%02d-%s";
	char path[32] = {0}, fname[7];
	struct rteipc_sw *sw = NULL;
	struct ep_connection *cn;
	struct bufferevent *bev;
	struct sockaddr_un addr;
	int ep_id;

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

	ep_id = rteipc_ep_open(path);
	if (ep_id < 0) {
		fprintf(stderr, "Failed to create ep=%s\n", path);
		goto free_cn;
	}

	cn = &sw->cn_list[sw->connections];
	cn->ep_id = ep_id;
	cn->handler = handler;
	cn->data = arg;
	cn->parent = sw;
	cn->ctx = rteipc_connect(path, data_cb, cn);
	if (cn->ctx < 0) {
		fprintf(stderr, "Failed to connect to ep=%s\n", path);
		goto free_cn;
	}

	sw->connections++;
	pthread_mutex_unlock(&sw_tbl_mutex);
	return ep_id;

free_cn:
	free(cn);
	pthread_mutex_unlock(&sw_tbl_mutex);
	return -1;
}

int rteipc_sw(void)
{
	struct rteipc_sw *sw;

	sw = malloc(sizeof(*sw));
	if (!sw) {
		fprintf(stderr, "Failed to create switch\n");
		return -1;
	}

	memset(sw, 0, sizeof(*sw));
	return sw_register(sw);
}
