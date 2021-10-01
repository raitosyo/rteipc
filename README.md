# rteipc
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/raitosyo/rteipc/master/LICENSE)
[![Build Status](https://img.shields.io/circleci/project/github/raitosyo/rteipc/master.svg)](https://circleci.com/gh/raitosyo/rteipc)

Event loop library for working with peripherals (e.g., I2C, SPI, and GPIO) on embedded Linux, and help to write programs in the same manner regardless of its bus type.

## Requirements

For debian/ubuntu:

    apt-get install -y cmake g++ gcc make pkg-config \
        libb64-dev libevent-dev libgpiod-dev libudev-dev libyaml-dev

For Fedora:

    dnf install -y cmake g++ make pkg-config \
        libb64-devel libevent-devel libgpiod-devel libyaml-devel systemd-devel

## Building

To create an build in the project tree:

    mkdir build && cd build
    cmake ../
    cmake --build .

## What is rteipc ? Why ?

rteipc (Route IPC) transfers data to a peripheral by routing data between two _endpoints_. An application writes data to one _endpoint_, then rteipc routes the data to the other _endpoint_ representing the peripheral. This indirect write helps with writing a program in a bus-independent manner.

#### Endpoint

An endpoint (EP) represents a process or peripheral and is a standard interface with them. When a process interacts with a peripheral, there must be two EPs, one is for the process and the other for the peripheral, which are bound together to transfer data between them.  
The following figure shows how a process reads/writes from/to a peripheral device.

                  EP                    EP
             (For process)        (For peripheral)
            +------------+         +------------+
       +--->|            |<------->|            |<---+
       |    +------------+  bound  +------------+    |
       |                                             |
       +---->'Backend'<-----+        'Backend'<------+
              (socket)      |         (Device)  read/write
                            |
               process <----+
                        read/write

###### _Backend_ supported by rteipc: _socket (Unix domain or Internet)_, _sysfs file_, _GPIO_, _TTY_, _I2C_, and _SPI_.

## How to use rteipc ?

This guide writes a simple program using rteipc and sends data to an I2C device on the target from the remote host.
To help set up endpoint configuration on the target device, we use rtemgr (a command-line tool) in the below steps, but of course, it's optional when you write a program that creates all endpoints needed on the target device.

##### Step 1 - Run the rtemgr container image
###### First, run the container image in privileged and host network mode to access all devices and networking on the target device.

    # docker run --privileged --network=host --rm -it raitosyo/rtemgr

##### Step 2 - Setup endpoints

###### 1. Open an endpoint on the container, for example:

    # rtemgr open -t i2c -n my-i2c /dev/i2c-0

###### 2. List all endpoints:

    # rtemgr list
    NAME             BUS    PATH                                 ROUTE
    my-i2c           i2c    /dev/i2c-0                           --

##### Step 3 - Reading or Writing from the command line

###### 1. Example for writing:

    (write [0xbb, 0xcc] to address:0xaa)
    # rtemgr xfer my-i2c --value "0xbb 0xcc" --addr 0xaa

###### 2. Example for reading:

    (first, request 1 byte from address:0xaa, register:0xbb)
    # rtemgr xfer my-i2c --addr 0xAA --value "0xBB" --read
    # rtemgr cat my-i2c

##### Step 4 - Reading or Writing from a program

###### 1. Open an endpoint for remote access:

    (create a TCP socket endpoint)
    # rtemgr open -t inet -n my-inet 0.0.0.0:9999

###### 2. Route data between two endpoints:

    (create data stream between TCP:9999 and I2C endpoint)
    # rtemgr route my-i2c my-inet

    (ensure the two endpoints are routed in 'ROUTE' column)
    # rtemgr list
    NAME             BUS    PATH                                 ROUTE
    my-i2c           i2c    /dev/i2c-0                           my-inet
    my-inet          inet   0.0.0.0:9999                         my-i2c

###### 3. (On the host machine) Write a program connecting to the target device:

    (The below sample program does the same thing as Step 3)

    #include <rteipc.h>

    static void read_i2c(int ctx, void *data, size_t len, void *arg)
    {
        struct event_base *base = arg;
        printf("[ 0x%02x ]\n", (uint_8 *)data);
        event_base_loopbreak(base);
    }

    void main(void)
    {
        struct event_base *base = event_base_new();
        uint16_t addr = 0xaa;
        uint8_t tx_buf[] = {0xbb, 0xcc};
        int ctx;

        /* Initialize rteipc with event_base */
        rteipc_init(base);

        /* Connect to the target device via INET endpoint */
        ctx = rteipc_connect("inet://<target ip>:9999");

        /* write [0xbb, 0xcc] to address:0xaa */
        rteipc_i2c_send(ctx, addr, tx_buf, sizeof(tx_buf), 0);
        /* request 1 byte from address:0xaa, register:0xbb */
        rteipc_i2c_send(ctx, addr, tx_buf, 1, 1);
        rteipc_setcb(ctx, read_cb, NULL, base, 0);

        /* Run eventloop */
        rteipc_dispatch(NULL);
    }

### API

##### void rteipc_init(struct event_base *base)

rteipc_init() initializes libevent with the argument _base_. If _base_ is NULL a new event_base will be automatically created and used. This function must be called before any rteipc functions are called.

##### int rteipc_open(const char *uri)

rteipc_open() creates endpoints for backends supported. The return value is an endpoint descriptor. The argument _uri_ has a different format depends on its type:

      "ipc://@socket-name"                            (UNIX domain socket in abstract namespace)
      "ipc:///tmp/path-name"                          (UNIX domain socket bound to a filesystem pathname)
      "inet://0.0.0.0:9110"                           (Internet socket for all IPv4 addresses and tcp port 9110)
      "sysfs://pwm:pwmchip0"                          (PWM1 via sysfs)
      "gpio://consumer-name@/dev/gpiochip0-1,out,0"   (GPIO_01 is configured as direction:out, value:0)
      "gpio://consumer-name@/dev/gpiochip0-1,in"      (GPIO_01 is configured as direction:in)
      "tty:///dev/ttyS0,115200"                       (/dev/ttyS0 setting speed to 115200 baud)
      "i2c:///dev/i2c-0"                              (I2C-1 device)
      "spi:///dev/spidev0.0,5000,3"                   (/dev/spidev0.0 setting max speed to 5kHz and SPI mode to 3)

##### int rteipc_bind(int ep_a, int ep_b)

rteipc_bind() connects two endpoints together. The return value is zero on success, otherwise -1. The argument _ep_a_, _ep_b_ are endpoint descriptors.

##### int rteipc_unbind(int ep)

rteipc_unbind() removes connection from two endpoints. The return value is zero on success, otherwise -1. The argument _ep_ is an endpoint descriptor bound.

##### int rteipc_connect(const char *uri)

rteipc_connect() is used for a process to connect to an IPC or INET endpoint. The endpoint specified by the _uri_ must be created before this function call. The return value is a context descriptor on success, otherwise -1. The argument _uri_ is an endpoint pathname (see above).
A process can read from and write to only IPC or INET endpoint. The data stored by the process in the endpoint will be available in the other endpoint to which it is bound, and vice versa.

##### int rteipc_send(int ctx, const void *buf, size_t len)

rteipc_send() is a generic helper function to transmit raw data to an endpoint. The argument _ctx_ is the context descriptor of the sending connection returned by rteipc_connect(). The data is found in _buf_ and has length _len_.

##### int rteipc_gpio_send(int ctx, uint8_t value)

rteipc_gpio_send() should be used to transmit data when the other end is GPIO endpoint. This sends data in a format specific to GPIO. The argument _ctx_ is the same as rtipc_send(). The argument _value_ is 1 (assert) or 0 (deassert).

##### int rteipc_spi_send(int ctx, const uint8_t *tx_buf, uint16_t len, bool rdmode)

rteipc_spi_send() should be used to transmit data when the other end is SPI endpoint. This sends data in a format specific to SPI. The argument _ctx_ is the same as rtipc_send(). The data is found in _tx_buf_ and has length _len_. The argument _rdmode_ determines if the endpoint reads SPI shift registers or not.

##### int rteipc_i2c_send(int ctx, uint16_t addr, const uint8_t *tx_buf, uint16_t wlen, uint16_t rlen)

rteipc_i2c_send() should be used to transmit data when the other end is I2C endpoint. This sends data in a format specific to I2C. The argument _ctx_ is the same as rtipc_send(). The data is found in _tx_buf_ and has length _wlen_. The argument _addr_ is I2C address and _rlen_ determines how many bytes the endpoint reads from the I2C device.

##### int rteipc_sysfs_send(int ctx, const char *attr, const char *value)

rteipc_sysfs_send() should be used to transmit data when the other end is SYSFS endpoint. This sends data in a format specific to SYSFS. The argument _ctx_ is the same as rtipc_send(). The argument _attr_ is the name of an attribute and _value_ is the new value of the attribute.

##### int rteipc_setcb(int ctx, rteipc_read_cb read_cb, rteipc_err_cb err_cb, void *arg, short flag)

rteipc_setcb() changes read/error callbacks. The argument _arg_ can be used to pass data to the callbacks and _flag_ is a bitmask of flags. The argument _frag_ is not used for now.

##### void rteipc_dispatch(struct timeval *tv)

rteipc_dispatch() runs event dispatching loop. If the argument _tv_ is specified, exit the event loop after the specified time.

## Example

There are several programs to provide how to use rteipc in the demo directory. To build demo, uncomment the following line in CMakeLists.txt:

    # Uncomment if you want to build demo applications
    #add_subdirectory(demo)
