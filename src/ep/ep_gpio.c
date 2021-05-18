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

/**
 * GPIO endpoint
 *
 * Data format:
 *   (Only for gpio-out direction)
 *   Input  { uint8_t }
 *     arg1 - GPIO value, 1(assert) or 0(deassert)
 *
 *   (Only for gpio-in direction)
 *   Output { int8_t, int64_t, int64_t }
 *     arg1 - GPIO value, 1(assert) or 0(deassert)
 *     arg2 - time of event occurrence (sec)
 *     arg3 - time of event occurrence (nsec)
 */

struct gpio_data {
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	int out;
	struct event *ev;
};

/**
 * GPIO event handling
 */
static void upstream(evutil_socket_t fd, short what, void *arg)
{
	struct rteipc_ep *self = arg;
	struct gpio_data *data = self->data;
	struct gpiod_line_event ev;
	struct evbuffer *buf = evbuffer_new();
	size_t nl;
	uint8_t value;

	if (gpiod_line_event_read(data->line, &ev) < 0) {
		fprintf(stderr, "Error reading gpio event\n");
		goto err;
	}

	/* discard the event if it's not bound yet */
	if (!self->bev)
		return;

	value = (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE ? 1 : 0);
	evbuffer_add(buf, &value, sizeof(value));                  /* arg1 */
	evbuffer_add(buf, &ev.ts.tv_sec, sizeof(ev.ts.tv_sec));    /* arg2 */
	evbuffer_add(buf, &ev.ts.tv_nsec, sizeof(ev.ts.tv_nsec));  /* arg3 */
	nl = htonl(evbuffer_get_length(buf));
	evbuffer_prepend(buf, &nl, 4);
	bufferevent_write_buffer(self->bev, buf);
	return;
err:
	event_del(data->ev);
	return;
}

static void gpio_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct gpio_data *data = self->data;
	struct evbuffer *in = bufferevent_get_input(bev);
	char *msg;
	size_t len;
	uint8_t value;
	int ret;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		if (!data->out) {
			fprintf(stderr, "Cannot write to an input GPIO\n");
			goto free_msg;
		}

		value = *((uint8_t *)msg);  /* arg1 */

		if (len != sizeof(uint8_t) || value > 1) {
			fprintf(stderr, "Invalid argument\n");
			goto free_msg;
		}

		gpiod_line_set_value(data->line, value);
free_msg:
		free(msg);
	}
}

static int gpio_open(struct rteipc_ep *self, const char *path)
{
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	struct gpio_data *data;
	struct event *ev;
	char consumer[256] = {0};
	char chip_path[PATH_MAX] = {0};
	char dir[4] = {0};
	int val = 0;
	int num, fd, ret;

	sscanf(path, "%[^@]@%[^-]-%d,%[^,],%d",
			consumer, chip_path, &num, dir, &val);

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

	if (strlen(dir) == 3 && !strncasecmp(dir, "out", 3)) {
		data->out = 1;
		ret = gpiod_line_request_output(line, consumer, val);
		if (ret < 0) {
			fprintf(stderr, "Failed to request gpio output\n");
			goto free_line;
		}
	} else if (strlen(dir) == 2 && !strncasecmp(dir, "in", 2)) {
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
		fprintf(stderr, "Invalid path:%s\n", path);
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

static void gpio_close(struct rteipc_ep *self)
{
	struct gpio_data *data = self->data;
	event_del(data->ev);
}

COMPATIBLE_WITH(gpio, COMPAT_IPC);
struct rteipc_ep_ops gpio_ops = {
	.on_data = gpio_on_data,
	.open = gpio_open,
	.close = gpio_close,
	.compatible = gpio_compatible
};
