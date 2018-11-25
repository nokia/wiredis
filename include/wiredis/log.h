/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

#include <boost/format.hpp>
#include <string>

namespace detail
{

    inline boost::format concatenate_format(boost::format const & format)
    {
        return boost::format(format);
    }

    template <typename T>
    boost::format concatenate_format(boost::format const & format, T && t)
    {
        return boost::format(format) % t;
    }

    template <typename T, typename... Ts>
    boost::format concatenate_format(boost::format const & format, T && t, Ts &&... args)
    {
        return concatenate_format(boost::format(format) % t, args...);
    }

    template <typename... Ts>
    std::string concatenate(std::string const & format, Ts &&... args)
    {
        return concatenate_format(boost::format(format), args...).str();
    }
}

