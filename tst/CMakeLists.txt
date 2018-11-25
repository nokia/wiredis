#
# Licensed under BSD-3-Clause License
# © 2018 Nokia
#

include_directories(BEGIN ../include/)
include_directories(BEGIN include/)

if (MEMORYCHECK_COMMAND STREQUAL "MEMORYCHECK_COMMAND-NOTFOUND")
    message(FATAL_ERROR "FYA: Can't find valgrind, if you execute tests the result won't be valid!")
endif()

add_subdirectory(redis-connection)
add_subdirectory(tcp-connection)
