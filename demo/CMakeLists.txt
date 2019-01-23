# Copyright (c) 2018 Ryosuke Saito All rights reserved.
# MIT licensed
#

add_executable(hello hello.c)
add_executable(switch switch.c)

target_link_libraries(hello LINK_PUBLIC rteipc)
target_link_libraries(switch LINK_PUBLIC rteipc)

install(TARGETS hello RUNTIME DESTINATION bin)
install(TARGETS switch RUNTIME DESTINATION bin)
