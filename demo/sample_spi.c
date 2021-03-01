// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/spi/spidev.h>
#include "rteipc.h"

#define MAX_BYTES	32

static uint8_t tx_buf[MAX_BYTES];
static uint8_t rx_buf[MAX_BYTES];
static struct spi_ioc_transfer spi_xfer[MAX_BYTES];

static void read_spi(int ctx, void *data, size_t len, void *arg)
{
	struct event_base *base = arg;
	struct spi_ioc_transfer *xfer = data;
	size_t num = len / sizeof(*xfer);
	int i;

	for (i = 0; i < num; i++, xfer++)
		printf(" 0x%02x", ((uint8_t *)xfer->rx_buf)[0]);
	event_base_loopbreak(base);
}

void main(int argc, char **argv)
{
	const char *ipc = "ipc://@/sample_spi";
	struct event_base *base = event_base_new();
	struct event *ev;
	int ctx, i = 0;
	uint8_t bytes;
	char *hex;

	if (argc != 3) {
		fprintf(stderr, "Usage: ./sample_spi <uri> <DATA>\n"
				"  (example: # ./sample_spi spi:///dev/spidev0.0,1000000,3 "
				"\"0xaa 0xbb 0xcc 0xdd\")\n");
		return;
	}

	rteipc_init(base);

	if (rteipc_bind(rteipc_open(ipc), rteipc_open(argv[1]))) {
		fprintf(stderr, "Failed to bind %s\n", argv[1]);
		return;
	}

	if ((ctx = rteipc_connect(ipc)) < 0) {
		fprintf(stderr, "Failed to connect %s\n", ipc);
		return;
	}

	printf("write: [");
	hex = strtok(argv[2], " ");
	do {
		tx_buf[i] = strtoul(hex, NULL, 16);
		printf(" 0x%02x", tx_buf[i]);
		spi_xfer[i].tx_buf = (unsigned long)&tx_buf[i];
		spi_xfer[i].rx_buf = (unsigned long)&rx_buf[i];
		spi_xfer[i].len = 1;
		i++;
	} while (i < MAX_BYTES && (hex = strtok(NULL, " ")));
	printf(" ]\n");

	rteipc_send(ctx, spi_xfer, sizeof(spi_xfer[0]) * i);
	rteipc_setcb(ctx, read_spi, NULL, base, 0);
	printf("read : [");
	rteipc_dispatch(NULL);
	printf(" ]\n");
	rteipc_shutdown();
}
