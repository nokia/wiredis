/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

#include <iostream>

#include <wiredis/proto/base.h>
#include <wiredis/types.h>



/*
 * todo [w]
 *
 * - write unit tests for the parser
 * - write move operator for reply and disable copy
 *
 */

namespace nokia
{
    namespace net
    {
        namespace proto
        {
            namespace redis
            {
                
                struct reply
                {
                    enum type
                    {
                        INVALID, // [w] only for debugging purpose
                        STRING,
                        INTEGER,
                        ARRAY,
                        NIL,
                        ERROR
                    };
                    
                    type type;
                    
                    std::string str;
                    int64_t integer;

                    std::vector<reply> elements;
                    
                    reply():
                        type(INVALID),
                        integer(0)
                    {}

                };

                // std::ostream & operator<<(std::ostream & out, reply & reply)
                // {
                //     out << std::endl
                //         << "type: " << (int)reply.type << std::endl
                //         << "str: " << reply.str << std::endl
                //         << "integer: " << reply.integer;
                //     for (auto i=0u; i<reply.elements.size(); ++i)
                //     {
                //         out << std::endl;
                //         out << reply.elements[i];
                //     }
                //     return out;
                // }

                class parser: public ::nokia::net::proto::parser_base<reply>
                {
                public:

                    using protocol_message_type = reply;
                
                    parser(std::size_t buffer_size):
                        ::nokia::net::proto::parser_base<reply>(buffer_size)
                    {
                    }

                
                    std::size_t parse(reply & reply, char * buffer, std::size_t size) override
                    {
                        // index is pointing to the next unprocessed byte
                        reply.type = reply::INVALID;
                    
                        reply.str = "";
                        reply.integer = 0;
                        reply.elements.clear();
                    
                        std::size_t index{0};
                        // std::cout << "reply: " << std::string(buffer, size) << std::endl;
                        index = parse(reply, buffer, size, index);
                        // std::cout << "index: " << index << std::endl;
                        // returning with the index of the next unprocessed byte
                        return index;
                    }

                protected:


                    std::size_t parse(reply & reply, char * buffer, std::size_t size, std::size_t index)
                    {
                        switch (buffer[index])
                        {
                            case '+':
                                index = parse_simple_string(reply, buffer, size, index);
                                break;
                            case '-':
                                index = parse_error_string(reply, buffer, size, index);
                                break;
                            case ':':
                                index = parse_integer(reply, buffer, size, index);
                                break;
                            case '$':
                                index = parse_bulk_string(reply, buffer, size, index);
                                break;
                            case '*':
                                index = parse_array(reply, buffer, size, index);
                                break;
                            default:
                                std::string error_message = "unknown message: " + std::string(&buffer[index], size-index); 
                                throw parse_error(error_message);
                        }
                        // std::cout << "index: " << index << std::endl;
                        // returning with the index of the next unprocessed byte
                        return index;
                    }


                    std::size_t parse_simple_string(reply & reply, char * buffer, std::size_t size, std::size_t index)
                    {
                        // simple string is terminated with "\r\n" and the string itself can't contain neither '\r' nor '\n'
                        // std::cout << "parse_simple_string" << std::endl;
                        if (size-index < 3)
                        {
                            // Minimal requirement: "+\r\n"
                            return 0;
                        }
                        // std::cout << "min cond met" << std::endl;
                        std::size_t start_index = index;
                    
                        while (index < size && buffer[index] != '\n')
                        {
                            ++index;
                        }
                        // std::cout << "index=" << index << std::endl;
                        if (index >= size)
                        {
                            // not enought read bytes
                            return 0;
                        }
                        // std::cout << "enough bytes to parse" << std::endl;
                        if ('\r' != buffer[index-1])
                        {
                            throw parse_error("");
                        }
                        reply.type = reply::STRING;
                        reply.str = std::move(std::string(&buffer[start_index+1], index-2-start_index));
                        return index+1;
                    
                    }

                    std::size_t parse_error_string(reply & reply, char * buffer, std::size_t size, std::size_t index)
                    {
                        index = parse_simple_string(reply, buffer, size, index);
                        reply.type = reply::ERROR;
                        return index;
                    }
                

                    std::size_t parse_integer(reply & reply, char * buffer, std::size_t size, std::size_t index)
                    {
                        int64_t integer{0};
                        index = get_integer(integer, buffer, size, index);
                        if (0 == index)
                        {
                            return 0;
                        }
                        reply.type = reply::INTEGER;
                        reply.integer = integer;
                        return index;
                    
                    }

                
                    std::size_t parse_bulk_string(reply & reply, char * buffer, std::size_t size, std::size_t index)
                    {
                        // "$6\r\nfoobar\r\n"
                        int64_t integer{0};
                        index = get_integer(integer, buffer, size, index);
                        if (0 == index)
                        {
                            return 0;
                        }
                    
                        if (-1 == integer)
                        {
                            // nil bulk string
                            reply.type = reply::NIL;
                            return index;
                        }
                    
                        if (index+integer+2 > size)
                        {
                            // We don't have enough bytes
                            return 0;
                        }
                    
                        if ('\r' != buffer[index+integer] || '\n' != buffer[index+integer+1])
                        {
                            throw parse_error("");
                        }
                        reply.type = reply::STRING;
                        // std::cout << "bulk string, index=" << index << ", integer=" << integer << std::endl;
                        reply.str = std::string(&buffer[index], integer);
                        return index+integer+2;
                    
                    }


                    std::size_t parse_array(reply & reply, char * buffer, std::size_t size, std::size_t index)
                    {
                        int64_t integer{0};
                        index = get_integer(integer, buffer, size, index);
                        if (0 == index)
                        {
                            return 0;
                        }
                    
                        if (-1 == integer)
                        {
                            // nil array
                            reply.type = reply::NIL;
                            return index;
                        }
                    
                        reply.type = reply::ARRAY;
                        // std::cout << "array length: " << integer << std::endl;
                        reply.elements.reserve(integer);
                        // std::cout << "size: " << reply.elements.size() << std::endl;
                        for (auto i=0u; i<integer; ++i)
                        {
                            reply.elements.push_back(::nokia::net::proto::redis::reply());
                            index = parse(reply.elements[i], buffer, size, index);
                            if (0 == index)
                            {
                                break;
                            }
                        }
                        return index;
                    }


                    std::size_t get_integer(int64_t & integer, char * buffer, std::size_t size, std::size_t index)
                    {
                        integer = 0;
                        if (size-index < 4)
                        {
                            // Minimal requirement: ":i\r\n"
                            return 0;
                        }
                        ++index;
                        bool minus{false};
                        if ('-' == buffer[index])
                        {
                            minus = true;
                            ++index;
                        }
                        while (index < size && buffer[index] != '\n')
                        {
                            if (buffer[index] != '\r')
                            {
                                integer = (integer * 10) + (buffer[index] - '0');
                            }
                            ++index;
                        }
                        if (index >= size)
                        {
                            // not enough read bytes
                            return 0;
                        }
                        if ('\r' != buffer[index-1])
                        {
                            throw parse_error("");
                        }
                        if (minus)
                        {
                            integer *= -1;
                        }
                        return index+1;
                    }
                
                private:
                };
                
            } // end of redis
        } // end of proto

        
        namespace parser
        {

        }
    }
}
