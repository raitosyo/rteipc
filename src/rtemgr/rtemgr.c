// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include "rtemgr-common.h"
#include "list.h"

#define _m(e)		(1 << (e))

#define check_val_return_error(k, v, required)                           \
	do {                                                             \
		if ((v != NULL) != required) {                           \
			fprintf(stderr,                                  \
				(!v) ?                                   \
				"'%s' requires an argment.\n" :          \
				"'%s' does not take an argument.\n", k); \
			return -1;                                       \
		}                                                        \
	} while (0)

#define check_bus_return_error(i, k, msk)                                    \
	do {                                                                 \
		if (!(_m((i)->bus_type) & msk)) {                            \
			fprintf(stderr,                                      \
				"'%s' is not a valid option for %s bus.\n",  \
				k, bus_to_str((i)->bus_type));               \
				return -1;                                   \
		}                                                            \
	} while (0)

/* Parser functions for each subcommand options */
static int open_parser(rtemgr_data *d, list_t *args, list_t *olist);
static int close_parser(rtemgr_data *d, list_t *args, list_t *olist);
static int route_parser(rtemgr_data *d, list_t *args, list_t *olist);
static int forget_parser(rtemgr_data *d, list_t *args, list_t *olist);
static int xfer_parser(rtemgr_data *d, list_t *args, list_t *olist);
static int cat_parser(rtemgr_data *d, list_t *args, list_t *olist);

struct argument {
	node_t node;
	char *key;
	char *val;
};

static struct option default_options[] = {
	{"help",      no_argument,            0, 'h'},
	{0,           0,                      0,  0 }
};

/* 'open' subcommand */
static struct option open_options[] = {
	{"name",      required_argument,      0, 'n'},
	{"options",   required_argument,      0, 'o'},
	{"bus",       required_argument,      0, 't'},
	{0,           0,                      0,  0 }
};

/* 'xfer' subcommand */
static struct option xfer_options[] = {
	{"address",   required_argument,      0, 'a'},
	{"attr",      required_argument,      0, 'k'},
	{"options",   required_argument,      0, 'o'},
	{"read",      no_argument,            0, 'r'},
	{"bus",       required_argument,      0, 't'},
	{"value",     required_argument,      0, 'w'},
	{0,           0,                      0,  0 }
};

/* For subcommand which takes no option */
static struct option none_options[] = {
	{0,         0,                        0,  0 }
};

static struct rtemgr_action {
	char *name;
	int action;
	char *optfmt;
	struct option *options;
	int (*parser)(rtemgr_data *, list_t *, list_t *);
} rtemgr_actions[] = {
	{"",        0,              "h",          default_options,  NULL         },
	{"list",    RTECMD_LIST,    "",           none_options   ,  NULL         },
	{"open",    RTECMD_OPEN,    "n:t:o:",     open_options   ,  open_parser  },
	{"close",   RTECMD_CLOSE,   "",           none_options   ,  close_parser },
	{"route",   RTECMD_ROUTE,   "",           none_options   ,  route_parser },
	{"forget",  RTECMD_FORGET,  "",           none_options   ,  forget_parser},
	{"xfer",    RTECMD_XFER,    "a:o:rk:w:",  xfer_options   ,  xfer_parser  },
	{"cat",     RTECMD_CAT,     "",           none_options   ,  cat_parser   },
	/* The last element of the array */
	{0,         RTECMD_MAX,     0,            0              ,  0            }
};

