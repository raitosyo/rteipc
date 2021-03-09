// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"
#include "message.h"


struct i2c_data {
	int fd;
};

static void i2c_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct i2c_data *data = self->data;
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len, nl, num;
	int ret, i;
	struct evbuffer *buf = evbuffer_new();
	struct i2c_rdwr_ioctl_data xfer;
	struct i2c_msg *item;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		if (len % sizeof(*item)) {
			fprintf(stderr, "data size is odd\n");
			goto free_msg;
		}

		item = (struct i2c_msg *)msg;
		xfer.msgs = item;
		xfer.nmsgs = len / sizeof(*item);

		if (ioctl(data->fd, I2C_RDWR, &xfer) < 0) {
			fprintf(stderr, "Error writing data to i2c(%s)\n",
					strerror(errno));
			goto free_msg;
		}

		for (i = 0; i < xfer.nmsgs; i++) {
			if (self->bev && (item[i].flags & I2C_M_RD)) {
				evbuffer_add(buf, item[i].buf, item[i].len);
				nl = htonl(item[i].len);
				evbuffer_prepend(buf, &nl, 4);
				bufferevent_write_buffer(self->bev, buf);
			}
		}
free_msg:
		free(msg);
	}
}

static int init_i2c(const char *path)
{
	int fd;
	unsigned long funcs;

	if ((fd = open(path, O_RDWR)) < 0) {
		fprintf(stderr, "Failed to open %s\n", path);
		return -1;
	}

	if (ioctl(fd, I2C_FUNCS, &funcs) < 0) {
		fprintf(stderr,
			"Failed to get info about I2C functionality\n");
		goto out;
	}

	if (!(funcs & I2C_FUNC_I2C)) {
		fprintf(stderr,
			"The device does not support I2C\n");
		goto out;
	}

	return fd;
out:
	close(fd);
	return -1;
}

static int i2c_open(struct rteipc_ep *self, const char *path)
{
	struct i2c_data *data;
	int fd;

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_i2c\n");
		return -1;
	}

	memset(data, 0, sizeof(*data));

	fd = init_i2c(path);
	if (fd < 0) {
		fprintf(stderr, "Failed to init i2c\n");
		return -1;
	}

	data->fd = fd;
	self->data = data;

	return 0;
}

static void i2c_close(struct rteipc_ep *self)
{
	struct i2c_data *data = self->data;
	close(data->fd);
}

struct rteipc_ep_ops i2c_ops = {
	.on_data = i2c_on_data,
	.open = i2c_open,
	.close = i2c_close,
};
