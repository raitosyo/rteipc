// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "rtemgr-common.h"
#include "message.h"
#include "list.h"


/**
 * Domain is a representation of a 'switch' of rteipc that contains
 * interfaces.
 */
struct domain {
	int id;
	char name[64];
	list_t iface_list;
	node_t node;
};

/**
 * Interface is a representation of a pair of 'port' and 'endpoint' of rteipc
 * that is bound together.
 */
struct interface;
typedef void (*iface_handler)(struct interface *self, void *data, size_t len);
struct interface {
	int id;
	int ep;
	char name[16];
	char uri[128];
	int bus_type;
	struct domain *domain;
	int managed;
	iface_handler handler;
	struct interface *partner;  /* always NULL for a managed iface */
	struct evbuffer *pending;
	node_t node;
};

static void iface_raw_handler(struct interface *self, void *data, size_t len);

static void iface_managed_handler(struct interface *self, void *data, size_t len);

/* Default domain */
struct domain default_domain = {
	.name = "default",
	.iface_list = LIST_INITIALIZER,
};

/* List of all domains */
static list_t domain_list = LIST_INITIALIZER;

/* @rtemgr control interface */
static struct interface *ctrl_iface;

static struct bus_prefix {
	int bus;
	char *prefix;
} bus_prefix_tbl[] = {
	{ EP_IPC,   PREFIX_IPC },
	{ EP_INET,  PREFIX_INET },
	{ EP_TTY,   PREFIX_TTY },
	{ EP_GPIO,  PREFIX_GPIO },
	{ EP_SPI,   PREFIX_SPI },
	{ EP_I2C,   PREFIX_I2C },
	{ EP_SYSFS, PREFIX_SYSFS },
};

/**
 * bus_type to URI prefix.
 * The length of @buf is expected to be greater than 128.
 */
static inline const char *bus_to_prefix(int bus)
{
	int i;
	for (i = 0; i < array_size(bus_prefix_tbl); i++) {
		if (bus_prefix_tbl[i].bus == bus)
			return bus_prefix_tbl[i].prefix;
	}
	return "";
}

static inline size_t hex_to_array(uint8_t **out, const char *str)
{
	char *hex, *s;
	uint8_t *arr;
	size_t count;
	int i;

	if (!strlen(str))
		return 0;

	s = strdup(str);
	if (!s)
		return 0;

	count = 1;
	/* Count up for each occurence of a space in the string */
	for (i = 0; i < strlen(str); i++) {
		if (str[i] == ' ')
			count++;
	}

	arr = calloc(count, sizeof(uint8_t));
	if (!arr) {
		free(s);
		return 0;
	}

	i = 0;
	hex = strtok(s, " ");
	do {
		arr[i++] = strtoul(hex, NULL, 16);
	} while (hex = strtok(NULL, " "));

	*out = arr;
	return count;
}

static struct interface *iface_lookup_by_name(struct domain *domain,
			const char *name)
{
	struct interface *iface;
	node_t *n;

	if (!domain)
		return NULL;

	list_each(&domain->iface_list, n, {
		iface = list_entry(n, struct interface, node);
		if (strmatch(iface->name, name))
			return iface;
	})
	return NULL;
}

static struct domain *domain_lookup_by_id(int domain_id)
{
	struct domain *domain;
	node_t *n;

	list_each(&domain_list, n, {
		domain = list_entry(n, struct domain, node);
		if (domain->id == domain_id)
			return domain;
	})
	return NULL;
}

static struct interface *iface_new(struct domain *domain)
{
	struct interface *iface;

	if ((iface = calloc(1, sizeof(*iface)))) {
		iface->domain = domain;
		iface->pending = evbuffer_new();
		if (!iface->pending) {
			free(iface);
			return NULL;
		}
		list_push(&domain->iface_list, &iface->node);
	}
	return iface;
}

static inline
void __do_forget(struct domain *domain, struct interface *forget)
{
	struct interface *iface;
	node_t *n;

	if (!domain || !forget)
		return;

	list_each(&domain->iface_list, n, {
		iface = list_entry(n, struct interface, node);
		if (iface != forget && iface->partner == forget)
			iface->partner = NULL;
	})
}

static void iface_forget(struct interface *iface)
{
	struct domain *domain;
	node_t *n;

	if (!iface)
		return;

	if (!iface->managed) {
		if (iface->partner) {
			iface->partner->partner = NULL;
			iface->partner = NULL;
		}
		return;
	}

	/* Find all the interfaces whose partner is iface, then forget it */
	list_each(&domain_list, n, {
		domain = list_entry(n, struct domain, node);
		__do_forget(domain, iface);
	})
}

