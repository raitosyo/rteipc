# User-defined routing IPC library

rteipc is a simple user-defined routing IPC library; uses libevent.

## building rteipc

To create an build in the project tree:

    mkdir build && cd build
    cmake ../
    cmake --build .

## what's rteipc?

When working on embedded systems, we sometimes see the bords running different
instances of operating system i.g. Linux and real-time OS such as FreeRTOS.
These bords might have two SoCs connected each other by UART with one running
Linux and the other running real-time OS, or a modern SoC employing
heterogeneous remote processor devices in AMP configurations which can run
real-time OS inside it. But in any case, a process running in Linux usually
needs to communicate with a task running in real-time OS.

The interface with real-time OS will be provided as TTY(/dev/tty*) or any other
specific device file in Linux depending on the implementation of the board
and/or the driver software.
So we need to write a process sending/receiving data to/from the interface with
real-time OS and also communicating with other processes in Linux if necessary.

rteipc is for them to do that.

## communicating with real-time OS
This is a very simple code snippet for communicating with real-time OS, assuming
that OpenAMP/RPMsg is used and TTY driver(/dev/ttyRPMSG) is provided.


    #include "rteipc.h"

    ...
    rteipc_init(NULL);
    /* Create IPC endpoint for Linux*/
    int ep1 = rteipc_ep_open("ipc://@rteipc");
    /* Create TTY endpoint for RTOS */
    int ep2 = rteipc_ep_open("tty:///dev/ttyRPMSG,115200");
    /* Add a route between two endpoints */
    rteipc_ep_route(ep1, ep2, RTEIPC_BIDIRECTIONAL);
    
    /* Connect to IPC endpoint */
    int ctx = rteipc_connect("ipc://@rteipc", NULL, NULL);

***
The above codes setup:

                           [IPC endpoint]
                           |----------------------|   tx
    Linux process <------> | (abstract namespace) |<---------|
                   Socket  |    "@rteipc"         |<--------||
                           |----------------------|   rx    ||
                                                            || bidirectional
                           [TTY endpoint]                   ||
                           |----------------------|   tx    ||
    RTOS task <----------> |   "/dev/ttyRPMSG"    |<--------||
                 OpenAMP   |                      |<---------|
                 (RPMsg)   |----------------------|   rx


***
Then we can send data:

    ...
    rteipc_send(ctx, "Hello", strlen("Hello"));
    /* Run event dispatch loop */
    rteipc_dispatch();