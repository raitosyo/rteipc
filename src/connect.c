// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "rteipc.h"
#include "message.h"
#include "ep.h"


#define MAX_NR_CN		(MAX_NR_EP * 2)

extern __thread struct event_base *__base;

struct rteipc_ctx {
	struct bufferevent *bev;
	rteipc_read_cb fn;
	void *arg;
};

static struct rteipc_ctx *__ctx_table[MAX_NR_CN];
static int __ctx_index;
static pthread_mutex_t ctx_tbl_mutex = PTHREAD_MUTEX_INITIALIZER;


static inline struct rteipc_ctx *ctx_get(int idx)
{
	struct rteipc_ctx *ctx;

	pthread_mutex_lock(&ctx_tbl_mutex);

	if (idx < MAX_NR_CN) {
		ctx = __ctx_table[idx];
		pthread_mutex_unlock(&ctx_tbl_mutex);
		return ctx;
	}

	pthread_mutex_unlock(&ctx_tbl_mutex);
	return NULL;
}

static inline int ctx_register(struct rteipc_ctx *ctx)
{
	int ret;

	pthread_mutex_lock(&ctx_tbl_mutex);

	if (__ctx_index < MAX_NR_CN) {
		ret = __ctx_index;
		__ctx_table[__ctx_index++] = ctx;
		pthread_mutex_unlock(&ctx_tbl_mutex);
		return ret;
	}

	pthread_mutex_unlock(&ctx_tbl_mutex);
	fprintf(stderr, "Connection limit(max=%d) exceeded\n", MAX_NR_CN);
	return -1;
}

static void connect_event_cb(struct bufferevent *bev, short events, void *arg)
{
	int id = (intptr_t)arg;
	struct rteipc_ctx *ctx = ctx_get(id);

	// connection established
	if (events & BEV_EVENT_CONNECTED)
		return;

	if (events & BEV_EVENT_ERROR)
		fprintf(stderr, "Got an error on the connection: %s\n",
			strerror(errno));

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		bufferevent_free(bev);
		ctx->bev = NULL;
	}

	event_base_loopbreak(__base);
}

static void connect_read_cb(struct bufferevent *bev, void *arg)
{
	int id = (intptr_t)arg;
	struct rteipc_ctx *ctx = ctx_get(id);
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len;
	int ret;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		if (ctx->fn)
			ctx->fn(id, msg, len, ctx->arg);
		free(msg);
	}
}

int rteipc_evsend(int id, struct evbuffer *buf)
{
	struct rteipc_ctx *ctx = ctx_get(id);
	int ret;

	if (!ctx || !ctx->bev) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
		return -1;
	}
	return rteipc_evbuffer(ctx->bev, buf);
}

int rteipc_send(int id, const void *data, size_t len)
{
	struct rteipc_ctx *ctx = ctx_get(id);
	int ret;

	if (!ctx || !ctx->bev) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
		return -1;
	}
	return rteipc_buffer(ctx->bev, data, len);
}

int rteipc_connect(const char *uri, rteipc_read_cb cb, void *arg)
{
	struct rteipc_ctx *ctx;
	struct bufferevent *bev;
	struct sockaddr_un addr;
	int addrlen;
	char protocol[16], path[128];
	int ctx_id, err;

	sscanf(uri, "%[^:]://%99[^:]", protocol, path);

	if (strcmp(protocol, "ipc")) {
		fprintf(stderr, "Unknown protocol:%s\n", protocol);
		return -1;
	}

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr,
			"Failed to allocate memory to create connection\n");
		return -1;
	}

	bev = bufferevent_socket_new(__base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Failed to create socket\n");
		goto free_ctx;
	}

	ctx->bev = bev;
	ctx->fn = cb;
	ctx->arg = arg;
	ctx_id = ctx_register(ctx);
	if (ctx_id < 0)
		goto free_bev;

	bufferevent_setcb(bev, connect_read_cb, NULL,
				connect_event_cb, (void *)(intptr_t)ctx_id);
	bufferevent_enable(bev, EV_READ);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	addrlen = offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
	if (addr.sun_path[0] == '@') {
		addr.sun_path[0] = 0;
		addrlen = addrlen - 1;
	}

	err = bufferevent_socket_connect(bev,
			(struct sockaddr *)&addr, addrlen);

	if (err < 0) {
		fprintf(stderr, "Failed to connect to %s\n", uri);
		goto free_ctx;
	}

	return ctx_id;

free_bev:
	bufferevent_free(ctx->bev);
free_ctx:
	free(ctx);
	return -1;
}
