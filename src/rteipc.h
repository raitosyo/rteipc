// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_H
#define _RTEIPC_H

#include <stdbool.h>
#include <sys/time.h>
#include <event2/event.h>
#include <event2/buffer.h>

#define RTEIPC_NO_EXIT_ON_ERR		(1 << 0)

typedef void (*rteipc_read_cb)(int ctx, void *data, size_t len, void *arg);
typedef void (*rteipc_err_cb)(int ctx, short events, void *arg);
typedef void (*rteipc_sw_cb)(int sw, int ep, void *data,
					size_t len, void *arg);
typedef bool (*rteipc_filter_cb)(const char *src, const char *dst, void *data,
		size_t len);

void rteipc_init(struct event_base *base);
void rteipc_reinit(void);
void rteipc_shutdown(void);
void rteipc_dispatch(struct timeval *tv);

int rteipc_connect(const char *uri);
int rteipc_send(int cid, const void *data, size_t len);
int rteipc_evsend(int cid, struct evbuffer *buf);
int rteipc_setcb(int cid, rteipc_read_cb read_cb, rteipc_err_cb err_cb,
			void *arg, short flag);

int rteipc_ep_open(const char *uri);
void rteipc_ep_close(int id);
int rteipc_ep_bind(int id_a, int id_b);
void rteipc_ep_unbind(int id);

int rteipc_sw(void);
int rteipc_port(int sid, const char *key);
int rteipc_filter(int sw, rteipc_filter_cb filter);

#endif /* _RTEIPC_H */
