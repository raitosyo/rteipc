// Copyright (c) 2018 - 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_H
#define _RTEIPC_H

#include <stdbool.h>
#include <sys/time.h>
#include <event2/event.h>
#include <event2/buffer.h>

/* Deprecated. This flag is no effect. */
#define RTEIPC_NO_EXIT_ON_ERR		(1 << 0)

void rteipc_init(struct event_base *base);
void rteipc_reinit(void);
void rteipc_shutdown(void);
void rteipc_dispatch(struct timeval *tv);

int rteipc_open(const char *uri);
void rteipc_close(int ep);
int rteipc_bind(int ea, int eb);
void rteipc_unbind(int ep);

/**
 * A process can send data to ipc, inet, or loopback endpoint, then the data
 * will be transferred between the other endpoint bound to it.
 */

/* Definitions for the ipc and inet endpoint */

typedef void (*rteipc_read_cb)(int ctx, void *data, size_t len, void *arg);
typedef void (*rteipc_err_cb)(int ctx, short events, void *arg);
int rteipc_connect(const char *uri);
int rteipc_setcb(int ctx, rteipc_read_cb read_cb, rteipc_err_cb err_cb,
			void *arg, short flag);

int rteipc_send(int ctx, const void *data, size_t len);
int rteipc_evsend(int ctx, struct evbuffer *buf);
int rteipc_gpio_send(int ctx, uint8_t value);
int rteipc_i2c_send(int ctx, uint16_t addr, const uint8_t *data,
			uint16_t wlen, uint16_t rlen);
int rteipc_spi_send(int ctx, const uint8_t *data, uint16_t len, bool rdmode);
int rteipc_sysfs_send(int ctx, const char *attr, const char *newval);

/* Definitions for the loopback endpoint */

typedef void (*rteipc_lo_cb)(const char *name, void *data, size_t len, void *arg);
int rteipc_xfer_setcb(const char *name, rteipc_lo_cb cb, void *arg);
int rteipc_xfer(const char *name, const void *data, size_t len);
int rteipc_evxfer(const char *name, struct evbuffer *buf);
int rteipc_gpio_xfer(const char *name, uint8_t value);
int rteipc_i2c_xfer(const char *name, uint16_t addr, const uint8_t *data,
			uint16_t wlen, uint16_t rlen);
int rteipc_spi_xfer(const char *name, const uint8_t *data, uint16_t len,
			bool rdmode);
int rteipc_sysfs_xfer(const char *name, const char *attr, const char *newval);

#endif /* _RTEIPC_H */
