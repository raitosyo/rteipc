// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <linux/i2c.h>
#include "rteipc.h"

#define MAX_BYTES	32

static struct i2c_msg i2c_msgs[2];
static uint8_t tx_buf[MAX_BYTES];
static uint8_t rx_buf[MAX_BYTES];

static void usage_exit()
{
	fprintf(stderr,
		"sample_i2c - I2C demo application using rteipc\n"
		"\n"
		"sample_i2c URI -a SlaveAddress\n"
		"    [ -w Data | -r Size ]\n"
		"\n"
		"Read byte at register:0x01 of slave:0x20 on I2C-1.\n"
		"sample_i2c \"i2c:///dev/i2c-1\" -a 0x20 -w 0x01 -r 1\n"
		"\n"
		"Write [0x0a 0x0b] into register:0x02 of slave:0x20 on "
		"I2C-1.\n"
		"sample_i2c \"i2c:///dev/i2c-1\" -a 0x20 "
		"-w \"0x02 0x0a 0x0b\"\n");
	exit(1);
}

static void read_i2c(int ctx, void *data, size_t len, void *arg)
{
	struct event_base *base = arg;
	uint8_t *byte_array = data;
	int i;

	printf("read : [");
	for (i = 0; i < len; i++)
		printf(" 0x%02x", ((uint8_t *)data)[i]);
	printf(" ]\n");
	event_base_loopbreak(base);
}

void main(int argc, char **argv)
{
	const char *ipc = "ipc://@/sample_i2c";
	struct event_base *base = event_base_new();
	int ctx, i = 0, c;
	uint16_t rsize = 0, addr = 0;
	char *uri, *hex, *buf = NULL;
	struct timeval tv = {0, 1};

	while ((c = getopt(argc, argv, "a:r:w:")) != -1) {
		switch (c) {
		case 'a':
			addr = strtoul(optarg, NULL, 16);
			break;
		case 'r':
			rsize = atoi(optarg);
			if (rsize > MAX_BYTES)
				rsize = MAX_BYTES;
			break;
		case 'w':
			buf = strdup(optarg);
			break;
		default:
			usage_exit();
		}
	}
	uri = (optind < argc) ? argv[optind] : NULL;

	if (!uri || !addr || (!buf && !rsize))
		usage_exit();

	rteipc_init(base);

	if (rteipc_bind(rteipc_open(ipc), rteipc_open(uri))) {
		fprintf(stderr, "Failed to bind %s\n", uri);
		return;
	}

	if ((ctx = rteipc_connect(ipc)) < 0) {
		fprintf(stderr, "Failed to connect %s\n", ipc);
		return;
	}

	if (buf) {
		printf("write: [");
		hex = strtok(buf, " ");
		do {
			tx_buf[i] = strtoul(hex, NULL, 16);
			printf(" 0x%02x", tx_buf[i]);
			i++;
		} while (i < MAX_BYTES && (hex = strtok(NULL, " ")));
		printf(" ]\n");
		i2c_msgs[0].addr = addr;
		i2c_msgs[0].flags = 0;
		i2c_msgs[0].buf = tx_buf;
		i2c_msgs[0].len = i;
	}

	if (rsize) {
		i2c_msgs[1].addr = addr;
		i2c_msgs[1].flags = I2C_M_RD;
		i2c_msgs[1].buf = rx_buf;
		i2c_msgs[1].len = rsize;
	}

	if (buf && rsize)
		rteipc_send(ctx, i2c_msgs, sizeof(i2c_msgs));
	else if (buf)
		rteipc_send(ctx, &i2c_msgs[0], sizeof(i2c_msgs[0]));
	else if (rsize)
		rteipc_send(ctx, &i2c_msgs[1], sizeof(i2c_msgs[1]));

	rteipc_setcb(ctx, read_i2c, NULL, base, 0);
	if (rsize)
		rteipc_dispatch(NULL);
	else
		rteipc_dispatch(&tv);
	rteipc_shutdown();
}
