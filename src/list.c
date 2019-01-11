// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include "list.h"


void list_init(list_t *list)
{
	list->head = NULL;
	list->tail = NULL;
}

int list_empty(list_t *list)
{
	return list->head == NULL;
}


iterator_t *iterator_new(list_t *list)
{
	iterator_t *it = malloc(sizeof(*it));
	if (it)
		it->next = list->head;
	return it;
}

void iterator_destroy(iterator_t *it)
{
	free(it);
}

node_t *iterator_next(iterator_t *it)
{
	node_t *cur = it->next;
	if (cur)
		it->next = cur->next;
	return cur;
}

void list_push(list_t *list, node_t *node)
{
	if (!node)
		return;

	if (list_empty(list)) {
		list->head = list->tail = node;
		node->prev = node->next = NULL;
	} else {
		node->prev = list->tail;
		node->next = NULL;
		list->tail->next = node;
		list->tail = node;
	}
}

node_t *list_pop(list_t *list)
{
	node_t *node = list->tail;

	if (list_empty(list))
		return NULL;

	if (node->prev) {
		node->prev->next = NULL;
		list->tail = node->prev;
	} else {
		list->tail = list->head = NULL;
	}

	node->next = node->prev = NULL;
	return node;
}

void list_remove(list_t *list, node_t *node)
{
	if (node->prev)
		node->prev->next = node->next;
	else
		list->head = node->next;

	if (node->next)
		node->next->prev = node->prev;
	else
		list->tail = node->prev;

	node->next = node->prev = NULL;
}
