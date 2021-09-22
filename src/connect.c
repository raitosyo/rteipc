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
		event_base_loopbreak(__base);
	}

	pthread_mutex_unlock(&ctx_mutex);
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

/**
 * rteipc_send - generic function to send data to an endpoint
 * @id: context id
 * @data: data to be sent
 * @len: length of data
 */
int rteipc_send(int id, const void *data, size_t len)
{
	struct rteipc_ctx *ctx = dtbl_get(&ctx_tbl, id);

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
		return -1;
	}
	return rteipc_buffer(ctx->bev, data, len);
}

/**
 * rteipc_evsend - another version of rteipc_send using evbuffer instead of
 *                 void pointer to send data
 * @id: context id
 * @buf: evbuffer containing data
 */
int rteipc_evsend(int id, struct evbuffer *buf)
{
	struct rteipc_ctx *ctx = dtbl_get(&ctx_tbl, id);

	if (!ctx) {
		fprintf(stderr, "Invalid connection id:%d\n", id);
		return -1;
	}
	return rteipc_evbuffer(ctx->bev, buf);
}

/**
 * rteipc_gpio_send - helper function to send data to GPIO endpoint
 * @id: context id
 * @value: GPIO value, 1(assert) or 0(deassert)
 */
int rteipc_gpio_send(int id, uint8_t value)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	if (value > 1) {
		fprintf(stderr, "Warn: gpio value must be 0 or 1\n");
		value = 1;
	}
	evbuffer_add(buf, &value, sizeof(value));
	ret = rteipc_evsend(id, buf);
	evbuffer_free(buf);
	return ret;
}
/**
 * rteipc_spi_send - helper function to send data to SPI endpoint
 * @id: context id
 * @data: data to be sent
 * @len: length of data
 * @rdmode: If true, return data from SPI device via rteipc_read_cb
 */
int rteipc_spi_send(int id, const uint8_t *data, uint16_t len, bool rdmode)
{
	struct evbuffer *buf = evbuffer_new();
	uint8_t rdflag = (rdmode) ? 1 : 0;
	int ret;

	evbuffer_add(buf, &len, sizeof(len));
	evbuffer_add(buf, &rdflag, sizeof(rdflag));
	if (len && data)
		evbuffer_add(buf, data, len);
	ret = rteipc_evsend(id, buf);
	evbuffer_free(buf);
	return ret;
}

/**
 * rteipc_i2c_send - helper function to send data to I2C endpoint
 * @id: context id
 * @addr: I2C slave address
 * @data: data to be sent
 * @wlen: length of data
 * @rlen: length of buffer for receiving
 */
int rteipc_i2c_send(int id, uint16_t addr, const uint8_t *data,
					uint16_t wlen, uint16_t rlen)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	evbuffer_add(buf, &addr, sizeof(addr));
	evbuffer_add(buf, &wlen, sizeof(wlen));
	evbuffer_add(buf, &rlen, sizeof(rlen));
	if (wlen && data)
		evbuffer_add(buf, data, wlen);
	ret = rteipc_evsend(id, buf);
	evbuffer_free(buf);
	return ret;
}

/**
 * rteipc_sysfs_send - helper function to send data to SYSFS endpoint
 * @id: context id
 * @attr: name of attribute
 * @val: new value of attribute
 */
int rteipc_sysfs_send(int id, const char *attr, const char *val)
{
	struct evbuffer *buf;
	int ret;

	if (!attr) {
		fprintf(stderr, "Invalid arguments: attr cannot be NULL\n");
		return -1;
	}

	buf = evbuffer_new();
	evbuffer_add_printf(buf, "%s", attr);
	if (val)
		evbuffer_add_printf(buf, "=%s", val);
	ret = rteipc_evsend(id, buf);
	evbuffer_free(buf);
	return ret;
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

	pthread_mutex_unlock(&ctx_mutex);

	err = bufferevent_socket_connect(bev,
			(struct sockaddr *)&addr, addrlen);

	if (err < 0) {
		fprintf(stderr, "Failed to connect to %s\n", uri);
		/*
		 * connect_event_cb already free()ed 'ctx' and 'bev' on error.
		 * So just return here.
		 */
		return -1;
	}

	return id;

free_ctx:
	free(ctx);
	pthread_mutex_unlock(&ctx_mutex);
free_bev:
	bufferevent_free(bev);
	return -1;
}
