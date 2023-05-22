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
enum ret_type
{
    VOTES_GETD,
    STATE_CHANGE,
    TIMEOUT
};
KVStore::KVStore(int id, std::vector<int> &info, size_t num_thread)
{
    id_ = id;
    nodes_ = info;
    num_nodes_ = nodes_.size();
    log_.push_back(LogEntry()); // log索引从1开始
    thread_pool_ = new ThreadPool(num_thread);
    std::thread server_thread([this](int port)
                              {
        server_.as_server(port);
        server_.bind("vote", &KVStore::vote, this);
        server_.bind("append", &KVStore::append, this);
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
        if (i != id_)
        {
            client_[i].as_client("127.0.0.1", nodes_[i]);
            client_[i].set_timeout(50); // 50ms超时重传
        }
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

KVStore::~KVStore()
{
    delete thread_pool_;
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
    thread_pool_->enqueue([&]()
                          { transition(state_); });
    // std::thread([&]()
    //             { transition(state_); })
    //     .detach();
}

void KVStore::follower_func()
{
    voted_for_ = -1;
    votes_received_ = 0;
    start_timer();
    {
        std::unique_lock<std::shared_mutex> lock(state_mtx_);
        state_ = candidate;
    }
}

void KVStore::candidate_func()
{
    voted_for_ = id_;
    votes_received_ = 1;
    {
        std::unique_lock<std::shared_mutex> lock(term_mtx_);
        term_++;
    }
    int ret = start_timer();
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  ret for  " << ret << std::endl;
    }
    if (ret == STATE_CHANGE)
    {
        std::unique_lock<std::shared_mutex> lock(state_mtx_);
        state_ = follower;
    }
    else if (ret == VOTES_GETD)
    {
        std::unique_lock<std::shared_mutex> lock(state_mtx_);
        state_ = leader;
    }
}

void KVStore::leader_func()
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  I'm leader!!!!" << std::endl;
    }
    voted_for_ = -1;
    start_timer();
    {
        std::unique_lock<std::shared_mutex> lock(state_mtx_);
        state_ = follower;
    }
}

int KVStore::start_timer()
{
    if (state_ == leader)
    {
        auto res = thread_pool_->enqueue_ret(&KVStore::heartbeat_timeout, this);
        int ret = res.get();
        return ret;
        // htimer_thread_ = std::thread(&KVStore::heartbeat_timeout, this);
        // htimer_thread_.detach();
    }
    else
    {
        auto res = thread_pool_->enqueue_ret(&KVStore::election_timeout, this);
        if (state_ == candidate)
        {
            start_election();
        }
        int ret = res.get();
        return ret;
    }
}

void KVStore::start_election()
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  start election!!!"
                  << "  term:  " << term_ << std::endl;
    }

    for (int i = 0; i < num_nodes_; ++i)
    {
        if (i != id_)
            thread_pool_->enqueue([this, i]()
                                  { send2other(i, "vote"); });
        // if (i != id_)
        //     election_threads.emplace_back([this, i]()
        //                                   { send2other(i, "vote"); });
    }
    // for (auto &thread : election_threads)
    // {
    //     thread.detach();
    // }
}

void KVStore::start_heartbeat()
{
    // std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

    // // 将时间点转换为毫秒（以 std::chrono::milliseconds 表示）
    // auto duration = now.time_since_epoch();
    // auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    // {
    //     std::lock_guard<std::mutex> lock(print_mtx);
    //     std::cout << id_ << ":start heartbeat!!!   " << ms << std::endl;
    // }
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":start heartbeat!!!" << std::endl;
    }
    {
        std::unique_lock<std::shared_mutex> lock(log_mtx_);
        log_tosent_.clear();
    }
    for (int i = 0; i < num_nodes_; ++i)
    {
        if (i != id_)
            thread_pool_->enqueue([this, i]()
                                  { send2other(i, "append"); });
        // {
        //     std::lock_guard<std::mutex> lock(print_mtx);
        //     std::cout << id_ << ":start append to " << i << std::endl;
        // }
    }
}

