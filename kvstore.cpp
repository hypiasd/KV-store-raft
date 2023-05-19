#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "kvstore.h"
#include <thread>

KVStore::KVStore(int id, std::vector<int> &info, int election_ms, int heartbeat_ms)
{
    id_ = id;
    nodes_ = info;
    eduration_ = std::chrono::milliseconds(election_ms);
    hduration_ = std::chrono::milliseconds(heartbeat_ms);
    server_.as_server(info[id]);
    std::cout << "run rpc server on: " << info[id] << std::endl;
    server_.run();
}

void KVStore::start_timer()
{
    if (state_ == leader)
    {
        htimer_thread_ = std::thread([this]()
                                     { run_timer(hstart_, hduration_, hmutex_); });
        htimer_thread_.detach();
    }
    else
    {
        etimer_thread_ = std::thread([this]()
                                     { run_timer(estart_, eduration_, emutex_); });
        etimer_thread_.detach();
    }
}

void KVStore::run_timer(std::chrono::system_clock::time_point &start, std::chrono::system_clock::duration duration, std::mutex &mutex)
{
    start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point end = start + duration;
    while (1)
    {
        if (std::chrono::system_clock::now() < end)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 暂停 50 毫秒
        }
        else
            state_ = candidate;

        {
            std::unique_lock<std::mutex> lock(mutex);
            end = start + duration;
        }
    }
}
