#
# Licensed under BSD-3-Clause License
# © 2018 Nokia
#
    
cmake_minimum_required(VERSION 3.7.2)

project(wiredis CXX)

set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
else()
    message(STATUS "Build type is '${CMAKE_BUILD_TYPE}'")
endif()
        
find_package(Boost 1.51 REQUIRED)

find_program(MEMORYCHECK_COMMAND valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS " --error-exitcode=8 --vgdb=no --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite" )
include(CTest)
    
include_directories(BEFORE include)

set(CMAKE_INSTALL_PREFIX /usr/)
install(DIRECTORY include/wiredis DESTINATION include FILES_MATCHING PATTERN "*.h")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_subdirectory(tst)
endif()
