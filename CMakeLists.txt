# Copyright (c) 2018 - 2021 Ryosuke Saito All rights reserved.
# MIT licensed
#

cmake_minimum_required(VERSION 3.1)

cmake_policy(SET CMP0057 NEW)

project(rteipc)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckStructHasMember)
include(CheckLibraryExists)
include(CheckCSourceCompiles)
include(GNUInstallDirs)

set(PACKAGE_VERSION 0.2.2)

option(RTEIPC_STATIC_LIB "Build static library instead of shared library." OFF)

#create Config files to help other lbiraries to find the package
include(CMakePackageConfigHelpers)
set(config_package_location cmake)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rteipcConfigVersion.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)

if (RTEIPC_STATIC_LIB)
	add_definitions (-DRTEIPC_STATIC_LIB)
endif ()

install(TARGETS LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

configure_file(rteipc.pc.in ${PROJECT_NAME}.pc @ONLY)
install (
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

add_subdirectory(src)

# Uncomment if you want to build rtemgr
#add_subdirectory(src/rtemgr)

# Uncomment if you want to build demo applications
#add_subdirectory(demo)
