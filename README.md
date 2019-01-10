# User-defined routing IPC library
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/raitosyo/rteipc/master/LICENSE)
[![Build Status](https://img.shields.io/circleci/project/github/raitosyo/rteipc/master.svg)](https://circleci.com/gh/raitosyo/rteipc)

rteipc is a simple user-defined routing IPC library for Linux; uses libevent.

## building rteipc

To create an build in the project tree:

    mkdir build && cd build
    cmake ../
    cmake --build .

## endpoint

When using rteipc, an endpoint is always an interface to a process or a file
(e.g. socket/tty/gpio).

## example

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <rteipc.h>

    static void read_cb(int ctx, void *data, size_t len, void *arg)
    {
        // display data from /dev/ttyUSB0
        printf("%.*s", len, data);
    }

    int main(void)
    {
        int ctx;
        int ipc, tty;
        const char *ipc_path = "ipc://@/tmp/rteipc";
        const char *tty_path = "tty:///dev/ttyUSB0,115200";

        rteipc_init(NULL);

        ipc = rteipc_ep_open(ipc_path);
        tty = rteipc_ep_open(tty_path);
        if (ipc < 0 || tty < 0) {
            fprintf(stderr, "Failed to open endpoints\n");
            exit(EXIT_FAILURE);
        }
        rteipc_ep_route(ipc, tty, RTEIPC_BIDIRECTIONAL);

        ctx = rteipc_connect(ipc_path);
        if (ctx < 0) {
            fprintf(stderr, "Failed to connect socket\n");
            exit(EXIT_FAILURE);
        }

        // write "hello" to /dev/ttyUSB0
        rteipc_send(ctx, "hello", strlen("hello"));
        rteipc_setcb(ctx, read_cb, NULL, NULL, 0);
        rteipc_dispatch(NULL);
        return 0;
    }

The above codes setup for:

                          [IPC endpoint]
                          |----------------------|   tx
    process <-----------> | (abstract namespace) |----------|
                 Socket   |    "@/tmp/rteipc"    |<--------||
                          |----------------------|   rx    ||
                                                           || bidirectional
                          [TTY endpoint]                   ||
                          |----------------------|   tx    ||
     DEVICE <===========> |    "/dev/ttyUSB0"    |---------||
           USB (Physical) |                      |<---------|
                          |----------------------|   rx

***
#### gpio endpoint

For hardware reset using a GPIO pin:

    const char *gpio_path = "gpio://reset-sample@/dev/gpiochip3-15,out,lo";
    int gpio = rteipc_ep_open(gpio_path);
    struct timeval tv = {0, 100 * 1000};

	rteipc_ep_route(ipc, gpio, RTEIPC_FORWARD);
    rteipc_send(ctx, "1", 1); // assert high
	rteipc_dispatch(&tv); // 100ms event loop
    ...

For more examples, check demo subdirectory.
