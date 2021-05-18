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

/**
 * I2C endpoint
 *
 * Data format:
 *   Input  { uint16_t, uint16_t, uint16_t, uint8_t[] }
 *     arg1 - I2C slave address
 *     arg2 - I2C tx buffer size
 *     arg3 - I2C rx buffer size
 *     arg4 - I2C tx buffer (optional)
 *
 *   Output { uint8_t[] }
 *     arg1 - I2C rx buffer
 */

struct i2c_data {
	int fd;
};

static void i2c_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct i2c_data *data = self->data;
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	uint16_t addr, wlen, rlen, *pos;
	uint8_t *rx_buf = NULL;
	size_t len;
	int ret, i;
	struct i2c_rdwr_ioctl_data xfer;
	struct i2c_msg msgs[2];

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		if (len < sizeof(uint16_t) * 3) {
			fprintf(stderr, "data size is odd\n");
			goto free_msg;
		}

		pos = (uint16_t *)msg;
		addr = *pos++;  /* arg1 */
		wlen = *pos++;  /* arg2 */
		rlen = *pos++;  /* arg3 */

		if ((!wlen && !rlen) || ((char *)pos + wlen != msg + len)) {
			fprintf(stderr, "Invalid arguments\n");
			goto free_msg;
		}

		if (wlen) {
			msgs[0].addr = addr;
			msgs[0].flags = 0;
			msgs[0].len = wlen;
			msgs[0].buf = (uint8_t *)pos;
		}

		if (rlen) {
			rx_buf = malloc(rlen);
			if (!rx_buf) {
				fprintf(stderr, "Failed to allocate rx_buf\n");
				goto free_msg;
			}
			msgs[1].addr = addr;
			msgs[1].flags = I2C_M_RD;
			msgs[1].len = rlen;
			msgs[1].buf = rx_buf;
		}

		if (wlen && rlen) {
			xfer.msgs = msgs;
			xfer.nmsgs = 2;
		} else {
			xfer.msgs = wlen ? &msgs[0] : &msgs[1];
			xfer.nmsgs = 1;
		}

		if (ioctl(data->fd, I2C_RDWR, &xfer) < 0) {
			fprintf(stderr, "Error writing data to i2c(%s)\n",
					strerror(errno));
			goto free_msg;
		}

		for (i = 0; i < xfer.nmsgs; i++) {
			/* return rx buffer if requested */
			if (self->bev && (xfer.msgs[i].flags & I2C_M_RD)) {
				rteipc_buffer(self->bev,
					      xfer.msgs[i].buf, xfer.msgs[i].len);
			}
		}
free_msg:
		if (rx_buf)
			free(rx_buf);
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
		free(data);
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

COMPATIBLE_WITH(i2c, COMPAT_IPC);
struct rteipc_ep_ops i2c_ops = {
	.on_data = i2c_on_data,
	.open = i2c_open,
	.close = i2c_close,
	.compatible = i2c_compatible
};
