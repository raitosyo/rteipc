# Copyright (c) 2018 Ryosuke Saito All rights reserved.
# MIT licensed
#

set(RTEIPC_SOURCES
    rteipc.h
    ep.h
    message.h
    list.h
    table.h

    base.c
    connect.c
    switch.c
    message.c
    list.c
    table.c
    ep.c
    ep/ep_ipc.c
    ep/ep_tty.c
    ep/ep_gpio.c
    ep/ep_spi.c)

if (RTEIPC_STATIC_LIB)
    add_library(${PROJECT_NAME} STATIC ${RTEIPC_SOURCES})
else ()
    add_library(${PROJECT_NAME} SHARED ${RTEIPC_SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          SOVERSION "1"
                          VERSION "1.0.0")
endif ()

target_include_directories(rteipc PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR};${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:include>")

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER rteipc.h)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(PkgConfig)
find_package(Threads REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent libevent_pthreads)
pkg_check_modules(LIBGPIOD REQUIRED libgpiod)

target_link_libraries(rteipc ${LIBEVENT_LIBRARIES} ${LIBGPIOD_LIBRARIES})
