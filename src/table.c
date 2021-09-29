// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdlib.h>
#include <string.h>
#include "table.h"


static inline void __set_desc_id(ev_uint64_t *desc, int id)
{
	int index = id / DESC_BIT_WIDTH;
	ev_uint64_t *bits = &desc[index];
	*bits |= (1ull << (id % DESC_BIT_WIDTH));
}

static inline void __clear_desc_id(ev_uint64_t *desc, int id)
{
	int index = id / DESC_BIT_WIDTH;
	ev_uint64_t *bits = &desc[index];
	*bits &= ~(1ull << (id % DESC_BIT_WIDTH));
}

static int __dtbl_next_id(const dtbl_t *table)
{
	int width = DESC_BIT_WIDTH;
	int start = table->next_entry % width;
	int index = table->next_entry / width;
	int max = table->max_entries;
	ev_uint64_t val;
	int i;

	if (index >= max)
		index = start = 0;

	for (;;) {
		if (index >= max)
			break;

		val = table->desc[index];
		for (i = start; i < start + width; i++) {
			if ((val >> (i % width)) & 1)
				continue; // used
			return (index * width) + (i % width);
		}
		index++;
	}

	return -1;
}

void *dtbl_get(dtbl_t *table, int id)
{
	void *retval = NULL;
	dtbl_entry_t *e;
	node_t *n;

	if (!table)
		return NULL;

	pthread_mutex_lock(&table->lock);

	list_each(&table->entry_list, n, {
		e = list_entry(n, dtbl_entry_t, node);
		if (e->id == id) {
			retval = e->value;
			break;
		}
	})

	pthread_mutex_unlock(&table->lock);
	return retval;
}

int dtbl_set(dtbl_t *table, void *val)
{
	dtbl_entry_t *entry;
	size_t entry_size;
	int id;

	if (!table)
		return -1;

	pthread_mutex_lock(&table->lock);

	if (!table->desc) {
		entry_size = DESC_BIT_WIDTH * table->max_entries;
		table->desc = malloc(entry_size);
		if (!table->desc) {
			id = -1;
			goto out;
		}
		memset(table->desc, 0, entry_size);
	}

	id = __dtbl_next_id(table);
	if (id < 0)
		goto out;

	entry = malloc(sizeof(*entry));
	if (!entry)
		goto out;

	entry->id = id;
	entry->value = val;
	list_push(&table->entry_list, &entry->node);

	__set_desc_id(table->desc, id);
	table->next_entry = id + 1;
out:
	pthread_mutex_unlock(&table->lock);
	return id;
}

void dtbl_del(dtbl_t *table, int id)
{
	if (table) {
		pthread_mutex_lock(&table->lock);

		if ((id / DESC_BIT_WIDTH) < table->max_entries)
			__clear_desc_id(table->desc, id);

		pthread_mutex_unlock(&table->lock);
	}
}
