/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

            
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <list>

#include <wiredis/proto/raw.h>
#include <wiredis/types.h>

namespace nokia
{
    namespace net
    {

        class tcp_send_buffer_full: public std::runtime_error
        {
        public:
            template <typename... Ts>
            tcp_send_buffer_full(Ts &&... ts):
                std::runtime_error(std::forward<Ts>(ts)...)
            {}
        };
        
        
        template <typename parser = ::nokia::net::proto::raw::parser>
        class tcp_connection
        {
        public:

            uint64_t const SEND_BUFFER_LIMIT = 10485760; // 10 Megabyte. todo [w] Do we need so much?
                
            template <typename... Ts>
            tcp_connection(boost::asio::io_service & io_service, Ts &&... parser_args):
                _io_service(io_service),
                _socket(_io_service),
                _astate(astate::DISCONNECTED),
                _ostate(ostate::DISCONNECTED),
                _ip(""),
                _port(0),
                _auto_reconnect(true),
                _tcp_keepalive_enabled(true),
                _reconnect_wait(2),
                _parser(std::forward<Ts>(parser_args)...),
                _timer(_io_service),
                _send_buffer_size{0}
            {
            }


            ~tcp_connection()
            {
            }


            /*
             * connected_callback: called if connection is esablished or given up the connecting
             *     In case of auto_reconnect, the tcp-connection will initiate reconnecting.
             *     Error code is passed the the callback function.
             *     
             * disconnected_callback: invoked on any read error -> Connection must be connected first.
             *     This callback can be used for cleanup procedure.
             *     In case of auto_reconnect, the tcp-connection will initiate reconnecting.
             *     Error code is passed the the callback function.
             *     User initiated disconnection doesn't invoke the callback.
             *
             * read_callback: passing one protocol message to the user.
             *
             */
            void connect(std::string const & ip,
                         uint16_t port,
                         std::function<void (boost::system::error_code const &)> connected_callback,
                         std::function<void (boost::system::error_code const &)> disconnected_callback,
                         std::function<void (typename parser::protocol_message_type &&)> read_callback,
                         bool auto_reconnect = true,
                         bool tcp_keepalive_enabled = true,
                         bool tcp_user_timeout_enabled = true)
            {
                internal_connect(ip, port, connected_callback, disconnected_callback, read_callback, auto_reconnect, tcp_keepalive_enabled, tcp_user_timeout_enabled);
            }


            bool connected() const
            {
                return (astate::CONNECTED == _astate && ostate::CONNECTED == _ostate);
            }
            
            
            void disconnect()
            {
                // Called by user
                _io_service.dispatch([this] ()
                                     {
                                         _connected_callback = nullptr;
                                         _disconnected_callback = nullptr;
                                         _read_callback = nullptr;

                                         _timer.cancel();
                                         disconnect(true);
                                     });
            }


            void join(std::function<void ()> cb)
            {
                _io_service.dispatch([this, cb] ()
                                     {
                                         if (astate::DISCONNECTED == _astate && ostate::DISCONNECTED == _ostate)
                                         {
                                             cb();
                                         }
                                         else
                                         {
                                             // todo [w] cancel timers!
                                             _timer.expires_from_now(std::chrono::milliseconds(10));
                                             _timer.async_wait([this, cb] (boost::system::error_code const & error)
                                                                {
                                                                    if (::boost::asio::error::operation_aborted == error)
                                                                    {
                                                                        // Timer has been canceled.
                                                                        return;
                                                                    }
                                                                    join(cb);
                                                                });
                                         }
                                     });
            }

            
            void sync_join()
            {
                // Blocking join. Do not call from io_service thread!
                std::mutex mutex;
                std::unique_lock<std::mutex> guard(mutex);
                std::condition_variable cv;
                
                join([&] ()
                         {
                             std::unique_lock<std::mutex> guard(mutex);
                             cv.notify_one();
                         });
                cv.wait(guard);
            }

            
            void reconnect()
            {
                _io_service.dispatch([this] ()
                                     {
                                         disconnect(false); // Preserve CONNECTED administration state
                                         if (!_auto_reconnect)
                                         {
                                             return;
                                         }
                                         _timer.expires_from_now(std::chrono::seconds(_reconnect_wait));
                                         _timer.async_wait([this] (boost::system::error_code const & error)
                                                           {
                                                                
                                                               if (::boost::asio::error::operation_aborted == error)
                                                               {
                                                                   // Timer has been canceled.
                                                                   return;
                                                               }
                                                               // Meanwhile user has disconnected
                                                               if (astate::DISCONNECTED == _astate)
                                                               {
                                                                   return;
                                                               }
                                                               
                                                               internal_connect(_ip,
                                                                       _port,
                                                                       _connected_callback,
                                                                       _disconnected_callback,
                                                                       _read_callback,
                                                                       _auto_reconnect,
                                                                       _tcp_keepalive_enabled,
                                                                       _tcp_user_timeout_enabled);
                                                           });
                                     });

            }

