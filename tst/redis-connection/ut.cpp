/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#include <gtest/gtest.h>

#include <string>
#include <iostream>
#include <thread>
#include <utility>

#include <wiredis/redis-connection.h>
#include <common.h>

namespace
{
    ::boost::asio::io_service ios;
}


TEST(redis_connection, some_basic_cases)
{
    ::nokia::net::redis_connection con(ios);

    con.connect("127.0.0.1",
                6379,
                [&] (boost::system::error_code const & error)
                {
                    std::cout << "* connected!" << std::endl;
                    if (error)
                    {
                        std::cout << "UT: Could not connect, reconnecting." << std::endl;
                        return;
                    }
                },
                [&] (boost::system::error_code const & ec)
                {
                    std::cout << "UT: Connection lost. error core: " << ec << std::endl;
                });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));
    uint64_t counter{0};
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    // std::cout << "Got reply: " << reply << std::endl;
                    ASSERT_EQ(reply.type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.str, "string_value");
                    ++counter;
                    // std::cout << counter << std::endl;
                },
                "GET", "string_key");
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    // std::cout << "Got reply: " << reply << std::endl;
                    // system("redis-cli hmset hash_key 1_key 1_value 2_key 2_value 3_key 3_value 4_key 4_value");
                    ASSERT_EQ(reply.type, ::nokia::net::proto::redis::reply::ARRAY);
                    ASSERT_EQ(reply.elements[0].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[0].str, "1_key");
                    ASSERT_EQ(reply.elements[1].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[1].str, "1_value");
                    ASSERT_EQ(reply.elements[2].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[2].str, "2_key");
                    ASSERT_EQ(reply.elements[3].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[3].str, "2_value");
                    ASSERT_EQ(reply.elements[4].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[4].str, "3_key");
                    ASSERT_EQ(reply.elements[5].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[5].str, "3_value");
                    ASSERT_EQ(reply.elements[6].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[6].str, "4_key");
                    ASSERT_EQ(reply.elements[7].type, ::nokia::net::proto::redis::reply::STRING);
                    ASSERT_EQ(reply.elements[7].str, "4_value");
                    ++counter;
                    // std::cout << counter << std::endl;
                },
                "HGETALL", "hash_key");
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    // std::cout << "Got reply: " << reply << std::endl;
                    ASSERT_EQ(reply.type, ::nokia::net::proto::redis::reply::NIL);
                    ++counter;
                    // std::cout << counter << std::endl;
                },
                "GET", "non-exist-key");
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    // std::cout << "Got reply: " << reply << std::endl;
                    ASSERT_EQ(reply.type, ::nokia::net::proto::redis::reply::INTEGER);
                    ASSERT_EQ(reply.integer, 11);
                    ++counter;
                    // std::cout << counter << std::endl;
                },
                "INCR", "integer-key");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return counter == 4;
                              },
                              10000)) << "counter: " << counter;
        
    con.disconnect();
    con.sync_join();
}




TEST(redis_connection, sending_in_disconnected_state)
{
    stop_server();
    ::nokia::net::redis_connection con(ios);

    con.connect("127.0.0.1",
                6379,
                [&] (boost::system::error_code const & error)
                {
                    if (error)
                    {
                        std::cout << "UT: Could not connect, reconnecting." << std::endl;
                        return;
                    }
                },
                [&] (boost::system::error_code const & ec)
                {
                    std::cout << "UT: Connection lost. error core: " << ec << std::endl;
                });

    uint64_t counter{0};
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    // std::cout << "Got reply: " << reply << std::endl;
                    ASSERT_EQ(reply.type, ::nokia::net::proto::redis::reply::ERROR);
                    ASSERT_EQ(reply.str, con.ERROR_TCP_CANNOT_SEND_MESSAGE);
                    ++counter;
                    // std::cout << counter << std::endl;
                },
                "GET", "string_key");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return counter == 1;
                              },
                              10000));
    con.disconnect();
    con.sync_join();

    start_server();
}