static struct {
	int bus_type;
	char *name;
} bus_name_tbl[] = {
	{EP_IPC,      "ipc"},
	{EP_INET,     "inet"},
	{EP_TTY,      "tty"},
	{EP_GPIO,     "gpio"},
	{EP_SPI,      "spi"},
	{EP_I2C,      "i2c"},
	{EP_SYSFS,    "sysfs"},
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s COMMAND [ARGUMENTS...] [OPTIONS]\n"
		"\n"
		"COMMAND\n"
		" list      list all endpoints available on the system\n"
		" open      create a new endpoint\n"
		" close     remove an existing endpoint\n"
		" route     route data between two endpoints\n"
		" forget    delete an existing route between two endpoints\n"
		" xfer      write data into an endpoint\n"
		" cat       read data from an endpoint\n"
		"\n"
		"list\n"
		"    Print endpoints available on the system.\n"
		"\n"
		"open PATH -n|--name NAME -t|--bus BUS_TYPE\n"
		"      [ -o|--options OPEN_OPTIONS ]\n"
		"    Open a a new endpoint.\n"
		"\n"
		"    BUS_TYPE\n"
		"     { ipc | inet | tty | gpio | spi | i2c | sysfs }\n"
		"\n"
		"    OPEN_OPTIONS\n"
		"     ipc (UNIX domain socket)\n"
		"      abs|abstruct    Create socket in abstruct namespace\n"
		"      file            Create socket in filesystem(default)\n"
		"      managed         Create ipc as a managed endpoint\n"
		"\n"
		"     inet (Internet socket)\n"
		"      managed         Create inet as a managed endpoint\n"
		"\n"
		"     tty\n"
		"      speed=value     TTY baud rate (default 115200)\n"
		"\n"
		"     gpio\n"
		"      line=value      GPIO line offset (default 0)\n"
		"      out or in       Set GPIO direction (default in)\n"
		"      hi or lo        Set The initial value (default lo)\n"
		"\n"
		"     spi\n"
		"      mode={0|1|2|3}  SPI mode (default 3)\n"
		"      speed=value     SPI speed (default 5000)\n"
		"\n"
		"close endpoint...\n"
		"    Close endpoint specified by the name.\n"
		"\n"
		"route endpoint endpoint\n"
		"    Route data between two endpoints.\n"
		"\n"
		"forget endpoint\n"
		"    Remove all route related to the endpoint.\n"
		"\n"
		"xfer endpoint -w|--value DATA\n"
		"      [ -a|--addr I2C_ADDRESS ]\n"
		"      [ -k|--attr SYSFS_ATTR ]\n"
		"      [ -r|--read ]\n"
		"      [ -o|--options XFER_OPTIONS ]\n"
		"\n"
		"    XFER_OPTIONS\n"
		"     i2c or spi\n"
		"      rsize=value    Set read buffer size in bytes (default 1)\n"
		"\n"
		"cat endpoint\n"
		"    Read data from the endpoint not routed to other.\n"
		"    Cannot read from endpoints that is routed to any endpoint.\n"
		"\n",
		prog);
}

static inline const char *bus_to_str(int bus)
{
	int i;
	for (i = 0; i < array_size(bus_name_tbl); i++) {
		if (bus_name_tbl[i].bus_type == bus)
			return bus_name_tbl[i].name;
	}
	return "UNKNOWN";
}

static inline int str_to_bus(const char *str)
{
	int i;
	for (i = 0; i < array_size(bus_name_tbl); i++) {
		if (strmatch(bus_name_tbl[i].name, str))
			return bus_name_tbl[i].bus_type;
	}
	return -1;
}

static inline void remove_arg(struct argument *arg)
{
	if (!arg)
		return;
	free(arg->key);
	free(arg->val);
	free(arg);
}

static inline void remove_arg_list(list_t *l)
{
	while (!list_empty(l))
		remove_arg(list_entry(list_pop(l), struct argument, node));
}

static inline struct argument *make_arg(const char *key, const char *val)
{
	struct argument *arg;

	arg = malloc(sizeof(*arg));
	if (arg) {
		if (key && strlen(key)) {
			arg->key = malloc(strlen(key));
			if (!arg->key)
				goto fail;
			strcpy(arg->key, key);
		}
		if (val && strlen(val)) {
			arg->val = malloc(strlen(val));
			if (!arg->val)
				goto fail;
			strcpy(arg->val, val);
		}
	}
	return arg;
fail:
	remove_arg(arg);
	return NULL;
}

static inline int parse_opts(char *opts, list_t *l)
{
	struct argument *o, *tmp;
	char *next, *key, *val;
	node_t *n;

	while (opts && strlen(opts)) {
		key = opts;
		next = strchr(opts, ',');
		if (next) {
			*next = '\0';
			next++;
		}

		val = strchr(key, '=');
		if (val) {
			*val = '\0';
			val++;
		}

		o = make_arg(key, val);
		if (!o)
			goto fail;

		list_each(l, n, {
			tmp = list_entry(n, struct argument, node);
			if (strmatch(tmp->key, o->key)) {
				remove_arg(o);
				goto fail;
			}
		})
		list_push(l, &o->node);
		opts = next;
	}
	return 0;
fail:
	remove_arg_list(l);
	return -1;
}

