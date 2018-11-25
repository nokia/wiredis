/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

#include <exception>

namespace nokia
{
    namespace net
    {

        enum class astate
        {
            DISCONNECTED,
            CONNECTED
        };

        enum class ostate
        {
            DISCONNECTED,
            CONNECTING,
            CONNECTED
        };
        
        class parse_error: public std::runtime_error
        {
        public:
            template <typename... Ts>
            parse_error(Ts &&... ts):
                std::runtime_error(std::forward<Ts>(ts)...)
            {}
        };
    }
}
