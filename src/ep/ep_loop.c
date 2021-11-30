// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"
#include "message.h"
#include "list.h"
#include "rteipc.h"

/**
 * Loopback endpoint
 *
 * This is a special endpoint that can be bound to any other like a normal
 * endpoint except it has no backend (i.e., it won't read/write any data
 * from/to the backend).
 * Instead, we can directly write/read to/from loopback endpoints by
 * utilizing rteipc_xfer functions without connecting to a backend, and the
 * data is transferred to/from the other endpoint bound to it consequently.
 * Since it has no backend, the process which calls rteipc_xfer and the
 * process which creates the endpoint are the same or at least share memory
 * space (i.e. threads).
 */

#define MAX_LOOP_NAME		16

struct loop {
	node_t entry;
	struct rteipc_ep *self;
	char name[MAX_LOOP_NAME];
	rteipc_lo_cb cb;
	void *arg;
};

static list_t lo_list = LIST_INITIALIZER;

static inline struct loop *lookup_lo(const char *name)
{
	struct loop *lo;
	node_t *n;

	list_each(&lo_list, n, {
		lo = list_entry(n, struct loop, entry);
		if (!strcmp(lo->name, name))
			return lo;
	})
	return NULL;
}

/**
 * rteipc_xfer - generic function to transfer data to loopback endpoint
 *               specified by 'name'
 * @name: loopback name
 * @data: buffer containing data
 * @len: length of buffer to be sent
 */
int rteipc_xfer(const char *name, const void *data, size_t len)
{
	struct loop *lo = lookup_lo(name);

	if (!lo) {
		fprintf(stderr, "%s: No such loop(%s) found\n", __func__, name);
		return -1;
	}
	return rteipc_buffer(lo->self->bev, data, len);
}

/**
 * rteipc_evxfer - another version of rteipc_xfer using evbuffer instead of
 *                 void pointer to transfer data
 * @name: loopback name
 * @buf: evbuffer containing data
 */
int rteipc_evxfer(const char *name, struct evbuffer *buf)
{
	struct loop *lo = lookup_lo(name);

	if (!lo) {
		fprintf(stderr, "%s: No such loop(%s) found\n", __func__, name);
		return -1;
	}
	return rteipc_evbuffer(lo->self->bev, buf);
}

/**
 * rteipc_gpio_xfer - helper function to transfer data to loopback endpoint
 *                    specified by 'name' which is bound to GPIO endpoint
 * @name: loopback name
 * @value: GPIO value, 1(assert) or 0(deassert)
 */
int rteipc_gpio_xfer(const char *name, uint8_t value)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	if (value > 1) {
		fprintf(stderr, "Warn: gpio value must be 0 or 1\n");
		value = 1;
	}
	evbuffer_add(buf, &value, sizeof(value));
	ret = rteipc_evxfer(name, buf);
	evbuffer_free(buf);
	return ret;
}

/**
 * rteipc_spi_xfer - helper function to transfer data to loopback endpoint
 *                   specified by 'name' which is bound to SPI endpoint
 * @name: loopback name
 * @data: data to be sent
 * @len: length of data
 * @rdmode: If true, return data from SPI device via rteipc_read_cb
 */
int rteipc_spi_xfer(const char *name, const uint8_t *data, uint16_t len,
			bool rdmode)
{
	struct evbuffer *buf = evbuffer_new();
	uint8_t rdflag = (rdmode) ? 1 : 0;
	int ret;

	evbuffer_add(buf, &len, sizeof(len));
	evbuffer_add(buf, &rdflag, sizeof(rdflag));
	if (len && data)
		evbuffer_add(buf, data, len);
	ret = rteipc_evxfer(name, buf);
	evbuffer_free(buf);
	return ret;
}

/**
 * rteipc_i2c_xfer - helper function to transfer data to loopback endpoint
 *                   specified by 'name' which is bound to I2C endpoint
 * @name: loopback name
 * @addr: I2C slave address
 * @data: tx buffer
 * @wlen: length of tx buffer to be sent
 * @rlen: length of buffer to be received
 */
int rteipc_i2c_xfer(const char *name, uint16_t addr, const uint8_t *data,
			uint16_t wlen, uint16_t rlen)
{
	struct evbuffer *buf = evbuffer_new();
	int ret;

	evbuffer_add(buf, &addr, sizeof(addr));
	evbuffer_add(buf, &wlen, sizeof(wlen));
	evbuffer_add(buf, &rlen, sizeof(rlen));
	if (wlen && data)
		evbuffer_add(buf, data, wlen);
	ret = rteipc_evxfer(name, buf);
	evbuffer_free(buf);
	return ret;
}

/**
 * rteipc_sysfs_xfer - helper function to transfer data to loopback endpoint
 *                     switch specified by 'name' which is bound to SYSFS
 *                     endpoint
 * @name: loopback name
 * @attr: name of attribute
 * @val: new value of attribute, null for requesting current value
 */
int rteipc_sysfs_xfer(const char *name, const char *attr, const char *val)
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
	ret = rteipc_evxfer(name, buf);
	evbuffer_free(buf);
	return ret;
}

/**
 * rteipc_xfer_setcb - register callback function invoked when the data comes
 * @name: loopback name
 * @cb: callback function
 * @arg: an argument passed to callback
 */
int rteipc_xfer_setcb(const char *name, rteipc_lo_cb cb, void *arg)
{
	struct loop *lo = lookup_lo(name);

	if (!lo) {
		fprintf(stderr, "No such loop found\n");
		return -1;
	}
	lo->cb = cb;
	lo->arg = arg;
	return 0;
}

static void loop_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct loop *lo = self->data;
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

		if (lo->cb)
			lo->cb(lo->name, msg, len, lo->arg);
		free(msg);
	}
}

static int loop_open(struct rteipc_ep *self, const char *path)
{
	struct loop *lo;

	if (!strlen(path)) {
		fprintf(stderr, "loop: name must be specified\n");
		return -1;
	}

	if (strlen(path) >= MAX_LOOP_NAME) {
		fprintf(stderr, "name length exceeds limit(%d)\n",
				MAX_LOOP_NAME - 1);
		return -1;
	}

	if (lookup_lo(path)) {
		fprintf(stderr, "loop name=%s already exists\n", path);
		return -1;
	}

	if (!(lo = calloc(1, sizeof(*lo)))) {
		fprintf(stderr, "Failed to allocate memory for loop\n");
		return -1;
	}

	strcpy(lo->name, path);
	lo->self = self;
	self->data = lo;
	list_push(&lo_list, &lo->entry);
	return 0;
}

static void loop_close(struct rteipc_ep *self)
{
	struct loop *lo = self->data;
	list_remove(&lo_list, &lo->entry);
	free(lo);
}

COMPATIBLE_WITH(loop, COMPAT_ANY);
struct rteipc_ep_ops loop_ops = {
	.on_data = loop_on_data,
	.open = loop_open,
	.close = loop_close,
	.compatible = loop_compatible
};