static int open_parser(rtemgr_data *d, list_t *args, list_t *olist)
{
	rtemgr_intf *intf = d->interfaces;
	struct argument *arg;
	char port[6] = {0};
	node_t *n;
	int num = 0;
	char buf[128];
	int abs = 0;
	uint8_t line = 0;
	char dir[4] = "in";
	char outval[2] = "0";
	int speed = 0;
	int mode = 3;

	if (!strlen(intf->name)) {
		fprintf(stderr, "'--name' must be specified.\n");
		return -1;
	}

	if (intf->bus_type < 0) {
		fprintf(stderr, "'--bus' must be specified.\n");
		return -1;
	}

	/* open subcommand takes only one argument <path> */
	list_each(args, n, {
		arg = list_entry(n, struct argument, node);
		if (num >= 1) {
			fprintf(stderr, "Unknown argument '%s'.\n",
					arg->val);
			return -1;
		}
		strncpy(intf->path, arg->val, sizeof(intf->path));
		num++;
	})

	if (num == 0)
		return -1;

	list_each(olist, n, {
		arg = list_entry(n, struct argument, node);
		/* 'speed' */
		if (strmatch(arg->key, "speed")) {
			check_bus_return_error(intf, arg->key,
					_m(EP_TTY)|_m(EP_SPI));
			check_val_return_error(arg->key, arg->val, true);
			speed = strtoul(arg->val, NULL, 0);
		/* 'file' or 'abstract' */
		} else if (strmatch(arg->key, "file") ||
			   strmatch(arg->key, "abstract") ||
			   strmatch(arg->key, "abs")) {
			check_bus_return_error(intf, arg->key, _m(EP_IPC));
			if (!strmatch(arg->key, "file"))
				abs = 1;
		/* 'managed' */
		} else if (strmatch(arg->key, "managed")) {
			check_bus_return_error(intf, arg->key,
					_m(EP_IPC)|_m(EP_INET));
			intf->managed = 1;
		/* 'line' */
		} else if (strmatch(arg->key, "line")) {
			check_bus_return_error(intf, arg->key, _m(EP_GPIO));
			line = strtoul(arg->val, NULL, 0);
		/* 'out' or 'in' */
		} else if (strmatch(arg->key, "out") ||
				strmatch(arg->key, "in")) {
			check_bus_return_error(intf, arg->key, _m(EP_GPIO));
			strncpy(dir, arg->key, sizeof(dir));
		/* 'hi' or 'lo' */
		} else if (strmatch(arg->key, "hi") ||
				strmatch(arg->key, "lo")) {
			check_bus_return_error(intf, arg->key, _m(EP_GPIO));
			sprintf(outval, "%d",
				strmatch(arg->key, "hi") ? 1 : 0);
		/* 'mode' */
		} else if (strmatch(arg->key, "mode")) {
			check_bus_return_error(intf, arg->key, _m(EP_SPI));
			mode = strtoul(arg->val, NULL, 0);
		} else {
			fprintf(stderr,
				"Invalid option '%s%s%s' specified.\n",
				arg->key,
				arg->val ? "=" : "",
				arg->val ?: "");
			return -1;
		}
	})

	/* Encode parameters into uri depending on the bus type */
	if (intf->bus_type == EP_IPC) {
		if (abs) {
			memmove(intf->path + 1, intf->path,
					strlen(intf->path) + 1);
			*intf->path = '@';
		}
	} else if (intf->bus_type == EP_INET) {
		/*
		 * If a port number is omitted from the path for INET, the
		 * default port 9110 is used. But explicitly add the port
		 * number to the path here so that later 'list' outputs
		 * include port information.
		 */
		sscanf(intf->path, "%*[^:]:%[^:]", port);
		if (!strlen(port))
			strcat(intf->path, ":9110");
	} else if (intf->bus_type == EP_TTY) {
		snprintf(buf, sizeof(buf), "%s,%d", intf->path,
				speed ?: 115200);
		snprintf(intf->path, sizeof(intf->path), "%s", buf);
	} else if (intf->bus_type == EP_GPIO) {
		snprintf(buf, sizeof(buf), "%s@%s-%d,%s%s%s", intf->name,
				intf->path, line, dir,
				strmatch(dir, "out") ? "," : "",
				strmatch(dir, "out") ? outval : "");
		snprintf(intf->path, sizeof(intf->path), "%s", buf);
	} else if (intf->bus_type == EP_SPI) {
		snprintf(buf, sizeof(buf), "%s,%d,%d", intf->path,
				speed ?: 5000, mode);
		snprintf(intf->path, sizeof(intf->path), "%s", buf);
	}
	return 0;
}

