// Copyright (c) 2021 Ryosuke Saito All rights reserved.
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
#include <libudev.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"
#include "message.h"

/**
 * SYSFS endpoint
 *
 * Data format:
 *   Input  { char[] }
 *     arg1 - string as 'attr=value' pair for setting or 'attr' for reading
 *            value
 *
 *   Output { char[] }
 *     arg1 - 'attr=value' string if a read requested
 */

struct sysfs_data {
	struct udev_device *device;
};

static void sysfs_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct sysfs_data *data = self->data;
	struct udev_list_entry *list_entry;
	struct evbuffer *in = bufferevent_get_input(bev);
	const char *value;
	char *msg, *pos, *text, buf[PATH_MAX];
	size_t len;
	int ret;

	for (;;) {
		if (!(ret = rteipc_msg_drain(in, &len, &msg)))
			return;

		if (ret < 0) {
			fprintf(stderr, "Error reading data\n");
			return;
		}

		/*
		 * Duplicate a new string to ensure having a terminating null
		 * byte.
		 */
		text = strndup(msg, len);
		free(msg);

		/*
		 * A msg passed as "attr=value" pair for setting, as "attr" for
		 * getting value. Setting a NULL value "attr=" is also
		 * acceptable.
		 */
		if ((pos = strchr(text, '='))) {
			*pos = '\0';
			/* NULL value ? */
			if (text + len <= ++pos)
				pos = NULL;

			if (udev_device_set_sysattr_value(data->device,
						text, pos) != 0) {
				fprintf(stderr,
					"Error setting attr:%s value:%s\n",
					text, pos ?: "NULL");
				goto free_msg;
			}
		} else {
			value = udev_device_get_sysattr_value(
					data->device, text);
			if (value) {
				snprintf(buf, sizeof(buf), "%s=%s", text, value);
				if (self->bev)
					rteipc_buffer(self->bev,
						      buf, strlen(buf));
			} else {
				fprintf(stderr,
					"Error getting attr:%s\n", text);
			}
		}
free_msg:
		free(text);
	}
}

static int sysfs_open(struct rteipc_ep *self, const char *path)
{
	struct udev *udev;
	struct udev_device *device;
	struct sysfs_data *data;
	char buf[PATH_MAX], *pos;

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_sysfs\n");
		return -1;
	}
	memset(data, 0, sizeof(*data));

	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "Failed to allocate memory for udev\n");
		goto free_data;
	}

	strncpy(buf, path, PATH_MAX);
	device = udev_device_new_from_syspath(udev, buf);
	if (!device && (pos = strchr(buf, ':'))) {
		device = udev_device_new_from_device_id(udev, buf);
		if (!device) {
			*pos = '\0';
			device = udev_device_new_from_subsystem_sysname(udev,
					buf, ++pos);
		}
	}

	if (device) {
		data->device = device;
		self->data = data;
		return 0;
	}

	fprintf(stderr, "Failed to open sysfs device(%s)\n", path);
	udev_unref(udev);
free_data:
	free(data);
	return -1;
}

static void sysfs_close(struct rteipc_ep *self)
{
	struct sysfs_data *data = self->data;
	udev_device_unref(data->device);
}

COMPATIBLE_WITH(sysfs, COMPAT_IPC);
struct rteipc_ep_ops sysfs_ops = {
	.on_data = sysfs_on_data,
	.open = sysfs_open,
	.close = sysfs_close,
	.compatible = sysfs_compatible
};
