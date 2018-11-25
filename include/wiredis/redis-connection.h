/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#pragma once

            
#include <deque>
#include <algorithm>
#include <string>

#include <wiredis/tcp-connection.h>
#include <wiredis/proto/redis.h>
#include <wiredis/log.h>

namespace nokia
{
    namespace net
    {

        class redis_connection
        {
        public:

            std::string const ERROR_TCP_DISCONNECTED;
            std::string const ERROR_TCP_CANNOT_SEND_MESSAGE;

            
            class subscription_already_exists: public std::runtime_error
            {
            public:
                template <typename... Ts>
                subscription_already_exists(Ts &&... ts):
                    std::runtime_error(std::forward<Ts>(ts)...)
                {}
            };


            class subscription_does_not_exist: public std::runtime_error
            {
            public:
                template <typename... Ts>
                subscription_does_not_exist(Ts &&... ts):
                    std::runtime_error(std::forward<Ts>(ts)...)
                {}
            };
            
            
            redis_connection(boost::asio::io_service & io_service):
                ERROR_TCP_DISCONNECTED{"TCP DISCONNECTED"},
                ERROR_TCP_CANNOT_SEND_MESSAGE{"TCP CANNOT SEND MESSAGE"},
                _tcp(io_service, 10240),
                _pubsub_mode(false)
            {
            }


            ~redis_connection()
            {
            }


            void set_log_callback(std::function<void (std::string const &)> cb)
            {
                _log_callback = cb;
            }


            void connect(std::string const & ip,
                         uint16_t port,
                         std::function<void (boost::system::error_code const &)> connected_callback,
                         std::function<void (boost::system::error_code const &)> disconnected_callback,
                         bool auto_reconnect = true,
                         bool keepalive_enabled = true)
            {
                _ip = ip;
                _port = port;
                _connected_callback = connected_callback;
                _disconnected_callback = disconnected_callback;
                _tcp.connect(ip,
                             port,
                             std::bind(&redis_connection::on_connected, this, std::placeholders::_1),
                             std::bind(&redis_connection::on_disconnected, this, std::placeholders::_1),
                             std::bind(&redis_connection::on_read, this, std::placeholders::_1),
                             auto_reconnect,
                             keepalive_enabled);
            }

            
            void disconnect()
            {
                _tcp.disconnect();
                _connected_callback = nullptr;
                _disconnected_callback = nullptr;
            }


            bool connected() const
            {
                return _tcp.connected();
            }
            

            void join(std::function<void ()> cb)
            {
                _tcp.join(cb);
            }

            
            void sync_join()
            {
                _tcp.sync_join();
            }

            
            template <typename... Ts>
            void execute(std::function<void (::nokia::net::proto::redis::reply &&)> callback, Ts &&... ts)
            {
                // todo [w] Throw exception if we are in pubsub mode and get non-proper command.
                // todo [w] Guard the _op_callbacks. Right now it's not an issue, since all redis
                //          related calls are coming from io_service, but that would be the
                //          generic solution.

                if (!_tcp.connected())
                {
                    ::nokia::net::proto::redis::reply error_reply;
                    error_reply.type = ::nokia::net::proto::redis::reply::ERROR;
                    error_reply.str = ERROR_TCP_CANNOT_SEND_MESSAGE;
                    callback(std::move(error_reply));
                    return;
                }
                
                std::string message = "*" + std::to_string(sizeof...(ts)) + "\r\n";
                append_bulk_string(message, ts...);
                // std::cout << "message to be sent: " << message << std::endl;
                if (nullptr != callback)
                {
                    // unsubscribe commands are handled different
                    _op_callbacks.emplace_back(callback);
                }
                try
                {
                    _tcp.send(std::move(message));
                }
                catch (std::exception const & ex)
                {
                    if (nullptr != callback)
                    {
                        ::nokia::net::proto::redis::reply error_reply;
                        error_reply.type = ::nokia::net::proto::redis::reply::ERROR;
                        error_reply.str = ex.what();
                        auto & op_callback = _op_callbacks.back();
                        op_callback(std::move(error_reply));
                        _op_callbacks.pop_back();
                    }

                }
            }

            

