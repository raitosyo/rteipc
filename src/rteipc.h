// Copyright (c) 2018 - 2021 Ryosuke Saito All rights reserved.
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
typedef bool (*rteipc_filter_cb)(const char *key_src, const char *key_dst,
		void *data, size_t len);

void rteipc_init(struct event_base *base);
void rteipc_reinit(void);
void rteipc_shutdown(void);
void rteipc_dispatch(struct timeval *tv);

int rteipc_connect(const char *uri);
int rteipc_send(int ctx, const void *data, size_t len);
int rteipc_evsend(int ctx, struct evbuffer *buf);
int rteipc_setcb(int ctx, rteipc_read_cb read_cb, rteipc_err_cb err_cb,
			void *arg, short flag);

int rteipc_open(const char *uri);
void rteipc_close(int ep);
int rteipc_bind(int ea, int eb);
void rteipc_unbind(int ep);

int rteipc_sw(void);
int rteipc_port(int sw, const char *key);
int rteipc_filter(int sw, rteipc_filter_cb filter);

#endif /* _RTEIPC_H */