static void iface_free(struct interface *iface)
{
	if (!iface)
		return;

	if (iface->id >= 0)
		rteipc_close(iface->id);

	if (iface->ep >= 0)
		rteipc_close(iface->ep);

	if (iface->domain)
		list_remove(&iface->domain->iface_list, &iface->node);

	iface_forget(iface);
	free(iface);
}

/**
 * Create a new iface that belongs to the domain.
 */
static inline
int iface_build(struct interface *iface, const char *name, const char *path,
			int bus_type, int managed)
{
	char uri[128];

	iface->bus_type = bus_type;
	iface->managed = managed;
	strcpy(iface->name, name);
	snprintf(uri, sizeof(uri), "%s%s", bus_to_prefix(bus_type), path);
	strcpy(iface->uri, uri);
	iface->id = rteipc_port(iface->domain->id, iface->name);
	iface->ep = rteipc_open(iface->uri);
	iface->handler = (iface->managed) ?
		iface_managed_handler : iface_raw_handler;

	if (iface->id < 0 || iface->ep < 0)
		return -1;

	if (rteipc_bind(iface->id, iface->ep) < 0)
		return -1;

	return 0;
}

/**
 * Preserve data in the pending queue.
 * Data in the pending queue will be sent out as soon as iface gets a partner.
 */
static int iface_pending(struct interface *iface, void *data, size_t len)
{
	struct evbuffer *buf;
	size_t sz;
	size_t nl;

	if (!(buf = evbuffer_new())) {
		fprintf(stderr,
			"Discarded pending data due to memory allocation error\n");
		return -1;
	}
	evbuffer_add(buf, data, len);
	sz = evbuffer_get_length(buf);
	nl = htonl(sz);
	evbuffer_prepend(buf, &nl, 4);
	evbuffer_add_buffer(iface->pending, buf);
	evbuffer_free(buf);
	return 0;
}

/**
 * Push out all availabe data in the queue to the partner.
 */
static void iface_trigger(struct interface *iface)
{
	char *msg;
	size_t len;
	int err;

	if (!iface->partner)
		return;

	for (;;) {
		if (!(err = rteipc_msg_drain(iface->pending, &len, &msg)))
			return;

		if (err < 0) {
			fprintf(stderr, "Cannot push out data in queue\n");
			return;
		}
		iface->handler(iface, msg, len);
		free(msg);
	}
}

/**
 * Callback function to handle data from a backend device on a raw
 * (i.e. non-managed) interface.
 */
static void iface_raw_handler(struct interface *self, void *data, size_t len)
{
	struct interface *partner = self->partner;
	rtemgr_data *d = NULL;
	rtemgr_intf *intf = NULL;
	void *val = NULL;
	unsigned char *buf = NULL;
	size_t bufsz, written;

	if (!partner) {
		/* If iface has no route yet, preserve data */
		iface_pending(self, data, len);
		return;
	}

	if (!partner->managed) {
		/* raw to raw interface */
		rteipc_xfer(partner->domain->id, partner->name, data, len);
	} else {
		/* raw to managed interface */
		bufsz = len * 3 / 2;
		if (bufsz < 512)
			bufsz = 512;
		if ((val = malloc(len)) &&
				(buf = malloc(bufsz)) &&
				(d = rtemgr_data_alloc()) &&
				(intf = rtemgr_data_alloc_interface(d))) {
			d->cmd.action = RTECMD_XFER;
			/*
			 * Do not assign the pointer to data to cmd.val.v
			 * directly, which causes the double free error as
			 * rtemgr_data_free and rteipc core does it.
			 */
			memcpy(val, data, len);
			d->cmd.val.v = val;
			d->cmd.val.s = len;
			/*
			 * Setup sender info.
			 */
			intf->id = self->id;
			intf->bus_type = self->bus_type;
			strcpy(intf->name, self->name);
			strcpy(intf->path, self->uri);
			intf->domain = self->domain->id;
			intf->managed = self->managed;
			strcpy(intf->partner, self->partner->name);
			if (!rtemgr_data_emit(d, buf, bufsz, &written))
				rteipc_xfer(partner->domain->id,
						partner->name, buf, written);
		}
		free(buf);
		rtemgr_data_free(d);  /* This also free val */
	}
}

/**
 * Callback function to handle data from a backend device (i.e. IPC socket) on
 * a managed interface.
 */
