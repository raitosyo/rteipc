# rteipc
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/raitosyo/rteipc/master/LICENSE)
[![Build Status](https://img.shields.io/circleci/project/github/raitosyo/rteipc/master.svg)](https://circleci.com/gh/raitosyo/rteipc)

IPC library for embedded Linux; uses libevent.

## Building

To create an build in the project tree:

    mkdir build && cd build
    cmake ../
    cmake --build .

## How to use rteipc

rteipc uses libevent and is a wrapper library for embedded systems.

### Initialization

rteipc can be initialized with a preconfigured event_base of libevent.

##### void rteipc_init(struct event_base *base)

rteipc_init() initializes libevent with the argument _base_. If _base_ is NULL
a new event_base will be created and used. This function must be called before
any rteipc functions are called.

### Endpoint

An endpoint is an interface to a process or a file (e.g. socket/tty/gpio).

##### int rteipc_ep_open(const char *uri)

rteipc_ep_open() creates IPC/TTY/GPIO endpoints. The return value is an
endpoint descriptor. The argument _uri_ has a different format depends on its
type:

      "ipc://@socket-name"                            (UNIX domain socket in abstract namespace)
      "ipc:///tmp/path-name"                          (UNIX domain socket bound to a filesystem pathname)
      "tty:///dev/ttyUSB0,115200"                     (/dev/ttyUSB0 setting speed to 115200 baud)
      "gpio://consumer-name@/dev/gpiochip0-1,out,lo"  (GPIO_01 is configured as direction:out, value:0)
      "gpio://consumer-name@/dev/gpiochip0-1,in"      (GPIO_01 is configured as direction:in)

##### int rteipc_ep_bind(int ep_a, int ep_b)

rteipc_ep_bind() connects two endpoints together. The return value is zero on
success, otherwise -1. The argument _ep_a_, _ep_b_ are endpoint descriptors.

##### int rteipc_ep_unbind(int ep_a, int ep_b)

rteipc_ep_unbind() removes connection from two endpoints. The return value is
zero on success, otherwise -1. The argument _ep_a_, _ep_b_ are endpoint
descriptors.

### Reading from and Writing to an IPC Endpoint

A process which uses rteipc can read from and write to only IPC endpoint. The
data stored in an IPC endpoint will be available in the other endpoint to which
the IPC endpoint is bound, and vice versa.

##### int rteipc_connect(const char *uri)

rteipc_connect() connects a process to an IPC endpoint. The endpoint specified
by the uri must be created before this function call. The return value is an
context descriptor on success, otherwise -1. The argument _uri_ is a endpoint
pathname (see above).

##### int rteipc_send(int cid, const void *buf, size_t len)

rteipc_send() are used to transmit data to an IPC endpoint. The argument _cid_
is the context descriptor of the sending connection returned by
rteipc_connect(). The data is found in _buf_ and has length _len_.

##### int rteipc_setcb(int cid, rteipc_read_cb read_cb, rteipc_err_cb err_cb, void *arg, short flag)

rteipc_setcb() changes read/error callbacks. The argument _arg_ can be used to
pass data to the callbacks and _flag_ is a bitmask of flags. The following
_flag_ is defined:  
_RTEIPC_NO_EXIT_ON_ERR_  
&nbsp; &nbsp; Do not exit from the event loop when an error occurs.

### Event loop

##### void rteipc_dispatch(struct timeval *tv)

rteipc_dispatch() runs event dispatching loop. If the argument _tv_ is
specified, exit the event loop after the specified time.

## Example

###### Serial Port Reading/Writing With GPIO reset

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/time.h>
    #include <rteipc.h>

    static void read_cb(int ctx, void *data, size_t len, void *arg)
    {
        // Display data from the device
        printf("%.*s", len, data);
    }

    int main(void)
    {
        int ctx;
        int ep_ipc, ep_gpio, ep_tty;
        const char *ipc = "ipc://@/tmp/rteipc";
        const char *gpio = "gpio://reset@/dev/gpiochip0-1,out,lo";
        const char *tty = "tty:///dev/ttyS0,115200";
        struct timeval tv = {0, 100 * 1000};

        rteipc_init(NULL);

        ep_ipc = rteipc_ep_open(ipc);
        ep_gpio = rteipc_ep_open(gpio);
        ep_tty = rteipc_ep_open(tty);
        if (ep_ipc < 0 || ep_gpio < 0 || ep_tty < 0) {
            fprintf(stderr, "Failed to open endpoints\n");
            exit(EXIT_FAILURE);
        }

        ctx = rteipc_connect(ipc);
        if (ctx < 0) {
            fprintf(stderr, "Failed to connect to socket\n");
            exit(EXIT_FAILURE);
        }

        /* Bind IPC and GPIO endpoints together to reset the device via the GPIO line */
        if (rteipc_ep_bind(ep_ipc, ep_gpio)) {
            fprintf(stderr, "Failed to bind GPIO endpoint\n");
            exit(EXIT_FAILURE);
        }
        /* Assert GPIO line by writing "1" */
        rteipc_send(ctx, "1", 1);
        /* 100ms event loop */
        rteipc_dispatch(&tv);

        /* Unbind them since we don't need the connection anymore */
        rteipc_ep_unbind(ep_ipc, ep_gpio);

        /* Rebind IPC and TTY endpoints together to communicate with the device via UART */
        if (rteipc_ep_bind(ep_ipc, ep_tty)) {
            fprintf(stderr, "Failed to bind TTY endpoint\n");
            exit(EXIT_FAILURE);
        }
        /* send "hello" to the device */
        rteipc_send(ctx, "hello", strlen("hello"));
        rteipc_setcb(ctx, read_cb, NULL, NULL, 0);
        /* Run event loop */
        rteipc_dispatch(NULL);
        return 0;
    }

The above codes assume:

                   IPC-Endpoint                   GPIO-Endpoint                  Physical
                   |----------------------| bind  |------------------|           Device
            socket | (abstract namespace) |<----->| "/dev/gpiochip0" |  GPIO1_1  |-------|
    process <----> |    "@/tmp/rteipc"    |<--|   |                  | <=======> |       |
                   |----------------------|   |   |------------------|           |       |
                                              |                                  |       |
                                              |   TTY-Endpoint                   |       |
                                              |   |------------------|           |       |
                                              |-->|   "/dev/ttyS0"   |   UART    |       |
                                                  |                  | <=======> |       |
                                                  |------------------|           |-------|