            void send(std::string && buffer)
            {
                bool send_now = true;
                {
                    std::unique_lock<std::mutex> guard(_send_buffer_mutex);
                    send_now = _send_buffer.empty();

                    if (_send_buffer_size + buffer.size() > SEND_BUFFER_LIMIT)
                    {
                        throw tcp_send_buffer_full("ERROR: TCP send buffer is full. Current limit is: " + std::to_string(SEND_BUFFER_LIMIT));
                    }
                    _send_buffer_size += buffer.size();

                    // we can send now if the sending buffer is empty, otherwise the send-callback will do that.
                    _send_buffer.emplace_back(std::move(buffer));
                }

                if (send_now)
                {
                    _io_service.dispatch([this] ()
                                         {
                                             try_to_send();
                                         });
                }
            }
            

            
        protected:


            void internal_connect(std::string const & ip,
                         uint16_t port,
                         std::function<void (boost::system::error_code const &)> connected_callback,
                         std::function<void (boost::system::error_code const &)> disconnected_callback,
                         std::function<void (typename parser::protocol_message_type &&)> read_callback,
                         bool auto_reconnect,
                         bool tcp_keepalive_enabled,
                         bool tcp_user_timeout_enabled)
            {
                _astate = astate::CONNECTED;
                _ostate = ostate::CONNECTING;
                _ip = ip;
                _port = port;
                _connected_callback = connected_callback;
                _disconnected_callback = disconnected_callback;
                _read_callback = read_callback;
                _auto_reconnect = auto_reconnect;
                _tcp_keepalive_enabled = tcp_keepalive_enabled;
                _tcp_user_timeout_enabled = tcp_user_timeout_enabled;

                _socket.open(boost::asio::ip::tcp::v4());
                set_socket_options();
                    
                boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
                _socket.async_connect(endpoint,
                                      [this] (boost::system::error_code const & error)
                                      {
                                          if (error)
                                          {
                                              _ostate = ostate::DISCONNECTED;
                                              if (_connected_callback)
                                              {
                                                  _connected_callback(error);
                                              }
                                              reconnect();
                                              return;
                                          }
                                          else
                                          {
                                              _ostate = ostate::CONNECTED;
                                              _send_buffer.clear();
                                              _send_buffer_size = 0;
                                              
                                              ::nokia::net::proto::char_buffer const & buffer = _parser.buffer();
                                              _socket.async_read_some(boost::asio::buffer(buffer.ptr, buffer.size),
                                                                      std::bind(&tcp_connection::on_read, this, std::placeholders::_1, std::placeholders::_2));
                                              if (_connected_callback)
                                              {
                                                  _connected_callback(error);
                                              }
                                          }
                                      });
            }


            void try_to_send(std::size_t start_byte = 0)
            {
                if (!connected())
                {
                    return;
                }

                std::unique_lock<std::mutex> guard(_send_buffer_mutex);
                if (_send_buffer.empty())
                {
                    return;
                }
                std::string & first_message = _send_buffer.front();
                std::size_t message_length = first_message.size() - start_byte;

                _socket.async_write_some(boost::asio::buffer(&first_message[start_byte], message_length),
                                         [this, start_byte, message_length] (boost::system::error_code const & error,
                                                                             std::size_t bytes_transferred)
                                         {
                                             if (!connected())
                                             {
                                                 // do nothing
                                                 return;
                                             }
                                             if (error)
                                             {
                                                 if (_disconnected_callback)
                                                 {
                                                     _disconnected_callback(error);
                                                 }
                                                 reconnect();
                                                 return;
                                             }
                                             
                                             if (message_length != bytes_transferred)
                                             {
                                                 // Some bytes haven't sent but we are still connected
                                                 // -> Resend the missing part
                                                 try_to_send(start_byte+bytes_transferred);
                                                 return;
                                             }
                                             // First message sent successfully
                                             bool need_to_recall = false;
                                             {
                                                 std::unique_lock<std::mutex> guard(_send_buffer_mutex);
                                                 _send_buffer_size -= _send_buffer.front().size();
                                                 _send_buffer.pop_front();
                                                 need_to_recall = !_send_buffer.empty();
                                             }
                                             if (need_to_recall)
                                             {
                                                 try_to_send();
                                             }
                                             return;
                                         });
            }
            
            
            void disconnect(bool end)
            {
                if (end)
                {
                    _astate = astate::DISCONNECTED;
                }
                try
                {
                    _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
                }
                catch (...)
                {
                    // Doesn't matter
                }
                _socket.close();
                _ostate = ostate::DISCONNECTED;
            }
            

