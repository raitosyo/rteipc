// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_MSG_H
#define _RTEIPC_MSG_H

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/util.h>

int rteipc_msg_drain(struct evbuffer *buf, size_t *size_out, char **msg_out);

int rteipc_msg_write(evutil_socket_t fd, const void *data, size_t len);

int rteipc_evbuffer(struct bufferevent *bev, struct evbuffer *buf);

int rteipc_buffer(struct bufferevent *bev, const void *data, size_t len);

#endif /* _RTEIPC_MSG_H */
