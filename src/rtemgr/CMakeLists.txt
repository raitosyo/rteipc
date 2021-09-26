# Copyright (c) 2021 Ryosuke Saito All rights reserved.
# MIT licensed
#

project(rtemgr)
set(PACKAGE_VERSION 0.0.1)

set(RTEMGR_INCLUDES
    rtemgr-common.h
    ../message.h
    ../ep.h
    ../list.h)

if (RTEIPC_STATIC_LIB)
  add_library(rtemgr-lib STATIC librtemgr.c ${RTEMGR_INCLUDES})
else ()
  add_library(rtemgr-lib SHARED librtemgr.c ${RTEMGR_INCLUDES})
  set_target_properties(rtemgr-lib PROPERTIES
				SOVERSION "1"
				VERSION "1.0.0")
endif ()

set_target_properties(rtemgr-lib PROPERTIES OUTPUT_NAME rtemgr)
set_target_properties(rtemgr-lib PROPERTIES PUBLIC_HEADER rtemgrcli.h)

add_executable(rtemgrd rtemgrd.c ${RTEMGR_INCLUDES})
add_executable(rtemgr rtemgr.c ${RTEMGR_INCLUDES})

configure_file(rtemgr.pc.in ${PROJECT_NAME}.pc @ONLY)
install (
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

find_package(PkgConfig)

pkg_check_modules(LIBYAML REQUIRED yaml-0.1)

set(LIBB64_LIBRARIES b64)
find_library(HAS_LIBB64 ${LIBB64_LIBRARIES})
if (NOT HAS_LIBB64)
  message(FATAL_ERROR "Error: Missing required library - ${LIBB64_LIBRARIES}")
endif ()

target_link_libraries(rtemgr-lib LINK_PUBLIC rteipc ${LIBYAML_LIBRARIES} ${LIBB64_LIBRARIES})
target_link_libraries(rtemgrd LINK_PUBLIC rteipc rtemgr-lib)
target_link_libraries(rtemgr LINK_PUBLIC rteipc rtemgr-lib)

install(TARGETS rtemgrd RUNTIME DESTINATION bin)
install(TARGETS rtemgr RUNTIME DESTINATION bin)
install(TARGETS rtemgr-lib
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
