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
    log_.push_back(LogEntry()); // log索引从1开始
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
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << "  transit to  " << state_ << std::endl;
    }
    switch (state_)
    {
    case follower:
        follower_func();
        break;
    case candidate:
        candidate_func();
        break;
    case leader:
        leader_func();
        break;
    }
    std::thread([&]()
                { transition(state_); })
        .detach();
}

void KVStore::follower_func()
{
    voted_for_ = -1;
    votes_received_ = 0;
    start_timer();
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        state_ = candidate;
    }
}

void KVStore::candidate_func()
{
    voted_for_ = id_;
    votes_received_ = 1;
    term_++;
    start_timer();
    if (voted_for_ != id_)
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        state_ = follower;
    }
    else if (votes_received_ * 2 >= num_nodes_)
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        state_ = leader;
    }
}

void KVStore::leader_func()
{
    voted_for_ = -1;
    start_timer();
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        state_ = follower;
    }
}

void KVStore::start_timer()
{
    if (state_ == leader)
    {
        htimer_thread_ = std::thread(&KVStore::heartbeat_timeout, this);
        htimer_thread_.detach();
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
}

void KVStore::send2other(int id, std::string func)
{
    if (func == "vote")
    {
        while (1)
        {
            if (state_ != candidate)
                break;
            auto recv = client_[id].call<RequestVoteRet>("vote", id_, term_);
            if (recv.error_msg() == "recv timeout")
                continue;
            else if (recv.val().vote_granted)
            {
                std::lock_guard<std::mutex> lock(votes_mtx_);
                votes_received_++;
            }
            break;
        }
    }
}

RequestVoteRet KVStore::vote(int id, int term, int log_index, int log_term)
{
    RequestVoteRet ret;
    if (term_ >= term)
    {
        term_ = term;
        ret.vote_granted = false;
    }
    else
    {
        int lindex = commit_index_;
        int lterm = log_[lindex].term;

        if (log_term < lterm)
            ret.vote_granted = false;
        else if (log_term == lterm)
        {
            if (log_index < lindex)
                ret.vote_granted = false;
            else if (log_index == lindex && state_ == candidate)
                ret.vote_granted = false;
            else
            {
                ret.vote_granted = true;
                voted_for_ = id;
            }
        }
        else
        {
            ret.vote_granted = true;
            voted_for_ = id;
        }
    }

    return ret;
}

AppendEntriesRet KVStore::append(int term, int lid, int prev_log_index, int prev_log_term, vector<LogEntry> &entries, int lcommit)
{
}

void KVStore::election_timeout()
{
    int election_ms = rand() % 151 + 150; // 生成随机整数
    eduration_ = std::chrono::milliseconds(election_ms);
    estart_ = std::chrono::system_clock::now();
    state start_state; // 初始状态

    std::chrono::system_clock::time_point end = estart_ + eduration_;
    while (1)
    {
        // 状态改变了
        if (start_state != state_)
            return;

        // 超时了
        if (std::chrono::system_clock::now() >= end)
            return;

        // 候选人达到票数要求或投票给别人了
        if (state_ == candidate && (votes_received_ * 2 >= num_nodes_ || voted_for_ != id_))
        {
            return;
        }

        {
            std::unique_lock<std::mutex> lock(emutex_);
            end = estart_ + eduration_;
        }
    }
}

void KVStore::heartbeat_timeout()
{
    hstart_ = std::chrono::system_clock::now();
    hduration_ = std::chrono::milliseconds(50); // 50ms发送一次心跳
    std::chrono::system_clock::time_point end = hstart_ + hduration_;
    while (1)
    {
        if (std::chrono::system_clock::now() >= end)
            start_heartbeat();

        if (voted_for_ != -1)
            return;

        {
            std::unique_lock<std::mutex> lock(hmutex_);
            end = start + duration;
        }
    }
}
