// Copyright (c) 2019 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _RTEIPC_LIST_H
#define _RTEIPC_LIST_H

#include <stdlib.h>


#define LIST_INITIALIZER \
	{ NULL, NULL }

/* Iterate over a list and do body statements */
#define list_each(list, node, body)                   \
	{                                             \
		iterator_t *it = iterator_new(list);  \
		while (node = iterator_next(it)) {    \
			body;                         \
		}                                     \
		iterator_destroy(it);                 \
	}

/* Return a pointer to the list item whose node_t member's name is 'node' */
#define list_entry(node_ptr, type) \
	(type *)((char *)(node_ptr) - (char *)&((type *)0)->node)

typedef struct node {
	struct node *prev;
	struct node *next;
} node_t;

typedef struct {
	node_t *head;
	node_t *tail;
} list_t;

typedef struct {
	node_t *next;
} iterator_t;

void list_init(list_t *list);

iterator_t *iterator_new(list_t *list);

void iterator_destroy(iterator_t *it);

node_t *iterator_next(iterator_t *it);

int list_empty(list_t *list);

void list_push(list_t *list, node_t *node);

node_t *list_pop(list_t *list);

void list_remove(list_t *list, node_t *node);

#endif /* _RTEIPC_LIST_H */