            void on_read(boost::system::error_code const & error,
                         std::size_t bytes_transferred)
                
            {
                if (boost::asio::error::operation_aborted == error)
                {
                    // We closed the socket, do nothing
                    return;
                }
                if (error)
                {
                    if (_disconnected_callback)
                    {
                        _disconnected_callback(error);
                    }
                    reconnect();
                    return;
                }

                try
                {
                    ::nokia::net::proto::char_buffer const & buffer = _parser.on_read(bytes_transferred, _read_callback);
                    _socket.async_read_some(boost::asio::buffer(buffer.ptr, buffer.size),
                                            std::bind(&tcp_connection::on_read, this, std::placeholders::_1, std::placeholders::_2));
                }
                catch (parse_error const &)
                {
                    // We couldn't parse the message, so there must be some problem with the communication.
                    if (_disconnected_callback)
                    {
                        _disconnected_callback(boost::asio::error::invalid_argument);
                    }
                    reconnect();
                    return;
                }
            }


            void set_socket_options()
            {
                /*
                 * TCP_SYNCNT: Number of SYN retries.
                 * 
                 * TCP_KEEPIDLE: overrides tcp_keepalive_time. The interval between the last data packet sent
                 *   (simple ACKs are not considered data) and the first keepalive probe; after the connection is marked to need keepalive, this counter is not used any further.
                 * TCP_KEEPINTVL: overrides tcp_keepalive_intvl. The interval between subsequential keepalive probes, regardless of what the connection has exchanged in the meantime.
                 * TCP_KEEPCNT: overrides tcp_keepalive_probes. The number of unacknowledged probes to send before considering the connection dead and notifying the application layer.
                 *
                 * TCP_USER_TIMEOUT: how long could be a packet unack'ed in milliseconds
                 * Note: the timout check is bound with retransmission try (exponential),
                 *       so the timer fire (connection close) won't be accurate.
                 *       See: https://lore.kernel.org/patchwork/patch/960970/
                 */

                auto native_socket = _socket.native_handle();
                int optval{2};
                socklen_t optlen = sizeof(optval);
                setsockopt(native_socket, IPPROTO_TCP, TCP_SYNCNT, &optval, optlen);
                
                if (_tcp_keepalive_enabled)
                {
                    // turn on
                    boost::asio::socket_base::keep_alive keep_alive_option(true);
                    _socket.set_option(keep_alive_option);
                
                    // set config (can't be done via boost::asio, it's posix standard, not portable)
                    optval = 2;
                    setsockopt(native_socket, SOL_TCP, TCP_KEEPIDLE, &optval, optlen);
                    optval = 2;
                    setsockopt(native_socket, SOL_TCP, TCP_KEEPINTVL, &optval, optlen);
                    optval = 3;
                    setsockopt(native_socket, SOL_TCP, TCP_KEEPCNT, &optval, optlen);
                }
                if (_tcp_user_timeout_enabled)
                {
                    optval = 6000;
                    setsockopt(native_socket, SOL_TCP, TCP_USER_TIMEOUT, &optval, optlen);
                }
                
            }
            
            
        private:


            boost::asio::io_service & _io_service;
            boost::asio::ip::tcp::socket _socket;
            astate _astate;
            ostate _ostate;

            std::string _ip;
            uint16_t _port;
            std::function<void (boost::system::error_code const &)> _connected_callback;
            std::function<void (boost::system::error_code const &)> _disconnected_callback;
            std::function<void (typename parser::protocol_message_type &&)> _read_callback;

            bool _auto_reconnect;
            bool _tcp_keepalive_enabled;
            bool _tcp_user_timeout_enabled;
            uint64_t _reconnect_wait; // Before automatic reconnect, wait this amount of seconds.

            parser _parser;

            boost::asio::steady_timer _timer;

            std::list<std::string> _send_buffer;
            uint64_t _send_buffer_size;
            std::mutex _send_buffer_mutex;
            
        };
    }
}

