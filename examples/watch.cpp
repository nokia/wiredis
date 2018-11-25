/*
 * Licensed under BSD-3-Clause License
 * © 2018 Nokia
 */


#include <wiredis/redis-connection.h>
#include <condition_variable>



int main(int argc, char * argv[])
{
    // Start ::boost's io_service
    
    ::boost::asio::io_service ios;
    
    bool loop_condition = true;

    int retval{0};
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

    // Start a redis-connection

    std::condition_variable cv;
    std::mutex cv_mutex;
    
    ::nokia::net::redis_connection con(ios);

    {
        std::unique_lock<std::mutex> connect_lock(cv_mutex);
        con.connect("127.0.0.1",
                    6379,
                    [&] (boost::system::error_code const & error)
                    {
                        std::cout << "* Connect callback: ";
                        if (error)
                        {
                            std::cout << "failed. Keep trying..." << std::endl;
                        }
                        else
                        {
                            std::cout << "connected!" << std::endl;
                        
                            std::unique_lock<std::mutex> lock(cv_mutex);
                            cv.notify_one();
                        }
                    },
                    [&] (boost::system::error_code const & ec)
                    {
                        std::cout << "Connection lost. Error core: " << ec << ". Reconnecting..."<< std::endl;
                    });


        cv.wait_for(connect_lock, std::chrono::seconds(5));
    }
    
    // Subscribe

    std::cout << "* Subscribe...";
    {
        std::unique_lock<std::mutex> lock(cv_mutex);

        con.subscribe("my-channel",
                      [&] ()
                      {
                          std::cout << "subscribed for channel \"my-channel\"" << std::endl;
                          std::unique_lock<std::mutex> lock(cv_mutex);
                          cv.notify_one();
                      },
                      [] (std::string const & channel, std::string const & message)
                      {
                          std::cout << "* Just get a message on channel \"" << channel << "\": " << message << std::endl;
                      },
                      [&] ()
                      {
                          std::cout << "* Unsubscribed from channel \"my-channel\"" << std::endl;
                          std::unique_lock<std::mutex> lock(cv_mutex);
                          cv.notify_one();
                      });
        cv.wait_for(lock, std::chrono::seconds(5));
    }

    // Send a message on a new connection
    {
        ::nokia::net::redis_connection sender(ios);
        
        std::unique_lock<std::mutex> lock(cv_mutex);
        sender.connect("127.0.0.1",
                    6379,
                    [&] (boost::system::error_code const & error)
                    {
                        if (!error)
                        {
                            std::cout << "* Sender connected, publish a message" << std::endl;

                            sender.execute([&] (::nokia::net::proto::redis::reply && reply)
                                           {
                                               if (::nokia::net::proto::redis::reply::INTEGER != reply.type)
                                               {
                                                   std::cerr << "Response should be an interger!" << std::endl;
                                               }
                                               else
                                               {
                                                   std::cout << "* Number of clients that received the message: " << reply.integer << std::endl;
                                               }
                                               std::unique_lock<std::mutex> lock(cv_mutex);
                                               cv.notify_one();
                                           },
                                "PUBLISH", "my-channel", "Hi there! I'm the sender.");
                        }
                        else
                        {
                        }
                    },
                    [&] (boost::system::error_code const & ec)
                    {
                        std::cout << "Connection lost. Error core: " << ec << ". Reconnecting..."<< std::endl;
                    });


        cv.wait_for(lock, std::chrono::seconds(5));
        sender.disconnect();
        sender.sync_join();
    }

    // Unsubscribe
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
        con.unsubscribe("my-channel");
        cv.wait_for(lock, std::chrono::seconds(5));
    }

    
    // Proper tear-down
    con.disconnect();
    con.sync_join();

    loop_condition = false;
    scheduler_thread.join();

    return 0;
}
