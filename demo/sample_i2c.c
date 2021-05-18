// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "rteipc.h"

#define MAX_BYTES	32

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
		printf(" 0x%02x", byte_array[i]);
	printf(" ]\n");
	event_base_loopbreak(base);
}

void main(int argc, char **argv)
{
	const char *ipc = "ipc://@/sample_i2c";
	struct event_base *base = event_base_new();
	uint8_t tx_buf[MAX_BYTES];
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

	if (!uri || uri != strstr(uri, "i2c://") || !addr || (!buf && !rsize))
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
	}
	rteipc_i2c_send(ctx, addr, tx_buf, i, rsize);
	rteipc_setcb(ctx, read_i2c, NULL, base, 0);
	rteipc_dispatch(!rsize ? &tv : NULL);
	rteipc_shutdown();
}
