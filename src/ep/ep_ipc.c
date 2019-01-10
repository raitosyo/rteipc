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
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include "ep.h"


struct ipc_data {
	struct evconnlistener *el;
	struct bufferevent *up_read, *up_write, *down;
	char *path;
	int abstract;
};

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
	data->down = NULL;
	/* accept another connection again */
	evconnlistener_enable(data->el);
}

static void upstream(struct bufferevent *bev, void *arg)
{
	struct rteipc_ep *self = arg;
	struct ipc_data *data = self->data;

	if (!data->up_write)
		return;

	evbuffer_add_buffer(bufferevent_get_output(data->up_write),
			bufferevent_get_input(bev));
}

static void downstream(struct bufferevent *bev, void *arg)
{
	struct rteipc_ep *self = arg;
	struct ipc_data *data = self->data;

	if (!data->down)
		return;

	evbuffer_add_buffer(bufferevent_get_output(data->down),
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
	evconnlistener_disable(el);

	bufferevent_setcb(bev, upstream, NULL, event_cb, self);
	bufferevent_enable(bev, EV_READ);
	data->down = bev;
	if (data->up_read)
		bufferevent_flush(data->up_read, EV_READ, BEV_FLUSH);
}

static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
	struct rteipc_ep *self = arg;
	event_base_loopbreak(self->base);
}

static int ipc_route(struct rteipc_ep *self, int what,
				struct bufferevent *up_write,
				struct bufferevent *up_read)
{
	struct ipc_data *data = self->data;

	if (what == RTEIPC_ROUTE_ADD) {
		if (up_write)
			data->up_write = up_write;
		if (up_read) {
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

static int ipc_bind(struct rteipc_ep *self, const char *path)
{
	struct sockaddr_un addr;
	struct ipc_data *data;
	struct event *ev_sigint;
	int abstract = 0;
	int addrlen;

	data = malloc(sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for ep_ipc\n");
		return -1;
	}

	memset(data, 0, sizeof(*data));
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	addrlen = offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;

	if (addr.sun_path[0] == '@') {
		data->abstract = 1;
		addr.sun_path[0] = 0;
		addrlen = addrlen - 1;
	} else {
		unlink(path);
		data->path = strdup(path);
	}

	data->el = evconnlistener_new_bind(
			self->base, listen_cb, (void *)self,
			(LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE), -1,
			(struct sockaddr *)&addr, addrlen);

	if (!data->el) {
		fprintf(stderr, "Could not create a listener\n");
		return -1;
	}

	evconnlistener_set_error_cb(data->el, error_cb);
	self->data = data;

	ev_sigint = evsignal_new(self->base, SIGINT, signal_cb, self);
	event_add(ev_sigint, NULL);
	return 0;
}

static void ipc_unbind(struct rteipc_ep *self)
{
	struct ipc_data *data = self->data;

	evconnlistener_free(data->el);
	if (data->up_read)
		bufferevent_free(data->up_read);
	if (data->up_write)
		bufferevent_free(data->up_write);
	if (data->down)
		bufferevent_free(data->down);
	if (!data->abstract)
		unlink(data->path);
	free(data);
}

struct rteipc_ep_ops ep_ipc = {
	.route = ipc_route,
	.bind = ipc_bind,
	.unbind = ipc_unbind,
};