static void iface_managed_handler(struct interface *self, void *data,
			size_t len)
{
	rtemgr_data *d;
	rtemgr_intf *intf;
	struct interface *dest;
	int id;
	const char *name;
	void *value;
	size_t size;
	uint8_t *byte_array, *new_array;
	unsigned char *buf;
	size_t bufsz, written;

	if (!(d = rtemgr_data_parse(data, len)))
		return;

	if (d->nr_intf != 1 || !d->interfaces)
		goto out;

	dest = iface_lookup_by_name(
			domain_lookup_by_id(d->interfaces->domain),
			d->interfaces->name);
	if (!dest)
		goto out;

	id = dest->domain->id;
	name = dest->name;
	value = d->cmd.val.v;
	size = d->cmd.val.s;
	if (!value || !size)
		goto out;

	switch (dest->bus_type) {
	case EP_IPC:
	case EP_INET:
	case EP_TTY:
	case EP_SYSFS:
		if (!dest->managed) {
			rteipc_xfer(id, name, value, size);
		} else {
			/* managed to managed interface */
			bufsz = len + 512;
			if ((buf = malloc(bufsz))) {
				/*
				 * Setup sender info.
				 */
				intf = d->interfaces;
				intf->id = self->id;
				intf->bus_type = self->bus_type;
				strcpy(intf->name, self->name);
				strcpy(intf->path, self->uri);
				intf->domain = self->domain->id;
				intf->managed = self->managed;
				if (self->partner)
					strcpy(intf->partner, self->partner->name);
				if (!rtemgr_data_emit(d, buf, bufsz, &written))
					rteipc_xfer(id, name, buf, written);
				free(buf);
			}
		}
		break;
	case EP_GPIO:
		rteipc_gpio_xfer(id, name,
				strmatch(value, "0") ? 0 : 1);
		break;
	case EP_SPI:
	case EP_I2C:
		/* Hex strings to byte array */
		size = hex_to_array(&byte_array, value);
		if (size) {
			if (dest->bus_type == EP_SPI) {
				if (size < d->cmd.val.extra.rsize) {
					new_array = realloc(byte_array,
							d->cmd.val.extra.rsize);
					if (!new_array) {
						fprintf(stderr,
							"Failed to expand buf.\n");
						free(byte_array);
						goto out;
					}
					memset(new_array + size, 0,
						d->cmd.val.extra.rsize - size);
					byte_array = new_array;
					size = d->cmd.val.extra.rsize;
				}
				rteipc_spi_xfer(id, name, byte_array, size,
						!!d->cmd.val.extra.rsize);
			} else {
				rteipc_i2c_xfer(id, name,
						d->cmd.val.extra.addr,
						byte_array, size,
						d->cmd.val.extra.rsize);
			}
			free(byte_array);
		}
		break;
	}
out:
	rtemgr_data_free(d);
}

static void domain_collect_ifaces(rtemgr_data *d, struct domain *domain)
{
	struct interface *iface;
	rtemgr_intf *intf;
	char path[128] = {0};
	node_t *n;

	list_each(&domain->iface_list, n, {
		iface = list_entry(n, struct interface, node);
		intf = rtemgr_data_alloc_interface(d);
		intf->id = iface->id;
		strcpy(intf->name, iface->name);
		intf->bus_type = iface->bus_type;
		sscanf(iface->uri, "%*[^:]://%99[^\n]", path);
		strcpy(intf->path, path);
		intf->domain = domain->id;
		intf->managed = iface->managed;
		if (iface->partner)
			strcpy(intf->partner, iface->partner->name);
	})
}

static int do_rtecmd_list(rtemgr_data *d)
{
	struct domain *domain;
	node_t *n;

	/* Ensure no interface data exist */
	rtemgr_data_cleanup_interfaces(d);

	/* Add data for interfaces iterating over all domains */
	list_each(&domain_list, n, {
		domain = list_entry(n, struct domain, node);
		domain_collect_ifaces(d, domain);
	})
	return 0;
}

static int do_rtecmd_open(rtemgr_data *d)
{
	rtemgr_intf *intf;
	struct interface *iface;
	struct domain *domain;

	if (!(intf = d->interfaces) || d->nr_intf < 1)
		return -1;

	if (d->nr_intf > 1)
		fprintf(stderr, "Warn: specified more than 2 interfaces."
				"First one is only taken.\n");

	domain = domain_lookup_by_id(intf->domain);
	if (!domain)
		return -1;

	/* Create new iface which belongs to the domain */
	iface = iface_new(domain);
	if (!iface)
		return -1;

	/* Build iface */
	if (iface_build(iface, intf->name, intf->path,
				intf->bus_type, intf->managed)) {
		iface_free(iface);
		return -1;
	}
	return 0;
}

