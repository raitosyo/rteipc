// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "rteipc.h"
#include "table.h"
#include "ep.h"


#define MAX_NR_SW		DESC_BIT_WIDTH

#define list_node_to_sep(n) \
	(struct sep *)((char *)(n) - (char *)&((struct sep *)0)->entry)

extern __thread struct event_base *__base;

struct sep {
	int id;
	int ctx;
	struct rteipc_sw *parent;
	node_t entry;
};

struct rteipc_sw {
	int id;
	rteipc_sw_cb handler;
	void *data;
	short flag;
	list_t ep_list;
};

struct sep_desc {
	int sid;
	int eid;
};

static dtbl_t sw_tbl = DTBL_INITIALIZER(MAX_NR_SW);
static pthread_mutex_t sw_mutex = PTHREAD_MUTEX_INITIALIZER;


static inline struct sep *__sep_list_find(int sid, int eid)
{
	struct rteipc_sw *sw = NULL;
	struct sep *ret = NULL;
	node_t *n;
	int i;

	sw = dtbl_get(&sw_tbl, sid);
	if (!sw)
		goto out;

	list_each(&sw->ep_list, n, {
		struct sep *sep = list_node_to_sep(n);
		if (sep->id == eid) {
			ret = sep;
			break;
		}
	})

out:
	return ret;
}

int rteipc_sw_evxfer(int sid, int eid, struct evbuffer *buf)
{
	struct sep *sep;
	int ctx;

	pthread_mutex_lock(&sw_mutex);

	sep = __sep_list_find(sid, eid);
	if (!sep) {
		pthread_mutex_unlock(&sw_mutex);
		fprintf(stderr, "Invalid sw-ep=%d is specified\n", eid);
		return -1;
	}
	ctx = sep->ctx;

	pthread_mutex_unlock(&sw_mutex);
	return rteipc_evsend(ctx, buf);
}

int rteipc_sw_xfer(int sid, int eid, const void *data, size_t len)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	evbuffer_add(buf, data, len);
	ret = rteipc_sw_evxfer(sid, eid, buf);
	evbuffer_free(buf);
	return ret;
}

static void err_cb(int ctx, short events, void *arg)
{
	struct sep_desc *desc = arg;
	struct sep *sep;
	struct rteipc_sw *sw;

	pthread_mutex_lock(&sw_mutex);

	fprintf(stderr, "switch fatal error occurred\n");
	sep = __sep_list_find(desc->sid, desc->eid);
	if (sep) {
		sw = sep->parent;
		list_remove(&sw->ep_list, &sep->entry);
		rteipc_ep_close(sep->id);
		free(sep);
		free(desc);
	}

	pthread_mutex_unlock(&sw_mutex);
}

static void data_cb(int ctx, void *data, size_t len, void *arg)
{
	struct sep_desc *desc = arg;
	struct sep *sep;
	struct rteipc_sw *sw;

	pthread_mutex_lock(&sw_mutex);

	sep = __sep_list_find(desc->sid, desc->eid);
	if (!sep) {
		fprintf(stderr, "sw ep already closed\n");
		pthread_mutex_unlock(&sw_mutex);
		return;
	}
	sw = sep->parent;

	if (sw->handler) {
		pthread_mutex_unlock(&sw_mutex);
		sw->handler(desc->sid, desc->eid, data, len, sw->data);
	} else {
		pthread_mutex_unlock(&sw_mutex);
	}
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

int rteipc_sw_setcb(int id, rteipc_sw_cb handler, void *arg, short flag)
{
	struct rteipc_sw *sw = dtbl_get(&sw_tbl, id);

	if (!sw) {
		fprintf(stderr, "Invalid switch id:%d\n", id);
		return -1;
	}
	sw->handler = handler;
	sw->data = arg;
	sw->flag = flag;
	return 0;
}

int rteipc_sw_ep_open(int id)
{
	const char *fmt = "ipc://@rteipc-sw%02d-%s";
	char path[32] = {0}, fname[7];
	struct rteipc_sw *sw = NULL;
	struct sep *sep;
	struct sep_desc *desc;
	struct bufferevent *bev;
	struct sockaddr_un addr;
	int err;

	sw = dtbl_get(&sw_tbl, id);
	if (!sw) {
		fprintf(stderr, "Invalid switch id is specified\n");
		return -1;
	}

	rand_fname(fname, sizeof(fname));
	snprintf(path, sizeof(path), fmt, id, fname);

	err = rteipc_ep_open(path);
	if (err < 0) {
		fprintf(stderr, "Failed to create ep=%s\n", path);
		return -1;
	}

	sep = malloc(sizeof(*sep));
	desc = malloc(sizeof(*desc));
	if (!sep || !desc) {
		fprintf(stderr, "Failed to allocate memory for ep\n");
		goto close_ep;
	}
	sep->id = err;
	sep->parent = sw;

	err = rteipc_connect(path);
	if (err < 0) {
		fprintf(stderr, "Failed to connect to ep=%s\n", path);
		goto free_sep;
	}
	sep->ctx = err;
	desc->sid = sw->id;
	desc->eid = sep->id;

	list_push(&sw->ep_list, &sep->entry);
	rteipc_setcb(sep->ctx, data_cb, err_cb, desc, RTEIPC_NO_EXIT_ON_ERR);
	return sep->id;

free_sep:
	err = sep->id;
	free(desc);
	free(sep);
close_ep:
	rteipc_ep_close(err);
	return -1;
}

void rteipc_sw_ep_close(int sid, int eid)
{
	struct sep *sep;
	struct rteipc_sw *sw;

	pthread_mutex_lock(&sw_mutex);

	sep = __sep_list_find(sid, eid);
	if (sep) {
		sw = sep->parent;
		list_remove(&sw->ep_list, &sep->entry);
		rteipc_ep_close(sep->id);
		free(sep);
	}

	pthread_mutex_unlock(&sw_mutex);
}

int rteipc_sw(void)
{
	struct rteipc_sw *sw = malloc(sizeof(*sw));

	if (!sw) {
		fprintf(stderr, "Failed to create sw\n");
		return -1;
	}
	sw->handler = NULL;
	sw->data = NULL;
	sw->flag = 0;
	list_init(&sw->ep_list);

	pthread_mutex_lock(&sw_mutex);

	sw->id = dtbl_set(&sw_tbl, sw);
	if (sw->id < 0) {
		fprintf(stderr, "Failed to register sw\n");
		goto free_sw;
	}

	pthread_mutex_unlock(&sw_mutex);
	return 0;

free_sw:
	free(sw);
	pthread_mutex_unlock(&sw_mutex);
	return -1;
}
