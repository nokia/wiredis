
# wiredis
wiredis is a header-only, asynchronous c++11 client library for [redis](http://redis.io/) database server.

It depends only on [boost](https://www.boost.org/) library and uses [::boost::asio::io_service](https://www.boost.org/doc/libs/1_51_0/doc/html/boost_asio/reference/io_service.html) as event handler.

Tested on
- Arch linux
  - cmake 3.11.4
  - g++ 8.1.1
  - boost 1.67
- Redhat
  - cmake 3.12.0
  - g++ 4.8.5
  - boost 1.68

## Compile and install

```
$ mkdir build && cd build && cmake ../ && sudo make install
```

## Features

- Asynchronous interface
- Standalone: depends only on [boost](https://www.boost.org) library.
- Auto reconnect
- TCP keepalive on idle connection
- Exact match with redis commands without inner logic
- Binary key/values
- PUB/SUB mode

## Usage

You can find working examples under [examples](examples/) folder and full documention below in this document.

First of all you need a running ::boost::asio::io_service instance.

```
    ::boost::asio::io_service ios;
    bool loop_condition = true;
    std::thread scheduler_thread(
        [&]
        {
            while (loop_condition)
            {
                ios.reset();
                ios.run();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
```

...then create a `::nokia::net::redis_connection` instance:

```
    ::nokia::net::redis_connection con(ios);

    con.connect("127.0.0.1",
                6379,
                [] (boost::system::error_code const & error)
                {
                    if (error)
                    {
                        std::cout << "Connect failed. Keep trying..." << std::endl;
                    }
                    else
                    {
                        std::cout << "Connected!" << std::endl;
                    }
                },
                [&] (boost::system::error_code const & ec)
                {
                    std::cout << "Connection lost. Error core: " << ec << ". Reconnecting..."<< std::endl;
                });
```

Once the connection established you can start working on the connection.
To send a command to redis-server you need to invoke `execute()` function with a callback function, the command and the arguments of the command.

```
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    if (::nokia::net::proto::redis::reply::NIL == reply.type)
                    {
                        std::cout << "Key not found" << std::endl;
                    }
                    else
                    {
                        std::cout << "Key found. Value:" << reply.str << std::endl;
                    }
                },
                "GET", "key");

```

If you use multiple arguments you need to separate them:

```
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    if (::nokia::net::proto::redis::reply::NIL == reply.type)
                    {
                        std::cout << "Key not found" << std::endl;
                    }
                    else
                    {
                        std::cout << "Key found. Value:" << reply.str << std::endl;
                    }
                },
                "HSET", "key", "field", "value");
```

Return object has type `::nokia::net::proto::redis::reply &&` which follows the [RESP](https://redis.io/topics/protocol) definition.

```
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
    };
```

To close your connection you need to use `join()` function.

```
    con.disconnect();
    con.join([] ()
             {
                 std::cout << "Cleanup done, we can die now." << std::endl;
             });
```

Alternatively, you can use `sync_join()` function but be warned: it uses the `io_service` object so never call from thread being run by your `io_service` object!

## [PUB/SUB](https://redis.io/topics/pubsub) mode

You can use redis connection to watch a given channel.

Note: once you switch to [PUB/SUB](https://redis.io/topics/pubsub) mode you can't use the conncetion for standard operations. Allowed commands in PUB/SUB mode: SUBSCRIBE, PSUBSCRIBE, UNSUBSCRIBE, PUNSUBSCRIBE, PING and QUIT.

```
    con.subscribe("my-channel",
                  [&] ()
                  {
                      std::cout << "subscribed for channel \"my-channel\"" << std::endl;
                  },
                  [] (std::string const & channel, std::string const & message)
                  {
                      std::cout << "* Just get a message on channel \"" << channel << "\": " << message << std::endl;
                  },
                  [&] ()
                  {
                      std::cout << "* Unsubscribed from channel \"my-channel\"" << std::endl;
                  });
```

## Documentation

### redis_connection()
```
redis_connection(boost::asio::io_service & io_service);
```
Simply constructor to create redis_connection object.
- io_service: The `::boost::asio::io_service` you want to use for event handler.


### connect()

```
void connect(std::string const & ip,
             uint16_t port,
             std::function<void (boost::system::error_code const &)> connected_callback,
             std::function<void (boost::system::error_code const &)> disconnected_callback,
             bool auto_reconnect = true,
             bool keepalive_enabled = true);
```
Initiate connecting to redis-server.
- ip: ip address of redis server.
- port: port of redis server.
- connected_callback: this function will be called every time the client connects to the server including reconnecting.
- disconnected_callback: this function will be called if the client losts conncection to server excluding the disconnect() function call.
- auto_reconnect: if it's true the client tries to reconnect to the server. After reconncection the connection is in standard mode, you lose every previous subscriptions.
- keepalive_enabled: if it's true the connection uses TCP keepaliving. The settings are
  - TCP_KEEPIDLE: 2
  - TCP_KEEPINTVL: 2
  - TCP_KEEPCNT: 3


### connected()
```
bool connected() const;
```
Returns true if the connection is established.


### disconnect()
```
void disconnect();
```
Disconnect from redis server. Always use this function if you want tear-down, it performs clean-up.


### join(), sync_join()
```
void join(std::function<void ()> cb);
void sync_join();
```
Waits for clean-up. The first version calls back once the clean-up has finished, while the second version blocks.

Note: `snyc_join()` uses the `io_service` object so never call from thread being run by your `io_service` object!


### execute()
```
template <typename... Ts>
void execute(std::function<void (::nokia::net::proto::redis::reply &&)> callback, Ts &&... ts);
```
Invoke redis command. The execution and return value are same as redis defines, there is no inner logic. See: https://redis.io/commands

Binary arguments are supported.

- callback: this function will be called with the result.
- ts: redis command and its arguments.


### subscribe(), psusbscribe()
```
void subscribe(std::string const & channel,
               std::function<void ()> subscribed_callback,
               std::function<void (std::string const & channel, std::string const & message)> change_callback,
               std::function<void ()> unsubscribed_callback);
void psubscribe(std::string const & pattern,
                std::function<void ()> subscribed_callback,
                std::function<void (std::string const & pattern, std::string const & channel, std::string const & message)> pattern_change_callback,
                std::function<void ()> unsubscribed_callback);
```
Subscribe for a given channel/channel matches with the pattern.
If you already subscribed for a channel you will get `subscription_already_exists` exception.
- channel/pattern: the channel/pattern of channel you want to subscribe.
- subscribed_callback: this function will be called once the subscription is ready.
- change_callback: this function will be called if new message arrives on the subscribed channel.
- unsubscribed_callback: this function will be called if you've successfully unsubscribed from the channel.


### unsubscribe(), punsubscribe()
```
void unsubscribe(std::string const & channel);
void punsubscribe(std::string const & pattern);
```

Unsubscribe from a previously subscribed channel/pattern of channel.
If the subscription doesn't exist you'll get a `subscription_does_not_exist` exception.
- channel/pattern: the channel/pattern of channel you want to unsubscribe from.


### set_log_callback()
```
void set_log_callback(std::function<void (std::string const &)> cb);
```
The logs are printed out to stdout by default. If you want to handle by your own, you need to call this function with your callback function.


## Tests

To run unit tests, you need to have installed valgrind, redis-server and need to use Debug configuration.

Tests start/stop redis-server, so turn off redis-server if it runs: systemctl stop redis

```
mkdir build
cd build
cmake ../ -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja
ctest  -T memcheck
```


## License

This project is licensed under the BSD-3-Clause license - see the [LICENSE](https://github.com/nokia/wiredis/blob/master/LICENSE).