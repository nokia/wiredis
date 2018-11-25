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
    
    // Fail case

    std::cout << "* Looking for non-existing key...";
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
    
        con.execute([&] (::nokia::net::proto::redis::reply && reply)
                    {
                    
                        // std::cout << "Got reply: " << reply << std::endl;
                        if (::nokia::net::proto::redis::reply::NIL == reply.type)
                        {
                            std::cout << "not found. Good." << std::endl;
                        }
                        else
                        {
                            std::cout << "found. Might be an error or the key exists." << std::endl;
                        }
                        std::unique_lock<std::mutex> lock(cv_mutex);
                        cv.notify_one();
                    },
            "GET", "I am pretty sure this key doesn't exist. #2018-11-20 16-51");
    
        cv.wait_for(lock, std::chrono::seconds(5));
    }


    // Success case

    std::cout << "* Fill DB...";
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
    
        con.execute([&] (::nokia::net::proto::redis::reply && reply)
                    {
                    
                        if (::nokia::net::proto::redis::reply::STRING == reply.type && "OK" == reply.str)
                        {
                            std::cout << "done." << std::endl;
                        }
                        else
                        {
                            std::cout << "something went wrong." << std::endl;
                            abort();
                        }
                        std::unique_lock<std::mutex> lock(cv_mutex);
                        cv.notify_one();
                    },
            "SET", "this is a key", "...and this is a value");
    
        cv.wait_for(lock, std::chrono::seconds(5));
    }


    std::cout << "* Read DB...";
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
    
        con.execute([&] (::nokia::net::proto::redis::reply && reply)
                    {
                    
                        if (::nokia::net::proto::redis::reply::STRING == reply.type && "...and this is a value" == reply.str)
                        {
                            std::cout << "done." << std::endl;
                        }
                        else
                        {
                            std::cout << "something went wrong." << std::endl;
                            abort();
                        }
                        std::unique_lock<std::mutex> lock(cv_mutex);
                        cv.notify_one();
                    },
            "GET", "this is a key");
    
        cv.wait_for(lock, std::chrono::seconds(5));
    }
    // Proper tear-down
    con.disconnect();
    con.sync_join();

    loop_condition = false;
    scheduler_thread.join();

    return 0;
}
