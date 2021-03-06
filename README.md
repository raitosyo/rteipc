# rteipc
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/raitosyo/rteipc/master/LICENSE)
[![Build Status](https://img.shields.io/circleci/project/github/raitosyo/rteipc/master.svg)](https://circleci.com/gh/raitosyo/rteipc)

IPC library for working with peripherals on embedded Linux; uses libevent.

## Building

To create an build in the project tree:

    mkdir build && cd build
    cmake ../
    cmake --build .

## How to use rteipc

rteipc uses libevent for event loop and has modules to interact with peripherals.

### Initialization

rteipc can be initialized with a preconfigured event_base of libevent.

##### void rteipc_init(struct event_base *base)

rteipc_init() initializes libevent with the argument _base_. If _base_ is NULL a new event_base will be automatically created and used. This function must be called before any rteipc functions are called.

### Endpoint

In rteipc, an endpoint (EP) is a common interface with a process, file, or peripheral. If a process wants to interact with a peripheral, there must be two EPs, one is for the process and the other for the peripheral, bound together to transfer data between them. The following figure shows how a process reads/writes from/to a peripheral device.

                  EP                    EP
             (For process)        (For peripheral)
            +------------+         +------------+
       +--->|            |<------->|            |<---+
       |    +------------+  bound  +------------+    |
       |                                             |
       +---->'Backend'<-----+        'Backend'<------+
            (Unix Soket)    |        (Device)  read/write
                            |
               process <----+
                        read/write

###### _Backend_ supported by rteipc: _Unix domain socket_, _sysfs file_, _GPIO_, _TTY_, _I2C_, and _SPI_.

##### int rteipc_open(const char *uri)

rteipc_open() creates endpoints for backends supported. The return value is an endpoint descriptor. The argument _uri_ has a different format depends on its type:

      "ipc://@socket-name"                            (UNIX domain socket in abstract namespace)
      "ipc:///tmp/path-name"                          (UNIX domain socket bound to a filesystem pathname)
      "sysfs://pwm:pwmchip0"                          (PWM1 via sysfs)
      "gpio://consumer-name@/dev/gpiochip0-1,out,lo"  (GPIO_01 is configured as direction:out, value:0)
      "gpio://consumer-name@/dev/gpiochip0-1,in"      (GPIO_01 is configured as direction:in)
      "tty:///dev/ttyS0,115200"                       (/dev/ttyS0 setting speed to 115200 baud)
      "i2c:///dev/i2c-0"                              (I2C-1 device)
      "spi:///dev/spidev0.0,5000,3"                   (/dev/spidev0.0 setting max speed to 5kHz and SPI mode to 3)

##### int rteipc_bind(int ep_a, int ep_b)

rteipc_bind() connects two endpoints together. The return value is zero on success, otherwise -1. The argument _ep_a_, _ep_b_ are endpoint descriptors.

##### int rteipc_unbind(int ep)

rteipc_unbind() removes connection from two endpoints. The return value is zero on success, otherwise -1. The argument _ep_ is an endpoint descriptor bound.

### Reading from and Writing to an IPC Endpoint

A process that uses rteipc can read from and write to only IPC endpoint. The data stored in an IPC endpoint will be available in the other endpoint to which the IPC endpoint is bound, and vice versa.

##### int rteipc_connect(const char *uri)

rteipc_connect() connects a process to an IPC endpoint. The endpoint specified by the _uri_ must be created before this function call. The return value is a context descriptor on success, otherwise -1. The argument _uri_ is an endpoint pathname (see above).

##### int rteipc_send(int ctx, const void *buf, size_t len)

rteipc_send() is used to transmit data to an IPC endpoint. The argument _ctx_ is the context descriptor of the sending connection returned by rteipc_connect(). The data is found in _buf_ and has length _len_.

##### int rteipc_setcb(int ctx, rteipc_read_cb read_cb, rteipc_err_cb err_cb, void *arg, short flag)

rteipc_setcb() changes read/error callbacks. The argument _arg_ can be used to pass data to the callbacks and _flag_ is a bitmask of flags. The following _flag_ is defined:  _RTEIPC_NO_EXIT_ON_ERR_
&nbsp; &nbsp; Do not exit from the event loop when an error occurs.

### Event loop

##### void rteipc_dispatch(struct timeval *tv)

rteipc_dispatch() runs event dispatching loop. If the argument _tv_ is specified, exit the event loop after the specified time.

## Example

Sample programs are in the demo directory.
