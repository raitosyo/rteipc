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

#define ctx_lock(_c)		pthread_mutex_lock(&(_c)->lock)
#define ctx_unlock(_c)		pthread_mutex_unlock(&(_c)->lock)

extern __thread struct event_base *__base;

struct rteipc_ctx {
	struct bufferevent *bev;
	rteipc_read_cb read_cb;
	rteipc_err_cb err_cb;
	void *arg;
	short flag;
	pthread_mutex_t lock;
};

static struct rteipc_ctx *__ctx_table[MAX_NR_CN];
static int __ctx_index;
static pthread_mutex_t ctx_tbl_mutex = PTHREAD_MUTEX_INITIALIZER;


static inline struct rteipc_ctx *ctx_get(int cid)
{
	struct rteipc_ctx *ret;

	if (cid >= MAX_NR_CN)
		return NULL;

	pthread_mutex_lock(&ctx_tbl_mutex);
	ret = __ctx_table[cid];
	pthread_mutex_unlock(&ctx_tbl_mutex);
	return ret;
}

static inline void ctx_destroy(struct rteipc_ctx *ctx)
{
	if (ctx) {
		bufferevent_free(ctx->bev);
		free(ctx);
	}
}

static inline int __find_next_ctx(int start)
{
	int i;

	for (i = start; i < start + MAX_NR_CN; i++) {
		if (__ctx_table[i % MAX_NR_CN] == NULL)
			return (i % MAX_NR_CN);
	}
	return -1;
}

static int ctx_register(struct rteipc_ctx *ctx)
{
	int cid;

	pthread_mutex_lock(&ctx_tbl_mutex);

	cid = __find_next_ctx(__ctx_index);
	if (cid < 0) {
		pthread_mutex_unlock(&ctx_tbl_mutex);
		fprintf(stderr, "Connection limit(max=%d) exceeded\n",
				MAX_NR_CN);
		return -1;
	}

	__ctx_table[cid] = ctx;
	__ctx_index = cid + 1;

	pthread_mutex_unlock(&ctx_tbl_mutex);
	return cid;
}

static inline void ctx_unregister(int cid)
{
	int i;

	if (cid >= MAX_NR_CN) {
		fprintf(stderr, "Invalid connection id is specified\n");
		return;
	}

	pthread_mutex_lock(&ctx_tbl_mutex);
	__ctx_table[cid] = NULL;
	pthread_mutex_unlock(&ctx_tbl_mutex);
}

static void connect_event_cb(struct bufferevent *bev, short events, void *arg)
{
	int cid = (intptr_t)arg;
	struct rteipc_ctx *ctx = ctx_get(cid);

	// connection established
	if (events & BEV_EVENT_CONNECTED)
		return;

	if (events & BEV_EVENT_ERROR)
		fprintf(stderr, "Got an error on the connection: %s\n",
			strerror(errno));

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		ctx_unregister(cid);
		if (ctx->err_cb)
			ctx->err_cb(cid, events, ctx->arg);
		ctx_destroy(ctx);
	}

	if (!(ctx->flag & RTEIPC_NO_EXIT_ON_ERR))
		event_base_loopbreak(__base);
}

static void connect_read_cb(struct bufferevent *bev, void *arg)
{
	int cid = (intptr_t)arg;
	struct rteipc_ctx *ctx = ctx_get(cid);
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len;
	int ret;

	ctx_lock(ctx);

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg))) {
			ctx_unlock(ctx);
			return;
		}

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			ctx_unlock(ctx);
			return;
		}

		if (ctx->read_cb)
			ctx->read_cb(cid, msg, len, ctx->arg);
		free(msg);
	}

	ctx_unlock(ctx);
}

int rteipc_setcb(int cid, rteipc_read_cb read_cb, rteipc_err_cb err_cb,
			void *arg, short flag)
{
	struct rteipc_ctx *ctx = ctx_get(cid);
	int ret;

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", cid);
		return -1;
	}

	ctx_lock(ctx);

	ctx->read_cb = read_cb;
	ctx->err_cb = err_cb;
	ctx->arg = arg;
	ctx->flag = flag;

	ctx_unlock(ctx);
	return 0;
}

int rteipc_evsend(int cid, struct evbuffer *buf)
{
	struct rteipc_ctx *ctx = ctx_get(cid);
	int ret;

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", cid);
		return -1;
	}
	return rteipc_evbuffer(ctx->bev, buf);
}

int rteipc_send(int cid, const void *data, size_t len)
{
	struct rteipc_ctx *ctx = ctx_get(cid);
	int ret;

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", cid);
		return -1;
	}
	return rteipc_buffer(ctx->bev, data, len);
}

int rteipc_connect(const char *uri)
{
	struct rteipc_ctx *ctx;
	struct bufferevent *bev;
	struct sockaddr_un addr;
	int addrlen;
	char protocol[16], path[128];
	int cid, err;

	sscanf(uri, "%[^:]://%99[^:]", protocol, path);

	if (strcmp(protocol, "ipc")) {
		fprintf(stderr, "Unknown protocol:%s\n", protocol);
		return -1;
	}

	bev = bufferevent_socket_new(__base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Failed to create socket\n");
		return -1;
	}

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr,
			"Failed to allocate memory to create connection\n");
		goto free_bev;
	}

	pthread_mutex_init(&ctx->lock, NULL);
	ctx->bev = bev;
	cid = ctx_register(ctx);
	if (cid < 0)
		goto free_ctx;

	bufferevent_setcb(bev, connect_read_cb, NULL,
				connect_event_cb, (void *)(intptr_t)cid);
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
		goto close_ctx;
	}

	return cid;

close_ctx:
	ctx_unregister(cid);
free_ctx:
	free(ctx);
free_bev:
	bufferevent_free(bev);
	return -1;
}
