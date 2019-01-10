// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <gpiod.h>
#include "ep.h"
#include "message.h"


struct gpio_data {
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	int out;
	struct event *ev;
	struct bufferevent *up_read, *up_write;
};

static void upstream(evutil_socket_t fd, short what, void *arg)
{
	struct rteipc_ep *self = arg;
	struct gpio_data *data = self->data;
	struct gpiod_line_event ev;
	struct evbuffer *buf = evbuffer_new();
	size_t nl;
	int val;

	if (gpiod_line_event_read(data->line, &ev) < 0) {
		fprintf(stderr, "Error reading gpio event\n");
		goto err;
	}

	val = gpiod_line_get_value(data->line);
	if (val < 0) {
		fprintf(stderr, "Error reading gpio value\n");
		goto err;
	}

	evbuffer_add(buf, val ? "1" : "0", 1);
	nl = htonl(1);
	evbuffer_prepend(buf, &nl, 4);
	bufferevent_write_buffer(data->up_write, buf);
	return;

err:
	event_del(data->ev);
	return;
}

static void downstream(struct bufferevent *bev, void *arg)
{
	struct rteipc_ep *self = arg;
	struct gpio_data *data = self->data;
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

		if ((len == 1 && *msg == '1') ||
		    (len == 2 && strncasecmp(msg, "hi", len))) {
			gpiod_line_set_value(data->line, 1);
		} else if ((len == 1 && *msg == '0') ||
		    (len == 2 && strncasecmp(msg, "lo", len))) {
			gpiod_line_set_value(data->line, 0);
		} else {
			fprintf(stderr,
				"Warn: ep_gpio cannot understand:%.*s\n",
				len, msg);
		}
		free(msg);
	}
}

static int gpio_route(struct rteipc_ep *self, int what,
				struct bufferevent *up_write,
				struct bufferevent *up_read)
{
	struct gpio_data *data = self->data;

	if (what == RTEIPC_ROUTE_ADD) {
		if (up_write)
			data->up_write = up_write;
		if (up_read) {
			if (!data->out) {
				fprintf(stderr,
					"Warn: cannot set "
					"RTEIPC_FORWARD to gpio input\n");
				return -1;
			}
			data->up_read = up_read;
			bufferevent_setcb(up_read, downstream,
						NULL, NULL, self);
			bufferevent_enable(up_read, EV_READ);
		}
	} else if (what == RTEIPC_ROUTE_DEL) {
		if (data->up_write == up_write) {
			bufferevent_free(up_write);
			data->up_write = NULL;
		}
		if (data->up_read == up_read) {
			bufferevent_free(up_read);
			data->up_read = NULL;
		}
	}
	return 0;
}

static int gpio_bind(struct rteipc_ep *self, const char *path)
{
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	struct gpio_data *data;
	struct event *ev;
	char consumer[256] = {0};
	char chip_path[PATH_MAX] = {0};
	char out[4] = {0};
	int val = 0;
	int num, fd, ret;

	sscanf(path, "%[^@]@%[^-]-%d,%[^,],%d",
			consumer, chip_path, &num, out, &val);

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_gpio\n");
		return -1;
	}

	memset(data, 0, sizeof(*data));

	chip = gpiod_chip_open(chip_path);
	if (!chip) {
		fprintf(stderr, "Failed to open gpiochip:%s\n", chip_path);
		goto free_data;
	}
	data->chip = chip;

	line = gpiod_chip_get_line(chip, num);
	if (!line) {
		fprintf(stderr, "Failed to get num=%s of %s\n",
				num, chip_path);
		goto free_chip;
	}
	data->line = line;

	if (strlen(out) == 1 && !strncasecmp(out, "1", 1) ||
			strlen(out) == 3 && !strncasecmp(out, "out", 3)) {
		data->out = 1;
		ret = gpiod_line_request_output(line, consumer, val);
		if (ret < 0) {
			fprintf(stderr, "Failed to request gpio output\n");
			goto free_line;
		}
	} else if (strlen(out) == 1 && !strncasecmp(out, "0", 1) ||
			strlen(out) == 2 && !strncasecmp(out, "in", 2)) {
		ret  = gpiod_line_request_both_edges_events(line, consumer);
		if (ret < 0) {
			fprintf(stderr, "Failed to request gpio events\n");
			goto free_line;
		}
		fd = gpiod_line_event_get_fd(line);
		if (fd < 0) {
			fprintf(stderr, "Failed to get gpio event fd\n");
			goto free_line;
		}
		ev = event_new(self->base, fd, EV_READ | EV_PERSIST,
			       upstream, self);
		data->ev = ev;
		event_add(ev, NULL);
	} else {
		fprintf(stderr, "Invalid path:%s\n", chip_path);
		goto free_line;
	}

	self->data = data;

	return 0;

free_line:
	gpiod_line_close_chip(line);
free_chip:
	gpiod_chip_close(chip);
free_data:
	free(data);
	return -1;
}

static void gpio_unbind(struct rteipc_ep *self)
{
	struct gpio_data *data = self->data;
	event_del(data->ev);
}

struct rteipc_ep_ops ep_gpio = {
	.route = gpio_route,
	.bind = gpio_bind,
	.unbind = gpio_unbind,
};