TEST(redis_connection, subscribe)
{
    ::nokia::net::redis_connection con(ios);

    con.connect("127.0.0.1",
                6379,
                [&] (boost::system::error_code const & error)
                {
                    if (error)
                    {
                        std::cout << "UT: Could not connect, reconnecting." << std::endl;
                        return;
                    }
                },
                [&] (boost::system::error_code const & ec)
                {
                    std::cout << "UT: Connection lost. error core: " << ec << std::endl;
                });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));
    bool subscribed{false};
    bool callback{false};
    bool unsubscribed{false};
    con.subscribe("apple-channel",
                  [&] ()
                  {
                      std::cout << "* UT: Subscribed" << std::endl;
                      subscribed = true;
                  },
                  [&] (std::string const & channel, std::string const & message)
                  {
                      std::cout << "* UT: Got message. channel: " << channel << ", message: " << message << std::endl;
                      ASSERT_EQ(channel, "apple-channel");
                      ASSERT_EQ(message, "This is a beautiful message. Especially if it arrives to somewhere...");
                      callback = true;
                  },
                  [&] ()
                  {
                      std::cout << "* UT: Unsubscribed" << std::endl;
                      unsubscribed = true;
                  });
    // Subscribed
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return subscribed;
                              },
                              10000));

    // Publish
    system("redis-cli publish \"apple-channel\" \"This is a beautiful message. Especially if it arrives to somewhere...\"");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return callback;
                              },
                              10000));

    // Unsubscribe
    con.unsubscribe("apple-channel");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return unsubscribed;
                              },
                              10000));

    // Psubscribe
    subscribed = false;
    callback = false;
    unsubscribed = false;
    con.psubscribe("apple*",
                   [&] ()
                   {
                       std::cout << "* UT: Subscribed" << std::endl;
                       subscribed = true;
                   },
                   [&] (std::string const & pattern, std::string const & channel, std::string const & message)
                   {
                       std::cout << "* UT: Got message. pattern: " << pattern << ", channel: " << channel << ", message: " << message << std::endl;
                       ASSERT_EQ(pattern, "apple*");
                       ASSERT_EQ(channel, "apple-pattern");
                       ASSERT_EQ(message, "This is a pattern message");
                       callback = true;
                   },
                   [&] ()
                   {
                       std::cout << "* UT: Unsubscribed" << std::endl;
                       unsubscribed = true;
                   });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return subscribed;
                              },
                              10000));
    system("redis-cli publish \"apple-pattern\" \"This is a pattern message\"");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return callback;
                              },
                              10000));

    // Unsubscribe
    con.punsubscribe("apple*");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return unsubscribed;
                              },
                              10000));
    // End and happy
    con.disconnect();
    con.sync_join();
}