static int close_parser(rtemgr_data *d, list_t *args, list_t *olist)
{
	struct argument *arg;
	node_t *n;
	int num = 0;

	list_each(args, n, {
		if (num >= d->nr_intf) {
			if (!(rtemgr_data_alloc_interface(d))) {
				fprintf(stderr, "Adding new interface failed.\n");
				return -ENOMEM;
			}
		}
		arg = list_entry(n, struct argument, node);
		strncpy(d->interfaces[num].name, arg->val,
				sizeof(((rtemgr_intf *)0)->name));
		num++;
	})

	if (num == 0)
		return -1;

	return 0;
}

static int route_parser(rtemgr_data *d, list_t *args, list_t *olist)
{
	rtemgr_intf *intf = d->interfaces;
	struct argument *arg;
	node_t *n;
	int num = 0;

	list_each(args, n, {
		arg = list_entry(n, struct argument, node);
		/* route subcommand takes only two interface as arguments. */
		if (num >= 2) {
			fprintf(stderr, "Unknown argument '%s'.\n", arg->val);
			return -1;
		}

		if (num >= d->nr_intf) {
			if (!(rtemgr_data_alloc_interface(d))) {
				fprintf(stderr, "Adding new interface failed.\n");
				return -ENOMEM;
			}
		}

		strncpy(d->interfaces[num].name, arg->val,
				sizeof(((rtemgr_intf *)0)->name));
		num++;
	})

	if (num != 2)
		return -1;

	return 0;
}

static int forget_parser(rtemgr_data *d, list_t *args, list_t *olist)
{
	rtemgr_intf *intf = d->interfaces;
	struct argument *arg;
	node_t *n;
	int num = 0;

	list_each(args, n, {
		arg = list_entry(n, struct argument, node);
		/* forget subcommand takes only one argument 'name' */
		if (num >= 1) {
			fprintf(stderr, "Unknown argument '%s'.\n", arg->val);
			return -1;
		}
		strncpy(intf->name, arg->val, sizeof(intf->name));
		num++;
	})

	if (num != 1)
		return -1;

	return 0;
}

static int xfer_parser(rtemgr_data *d, list_t *args, list_t *olist)
{
	rtemgr_intf *intf = d->interfaces;
	struct argument *arg;
	node_t *n;
	int num = 0;

	list_each(args, n, {
		arg = list_entry(n, struct argument, node);
		/* xfer subcommand takes only one argument 'name' */
		if (num >= 1) {
			fprintf(stderr, "Unknown argument '%s'.\n", arg->val);
			return -1;
		}
		strncpy(intf->name, arg->val, sizeof(intf->name));
		num++;
	})

	if (num != 1)
		return -1;

	list_each(olist, n, {
		arg = list_entry(n, struct argument, node);
		/* 'rsize' */
		if (strmatch(arg->key, "rsize")) {
			check_val_return_error(arg->key, arg->val, true);
			d->cmd.val.extra.rsize = strtoul(arg->val, NULL, 0);
		} else {
			fprintf(stderr,
				"Invalid option '%s%s%s' specified.\n",
				arg->key,
				arg->val ? "=" : "",
				arg->val ?: "");
			return -1;
		}
	})

	if (!d->cmd.val.v || (intf->bus_type == EP_I2C &&
				!d->cmd.val.extra.addr)) {
		fprintf(stderr, "Missing a required parameter.\n");
		return -1;
	}
	return 0;
}

static int cat_parser(rtemgr_data *d, list_t *args, list_t *olist)
{
	rtemgr_intf *intf = d->interfaces;
	struct argument *arg;
	node_t *n;
	int num = 0;

	list_each(args, n, {
		arg = list_entry(n, struct argument, node);
		/* cat subcommand takes only one argument 'name' */
		if (num >= 1) {
			fprintf(stderr, "Unknown argument '%s'.\n", arg->val);
			return -1;
		}
		strncpy(intf->name, arg->val, sizeof(intf->name));
		num++;
	})

	if (num != 1)
		return -1;

	return 0;
}

