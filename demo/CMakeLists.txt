# Copyright (c) 2018 Ryosuke Saito All rights reserved.
# MIT licensed
#

add_executable(hello hello.c)
add_executable(relay relay.c)
add_executable(broadcast broadcast.c)

target_link_libraries(hello LINK_PUBLIC rteipc)
target_link_libraries(relay LINK_PUBLIC rteipc)
target_link_libraries(broadcast LINK_PUBLIC rteipc)

install(TARGETS hello RUNTIME DESTINATION bin)
install(TARGETS relay RUNTIME DESTINATION bin)
install(TARGETS broadcast RUNTIME DESTINATION bin)
