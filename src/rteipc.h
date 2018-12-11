// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_H
#define _RTEIPC_H

#include <event2/event.h>
#include <event2/buffer.h>

#define RTEIPC_FORWARD		1
#define RTEIPC_REVERSE		2
#define RTEIPC_BIDIRECTIONAL  (RTEIPC_FORWARD | RTEIPC_REVERSE)

typedef void (*rteipc_read_cb)(int ctx, void *data, size_t len, void *arg);
typedef void (*rteipc_sw_handler)(int sw, int ep, void *data,
				  size_t len, void *arg);

void rteipc_init(struct event_base *base);
void rteipc_reinit(void);
void rteipc_shutdown(void);
void rteipc_dispatch(void);
int rteipc_connect(const char *uri, rteipc_read_cb fn, void *arg);
int rteipc_send(int id, const void *data, size_t len);
int rteipc_evsend(int id, struct evbuffer *buf);
int rteipc_sw(void);
int rteipc_sw_xfer(int sw_id, int ep_id, const void *data, size_t len);
int rteipc_sw_evxfer(int sw_id, int ep_id, struct evbuffer *buf);
int rteipc_sw_ep_open(int sw_id, rteipc_sw_handler handler, void *arg);
int rteipc_ep_open(const char *uri);
int rteipc_ep_route(int ep_src, int ep_dst, int flag);

#endif /* _RTEIPC_H */
