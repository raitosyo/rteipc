// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rteipc.h"

#define MAX_BYTES	32

static void read_spi(const char *name, void *data, size_t len, void *arg)
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
	struct event_base *base = event_base_new();
	uint8_t tx_buf[MAX_BYTES];
	int ctx, i = 0;
	char *hex;

	if (argc != 3 || argv[1] != strstr(argv[1], "spi://")) {
		fprintf(stderr, "Usage: %s <uri> <DATA>\n"
				"  (example: # ./sample_spi spi:///dev/spidev0.0,1000000,3 "
				"\"0xaa 0xbb 0xcc 0xdd\")\n",
				argv[0]);
		return;
	}

	rteipc_init(base);

	if (rteipc_bind(rteipc_open("lo"), rteipc_open(argv[1]))) {
		fprintf(stderr, "Failed to bind %s\n", argv[1]);
		return;
	}

	printf("write: [");
	hex = strtok(argv[2], " ");
	do {
		tx_buf[i] = strtoul(hex, NULL, 16);
		printf(" 0x%02x", tx_buf[i]);
		i++;
	} while (i < MAX_BYTES && (hex = strtok(NULL, " ")));
	printf(" ]\n");

	rteipc_spi_xfer("lo", tx_buf, i, true /* read flag */);
	rteipc_xfer_setcb("lo", read_spi, base);
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}
