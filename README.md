# rteipc
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/raitosyo/rteipc/master/LICENSE)
[![Build Status](https://img.shields.io/circleci/project/github/raitosyo/rteipc/master.svg)](https://circleci.com/gh/raitosyo/rteipc)

Event loop library for working with peripherals (e.g., I2C, SPI, and GPIO) on embedded Linux, and help to write programs in the same manner regardless of its bus type.

## Building

To create an build in the project tree:

    mkdir build && cd build
    cmake ../
    cmake --build .

## What is rteipc ? Why ?

rteipc (Route IPC) transfers data to a peripheral by routing data between two _endpoints_. An application writes data to one _endpoint_, then rteipc routes the data to the other _endpoint_ representing the peripheral. This indirect write helps with writing a program in a bus-independent manner.

#### Endpoint

An endpoint (EP) represents a process or peripheral and is a standard interface. An EP can have one _backend_ depending on its type (e.g., I2C type can have /dev/i2c-0 and IPC can have Unix domain socket as its _backend_) and a process or peripheral writes data to the EP via the _backend_. The data will be transferred to the other EP to which the EP of the _backend_ is bound.

A process cannot send data directly to the peripheral types of EPs (GPIO, I2C, SPI, etc..). Instead, it can send data to IPC, INET, or LOOP EPs which can be bound to any EPs.

The following figure shows how a process interacts with an I2C device using an IPC along with related APIs:


            rteipc_open()               rteipc_open()
            +-----------+               +-----------+
       +--->|  EP_IPC   |<------------->|  EP_I2C   |<---+
       |    |           | rteipc_bind() |           |    |
       |    +-----------+               +-----------+    |
       |                                                 |
       +--> [@unix_socket] <--+     +--> [/dev/i2c-0] <--+
                              |     |
            rteipc_connect()  |     +---------> I2C Device
            rteipc_i2c_send() |
            rteipc_setcb()    |
                              |
       Process <--------------+

The below is the same example for interacting with an I2C device but using a LOOP:

       +-----> Process
       |
       | rteipc_i2c_xfer()
       | rteipc_xfer_setcb()
       |
       |    rteipc_open()               rteipc_open()
       |    +-----------+               +-----------+
       +--->|  EP_LOOP  |<------------->|  EP_I2C   |<---+
            |           | rteipc_bind() |           |    |
            +-----------+               +-----------+    |
                                                         |
                                    +--> [/dev/i2c-0] <--+
                                    |
                                    +---------> I2C Device

The LOOP is the unique endpoint in that it has no _backend_; therefore, a process that sends data to a LOOP must be the same with the process which creates it or at least shares the memory region (i.e., threads). But this is useful when a process needs to interact with multiple EPs in its context, like as below:

       +----> Process-A
       |
       |    +-----------+    +-----------+
       +--->|  EP_LOOP  |<-->|  EP_IPC   |<---> [@unix_socket] <---> Process-B
       |    +-----------+    +-----------+
       |    +-----------+    +-----------+
       +--->|  EP_LOOP  |<-->|  EP_INET  |<---> [0.0.0.0:9999] <---> Process-C (remote host)
       |    +-----------+    +-----------+
       |    +-----------+    +-----------+
       +--->|  EP_LOOP  |<-->|  EP_I2C   |<---> [/dev/i2c-0] <---> I2C Device
       |    +-----------+    +-----------+
       |    +-----------+    +-----------+
       +--->|  EP_LOOP  |<-->|  EP_GPIO  |<---> [/dev/gpiochip0] <---> GPIO_01
            +-----------+    +-----------+

## Example

Let's say that we need to write a driver program for the device which has a GPIO interrupt line and an I2C interface as shown below, and need to check a register value of the device via the I2C interface whenever the GPIO line is asserted.

       +----> Process                                  Physical Device
       |                                              +---------------+
       |    +-----------+    +-----------+  GPIO_01   |               |
       +--->|  EP_LOOP  |<---|  EP_GPIO  |<========== |               |
       |    +-----------+    +-----------+            |               |
       |    +-----------+    +-----------+   I2C_1    |               |
       +--->|  EP_LOOP  |<-->|  EP_I2C   |<=========> |               |
            +-----------+    +-----------+            +---------------+

Then the program would be like:

    #include <stdio.h>
    #include <stdint.h>
    /* rteipc header */
    #include <rteipc.h>

    static void gpio_cb(const char *name, void *data, size_t len, void *arg)
    {
        uint8_t *hi = data;
        uint16_t addr = 0xaa;
        uint8_t val[] = {0xbb};

        /* GPIO pin high state? */
        if (*hi) {
            /* Read 1 byte from I2C address:0xaa, register:0xbb */
            rteipc_i2c_xfer("i2c", addr, val, 1, 1);
        }
    }

    static void i2c_cb(const char *name, void *data, size_t len, void *arg)
    {
        /* Print the register value */
        printf("[ 0x%02x ]\n", *(uint8_t *)data);
        /* Do something.. */
    }

    void main(void)
    {
        /* Initialize rteipc library */
        rteipc_init(NULL);

        /* Open LOOP and GPIO then bind, set callback */
        rteipc_bind(
            /* LOOP named 'my_gpio' */
            rteipc_open("my_gpio"),
            /* GPIO with chip:0, line:1 */
            rteipc_open("gpio://gp1@/dev/gpiochip0-1,in")
        );
        rteipc_xfer_setcb("my_gpio", gpio_cb, NULL);

        /* Open LOOP and I2C then bind, set callback */
        rteipc_bind(
            /* LOOP named 'my_i2c' */
            rteipc_open("my_i2c"),
            /* I2C with '/dev/i2c-1' as backend */
            rteipc_open("i2c:///dev/i2c-1")
        );
        rteipc_xfer_setcb("my_i2c", i2c_cb, NULL);

        /* Run eventloop */
        rteipc_dispatch(NULL);
    }

There are several programs to provide how to use rteipc in the demo directory. To build demo, uncomment the following line in CMakeLists.txt:

    # Uncomment if you want to build demo applications
    #add_subdirectory(demo)

## Using rtemgr tool

rtemgr is a command-line tool to help set up endpoint configuration and write/read data to/from peripherals without writing a program, which might be helpful for development purposes.
This guide uses a docker container image on the target device, creates an I2C endpoint, and sends data from the command line. Then, create INET endpoint and bind them to control it from a remote host machine.

##### Step 1 - Run the rtemgr container image on the target device
###### First, run the container image in privileged and host network mode to access all devices and networking on the target device.

    # docker run --privileged --network=host --rm -it raitosyo/rtemgr

##### Step 2 - Set up endpoint

###### 1. Open an endpoint on the container, for example:

    # rtemgr open -t i2c -n my-i2c /dev/i2c-0

###### 2. List all endpoints:

    # rtemgr list
    NAME             BUS    PATH                                 ROUTE
    my-i2c           i2c    /dev/i2c-0                           --

##### Step 3 - Reading or Writing from the command line

###### 1. Example for writing:

    (write [0xbb, 0xcc] to address:0xaa)
    # rtemgr xfer my-i2c --addr 0xaa --value "0xbb 0xcc"

###### 2. Example for reading:

    (first, request 1 byte from address:0xaa, register:0xbb)
    # rtemgr xfer my-i2c --addr 0xaa --value 0xbb --read
    # rtemgr cat my-i2c

##### Step 4 - Reading or Writing from a program

###### 1. Open another endpoint for remote host access:

    (create a TCP socket endpoint)
    # rtemgr open -t inet -n my-inet 0.0.0.0:9999

###### 2. Route data between two endpoints:

    (create data stream between TCP:9999 and I2C endpoint)
    # rtemgr route my-i2c my-inet

    (ensure the two endpoints are routed in the 'ROUTE' column)
    # rtemgr list
    NAME             BUS    PATH                                 ROUTE
    my-i2c           i2c    /dev/i2c-0                           my-inet
    my-inet          inet   0.0.0.0:9999                         my-i2c

###### 3. Write a program to connect to INET on the host machine:

    (on your host machine)

    #include <rteipc.h>

    void main(void)
    {
        int ctx;
        ...
        rteipc_init(NULL);

        /* Connect to the target device via INET endpoint */
        ctx = rteipc_connect("inet://192.168.0.100:9999");

        /* Send data to I2C on the target device */
        rteipc_i2c_send(ctx, ...);
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
      "i2c:///dev/i2c-0"                              (I2C-0 device)
      "spi:///dev/spidev0.0,5000,3"                   (/dev/spidev0.0 setting max speed to 5kHz and SPI mode to 3)
      "loop"                                          (Loopback endpoint named as 'loop', without backend)

##### int rteipc_bind(int ep_a, int ep_b)

rteipc_bind() connects two endpoints together. The return value is zero on success, otherwise -1. The argument _ep_a_, _ep_b_ are endpoint descriptors.

##### int rteipc_unbind(int ep)

rteipc_unbind() removes connection from two endpoints. The return value is zero on success, otherwise -1. The argument _ep_ is an endpoint descriptor bound.

##### void rteipc_dispatch(struct timeval *tv)

rteipc_dispatch() runs event dispatching loop. If the argument _tv_ is specified, exit the event loop after the specified time.

##### int rteipc_connect(const char *uri)

rteipc_connect() is used for a process to connect to an IPC or INET endpoint. The endpoint specified by the _uri_ must be created before this function call. The return value is a context descriptor on success, otherwise -1. The argument _uri_ is an endpoint pathname (see above).
A process can read from and write to IPC and INET endpoint. The data stored by the process in the endpoint will be available in the other endpoint to which it is bound, and vice versa.

##### int rteipc_setcb(int ctx, rteipc_read_cb read_cb, rteipc_err_cb err_cb, void *arg, short flag)

rteipc_setcb() changes read/error callbacks. The argument _arg_ can be used to pass data to the callbacks and _flag_ is a bitmask of flags. The argument _frag_ is not used for now.

##### int rteipc_send(int ctx, const void *buf, size_t len)

rteipc_send() is a generic helper function to transmit raw data to an endpoint. The argument _ctx_ is the context descriptor of the sending connection returned by rteipc_connect(). The data is found in _buf_ and has length _len_.

##### int rteipc_gpio_send(int ctx, uint8_t value)

rteipc_gpio_send() should be used to transmit data when the other end is GPIO endpoint. This sends data in a format specific to GPIO. The argument _ctx_ is the same as rtipc_send(). The argument _value_ is 1 (assert) or 0 (deassert).

##### int rteipc_spi_send(int ctx, const uint8_t *tx_buf, uint16_t len, bool rdmode)

rteipc_spi_send() should be used to transmit data when the other end is SPI endpoint. This sends data in a format specific to SPI. The argument _ctx_ is the same as rtipc_send(). The data is found in _tx_buf_ and has length _len_. The argument _rdmode_ determines if the endpoint reads SPI shift registers or not.

##### int rteipc_i2c_send(int ctx, uint16_t addr, const uint8_t *tx_buf, uint16_t wlen, uint16_t rlen)

rteipc_i2c_send() should be used to transmit data when the other end is I2C endpoint. This sends data in a format specific to I2C. The argument _ctx_ is the same as rtipc_send(). The data is found in _tx_buf_ and has length _wlen_. The argument _addr_ is I2C address and _rlen_ determines how many bytes the endpoint reads from the I2C device.

##### int rteipc_sysfs_send(int ctx, const char *attr, const char *value)

rteipc_sysfs_send() should be used to transmit data when the other end is SYSFS endpoint. This sends data in a format specific to SYSFS. The argument _ctx_ is the same as rtipc_send(). The argument _attr_ is the name of an attribute and _value_ is the new value of the attribute. If _value_ is NULL, read the current value of the attribute.

##### int rteipc_xfer(const char *name, const void *buf, size_t len)

rteipc_xfer() is equivalent to rteipc_send() but is a function dedicated for sending data to the LOOP endpoint. The argument _name_ is the name of the LOOP endpoint specified when calling rteipc_open().

##### int rteipc_gpio_xfer(const char *name, uint8_t value)

rteipc_gpio_xfer() is equivalent to rteipc_gpio_send() but is a function dedicated for sending data to the LOOP endpoint. The argument _name_ is the name of the LOOP endpoint specified when calling rteipc_open().

##### int rteipc_spi_xfer(const char *name, const uint8_t *tx_buf, uint16_t len, bool rdmode)

rteipc_spi_xfer() is equivalent to rteipc_spi_send() but is a function dedicated for sending data to the LOOP endpoint. The argument _name_ is the name of the LOOP endpoint specified when calling rteipc_open().

##### int rteipc_i2c_xfer(const char *name, uint16_t addr, const uint8_t *tx_buf, uint16_t wlen, uint16_t rlen)

rteipc_i2c_xfer() is equivalent to rteipc_i2c_send() but is a function dedicated for sending data to the LOOP endpoint. The argument _name_ is the name of the LOOP endpoint specified when calling rteipc_open().

##### int rteipc_sysfs_xfer(const char *name, const char *attr, const char *value)

rteipc_sysfs_xfer() is equivalent to rteipc_sysfs_send() but is a function dedicated for sending data to the LOOP endpoint. The argument _name_ is the name of the LOOP endpoint specified when calling rteipc_open().