            void subscribe(std::string const & channel,
                           std::function<void ()> subscribed_callback,
                           std::function<void (std::string const & channel, std::string const & message)> change_callback,
                           std::function<void ()> unsubscribed_callback)
            {
                _pubsub_mode = true;
                
                if (_subs.end() != _subs.find(channel))
                {
                    throw subscription_already_exists(channel);
                }
                
                _subs[channel] = { subscribed_callback, change_callback, nullptr, unsubscribed_callback } ;
                execute(std::bind(&redis_connection::on_subscribe_callback, this, std::placeholders::_1),
                        "SUBSCRIBE", channel);
            }


            void unsubscribe(std::string const & channel)
            {
                if (_subs.end() == _subs.find(channel))
                {
                    throw subscription_does_not_exist(channel);
                }
                execute(nullptr,
                        "UNSUBSCRIBE", channel);
            }
            

            void psubscribe(std::string const & pattern,
                            std::function<void ()> subscribed_callback,
                            std::function<void (std::string const & pattern, std::string const & channel, std::string const & message)> pattern_change_callback,
                            std::function<void ()> unsubscribed_callback)
            {
                _pubsub_mode = true;
                
                if (_subs.end() != _subs.find(pattern))
                {
                    throw subscription_already_exists(pattern);
                }
                
                _subs[pattern] = { subscribed_callback, nullptr, pattern_change_callback, unsubscribed_callback } ;
                execute(std::bind(&redis_connection::on_subscribe_callback, this, std::placeholders::_1),
                        "PSUBSCRIBE", pattern);
            }


            void punsubscribe(std::string const & pattern)
            {
                if (_subs.end() == _subs.find(pattern))
                {
                    throw subscription_does_not_exist(pattern);
                }
                execute(nullptr, "PUNSUBSCRIBE", pattern);
            }


        protected:

            template <typename... Ts>
            void ferror(Ts &&... ts)
            {
                std::string message = detail::concatenate(std::forward<Ts>(ts)...);
                if (_log_callback)
                {
                    _log_callback(message);
                }
                else
                {
                    std::cerr << message << std::endl;
                }
            }


            void on_connected(boost::system::error_code const & error)
            {
                _pubsub_mode = false;
                _subs.clear();
                
                if (_connected_callback)
                {
                    _connected_callback(error);
                }
            }

            
            void on_disconnected(boost::system::error_code const & error)
            {
                ferror("redis-connection error: disconnected ungracefully. Notify all pending requests and reconnect. ip=%1%, port=%2%, reason=%3%", _ip, _port, error.message());
                notify_all_pending_requests(ERROR_TCP_DISCONNECTED);
                if (_disconnected_callback)
                {
                    _disconnected_callback(error);
                }
            }


            void notify_all_pending_requests(std::string const & error_message)
            {
                while (!_op_callbacks.empty())
                {
                    ::nokia::net::proto::redis::reply error_reply;
                    error_reply.type = ::nokia::net::proto::redis::reply::ERROR;
                    error_reply.str = error_message;
                    auto & op_callback = _op_callbacks.front();
                    op_callback(std::move(error_reply));
                    _op_callbacks.pop_front();
                }
            }

            
            void on_read(::nokia::net::proto::redis::reply && reply)
            {
                // Subscribe related callbacks
                if (_pubsub_mode && check_subscribe_callback(reply))
                {
                    return;
                }
                // Regular callbacks
                if (_op_callbacks.empty())
                {
                    ferror("redis-connection error: got reply but doesn't have any stored callback (should not happen). Reconnecting. ip=%1%, port=%2%", _ip, _port);
                    // std::cout << "   reply: " << reply << std::endl;
                    _tcp.reconnect();
                    return;
                }
                auto & op_callback = _op_callbacks.front();
                op_callback(std::move(reply));
                _op_callbacks.pop_front();
            }

            
            void append_bulk_string(std::string & target, std::string const & t)
            {
                target += "$" + std::to_string(t.size()) + "\r\n" + t + "\r\n";
            }

            
            template <typename... Ts>
            void append_bulk_string(std::string & target, std::string const & t, Ts &&... ts)
            {
                target += "$" + std::to_string(t.size()) + "\r\n" + t + "\r\n";
                append_bulk_string(target, ts...);
            }