static int do_rtecmd_close(rtemgr_data *d)
{
	rtemgr_intf *intf;
	struct interface *iface;
	struct domain *domain;
	int i;

	if (!(intf = d->interfaces) || d->nr_intf < 1)
		return -1;

	for (i = 0; i < d->nr_intf; i++) {
		iface = iface_lookup_by_name(
				domain_lookup_by_id(intf[i].domain),
				intf[i].name);
		iface_free(iface);
	}
	return 0;
}

static int do_rtecmd_route(rtemgr_data *d)
{
	rtemgr_intf *intf_lh, *intf_rh;
	struct interface *iface_lh, *iface_rh;

	if (!d->interfaces || d->nr_intf != 2)
		return -1;

	intf_lh = &d->interfaces[0];
	intf_rh = &d->interfaces[1];

	iface_lh = iface_lookup_by_name(domain_lookup_by_id(intf_lh->domain),
			intf_lh->name);

	iface_rh = iface_lookup_by_name(domain_lookup_by_id(intf_rh->domain),
			intf_rh->name);

	if (!iface_lh || !iface_rh || iface_lh == iface_rh)
		return -1;

	/*
	 * A raw (non-managed) iface must be bound to a specific one in order
	 * to route a packet whereas a managed iface does not need to bind to
	 * any other as it can have a destination in a packet.
	 */
	if (!iface_lh->managed) {
		iface_lh->partner = iface_rh;
		iface_trigger(iface_lh);
	}
	if (!iface_rh->managed) {
		iface_rh->partner = iface_lh;
		iface_trigger(iface_rh);
	}
	return 0;
}

static int do_rtecmd_forget(rtemgr_data *d)
{
	struct interface *iface = iface_lookup_by_name(
			domain_lookup_by_id(d->interfaces->domain),
			d->interfaces->name);

	if (!iface) {
		fprintf(stderr, "No such interface '%s' is found.\n",
				d->interfaces->name);
		return -1;
	}

	iface_forget(iface);
	return 0;
}

static int do_rtecmd_cat(rtemgr_data *d)
{
	struct interface *iface = iface_lookup_by_name(
			domain_lookup_by_id(d->interfaces->domain),
			d->interfaces->name);
	struct evbuffer *buf;
	char *msg;
	size_t len;
	uint8_t *byte_array;
	struct tm *tm;
	uint64_t tv_sec, tv_nsec;
	uint8_t *p_arg, value;
	char dstr[64];
	int err, i;

	if (!iface) {
		fprintf(stderr, "No such interface '%s' is found.\n",
				d->interfaces->name);
		return -1;
	}

	if (iface->partner) {
		fprintf(stderr, "%s has partner %s, unable to intercept.\n",
				iface->name, iface->partner);
		return -1;
	}

	if (!(buf = evbuffer_new())) {
		fprintf(stderr,
			"Memory allocation error for reading data in queue.\n");
		return -1;
	}

	for (;;) {
		if (!(err = rteipc_msg_drain(iface->pending, &len, &msg)))
			break;  /* no more data */

		if (err < 0) {
			fprintf(stderr, "Error reading data in queue.\n");
			goto err;
		}

		if (iface->bus_type == EP_I2C || iface->bus_type == EP_SPI) {
			evbuffer_add_printf(buf, "%s", "[");
			byte_array = (uint8_t *)msg;
			for (i = 0; i < len; i++) {
				evbuffer_add_printf(
					buf, " 0x%02x", byte_array[i]);
			}
			evbuffer_add_printf(buf, "%s", " ]\n");
		} else if (iface->bus_type == EP_GPIO) {
			p_arg = (uint8_t *)msg;
			value = *p_arg++;
			tv_sec = *((uint64_t *)p_arg);
			p_arg += sizeof(uint64_t);
			tv_nsec = *((uint64_t *)p_arg);
			tm = localtime(&tv_sec);
			strftime(dstr, sizeof(dstr), "%Y-%m-%d %H:%M:%S", tm);
			evbuffer_add_printf(buf, "[%s.%06lld] %s ==> %s\n",
					dstr, tv_nsec,
					!value ? "Hi" : "Lo",
					value ? "Hi" : "Lo");
		} else {
			evbuffer_add_printf(buf, "%.*s\n", len, msg);
		}
		free(msg);
	}
	d->cmd.val.s = evbuffer_get_length(buf);
	if (d->cmd.val.s) {
		d->cmd.val.v = malloc(d->cmd.val.s);
		if (!d->cmd.val.v) {
			fprintf(stderr, "unable to allocate memory to return data.\n");
			goto err;
		}
		evbuffer_remove(buf, d->cmd.val.v, d->cmd.val.s);
	}
	evbuffer_free(buf);
	return 0;
err:
	evbuffer_free(buf);
	return -1;
}

