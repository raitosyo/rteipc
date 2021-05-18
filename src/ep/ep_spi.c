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

/**
 * SPI endpoint
 *
 * Data format:
 *   Input  { uint16_t, uint8_t, uint8_t[] }
 *     arg1 - SPI tx buffer size
 *     arg2 - RD flag, if true, return rx buffer, otherwise discard data
 *            in rx buffer
 *     arg3 - SPI tx buffer
 *
 *   Output { uint8_t[] }
 *     arg1 - SPI rx buffer, if RD flag is set
 */

struct spi_data {
	int fd;
};

static void spidev_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct spi_data *data = self->data;
	struct evbuffer *in = bufferevent_get_input(bev);
	struct spi_ioc_transfer *xfer = NULL;
	char *msg, *pos;
	uint16_t wlen;
	uint8_t rdflag, *rx_buf = NULL;
	size_t len, nl, num;
	int ret, i;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		if (len < sizeof(wlen) + sizeof(rdflag)) {
			fprintf(stderr, "data size is odd\n");
			goto free_msg;
		}

		pos = msg;
		wlen = *((uint16_t *)pos);   /* arg1 */
		pos += sizeof(wlen);
		rdflag = *((uint8_t *)pos);  /* arg2 */
		pos += sizeof(rdflag);
		/* below, pos points to tx data(arg3) */

		if (pos + wlen != msg + len) {
			fprintf(stderr, "Invalid arguments\n");
			goto free_msg;
		}

		rx_buf = malloc(wlen);
		xfer = malloc(sizeof(*xfer) * wlen);
		if (!rx_buf || !xfer) {
			fprintf(stderr, "Failed to allocate memory\n");
			goto free_msg;
		}

		for (i = 0; i < wlen; i++) {
			xfer[i].tx_buf = (unsigned long)&pos[i];
			xfer[i].rx_buf = (unsigned long)&rx_buf[i];
			xfer[i].len = 1;

			if (ioctl(data->fd, SPI_IOC_MESSAGE(1),
						&xfer[i]) < 0) {
				fprintf(stderr,
					"Error writing data to spidev(%d)\n",
					errno);
				goto free_msg;
			}
		}

		if (self->bev && rdflag) {
			/* return rx_buf if requested */
			rteipc_buffer(self->bev, (void *)rx_buf, wlen);
		}
free_msg:
		if (rx_buf)
			free(rx_buf);
		if (xfer)
			free(xfer);
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