            bool check_subscribe_callback(::nokia::net::proto::redis::reply & reply)
            {
                if (::nokia::net::proto::redis::reply::ARRAY != reply.type) { return false; }
                if (0 == reply.elements.size()) { return false; }
                ::nokia::net::proto::redis::reply & first_item = reply.elements[0];
                if (::nokia::net::proto::redis::reply::STRING != first_item.type) { return false; }
                std::string str = first_item.str;
                std::transform(str.begin(), str.end(), str.begin(), ::toupper);
                if ("MESSAGE" == str || "UNSUBSCRIBE" == str || "PMESSAGE" == str || "PUNSUBSCRIBE" == str)
                {
                    on_subscribe_callback(std::move(reply));
                    return true;
                }
                return false;
                
            }
            void on_subscribe_callback(::nokia::net::proto::redis::reply && reply)
            {
                // todo [w] Refactor this function!
                
                auto check = [this, &reply] (bool condition) -> bool
                    {
                        if (condition)
                        {
                            return true;
                        }
                        else
                        {
                            ferror("redis-connection error: got non-valid subscribe-related response. Reconnecting. ip=%1%, port=%2%", _ip, _port);
                            // std::cout << "   reply: " << reply << std::endl;
                            _tcp.reconnect();
                            return false;
                        }
                    };
                if (!check(::nokia::net::proto::redis::reply::ARRAY == reply.type)) { return; }
                if (!check(1 <= reply.elements.size())) { return; }
                
                ::nokia::net::proto::redis::reply const & reply_command = reply.elements[0];
                if (!check(::nokia::net::proto::redis::reply::STRING == reply_command.type)) { return; }
                
                std::string command = reply_command.str;
                std::transform(command.begin(), command.end(), command.begin(), ::toupper);
                if ("SUBSCRIBE" == command || "PSUBSCRIBE" == command)
                {
                    if (!check(3 <= reply.elements.size())) { return; }
                    ::nokia::net::proto::redis::reply const & reply_channel = reply.elements[1];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_channel.type)) { return; }
                    std::string const & channel = reply_channel.str;

                    ::nokia::net::proto::redis::reply const & reply_num_of_subs = reply.elements[2];
                    if (!check(::nokia::net::proto::redis::reply::INTEGER == reply_num_of_subs.type)) { return; }

                    auto it = _subs.find(channel);
                    if (_subs.end() == it)
                    {
                        ferror("redis-connection error: cannot find subscription callback for reply. Reconnecting. ip=%1%, port=%2%", _ip, _port);
                        _tcp.reconnect();
                        return;
                    }
                    auto & callbacks = it->second;
                    callbacks.subscribed_callback();
                    return;
                }
                if ("MESSAGE" == command)
                {
                    if (!check(3 <= reply.elements.size())) { return; }
                    ::nokia::net::proto::redis::reply const & reply_channel = reply.elements[1];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_channel.type)) { return; }
                    std::string const & channel = reply_channel.str;