TEST(redis_connection, subscribe_stress)
{
    ::nokia::net::redis_connection con(ios);

    con.connect("127.0.0.1",
                6379,
                [&] (boost::system::error_code const & error)
                {
                    if (error)
                    {
                        std::cout << "UT: Could not connect, reconnecting." << std::endl;
                        return;
                    }
                },
                [&] (boost::system::error_code const & ec)
                {
                    std::cout << "UT: Connection lost. error core: " << ec << std::endl;
                });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));
    bool subscribed{false};
    bool callback{false};
    bool unsubscribed{false};
    con.subscribe("apple-channel",
                  [&] ()
                  {
                      std::cout << "* UT: Subscribed" << std::endl;
                      subscribed = true;
                  },
                  [&] (std::string const & channel, std::string const & message)
                  {
                      std::cout << "* UT: Got message. channel: " << channel << ", message: " << message << std::endl;
                      ASSERT_EQ(channel, "apple-channel");
                      ASSERT_EQ(message, "Demo message");
                      callback = true;
                  },
                  [&] ()
                  {
                      std::cout << "* UT: Unsubscribed" << std::endl;
                      unsubscribed = true;
                  });
   
    // Subscribed
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return subscribed;
                              },
                              10000));
    
    bool psubscribed{false};
    bool pcallback{false};
    bool punsubscribed{false};
    
    con.psubscribe("apple*",
                   [&] ()
                   {
                       std::cout << "* UT: Subscribed" << std::endl;
                       psubscribed = true;
                   },
                   [&] (std::string const & pattern, std::string const & channel, std::string const & message)
                   {
                       std::cout << "* UT: Got message. pattern: " << pattern << ", channel: " << channel << ", message: " << message << std::endl;
                       ASSERT_EQ(pattern, "apple*");
                       ASSERT_EQ(channel, "apple-channel");
                       ASSERT_EQ(message, "Demo message");
                       pcallback = true;
                   },
                   [&] ()
                   {
                       std::cout << "* UT: Unsubscribed" << std::endl;
                       punsubscribed = true;
                   });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return psubscribed;
                              },
                              10000));

    // Both subscription is working now
    std::cout << "\n\nSend a message, both subscription has to got it\n\n";
    system("redis-cli publish \"apple-channel\" \"Demo message\"");
    
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return callback && pcallback;
                              },
                              10000));

    std::cout << "\n\nDouble subscription try\n\n";
    try
    {
        con.subscribe("apple-channel",
                      [&] ()
                      {
                      },
                      [&] (std::string const & channel, std::string const & message)
                      {
                      },
                      [&] ()
                      {
                      });
        FAIL() << "Exception should have been thrown!";
    }
    catch (::nokia::net::redis_connection::subscription_already_exists const & ex)
    {
        ASSERT_EQ(std::string(ex.what()), "apple-channel");
    }

    try
    {
        con.psubscribe("apple*",
                      [&] ()
                      {
                      },
                      [&] (std::string const & pattern, std::string const & channel, std::string const & message)
                      {
                      },
                      [&] ()
                      {
                      });
        FAIL() << "Exception should have been thrown!";
    }
    catch (::nokia::net::redis_connection::subscription_already_exists const & ex)
    {
        ASSERT_EQ(std::string(ex.what()), "apple*");
    }

    std::cout << "\n\nUnsubscribe from non-existing channels\n\n";
    try
    {
        con.unsubscribe("not-existing-channel");
        FAIL() << "Exception should have been thrown!";
    }
    catch (::nokia::net::redis_connection::subscription_does_not_exist const & ex)
    {
        ASSERT_EQ(std::string(ex.what()), "not-existing-channel");
    }

    try
    {
        con.punsubscribe("not-existing-channel*");
        FAIL() << "Exception should have been thrown!";
    }
    catch (::nokia::net::redis_connection::subscription_does_not_exist const & ex)
    {
        ASSERT_EQ(std::string(ex.what()), "not-existing-channel*");
    }

    std::cout << "\n\nDouble unsubscribe\n\n";
    con.unsubscribe("apple-channel"); // This should work
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return unsubscribed;
                              },
                              10000));
    try
    {
        con.unsubscribe("apple-channel");
        FAIL() << "Exception should have been thrown!";
    }
    catch (::nokia::net::redis_connection::subscription_does_not_exist const & ex)
    {
        ASSERT_EQ(std::string(ex.what()), "apple-channel");
    }

    con.punsubscribe("apple*"); // This should work
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return punsubscribed;
                              },
                              10000));
    try
    {
        con.punsubscribe("apple*");
        FAIL() << "Exception should have been thrown!";
    }
    catch (::nokia::net::redis_connection::subscription_does_not_exist const & ex)
    {
        ASSERT_EQ(std::string(ex.what()), "apple*");
    }


    
    std::cout << "\n\nPattern like subscription\n\n";

    subscribed = false;
    callback = false;
    unsubscribed = false;
    con.subscribe("apple*",
                  [&] ()
                  {
                      subscribed = true;
                  },
                  [&] (std::string const & channel, std::string const & message)
                  {
                      callback = true;
                      ASSERT_EQ(channel, "apple*");
                      ASSERT_EQ(message, "Pattern-like message");
                  },
                  [&] ()
                  {
                      unsubscribed = true;
                  });
    
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return subscribed;
                              },
                              10000));

    system("redis-cli publish \"apple*\" \"Pattern-like message\"");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return callback;
                              },
                              10000));


    std::cout << "\n\nRegular command\n\n";

    bool regular_reply{false};
    con.execute([&] (::nokia::net::proto::redis::reply && reply)
                {
                    ASSERT_EQ(reply.type, ::nokia::net::proto::redis::reply::ERROR);
                    std::cout << "reply.str: " << reply.str << std::endl;
                    regular_reply = true;
                },
                "GET", "string_key");
    ASSERT_TRUE(wait_for_true(regular_reply,
                              10000));


    // End and happy
    con.disconnect();
    con.sync_join();
}



int main(int argc, char* argv[])
{
    stop_server();
    start_server();

    system("redis-cli set string_key string_value");
    system("redis-cli del hash_key");
    system("redis-cli hmset hash_key 1_key 1_value 2_key 2_value 3_key 3_value 4_key 4_value");
    system("redis-cli del non-exist-key");
    system("redis-cli set integer-key 10");

    
    bool loop_condition = true;

    int retval{0};
    std::thread scheduler_thread(
        [&]
        {
            while (loop_condition)
            {
                ios.reset();
                ios.run();
                msleep(10);
            }
        });

    ::testing::InitGoogleTest(&argc, argv);

    retval = RUN_ALL_TESTS();

    loop_condition = false;
    scheduler_thread.join();

    return retval;
}