static inline struct rtemgr_action *get_rtemgr_action(const char *cmd)
{
	int i = 0;
	char *s;

	while (s = rtemgr_actions[i].name) {
		if (strmatch(s, cmd))
			return &rtemgr_actions[i];
		i++;
	}
	return &rtemgr_actions[i];
}

static inline rtemgr_data *subcmd_opt_check(int argc, char *argv[],
				struct rtemgr_action* entry)
{
	int c, index = 0;
	list_t subargs = LIST_INITIALIZER;
	list_t olist = LIST_INITIALIZER;
	char subopts[256] = {0};
	struct argument *subarg;
	rtemgr_data *d;
	rtemgr_intf *intf;
	char *attr = NULL, *value = NULL;
	char buf[256];

	if (!(d = rtemgr_data_alloc()))
		goto exit_print;

	d->cmd.action = entry->action;

	if (!(intf = rtemgr_data_alloc_interface(d))) {
		errno = ENOMEM;
		goto exit_print;
	}

	optind = 0;
	while ((c = getopt_long(argc, argv, entry->optfmt,
				entry->options, &index)) != -1) {
		switch (c) {
		case '?':
			usage(argv[0]);
			errno = EINVAL;
			goto exit;
		case 'n':  /* --name */
			strncpy(intf->name, optarg, sizeof(intf->name));
			break;
		case 't':  /* --bus */
			intf->bus_type = str_to_bus(optarg);
			break;
		case 'o':  /* --options */
			strncpy(subopts, optarg, sizeof(subopts));
			break;
		case 'a':  /* --address */
			d->cmd.val.extra.addr = strtoul(optarg, NULL, 16);
			break;
		case 'r':  /* --read */
			d->cmd.val.extra.rsize = 1; /* '-o rsize' can overwrite it */
			break;
		case 'k':  /* --attr */
			attr = strdup(optarg);
			break;
		case 'w':  /* --value */
			value = strdup(optarg);
			break;
		};
	}

	if (attr && value) {
		snprintf(buf, sizeof(buf), "%s=%s", attr, value);
		d->cmd.val.v = strdup(buf);
	} else if (attr) {
		d->cmd.val.v = attr;
	} else if (value) {
		d->cmd.val.v = value;
	}

	if (d->cmd.val.v)
		d->cmd.val.s = strlen(d->cmd.val.v);

	optind++;  /* discard subcommand */
	while (optind < argc) {
		subarg = make_arg(NULL, argv[optind++]);
		if (!subarg) {
			errno = ENOMEM;
			goto exit_print;
		}
		list_push(&subargs, &subarg->node);
	}

	if (parse_opts(subopts, &olist)) {
		fprintf(stderr, "Failed to parse '-o' options.\n");
		goto exit;
	}

	if (entry->parser && entry->parser(d, &subargs, &olist)) {
		fprintf(stderr, "%s %s: format error.\n",
				argv[0], entry->name);
		usage(argv[0]);
		errno = EINVAL;
		goto exit;
	}

	remove_arg_list(&olist);
	remove_arg_list(&subargs);
	return d;

exit_print:
	fprintf(stderr, "%s\n", strerror(errno));
exit:
	exit(EXIT_FAILURE);
}

