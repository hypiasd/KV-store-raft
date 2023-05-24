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
        server_.bind("request", &KVStore::request, this);
        {std::lock_guard<std::mutex> lock(server_cv_mtx_);
        server_cv_.notify_one();}
        {std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "run rpc server on: " << port << std::endl;}
        server_.run(); },
                              nodes_[id_]);
    {
        std::unique_lock<std::mutex> lock(server_cv_mtx_);
        server_cv_.wait(lock);
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
    thread_pool_->enqueue(&KVStore::apply, this);
    server_thread.join();
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
    leader_id_ = -1;
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
    leader_id_ = -1;
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
    leader_id_ = id_;
    for (int i = 0; i < num_nodes_; i++)
    {
        next_index_[i] = log_.size();
        match_index_[i] = 0;
    }
    // 唤醒等待领导者的线程
    {
        std::lock_guard<std::mutex> lock(server_cv_mtx_);
        server_cv_.notify_all();
    }
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

    // {
    //     std::lock_guard<std::mutex> lock(print_mtx);
    //     std::cout << id_ << ":start heartbeat!!!" << std::endl;
    // }
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
        int prev_log_index = next_index_[id];
        int prev_log_term = 0;
        if (prev_log_index >= 0)
            prev_log_term = log_[prev_log_index].term;

        if (log_tosent_.size())
        {
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << id << "  log_tosent:" << std::endl;
            std::cout << log_tosent_[0].term << log_tosent_[0].func_name << log_tosent_[0].key
                      << log_tosent_[0].value << std::endl;
        }
        std::string test = "hahahah";
        std::shared_lock<std::shared_mutex> lock(log_mtx_);
        auto recv = client_[id].call<AppendEntriesRet>("append", test, term_, id_, prev_log_index, prev_log_term, log_tosent_, commit_index_);
        // log_tosent_.clear();
        lock.unlock();
        if (recv.error_msg() == "recv timeout")
        {
            {
                std::lock_guard<std::mutex> lock(print_mtx);
                std::cout << "recv timeout!!!" << std::endl;
            }
        }
        else
        {
            if (!recv.val().success)
            {
                if ([&]()
                    {   std::shared_lock<std::shared_mutex> lock(term_mtx_);
                            return recv.val().term > term_; }())
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
                else
                {
                    next_index_[id]--;
                }
            }
            else
            {
                match_index_[id] = next_index_[id];
                next_index_[id]++;
            }
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

AppendEntriesRet KVStore::append(std::string test, int term, int lid, int prev_log_index, int prev_log_term, vector<LogEntry> entries, int lcommit)
{
    AppendEntriesRet ret;
    if ([&]()
        {   std::shared_lock<std::shared_mutex> lock(term_mtx_);
                return term_ > term; }())
        ret.success = false;
    else
    {
        {
            std::unique_lock<std::shared_mutex> lock(term_mtx_);
            term_ = term;
        }

        leader_id_ = lid;

        {
            std::lock_guard<std::mutex> lock(leaderid_cv_mtx_);
            leaderid_cv_.notify_all();
        }
        // 提交状态
        if (commit_index_ < lcommit)
        {
            commit_index_ = lcommit;
            std::lock_guard<std::mutex> lock(apply_cv_mtx_);
            apply_cv_.notify_one();
        }

        // 如果是心跳
        if (entries.empty())
        {
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
        else // 添加日志
        {
            if (prev_log_term != log_[prev_log_index].term)
            {
                ret.success = false;
            }
            else
            {
                int len1 = entries.size(), len2 = log_.size();
                for (int i = 0, idx = prev_log_index + 1; i < len1; i++, idx++)
                {
                    if (idx < len2)
                    {
                        if (log_[idx].term != entries[i].term)
                        {
                            log_[idx] = entries[i];
                            log_.erase(log_.begin() + idx + 1, log_.end());
                            len2 = log_.size();
                        }
                    }
                    else
                        log_.emplace_back(entries[i]);
                }
                ret.success = true;
            }
        }
    }

    ret.term = term_;
    return ret;
}

ClientReqRet KVStore::request(std::string func, std::string key, std::string value)
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  get request!!!   " << std::endl;
    }
    {
        std::unique_lock<std::mutex> lock(leaderid_cv_mtx_);
        leaderid_cv_.wait(lock, [this]
                          { return leader_id_ != -1; });
    }
    ClientReqRet ret;
    ret.leader_id = leader_id_;
    ret.info = "OK";
    ret.value = "";
    if (leader_id_ != id_)
    {
        ret.leader_id = leader_id_;
        return ret;
    }
    LogEntry tmp = {term_, func, key, value};

    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << id_ << ":  append log!!!   " << std::endl;
    }

    {
        std::unique_lock<std::shared_mutex> lock(log_mtx_);
        log_tosent_.emplace_back(tmp);
    }
    log_.emplace_back(tmp);

    return ret;
    // std::future<int> th[num_nodes_];
    // for (int i = 0; i < num_nodes_; ++i)
    // {
    //     if (i != id_)
    //         th[i] = thread_pool_->enqueue_ret([this, i]() -> int
    //                                           { send2other(i, "append"); return 1; });
    // }
    // // 等待线程结束
    // for (int i = 0; i < num_nodes_; ++i)
    // {
    //     if (i != id_)
    //         int tmp = th[i].get();
    // }
}

ClientReqRet KVStore::set(std::string key, std::string value)
{
    ClientReqRet ret;
}

ClientReqRet KVStore::get(std::string key)
{
    ClientReqRet ret;
}

ClientReqRet KVStore::del(std::string key)
{
    ClientReqRet ret;
    kv_store_.erase(key);
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

        if (log_tosent_.size() || std::chrono::system_clock::now() >= end)
        {
            start_heartbeat();
            {
                std::unique_lock<std::mutex> lock(hmutex_);
                hstart_ = std::chrono::system_clock::now();
            }
            end = hstart_ + hduration_;
        }
    }
}

void KVStore::apply()
{
    while (1)
    {
        if (last_applied_ == commit_index_)
        {
            std::unique_lock<std::mutex> lock(server_cv_mtx_);
            server_cv_.wait(lock);
        }

        for (int i = last_applied_ + 1; i <= commit_index_ || i < log_.size(); i++)
        {
            auto t = log_[++last_applied_];
            if (t.func_name == "set")
                kv_store_[t.key] = t.value;
            else if (t.func_name == "del")
                kv_store_.erase(t.key);
        }
    }
}
