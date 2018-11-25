/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia 
 */
#pragma once

#include <iostream>
#include <thread>
#include <chrono>
#include <functional>

#define MUST_BE_ZERO(cmd) \
    { \
        auto return_code = cmd; \
        if (0 != return_code) \
        { \
            std::cerr << "Exit code of command \"" << #cmd "\" isn't 0. Exiting..." << std::endl; \
            exit(-1); \
        } \
    }


inline void msleep(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

inline bool wait_for_true(bool & var, uint32_t timeout_ms)
{
    uint32_t counter{0};
    uint32_t const sleep_time{100};
    while (!var && ++counter < timeout_ms/sleep_time)
    {
        msleep(sleep_time);
    }
    return var;
}


inline bool wait_for_true(std::function<bool ()> func, uint32_t timeout_ms, uint32_t sleep_time_ms = 100)
{
    uint32_t counter{0};
    bool result = func();
    while (!result && ++counter < timeout_ms/sleep_time_ms)
    {
        msleep(sleep_time_ms);
        result = func();
    }
    std::cout << "wait_for_true: took " << sleep_time_ms*counter << " milliseconds" << std::endl;
    return result;
}


// uint32_t execute_command(std::string const & command)
// {
//     std::cout << "* Executing command: " << command << std::endl;
//     auto result = system(command.c_str());
//     std::cout << "* Result of command: " << result << std::endl << std::endl;
//     return result;
// }


static void start_server()
{
    system("(redis-server &) &> /tmp/redis.server.out");
    msleep(1000);
    int result(1);
    while (result)
    {
        result = system("redis-cli get dummy_key");
        if (result)
        {
            msleep(1000);
        }
    }
    system("ps aux");
    std::cout << "redis-server is working well" << std::endl;
}



static void stop_server()
{
    system("killall redis-server");
    msleep(500);
}
