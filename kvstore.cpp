#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "kvstore.h"
#include <thread>
#include <time.h>

extern std::mutex print_mtx;
KVStore::KVStore(int id, std::vector<int> &info)
{
    id_ = id;
    nodes_ = info;
    num_nodes_ = nodes_.size();
    std::thread server_thread([this](int port)
                              {
        server_.as_server(port);
        server_.bind("vote", &KVStore::vote, this);
        {std::lock_guard<std::mutex> lock(cv_mtx_);
        cv_.notify_one();}
        {std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "run rpc server on: " << port << std::endl;}
        server_.run(); },
                              nodes_[id_]);
    {
        std::unique_lock<std::mutex> lock(cv_mtx_);
        cv_.wait(lock);
    }
    for (int i = 0; i < num_nodes_; i++)
    {
        client_[i].as_client("127.0.0.1", nodes_[i]);
        client_[i].set_timeout(50); // 50ms超时重传
    }
    transition(follower);
    server_thread.join();
    // server_.bind("vote", &KVStore::vote, this);
    // start_timer();
    // client_[0].as_client("127.0.0.1", nodes_[0]);
    // client_[1].as_client("127.0.0.1", nodes_[1]);
    // for (int i = 0; i < num_nodes_; i++)
    // {
    //     client_[i].as_client("127.0.0.1", nodes_[i]);
    // }
}

void KVStore::transition(state st)
{
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        state_ = st;
    }
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << "  transit to  " << state_ << std::endl;
    }
    switch (state_)
    {
    case follower:
    {
        follower_func();
        std::thread([&]()
                    { transition(candidate); })
            .detach();
        break;
    }
    case candidate:
    {
        candidate_func();
        if (votes_received_ * 2 <= num_nodes_)
        {
            std::thread([&]()
                        { transition(candidate); })
                .detach();
        }
        else
        {
            std::thread([&]()
                        { transition(leader); })
                .detach();
        }
        break;
    }
    case leader:
    {
        leader_func();
        std::thread([&]()
                    { transition(follower); })
            .detach();
        break;
    }
    }
}

void KVStore::follower_func()
{
    voted_for_ = -1;
    votes_received_ = 0;
    start_timer();
}

void KVStore::candidate_func()
{
    voted_for_ = id_;
    votes_received_ = 1;
    term_++;
    start_timer();
}

void KVStore::leader_func()
{
}

void KVStore::start_timer()
{
    if (state_ == leader)
    {
        // htimer_thread_ = std::thread(&KVStore::heartbeat_timeout, this);
        // htimer_thread_.detach();
    }
    else
    {
        etimer_thread_ = std::thread(&KVStore::election_timeout, this);
        if (state_ == candidate)
        {
            start_election();
        }
        etimer_thread_.join();
    }
}

void KVStore::start_election()
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  start election!!!" << std::endl;
        std::cout << "term:  " << term_ << std::endl;
    }

    std::vector<std::thread> election_threads;
    for (int i = 0; i < num_nodes_; ++i)
    {
        if (i != id_)
            election_threads.emplace_back([this, i]()
                                          { send2other(i, "vote"); });
    }
    for (auto &thread : election_threads)
    {
        thread.detach();
    }
}

void KVStore::start_heartbeat()
{
    state_ = leader;
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  I'm leader!!!" << std::endl;
    }
}

void KVStore::send2other(int id, std::string func)
{
    if (func == "vote")
    {
        while (1)
        {
            if (state_ != candidate)
                break;
            auto recv = client_[id].call<bool>("vote", id_, term_);
            if (recv.error_msg() == "recv timeout")
                continue;
            else if (recv.val())
            {
                std::lock_guard<std::mutex> lock(votes_mtx_);
                votes_received_++;
            }
            break;
        }
    }
}

bool KVStore::vote(int id, int term)
{
    if (voted_for_ == -1 && term >= term_)
    {
        voted_for_ = id;
        term_ = term;
    }
    // {
    //     std::lock_guard<std::mutex> lock(print_mtx);
    //     std::cout << id_ << ":  vote to " << voted_for_ << std::endl;
    //     std::cout << "term is:  " << term_ << std::endl;
    //     std::cout << "voted term is:  " << term << std::endl;
    // }
    if (voted_for_ == id)
    {
        {
            std::lock_guard<std::mutex> lock(emutex_);
            estart_ = std::chrono::system_clock::now();
        }
    }
    return voted_for_ == id;
}

void KVStore::election_timeout()
{
    int election_ms = rand() % 151 + 150; // 生成随机整数
    eduration_ = std::chrono::milliseconds(election_ms);
    estart_ = std::chrono::system_clock::now();

    std::chrono::system_clock::time_point end = estart_ + eduration_;
    while (1)
    {
        if (std::chrono::system_clock::now() >= end)
            return;

        if (state_ == candidate && votes_received_ * 2 >= num_nodes_)
        {
            return;
        }

        {
            std::unique_lock<std::mutex> lock(emutex_);
            end = estart_ + eduration_;
        }
    }
}
// void KVStore::heartbeat_timeout()
// {
//     hstart_ = std::chrono::system_clock::now();
//     hduration_ = std::chrono::milliseconds(50); // 50ms发送一次心跳
//     std::chrono::system_clock::time_point end = hstart_ + hduration_;
//     while (1)
//     {
//         if (std::chrono::system_clock::now() < end)
//         {
//             std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 暂停 50 毫秒
//         }
//         else
//             state_ = candidate;

//         {
//             std::unique_lock<std::mutex> lock(mutex);
//             end = start + duration;
//         }
//     }
// }
