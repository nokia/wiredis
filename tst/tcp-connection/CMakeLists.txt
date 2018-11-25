#
# Licensed under BSD-3-Clause License
# © 2018 Nokia
#

add_executable(tcp-connection-ut ut.cpp)
target_link_libraries(tcp-connection-ut boost_system pthread gtest)

add_test(NAME tcp-connection-ut COMMAND tcp-connection-ut)
