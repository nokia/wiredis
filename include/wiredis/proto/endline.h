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

            namespace endline
            {
                class parser: public ::nokia::net::proto::parser_base<std::string>
                {
                public:

                    using protocol_message_type = std::string;
                
                    parser(std::size_t buffer_size):
                        ::nokia::net::proto::parser_base<std::string>(buffer_size)
                    {
                    }

                    std::size_t parse(protocol_message_type & message, char * buffer, std::size_t size) override
                    {
                        std::size_t index{0};
                        while (index < size && buffer[index] != '\n')
                        {
                            ++index;
                        }
                        if (index >= size)
                        {
                            return 0;
                        }
                        message = std::string(buffer, size-1);
                        return index + 1;
                    }

                protected:
                private:
                };
                
            }
        }
    }
}
