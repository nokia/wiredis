/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

namespace nokia
{
    namespace net
    {
        namespace proto
        {

            struct char_buffer
            {
                char * ptr;
                std::size_t size;
            };

            
            template <typename protocol_message_type>
            class parser_base
            {
            public:
                parser_base(std::size_t buffer_size):
                    _buffer_size(buffer_size),
                    _used_bytes(0),
                    _buffer(new char[_buffer_size]),
                    _char_buffer{_buffer.get(), _buffer_size}
                {
                }
                
                parser_base(parser_base const &) = delete;
                parser_base & operator=(parser_base const &) = delete;
                

                parser_base(parser_base && rhs)
                {
                    local_move(std::move(rhs));
                }
                
                parser_base & operator=(parser_base && rhs)
                {
                    if (&rhs != this)
                    {
                        local_move(std::move(rhs));
                    }
                    return *this;
                }
                
                virtual ~parser_base() {}
                
                virtual std::size_t parse(protocol_message_type & message, char * buffer, std::size_t size) = 0;
                
                /*
                 * called by connection
                 * check if it's a full message then call the ready callback
                 */
                char_buffer const & on_read(std::size_t read_bytes, std::function<void (protocol_message_type &&)> on_read_callback)
                {
                    _used_bytes += read_bytes;
                    assert(_used_bytes <= _buffer_size);
                    char * ptr = _buffer.get();
                    
                    std::size_t length = parse(_message, ptr, _used_bytes);
                    
                    while (length > 0)
                    {
                        if (on_read_callback)
                        {
                            on_read_callback(std::move(_message));
                        }
                        ptr += length;
                        _used_bytes -= length;
                        if (_used_bytes == 0)
                        {
                            break;
                        }
                        length = parse(_message, ptr, _used_bytes);
                    }
                    if (ptr != _buffer.get())
                    {
                        // At lease one message was parsed
                        // -> ptr is pointing to the first unparsed byte
                        // -> _used_bytes is the number of unparsed bytes
                        memmove(_buffer.get(), ptr, _used_bytes);
                    }

                    return buffer();
                }

                // return a pointer to the first usable byte in the buffer
                char_buffer const & buffer()
                {
                    _char_buffer.ptr = _buffer.get() + _used_bytes;
                    _char_buffer.size = _buffer_size - _used_bytes;
                    return _char_buffer;
                }

            protected:
                
            private:
                std::size_t _buffer_size;
                std::size_t _used_bytes;
                std::unique_ptr<char[]> _buffer;
                char_buffer _char_buffer;
                
                std::function<void (char const * buffer, std::size_t size)> _proto_message_ready_callback;

                protocol_message_type _message;

                void local_move(parser_base && rhs)
                {
                    _buffer_size = rhs._buffer_size;
                    _used_bytes = rhs._used_bytes;
                    _buffer.swap(rhs._buffer);
                    _char_buffer = rhs._char_buffer;
                    _proto_message_ready_callback = rhs._proto_message_ready_callback;

                    rhs._buffer_size = 0;
                    rhs._used_bytes = 0;
                    rhs._char_buffer = {0, 0};
                    rhs._proto_message_ready_callback = nullptr;
                }
            };
        }
    }
}
