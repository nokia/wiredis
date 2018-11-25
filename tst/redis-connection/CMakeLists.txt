#
# Licensed under BSD-3-Clause License
# © 2018 Nokia
#

add_executable(redis-connection-ut ut.cpp)
target_link_libraries(redis-connection-ut boost_system pthread gtest)

add_test(NAME redis-connection-ut COMMAND redis-connection-ut)