static void reply_callback(int ctx, void *data, size_t len, void *arg)
{
	struct event_base *base = arg;
	rtemgr_data *d;
	rtemgr_intf *intf;
	const char *fmt = "%-16.16s %-6.6s %-36.36s %-16.16s\n";
	int i;

	if (!(d = rtemgr_data_parse(data, len)))
		return;

	if (!(intf = d->interfaces))
		goto out;

	switch (d->cmd.action) {
	case RTECMD_LIST:
		printf("%-16s %-6s %-36s %-16s\n",
			"NAME", "BUS", "PATH", "ROUTE");
		for (i = 0; i < d->nr_intf; i++) {
			if (intf[i].id == 0)
				continue;  /* Do not show @rtemgrd */
			printf(fmt, intf[i].name, bus_to_str(intf[i].bus_type),
					intf[i].path, intf[i].managed ?
					"**" : strlen(intf[i].partner) ?
					intf[i].partner : "--");
		}
		break;
	case RTECMD_OPEN:
		printf("%s '%s'.\n", !d->cmd.error ?
			"Successfully created" : "Failed to create",
			intf->name);
		break;
	case RTECMD_CLOSE:
		printf("%s.\n", !d->cmd.error ?
			"Successfully closed" : "Failed to close");
		break;
	case RTECMD_ROUTE:
		if (d->nr_intf == 2) {
			printf("%s '%s' and '%s'.\n", !d->cmd.error ?
				"Successfully bound" : "Failed to bind",
				intf[0].name, intf[1].name);
		} else {
			printf("Unknown error occured.\n");
		}
		break;
	case RTECMD_FORGET:
		printf("%s '%s'.\n", !d->cmd.error ?
			"Successfully unbound" : "Failed to unbind",
			intf[0].name);
		break;
	case RTECMD_XFER:
		printf("%s '%s'.\n", !d->cmd.error ?
			"Data successfully transfered to" :
			"Failed to transfer data to",
			intf->name);
		break;
	case RTECMD_CAT:
		if (!d->cmd.error) {
			printf("%.*s\n", d->cmd.val.s, d->cmd.val.v);
		} else {
			printf("Failed to read data from '%s'.\n", intf->name);
		}
		break;
	}

out:
	rtemgr_data_free(d);
	event_base_loopbreak(base);
}

static void timeout_handler(int sock, short which, void *arg)
{
	struct event_base *base = arg;
	printf("Timed out.\n");
	event_base_loopbreak(base);
}

int main(int argc, char *argv[])
{
	rtemgr_data *d;
	struct rtemgr_action *entry = NULL;
	char *subcmd;
	int option_index = 0;
	int c;

	/* 32 is enough for subcommand options for now */
	struct option all_options[32] = { 0 };
	struct option *sub_opts;
	int i, j, total = 0;

	struct option *subcmd_opts[] = {
		default_options,
		open_options,    /* 'open' */
		xfer_options,    /* 'xfer' */
	};

	unsigned char buf[4096] = {0};
	size_t written;

	struct event_base *base;
	struct event *ev;
	struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
	int ctx;

	/**
	 *  Initialize libevent and librteipc
	 */
	base = event_base_new();
	if (!base || !(ev = evtimer_new(base, timeout_handler, base))) {
		fprintf(stderr, "Failed to initialize library.\n");
		exit(EXIT_FAILURE);
	}
	/* set timeout to 10 secs */
	evtimer_add(ev, &tv);
	rteipc_init(base);

	/*
	 * To create all available options, simply merge all subcommand
	 * options.
	 *
	 * NOTE: don't care about duplicate options here.
	 */
	for (i = 0; i < array_size(subcmd_opts); i++) {
		sub_opts = subcmd_opts[i];
		j = 0;
		while (sub_opts[j].name)
			all_options[total++] = sub_opts[j++];
	}

	while ((c = getopt_long(argc, argv,
				/*
				 * tests for other options will be taken
				 * by subcommand routine later.
				 */
				"+h",
				all_options,
				&option_index)) != -1) {
		switch (c) {
		case 'h':  /* --help */
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		};
	}

	/* check subcommand */
	if (optind < argc) {
		subcmd = argv[optind++];
		entry = get_rtemgr_action(subcmd);
	}

	/* unknown or no subcommand found */
	if (!entry || entry->action == RTECMD_MAX) {
		fprintf(stderr, "Unknown command: %s\n", subcmd ?: "");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	/* parse subcommand options and setup data, exit() if failed */
	d = subcmd_opt_check(argc, argv, entry);

	/* connect to rtemgrd service */
	ctx = rteipc_connect(URI(IPC, RTEMGRD_CTLPORT));
	if (ctx < 0) {
		fprintf(stderr, "Failed to connect to rtemgrd.\n");
		exit(EXIT_FAILURE);
	}

	/* convert data to formatted string */
	if (rtemgr_data_emit(d, buf, sizeof(buf), &written)) {
		fprintf(stderr, "Failed to emit data\n");
		return -1;
	}

	/* send request to rtemgrd service */
	rteipc_send(ctx, buf, written);
	rteipc_setcb(ctx, reply_callback, NULL, base, 0);
	rteipc_dispatch(NULL);
	rteipc_shutdown();
	return 0;
}