                    ::nokia::net::proto::redis::reply const & reply_message = reply.elements[2];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_message.type)) { return; }
                    std::string const & message = reply_message.str;

                    auto it = _subs.find(channel);
                    if (_subs.end() == it)
                    {
                        ferror("redis-connection error: cannot find subscription callback for reply. Reconnecting. ip=%1%, port=%2%", _ip, _port);
                        // std::cout << "   reply: " << reply << std::endl;
                        _tcp.reconnect();
                        return;
                    }
                    auto & callbacks = it->second;
                    callbacks.change_callback(channel, message);
                    return;
                }
                if ("UNSUBSCRIBE" == command)
                {
                    if (!check(3 <= reply.elements.size())) { return; }
                    ::nokia::net::proto::redis::reply const & reply_channel = reply.elements[1];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_channel.type)) { return; }
                    std::string const & channel = reply_channel.str;

                    ::nokia::net::proto::redis::reply const & reply_num_of_subs = reply.elements[2];
                    if (!check(::nokia::net::proto::redis::reply::INTEGER == reply_num_of_subs.type)) { return; }

                    auto it = _subs.find(channel);
                    if (_subs.end() == it)
                    {
                        ferror("redis-connection error: cannot find subscription callback for reply. Reconnecting. ip=%1%, port=%2%", _ip, _port);
                        // std::cout << "   reply: " << reply << std::endl;
                        _tcp.reconnect();
                        return;
                    }
                    auto & callbacks = it->second;
                    callbacks.unsubscribed_callback();
                    _subs.erase(it);
                    return;
                }
                if ("PMESSAGE" == command)
                {
                    if (!check(4 <= reply.elements.size())) { return; }

                    ::nokia::net::proto::redis::reply const & reply_pattern = reply.elements[1];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_pattern.type)) { return; }
                    std::string const & pattern = reply_pattern.str;
                    
                    ::nokia::net::proto::redis::reply const & reply_channel = reply.elements[2];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_channel.type)) { return; }
                    std::string const & channel = reply_channel.str;


                    ::nokia::net::proto::redis::reply const & reply_message = reply.elements[3];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_message.type)) { return; }
                    std::string const & message = reply_message.str;

                    auto it = _subs.find(pattern);
                    if (_subs.end() == it)
                    {
                        ferror("redis-connection error: cannot find subscription callback for reply. Reconnecting. ip=%1%, port=%2%", _ip, _port);
                        // std::cout << "   reply: " << reply << std::endl;
                        _tcp.reconnect();
                        return;
                    }
                    auto & callbacks = it->second;
                    callbacks.pattern_change_callback(pattern, channel, message);
                    return;
                }
                if ("PUNSUBSCRIBE" == command)
                {
                    if (!check(3 <= reply.elements.size())) { return; }
                    ::nokia::net::proto::redis::reply const & reply_pattern = reply.elements[1];
                    if (!check(::nokia::net::proto::redis::reply::STRING == reply_pattern.type)) { return; }
                    std::string const & pattern = reply_pattern.str;

                    ::nokia::net::proto::redis::reply const & reply_num_of_subs = reply.elements[2];
                    if (!check(::nokia::net::proto::redis::reply::INTEGER == reply_num_of_subs.type)) { return; }

                    auto it = _subs.find(pattern);
                    if (_subs.end() == it)
                    {
                        ferror("redis-connection error: cannot find subscription callback for reply. Reconnecting. ip=%1%, port=%2%", _ip, _port);
                        // std::cout << "   reply: " << reply << std::endl;
                        _tcp.reconnect();
                        return;
                    }
                    auto & callbacks = it->second;
                    callbacks.unsubscribed_callback();
                    _subs.erase(it);
                    return;
                }
            }
            
            
        private:
            
            struct pubsub_callbacks
            {
                std::function<void ()> subscribed_callback;
                std::function<void (std::string const & channel, std::string const & message)> change_callback;
                std::function<void (std::string const & pattern,std::string const & channel, std::string const & message)> pattern_change_callback;
                std::function<void ()> unsubscribed_callback;

                pubsub_callbacks()
                {}

                pubsub_callbacks(std::function<void ()> subscribed_callback,
                                 std::function<void (std::string const & channel, std::string const & message)> change_callback,
                                 std::function<void (std::string const & pattern,std::string const & channel, std::string const & message)> pattern_change_callback,
                                 std::function<void ()> unsubscribed_callback):
                    subscribed_callback(subscribed_callback),
                    change_callback(change_callback),
                    pattern_change_callback(pattern_change_callback),
                    unsubscribed_callback(unsubscribed_callback)
                {
                }
            };

            ::nokia::net::tcp_connection<::nokia::net::proto::redis::parser> _tcp;
            std::string _ip;
            uint16_t _port;
            std::function<void (boost::system::error_code const &)> _connected_callback;
            std::function<void (boost::system::error_code const &)> _disconnected_callback;
            std::function<void (std::string const &)> _log_callback;
            
            std::deque<std::function<void (::nokia::net::proto::redis::reply &&)>> _op_callbacks;

            bool _pubsub_mode;
            std::map<std::string, pubsub_callbacks> _subs;
        };

    }
}

