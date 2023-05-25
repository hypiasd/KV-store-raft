#include <chrono>
#include <fstream>
#include <iostream>
#include "rest_rpc.hpp"

using namespace rest_rpc;
using namespace rest_rpc::rpc_service;

// void test_add()
// {
//     try
//     {
//         rpc_client client("127.0.0.1", 9000);
//         bool r = client.connect();
//         if (!r)
//         {
//             std::cout << "connect timeout" << std::endl;
//             return;
//         }

//         {
//             auto result = client.call<int>("add", 1, 2);
//             std::cout << result << std::endl;
//         }

//         {
//             auto result = client.call<2000, int>("add", 1, 2);
//             std::cout << result << std::endl;
//         }
//     }
//     catch (const std::exception &e)
//     {
//         std::cout << e.what() << std::endl;
//     }
// }

struct LogEntry
{
    int term;
    std::string func;
    std::string key;
    std::string value;

    MSGPACK_DEFINE(term, func, key, value);
};

void test()
{
    try
    {
        rpc_client client("127.0.0.1", 12343);
        bool r = client.connect();
        if (!r)
        {
            std::cout << "connect timeout" << std::endl;
            return;
        }
        LogEntry log[10];
        for (int i = 0; i < 10; i++)
            log[i] = {i, "func", "key", "value"};
        {
            auto result = client.call<int>("asdf", log);
            std::cout << result << std::endl;
        }
        r = client.connect();
        {
            auto result = client.call<2000, int>("mytest", log);
            std::cout << result << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}
int main()
{
    // benchmark_test();
    // test_connect();
    // test_callback();
    // test_echo();
    // test_sync_client();
    // test_async_client();
    // test_threads();
    // test_sub1();
    // test_call_with_timeout();
    // test_connect();
    // test_upload();
    // test_download();
    // multi_client_performance(20);
    // test_performance1();
    // test_multiple_thread();
    test();
    return 0;
}