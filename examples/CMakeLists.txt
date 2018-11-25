#
# Licensed under BSD-3-Clause License
# © 2018 Nokia
#

cmake_minimum_required(VERSION 3.7.2)

project(wiredis-examples CXX)

find_package(Boost 1.51 REQUIRED)

add_executable(operation-example operation.cpp)
target_link_libraries(operation-example boost_system pthread)

add_executable(watch-example watch.cpp)
target_link_libraries(watch-example boost_system pthread)
