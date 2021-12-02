// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rteipc.h"


static void usage_exit()
{
	fprintf(stderr,
		"sample_sysfs - SYSFS demo application using rteipc\n"
		"\n"
		"sample_sysfs URI Attribute [Value]\n"
		"\n"
		"URI syntax:\n"
		"  \"sysfs://<subsystem>:<device>\"\n"
		"  \"sysfs://<device_id>\"\n"
		"  \"sysfs://<path>\"\n"
		"\n"
		"Example:\n"
		"  \"sysfs://pwm:pwmchip0\"\n"
		"  \"sysfs://c29:0\"\n"
		"  \"sysfs:///sys/class/backlight/backlight\"\n"
		"\n"
		"Read brightness of a display.\n"
		"sample_sysfs \"sysfs://backlight:backlight\" \"brightness\"\n"
		"\n"
		"Control PWM device with 1kHz frequency and 50%% duty cycle.\n"
		"sample_sysfs \"sysfs://pwm:pwmchip0\" \"export\" 0; \\\n"
		"  sample_sysfs \"sysfs://pwm:pwmchip0\" \"pwm0/period\" 1000000; \\\n"
		"  sample_sysfs \"sysfs://pwm:pwmchip0\" \"pwm0/duty_cycle\" 500000; \\\n"
		"  sample_sysfs \"sysfs://pwm:pwmchip0\" \"pwm0/enable\" 1;\n"
		"\n");
	exit(1);
}

static void read_sysfs(const char *name, void *data, size_t len, void *arg)
{
	struct event_base *base = arg;
	printf("%.*s\n", len, data);
	event_base_loopbreak(base);
}

void main(int argc, char **argv)
{
	struct event_base *base = event_base_new();
	bool wr;

	if ((argc != 3 && argc != 4) || argv[1] != strstr(argv[1], "sysfs://"))
		usage_exit();

	wr = !!(argc == 4);

	rteipc_init(base);

	if (rteipc_bind(rteipc_open("lo"), rteipc_open(argv[1]))) {
		fprintf(stderr, "Failed to bind %s\n", argv[1]);
		return;
	}

	if (wr) {
		/* Request writing */
		rteipc_sysfs_xfer("lo", argv[2], argv[3]);
	}
	/* Request reading to show the current value of the attribute */
	rteipc_sysfs_xfer("lo", argv[2], NULL /* NULL for reading value */);
	rteipc_xfer_setcb("lo", read_sysfs, base);
	rteipc_dispatch(NULL);
	rteipc_shutdown();
}
