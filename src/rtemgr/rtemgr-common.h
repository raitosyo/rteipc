// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEMGR_COMMON_H
#define _RTEMGR_COMMON_H

#include <stdint.h>
#include "rteipc.h"
#include "ep.h"

#define array_size(array) \
		(sizeof(array) / sizeof(array[0]))

#define strmatch(lh, rh) \
		!!((strlen(lh) == strlen(rh)) && !strcmp(lh, rh))

#define PREFIX_IPC		"ipc://"
#define PREFIX_INET		"inet://"
#define PREFIX_TTY		"tty://"
#define PREFIX_SPI		"spi://"
#define PREFIX_I2C		"i2c://"
#define PREFIX_GPIO		"gpio://"
#define PREFIX_SYSFS		"sysfs://"

#define __URI(prefix, path)	prefix path
#define URI(bus, path)		__URI(PREFIX_##bus, path)

#define RTEMGRD_CTLPORT		"@rtemgrd"

enum {
	RTECMD_LIST = 1,
	RTECMD_OPEN,
	RTECMD_CLOSE,
	RTECMD_ROUTE,
	RTECMD_FORGET,
	RTECMD_XFER,
	RTECMD_CAT,
	RTECMD_MAX
};

typedef struct rtemgr_interface {
	int id;            /* interface id */
	int bus_type;      /* type of endpoint */
	char name[16];     /* interface name */
	char path[128];    /* uri of endpoint */
	int domain;        /* domain id */
	int managed;       /* if it's a managed interface or not */
	char partner[16];  /* partner or sender interface name */
} rtemgr_intf;

/* Packet for request and reply between rtemgr client and server */
typedef struct rtemgr_managed_data {
	struct rtecmd {
		int action;      /* RTECMD_{OPEN,BIND,...} */
		int error;       /* 0 on success, -1 on failure */

		struct rtecmd_val {
			void *v;
			size_t s;
			struct {
				uint16_t addr;
				uint16_t rsize;
			} extra;
		} val;
	} cmd;
	int nr_intf;
	rtemgr_intf *interfaces;
} rtemgr_data;

/* Allocate new interface and return a pointer to it */
rtemgr_intf *rtemgr_data_alloc_interface(rtemgr_data *d);

/* Remove interface and return remaining number of interfaces */
int rtemgr_data_remove_interface(rtemgr_data *d);

/* Remove all interfaces */
void rtemgr_data_cleanup_interfaces(rtemgr_data *d);

/* Allocate rtemgr_data */
rtemgr_data *rtemgr_data_alloc(void);

/* Free rtemgr_data */
void rtemgr_data_free(rtemgr_data *d);

/* Convert rtemgr_data to yaml formatted string */
int rtemgr_data_emit(const rtemgr_data *d, unsigned char *output, size_t size,
			size_t *written);

/* Parse yaml formatted data and convert it to rtemgr_data */
rtemgr_data *rtemgr_data_parse(const unsigned char *input, size_t size);

#endif /* _RTEMGR_COMMON_H */
