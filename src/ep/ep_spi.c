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
#include <linux/spi/spidev.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"
#include "message.h"


/*
 * NOTE: EP_SPI might affect the performance of other endpoints in the context
 * where EP_SPI is processed (i.e., using the same event_base) because of no
 * async I/O support like in the spidev linux driver at this time.
 */

struct spi_data {
	int fd;
};

static void spidev_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct spi_data *data = self->data;
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len, nl, num;
	int ret, i;
	struct spi_ioc_transfer *xfer;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		if (len % sizeof(*xfer)) {
			fprintf(stderr, "data size is odd\n");
			goto free_msg;
		}

		for (i = 0, num = len / sizeof(*xfer); i < num; i++) {
			xfer = (struct spi_ioc_transfer *)msg + i;

			if (ioctl(data->fd, SPI_IOC_MESSAGE(1), xfer) < 0) {
				fprintf(stderr, "Error writing data to spidev\n");
				goto free_msg;
			}

			if (self->bev && xfer->rx_buf) {
				rteipc_buffer(self->bev,
					      (void *)xfer->rx_buf, xfer->len);
			}
		}
free_msg:
		free(msg);
	}
}

static int init_spidev(const char *path, int speed, int mode)
{
	int fd;

	if ((fd = open(path, O_RDWR)) < 0) {
		fprintf(stderr, "Failed to open spidev\n");
		return -1;
	}

	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
		fprintf(stderr, "Failed to set spi mode\n");
		goto out;
	}

	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
		fprintf(stderr, "Failed to set spi mode\n");
		goto out;
	}

	return fd;
out:
	close(fd);
	return -1;
}

static int spidev_open(struct rteipc_ep *self, const char *path)
{
	struct spi_data *data;
	char dev[128] = {0};
	int speed, mode = 3;
	int fd;

	sscanf(path, "%[^,],%d,%d", dev, &speed, &mode);

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_spi\n");
		return -1;
	}

	memset(data, 0, sizeof(*data));

	fd = init_spidev(dev, speed, mode);
	if (fd < 0) {
		fprintf(stderr, "Failed to init spidev\n");
		return -1;
	}

	data->fd = fd;
	self->data = data;

	return 0;
}

static void spidev_close(struct rteipc_ep *self)
{
	struct spi_data *data = self->data;
	close(data->fd);
}

COMPATIBLE_WITH(spidev, COMPAT_IPC);
struct rteipc_ep_ops spi_ops = {
	.on_data = spidev_on_data,
	.open = spidev_open,
	.close = spidev_close,
	.compatible = spidev_compatible
};
