#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "rteipc.h"
#include "table.h"
#include "ep.h"


#define to_core(e) \
	(struct ep_core *)((char *)(e) - (char *)&(((struct ep_core *)0)->ep))

extern __thread struct event_base *__base;

extern struct rteipc_ep_ops ipc_ops;
extern struct rteipc_ep_ops tty_ops;
extern struct rteipc_ep_ops gpio_ops;
extern struct rteipc_ep_ops spi_ops;
extern struct rteipc_ep_ops i2c_ops;
extern struct rteipc_ep_ops sysfs_ops;
extern struct rteipc_ep_ops loop_ops;

struct ep_core {
	int id;
	int partner_id;
	struct rteipc_ep ep;
};

static struct rteipc_ep_ops *ep_ops_list[] = {
	[EP_TEMPLATE]  = NULL,
	[EP_IPC]  = &ipc_ops,
	[EP_TTY]  = &tty_ops,
	[EP_GPIO] = &gpio_ops,
	[EP_SPI] = &spi_ops,
	[EP_I2C] = &i2c_ops,
	[EP_SYSFS] = &sysfs_ops,
	[EP_INET]  = &ipc_ops,
	[EP_LOOP]  = &loop_ops,
};

static dtbl_t ep_tbl = DTBL_INITIALIZER(MAX_NR_EP);

int bind_endpoint(struct rteipc_ep *lh, struct rteipc_ep *rh,
		bufferevent_data_cb readcb, bufferevent_data_cb writecb,
		bufferevent_event_cb eventcb)
{
	struct bufferevent *pair[2];

	if (lh->bev || rh->bev) {
		fprintf(stderr, "endpoint is busy\n");
		return -1;
	}

	if (bufferevent_pair_new(__base, 0, pair)) {
		fprintf(stderr, "Failed to allocate bufferevent pair\n");
		return -1;
	}

	lh->bev = pair[0];
	rh->bev = pair[1];
	(to_core(lh))->partner_id = (to_core(rh))->id;
	(to_core(rh))->partner_id = (to_core(lh))->id;
	bufferevent_setcb(lh->bev, readcb, writecb, eventcb, lh);
	bufferevent_setcb(rh->bev, readcb, writecb, eventcb, rh);
	bufferevent_enable(lh->bev, EV_READ);
	bufferevent_enable(rh->bev, EV_READ);
	return 0;
}

struct rteipc_ep *get_partner_endpoint(struct rteipc_ep *ep)
{
	return find_endpoint((to_core(ep))->partner_id);
}

void unbind_endpoint(struct rteipc_ep *ep)
{
	struct ep_core *partner;
	struct bufferevent *pair;

	if (ep->bev && (pair = bufferevent_pair_get_partner(ep->bev))) {
		partner = dtbl_get(&ep_tbl, (to_core(ep))->partner_id);

		assert(partner);
		assert(partner->ep.bev == pair);

		bufferevent_free(ep->bev);
		bufferevent_free(partner->ep.bev);
		ep->bev = partner->ep.bev = NULL;
		(to_core(ep))->partner_id = partner->partner_id = -1;
	}
}

struct rteipc_ep *find_endpoint(int desc)
{
	struct ep_core *core = dtbl_get(&ep_tbl, desc);
	return core ? &core->ep : NULL;
}

struct rteipc_ep *allocate_endpoint(int type)
{
	struct ep_core *core;
	struct rteipc_ep *ep;

	core = malloc(sizeof(*core));
	if (!core) {
		fprintf(stderr, "Failed to allocate memory for core\n");
		return NULL;
	}

	core->id = core->partner_id = -1;
	ep = &core->ep;
	ep->base = __base;
	ep->type = type;
	ep->ops = ep_ops_list[type];
	ep->bev = NULL;
	ep->data = NULL;

	return ep;
}

void destroy_endpoint(struct rteipc_ep *ep)
{
	struct ep_core *core = to_core(ep);
	free(core);
}

int register_endpoint(struct rteipc_ep *ep)
{
	struct ep_core *core = to_core(ep);
	int desc;

	desc = dtbl_set(&ep_tbl, core);
	if (desc < 0) {
		fprintf(stderr, "Failed to register ep\n");
		return -1;
	}
	core->id = desc;

	return core->id;
}

void unregister_endpoint(struct rteipc_ep *ep)
{
	struct ep_core *core = to_core(ep);
	dtbl_del(&ep_tbl, core->id);
	core->id = -1;
	/* If endpoint is bound to another should unbind here */
	unbind_endpoint(ep);
}