void KVStore::send2other(int id, std::string func)
{
    if (func == "vote")
    {
        while (1)
        {
            {
                std::shared_lock<std::shared_mutex> lock(state_mtx_);
                if (state_ != candidate)
                    break;
            }
            auto recv = client_[id].call<RequestVoteRet>("vote", id_, term_);
            if (recv.error_msg() == "recv timeout")
                continue;
            else if (recv.val().vote_granted)
            {
                {
                    std::unique_lock<std::shared_mutex> lock(votes_mtx_);
                    votes_received_++;
                }
                {
                    std::lock_guard<std::mutex> lock(print_mtx);
                    std::cout << id << ":  vote to  " << id_ << std::endl;
                }
            }
            return;
        }
    }
    else if (func == "append")
    {
        int prev_log_index = log_.size() - 2;
        int prev_log_term = 0;
        if (prev_log_index >= 0)
            prev_log_term = log_[prev_log_index].term;
        auto recv = client_[id].call<AppendEntriesRet>("append", term_, id_, prev_log_index, prev_log_term, log_tosent_, commit_index_);
        if (recv.error_msg() == "recv timeout")
        {
            {
                std::lock_guard<std::mutex> lock(print_mtx);
                std::cout << "recv timeout!!!" << std::endl;
            }

            return;
        }
        else
        {

            if (!recv.val().success && [&]()
                {   std::shared_lock<std::shared_mutex> lock(term_mtx_);
                    return recv.val().term >= term_; }())
            {
                {
                    std::unique_lock<std::shared_mutex> lock(state_mtx_);
                    state_ = follower;
                }

                {
                    std::unique_lock<std::shared_mutex> lock(term_mtx_);
                    term_ = recv.val().term;
                }
            }
            return;
        }
    }
}

RequestVoteRet KVStore::vote(int id, int term, int log_index, int log_term)
{
    RequestVoteRet ret;
    if ([&]()
        {   std::shared_lock<std::shared_mutex> lock(term_mtx_); 
            return term_ >= term; }())
    {
        ret.vote_granted = false;
    }
    else
    {
        {
            std::unique_lock<std::shared_mutex> lock(term_mtx_);
            term_ = term;
        }
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
    AppendEntriesRet ret;
    if (entries.empty())
    {
        if ([&]()
            {   std::shared_lock<std::shared_mutex> lock(term_mtx_);
                return term_ <= term; }())
        {
            {
                std::unique_lock<std::shared_mutex> lock(term_mtx_);
                term_ = term;
            }

            if ([&]()
                {   std::shared_lock<std::shared_mutex> lock(state_mtx_);
                    return state_ == candidate; }())
            {
                std::unique_lock<std::shared_mutex> lock(state_mtx_);
                state_ = follower;
            }
            else
            {
                std::lock_guard<std::mutex> lock(emutex_);
                int election_ms = rand() % 151 + 150; // 生成随机整数
                eduration_ = std::chrono::milliseconds(election_ms);
                estart_ = std::chrono::system_clock::now();
                eend_ = estart_ + eduration_;
            }
            ret.success = true;
        }
        else
            ret.success = false;
    }
    ret.term = term_;
    return ret;
}

int KVStore::election_timeout()
{
    int election_ms = rand() % 151 + 150; // 生成随机整数
    eduration_ = std::chrono::milliseconds(election_ms);
    estart_ = std::chrono::system_clock::now();
    state start_state = state_; // 初始状态

    eend_ = estart_ + eduration_;
    while (1)
    {
        // 状态改变了,候选者变跟随者
        {
            std::shared_lock<std::shared_mutex> lock(state_mtx_);
            if (start_state != state_)
                return STATE_CHANGE;
        }

        // 超时了
        if (std::chrono::system_clock::now() >= eend_)
        {
            {
                std::lock_guard<std::mutex> lock(print_mtx);
                std::cout << id_ << ":  electiontimeout!" << std::endl;
            }
            return TIMEOUT;
        }

        // 候选人达到票数要求或投票给别人了
        {
            std::shared_lock<std::shared_mutex> lock(votes_mtx_);
            std::shared_lock<std::shared_mutex> lock1(state_mtx_);
            if (state_ == candidate && (votes_received_ * 2 >= num_nodes_ || voted_for_ != id_))
            {
                return VOTES_GETD;
            }
        }

        {
            std::unique_lock<std::mutex> lock(emutex_);
            eend_ = estart_ + eduration_;
        }
    }
}

int KVStore::heartbeat_timeout()
{
    hstart_ = std::chrono::system_clock::now();
    hduration_ = std::chrono::milliseconds(50); // 50ms发送一次心跳
    std::chrono::system_clock::time_point end = hstart_;
    while (1)
    {
        // 状态改变了，leader变follower
        {
            std::shared_lock<std::shared_mutex> lock(state_mtx_);
            if (state_ != leader || voted_for_ != -1)
                return STATE_CHANGE;
        }

        if (std::chrono::system_clock::now() >= end)
        {
            start_heartbeat();
            {
                std::unique_lock<std::mutex> lock(hmutex_);
                hstart_ = std::chrono::system_clock::now();
            }
            end = hstart_ + hduration_;
        }

        if (is_recvmsg_)
        {
            {
                std::unique_lock<std::mutex> lock(hmutex_);
                hstart_ = std::chrono::system_clock::now();
            }
            end = hstart_;
        }
    }
}
