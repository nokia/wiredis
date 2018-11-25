/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

#include <wiredis/proto/base.h>

namespace nokia
{
    namespace net
    {
        namespace proto
        {
            namespace raw
            {
                class parser: public ::nokia::net::proto::parser_base<char_buffer>
                {
                public:
                    using protocol_message_type = char_buffer;
                
                    parser(std::size_t buffer_size):
                        ::nokia::net::proto::parser_base<char_buffer>(buffer_size)
                    {
                    }

                    std::size_t parse(char_buffer & message, char * buffer, std::size_t size) override
                    {
                        if (0 < size)
                        {
                            message.ptr = buffer;
                            message.size = size;
                        }
                        return size;
                    }

                protected:
                private:
                };
            }
        }
    }
}
