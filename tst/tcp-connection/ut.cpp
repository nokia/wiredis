/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */
#include <gtest/gtest.h>

#include <string>
#include <iostream>
#include <thread>
#include <utility>

#include <common.h>
#include <wiredis/tcp-connection.h>
 

namespace
{
    ::boost::asio::io_service ios;
}

TEST(tcp_connection, server_is_not_started)
{
    stop_server();
    msleep(2000);
    ::nokia::net::tcp_connection<> con(ios, 100);

    uint32_t num_of_disconnection{0};
    
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
                    ++num_of_disconnection;
                },
                [&] (::nokia::net::proto::char_buffer && reply)
                {
                });

    msleep(1000);
    ASSERT_FALSE(con.connected());

    start_server();
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    std::cout << "connected, sleep" << std::endl;
    con.disconnect();
    con.sync_join();
    ASSERT_EQ(0, num_of_disconnection);
}




TEST(tcp_connection, server_is_started_then_killed_then_restarted)
{
    stop_server();
    msleep(2000);
    start_server();
    ::nokia::net::tcp_connection<> con(ios, 100);

    uint32_t num_of_disconnection{0};
    
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
                    ++num_of_disconnection;
                },
                [&] (::nokia::net::proto::char_buffer && reply)
                {
                });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    // kill server
    stop_server();
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return !con.connected();
                              },
                              10000));
    
    start_server();
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    con.disconnect();
    con.sync_join();
    ASSERT_EQ(1, num_of_disconnection);
}



TEST(tcp_connection, cable_cut)
{
    stop_server();
    msleep(2000);
    start_server();
    ::nokia::net::tcp_connection<> con(ios, 100);

    uint32_t num_of_disconnection{0};
    
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
                    ++num_of_disconnection;
                },
                [&] (::nokia::net::proto::char_buffer && reply)
                {
                });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    // cut cable
    system("sudo iptables -A INPUT -p tcp --destination-port 6379 -j DROP");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return !con.connected();
                              },
                              10000));

    // restore cable
    system("sudo iptables -D INPUT -p tcp --destination-port 6379 -j DROP");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    con.disconnect();
    con.sync_join();
    ASSERT_EQ(1, num_of_disconnection);
}



TEST(tcp_connection, cable_cut_during_traffic)
{
    stop_server();
    msleep(2000);
    start_server();
    ::nokia::net::tcp_connection<> con(ios, 100);

    uint32_t num_of_disconnection{0};
    
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
                    std::cout << std::time(nullptr) << std::endl;
                    ++num_of_disconnection;
                },
                [&] (::nokia::net::proto::char_buffer && reply)
                {
                },
                true, // auto-reconnect
                true, // keepalive
                true); // user-timeout

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    // cut cable
    system("sudo iptables -A INPUT -p tcp --destination-port 6379 -j DROP");

    // send a command, it should be retransmitted over and over, so tcp-keepalive will not work
    std::string small_command{"*3\r\n$3\r\nSET\r\n$9\r\nafter_key\r\n$5\r\nvalue\r\n"};
    con.send(std::move(small_command));
    std::cout << "send message: " << std::time(nullptr) << std::endl;

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return !con.connected();
                              },
                              20000));


    // restore cable
    system("sudo iptables -D INPUT -p tcp --destination-port 6379 -j DROP");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    con.disconnect();
    con.sync_join();
    ASSERT_EQ(1, num_of_disconnection);
}



TEST(tcp_connection, disconnect_during_reconnecting)
{
    stop_server();
    msleep(2000);
    start_server();
    ::nokia::net::tcp_connection<> con(ios, 100);

    uint32_t num_of_disconnection{0};
    
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
                    ++num_of_disconnection;
                },
                [&] (::nokia::net::proto::char_buffer && reply)
                {
                });

    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return con.connected();
                              },
                              10000));

    // cut cable
    system("sudo iptables -A INPUT -p tcp --destination-port 6379 -j DROP");
    ASSERT_TRUE(wait_for_true([&] ()
                              {
                                  return !con.connected();
                              },
                              10000));

    con.disconnect();
    con.sync_join();
    ASSERT_EQ(1, num_of_disconnection);

    // restore cable
    system("sudo iptables -D INPUT -p tcp --destination-port 6379 -j DROP");
}



TEST(tcp_connection, disconnect_during_connecting)
{
    stop_server();
    msleep(2000);
    start_server();
    ::nokia::net::tcp_connection<> con(ios, 100);

    uint32_t num_of_disconnection{0};
    system("sudo iptables -A INPUT -p tcp --destination-port 6379 -j DROP");
    
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
                    ++num_of_disconnection;
                },
                [&] (::nokia::net::proto::char_buffer && reply)
                {
                });

    msleep(3000);
    con.disconnect();
    con.sync_join();
    ASSERT_EQ(0, num_of_disconnection);
    ASSERT_FALSE(con.connected());

    // restore cable
    system("sudo iptables -D INPUT -p tcp --destination-port 6379 -j DROP");
}



int main(int argc, char* argv[])
{
    stop_server();

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
