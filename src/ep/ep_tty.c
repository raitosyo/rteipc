// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"
#include "message.h"


struct tty_data {
	int fd;
	struct event *ev;
};

static void upstream(evutil_socket_t fd, short what, void *arg)
{
	struct rteipc_ep *self = arg;
	struct tty_data *data = self->data;
	struct evbuffer *buf = evbuffer_new();
	char msg[256];
	size_t len, nl;
	int ret;

	if (!self->bev)
		return;

	len = read(fd, msg, sizeof(msg));

	if (len <= 0) {
		if (len == -1)
			fprintf(stderr, "tty upstream error:%d\n", errno);
		else if (len == 0)
			fprintf(stderr, "Connection closed\n");
		event_del(data->ev);
		return;
	}

	evbuffer_add(buf, msg, len);
	len = evbuffer_get_length(buf);
	nl = htonl(len);
	evbuffer_prepend(buf, &nl, 4);
	bufferevent_write_buffer(self->bev, buf);
}

static int open_uart(char const *path, int speed)
{
	struct termios ios;
	char buf[256];
	int ret, fd;

	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return -1;

	tcgetattr(fd, &ios);
	memset(&ios, 0, sizeof(ios));
	ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
	ios.c_cflag &= ~(CSIZE | PARENB);
	ios.c_cflag |= (CS8 | CLOCAL | CREAD);
	ios.c_iflag |= IGNPAR;
	ios.c_cc[VMIN]  = 1;
	ios.c_cc[VTIME] = 0;

	if(cfsetspeed(&ios, speed) < 0) {
		fprintf(stderr, "Failed to set baud rate\n");
		return -1;
	}

	if (tcsetattr(fd, TCSAFLUSH, &ios) < 0) {
		fprintf(stderr, "Failed to apply termios configs\n");
		return -1;
	}

	/* empty uart socket out */
	do {
		ret = read(fd, buf, sizeof(buf));
	} while (ret > 0 || (ret < 0 && errno == EINTR));

	return fd;
}

static void tty_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct tty_data *data = self->data;
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

		rteipc_msg_write(data->fd, msg, len);
		free(msg);
	}
}

static int tty_open(struct rteipc_ep *self, const char *path)
{
	struct tty_data *data;
	struct event *ev;
	char dev[128] = {0}, baudrate[16] = {0};
	int speed, fd;

	sscanf(path, "%[^,],%[^,]", dev, baudrate);

	if (!strcmp(baudrate, "921600"))
		speed = B921600;
	else if (!strcmp(baudrate, "576000"))
		speed = B576000;
	else if (!strcmp(baudrate, "500000"))
		speed = B500000;
	else if (!strcmp(baudrate, "460800"))
		speed = B460800;
	else if (!strcmp(baudrate, "230400"))
		speed = B230400;
	else if (!strcmp(baudrate, "115200"))
		speed = B115200;
	else if (!strcmp(baudrate, "57600"))
		speed = B57600;
	else
		speed = B115200;

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_tty\n");
		return -1;
	}

	memset(data, 0, sizeof(*data));
	fd = open_uart(dev, speed);
	if (fd < 0) {
		fprintf(stderr, "Failed to open tty\n");
		return -1;
	}

	data->fd = fd;
	self->data = data;

	ev = event_new(self->base, fd, EV_READ | EV_PERSIST, upstream, self);
	data->ev = ev;
	event_add(ev, NULL);
	return 0;
}

static void tty_close(struct rteipc_ep *self)
{
	struct tty_data *data = self->data;
	event_del(data->ev);
	close(data->fd);
}

struct rteipc_ep_ops tty_ops = {
	.on_data = tty_on_data,
	.open = tty_open,
	.close = tty_close,
};
