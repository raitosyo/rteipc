// Copyright (c) 2019 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_DTBL_H
#define _RTEIPC_DTBL_H

#include <event2/util.h>
#include "list.h"


#define DESC_BIT_WIDTH		64

#define DTBL_INITIALIZER(n)                             \
	{                                               \
		.desc = NULL,                           \
		.next_entry = 0,                        \
		.max_entries = ((n) / DESC_BIT_WIDTH),  \
		.entry_list = LIST_INITIALIZER,         \
		.lock = PTHREAD_MUTEX_INITIALIZER       \
	}

typedef struct descriptor_table_entry {
	int id;
	void *value;
	node_t node;
} dtbl_entry_t;

typedef struct descriptor_table {
	ev_uint64_t *desc;
	int next_entry;
	size_t max_entries;
	list_t entry_list;
	pthread_mutex_t lock;
} dtbl_t;

int dtbl_set(dtbl_t *table, void *val);

void dtbl_del(dtbl_t *table, int id);

void *dtbl_get(dtbl_t *table, int id);

#endif /* _RTEIPC_DTBL_H */
