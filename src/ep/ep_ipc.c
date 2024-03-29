// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"


/* Default port number used by INET endpoint*/
#define INET_DEFAULT_PORT		9110

struct ipc_data {
	struct evconnlistener *el;
	struct bufferevent *cli;
	char *path;
	int abstract;
};

static void listen_cb(struct evconnlistener *, evutil_socket_t,
				struct sockaddr *, int, void *);

static void event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct rteipc_ep *self = arg;
	struct ipc_data *data = self->data;

	if (events & BEV_EVENT_EOF) {
		printf("Connection closed.\n");
	} else if (events & BEV_EVENT_ERROR) {
		printf("Got an error on the connection: %s\n",
				strerror(errno));
	}
	bufferevent_free(bev);
	data->cli = NULL;
	/* accept another connection again */
	evconnlistener_set_cb(data->el, listen_cb, (void *)self);
}

static void read_cb(struct bufferevent *bev, void *arg)
{
	struct rteipc_ep *self = arg;

	if (self->bev)
		evbuffer_add_buffer(bufferevent_get_output(self->bev),
				bufferevent_get_input(bev));
}

static void error_cb(struct evconnlistener *el, void *arg)
{
	struct rteipc_ep *self = arg;
	int err = EVUTIL_SOCKET_ERROR();

	fprintf(stderr, "Error %d (%s) on the listener\n", err,
			evutil_socket_error_to_string(err));
	event_base_loopexit(self->base, NULL);
}

static void listen_cb(struct evconnlistener *el, evutil_socket_t fd,
				struct sockaddr *sa, int socklen, void *arg)
{
	struct rteipc_ep *self = arg;
	struct ipc_data *data = self->data;
	struct bufferevent *bev = bufferevent_socket_new(self->base, fd,
			BEV_OPT_CLOSE_ON_FREE);

	if (!bev) {
		fprintf(stderr, "Error constructing bufferevent\n");
		return;
	}

	/* accept only one connection */
	evconnlistener_set_cb(el, NULL, NULL);

	bufferevent_setcb(bev, read_cb, NULL, event_cb, self);
	bufferevent_enable(bev, EV_READ);
	data->cli = bev;
	if (self->bev)
		bufferevent_flush(self->bev, EV_READ, BEV_FLUSH);
}

/**
 * IPC endpoint data format
 *
 *   Input:  { ANY }
 *   Output: { ANY }
 *     data format is defined by the end users (i.e, process)
 */
static void ipc_on_data(struct rteipc_ep *self, struct bufferevent *bev)
{
	struct ipc_data *data = self->data;

	if (!data->cli)
		return;

	evbuffer_add_buffer(bufferevent_get_output(data->cli),
			bufferevent_get_input(bev));
}

static int ipc_open(struct rteipc_ep *self, const char *path)
{
	struct sockaddr *addr;
	struct sockaddr_in sin;
	struct sockaddr_un sun;
	struct ipc_data *data;
	int abstract = 0;
	int addrlen;
	char sip[16] = {0}, sport[6] = {0};
	long port;

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_ipc\n");
		return -1;
	}

	memset(data, 0, sizeof(*data));
	if (self->type == EP_INET) {
		memset(&sin, 0, sizeof(sin));
		sscanf(path, "%[^:]:%[^:]", sip, sport);
		sin.sin_family = AF_INET;
		if (inet_pton(AF_INET, sip,  &sin.sin_addr.s_addr) != 1) {
			fprintf(stderr, "Invalid ip address\n");
			goto out;
		}
		if (!strlen(sport)) {
			sin.sin_port = htons(INET_DEFAULT_PORT);
		} else {
			errno = 0;
			port = strtol(sport, NULL, 10);
			if (errno) {
				fprintf(stderr, "Invalid port number\n");
				goto out;
			}
			sin.sin_port = htons(port);
		}
		addrlen = sizeof(sin);
		addr = (struct sockaddr *)&sin;
	} else {
		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		strncpy(sun.sun_path, path, sizeof(sun.sun_path) - 1);
		addrlen = offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;

		if (sun.sun_path[0] == '@') {
			data->abstract = 1;
			sun.sun_path[0] = 0;
			addrlen = addrlen - 1;
		} else {
			unlink(path);
			data->path = strdup(path);
		}
		addr = (struct sockaddr *)&sun;
	}

	data->el = evconnlistener_new_bind(
			self->base, listen_cb, (void *)self,
			(LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE), -1,
			addr, addrlen);
out:
	if (!data->el) {
		fprintf(stderr, "Could not create a listener\n");
		return -1;
	}

	evconnlistener_set_error_cb(data->el, error_cb);
	self->data = data;
	return 0;
}

static void ipc_close(struct rteipc_ep *self)
{
	struct ipc_data *data = self->data;

	if (data->cli)
		bufferevent_free(data->cli);

	evconnlistener_free(data->el);

	if (!data->abstract)
		unlink(data->path);

	free(data);
}

COMPATIBLE_WITH(ipc, COMPAT_ANY);
struct rteipc_ep_ops ipc_ops = {
	.on_data = ipc_on_data,
	.open = ipc_open,
	.close = ipc_close,
	.compatible = ipc_compatible
};
