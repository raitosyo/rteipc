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
#include "table.h"
#include "ep.h"


#define MAX_NR_CN		(MAX_NR_EP * 2)

extern __thread struct event_base *__base;

struct rteipc_ctx {
	struct bufferevent *bev;
	rteipc_read_cb read_cb;
	rteipc_err_cb err_cb;
	void *arg;
	short flag;
};

static dtbl_t ctx_tbl = DTBL_INITIALIZER(MAX_NR_CN);
static pthread_mutex_t ctx_mutex = PTHREAD_MUTEX_INITIALIZER;


static void connect_event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct rteipc_ctx *ctx;
	int id = (intptr_t)arg;


	// connection established
	if (events & BEV_EVENT_CONNECTED)
		return;

	if (events & BEV_EVENT_ERROR)
		fprintf(stderr, "Got an error on the connection: %s\n",
			strerror(errno));

	pthread_mutex_lock(&ctx_mutex);

	ctx = dtbl_get(&ctx_tbl, id);

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		dtbl_del(&ctx_tbl, id);
		bufferevent_free(bev);
		if (ctx->err_cb)
			ctx->err_cb(id, events, ctx->arg);
		free(ctx);
	}

	pthread_mutex_unlock(&ctx_mutex);

	if (!(ctx->flag & RTEIPC_NO_EXIT_ON_ERR))
		event_base_loopbreak(__base);
}

static void connect_read_cb(struct bufferevent *bev, void *arg)
{
	struct rteipc_ctx *ctx;
	int id = (intptr_t)arg;
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len;
	int ret;

	pthread_mutex_lock(&ctx_mutex);

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			goto out;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			goto out;
		}


		ctx = dtbl_get(&ctx_tbl, id);
		if (ctx->read_cb)
			ctx->read_cb(id, msg, len, ctx->arg);
		free(msg);

	}

out:
	pthread_mutex_unlock(&ctx_mutex);
}

int rteipc_setcb(int id, rteipc_read_cb read_cb, rteipc_err_cb err_cb,
			void *arg, short flag)
{
	struct rteipc_ctx *ctx;
	int ret = -1;

	pthread_mutex_lock(&ctx_mutex);

	ctx = dtbl_get(&ctx_tbl, id);
	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
		goto out;
	}

	ret = 0;
	ctx->read_cb = read_cb;
	ctx->err_cb = err_cb;
	ctx->arg = arg;
	ctx->flag = flag;

out:
	pthread_mutex_unlock(&ctx_mutex);
	return ret;
}

int rteipc_evsend(int id, struct evbuffer *buf)
{
	struct rteipc_ctx *ctx = dtbl_get(&ctx_tbl, id);

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
		return -1;
	}

	return rteipc_evbuffer(ctx->bev, buf);
}

int rteipc_send(int id, const void *data, size_t len)
{
	struct rteipc_ctx *ctx = dtbl_get(&ctx_tbl, id);

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
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
	int id, err;

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

	pthread_mutex_lock(&ctx_mutex);

	id = dtbl_set(&ctx_tbl, ctx);
	if (id < 0)
		goto free_ctx;

	ctx->bev = bev;
	bufferevent_setcb(bev, connect_read_cb, NULL,
				connect_event_cb, (void *)(intptr_t)id);
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
		goto unreg_ctx;
	}

	pthread_mutex_unlock(&ctx_mutex);
	return id;

unreg_ctx:
	dtbl_del(&ctx_tbl, id);
free_ctx:
	free(ctx);
	pthread_mutex_unlock(&ctx_mutex);
free_bev:
	bufferevent_free(bev);
	return -1;
}
