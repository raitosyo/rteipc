// Copyright (c) 2018 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <event2/event.h>


__thread struct event_base *__base;

void rteipc_dispatch(struct timeval *tv)
{
	if (!__base)
		return;

	if (tv)
		event_base_loopexit(__base, tv);
	event_base_dispatch(__base);
}

void rteipc_reinit(void)
{
	if (!__base) {
		fprintf(stderr, "rteipc is not initialized\n");
		return;
	}
	event_reinit(__base);
}

void rteipc_init(struct event_base *base)
{
	if (__base) {
		fprintf(stderr, "rteipc is already initialized\n");
		return;
	}
	__base = (base) ?: event_base_new();
}

void rteipc_shutdown(void)
{
	if (__base)
		event_base_free(__base);
	libevent_global_shutdown();
}