static void process_ctlport(void *data, size_t len)
{
	rtemgr_data *d;
	unsigned char buf[4096] = {0};
	size_t written;
	int (*action)(rtemgr_data *) = NULL;

	if (!(d = rtemgr_data_parse(data, len)))
		return;

	d->cmd.error = -1;
	switch (d->cmd.action) {
	case RTECMD_LIST:
		action = do_rtecmd_list;
		break;
	case RTECMD_OPEN:
		action = do_rtecmd_open;
		break;
	case RTECMD_CLOSE:
		action = do_rtecmd_close;
		break;
	case RTECMD_ROUTE:
		action = do_rtecmd_route;
		break;
	case RTECMD_FORGET:
		action = do_rtecmd_forget;
		break;
	case RTECMD_XFER:
		/* Let managed iface callback handle it */
		iface_managed_handler(ctrl_iface, data, len);
		//FIXME
		d->cmd.error = 0;
		break;
	case RTECMD_CAT:
		action = do_rtecmd_cat;
		break;
	}

	if (action)
		d->cmd.error = action(d);

	/* Emit reply data into buffer */
	if (rtemgr_data_emit(d, buf, sizeof(buf), &written)) {
		fprintf(stderr, "Failed to emit data\n");
		goto out;
	}

	/* Send reply to client */
	rteipc_xfer(default_domain.id, RTEMGRD_CTLPORT, buf, written);
out:
	rtemgr_data_free(d);
}

/**
 * Callback to handle data from a backend device behind an interface.
 * An interface-level callback is subsequently invoked except for the control
 * interface that is special.
 */
static void default_domain_handler(int domain_id, const char *name,
					void *data, size_t len)
{
	struct interface *iface;

	if (default_domain.id == domain_id &&
			strmatch(name, RTEMGRD_CTLPORT)) {
		/* For the control interface */
		process_ctlport(data, len);
	} else {
		/* Invoke an interface-level callback */
		iface = iface_lookup_by_name(
				domain_lookup_by_id(domain_id), name);
		if (iface && iface->handler)
			iface->handler(iface, data, len);
	}
}

static void rtemgrd(void)
{
	/* Initialize rteipc */
	rteipc_init(NULL);

	/*
	 * Create default_domain.
	 */
	default_domain.id = rteipc_sw();

	/* default_domain is first created switch, hence the id is always 0 */
	assert(default_domain.id == 0);

	rteipc_sw_setcb(default_domain.id, default_domain_handler);
	list_push(&domain_list, &default_domain.node);

	/**
	 * Create and setup '@rtemgrd' control iface to which client
	 * connects.
	 */
	ctrl_iface = iface_new(&default_domain);
	if (!ctrl_iface)
		goto error;

	/**
	 * Build control iface as a managed interface.
	 *
	 * NOTE:
	 *   Not like other managed ifaces the control iface is handled by
	 *   domain handler (i.e. default_domain_handler) not by iface
	 *   handler.
	 */
	if (iface_build(ctrl_iface, RTEMGRD_CTLPORT, RTEMGRD_CTLPORT,
				EP_IPC, 1))
		goto error;

	/**
	 * Run event loop
	 */
	rteipc_dispatch(NULL);
	rteipc_shutdown();
error:
	fprintf(stderr, "Cannot setup ctl-iface.\n");
	exit(EXIT_FAILURE);
}

static void usage(void)
{
	fprintf(stderr, "usage: rtemgrd [-B]\n"
			"options:\n"
			"   -B   run daemon in the background\n");
}

int main(int argc, char **argv)
{
	int daemonize = 0;
	int c;

	while (1) {
		c = getopt(argc, argv, "B");
		if (c < 0)
			break;

		switch (c) {
		case 'B':
			daemonize++;
			break;
		default:
			usage();
			exit(1);
		}
	}

	if (daemonize && daemon(0, 0) < 0) {
		fprintf(stderr, "Failed to run as a daemon\n");
		exit(1);
	}

	rtemgrd();
	return 0;
}
