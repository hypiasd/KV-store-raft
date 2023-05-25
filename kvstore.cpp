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
KVStore::KVStore(int id, std::vector<int> &info, size_t num_thread) : Server(info[id])
{
    id_ = id;
    nodes_ = info;
    num_nodes_ = nodes_.size();
    LogEntry tmp;
    tmp.term = 0;
    log_.push_back(tmp); // log索引从1开始
    thread_pool_ = new ThreadPool(num_thread);
    hduration_ = std::chrono::milliseconds(50); // 50ms发送一次心跳
    std::thread server_thread([this](int port)
                              {
        rpc_server_ = new rpc_server(port, std::thread::hardware_concurrency());
        rpc_server_->register_handler("vote", &KVStore::vote, this);
        rpc_server_->register_handler("append", &KVStore::append, this);
        {std::lock_guard<std::mutex> lock(server_cv_mtx_);
        server_cv_.notify_one();}
        {std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "run rpc server on: " << port << std::endl;}
        rpc_server_->run(); },
                              nodes_[id_] + num_nodes_);
    {
        std::unique_lock<std::mutex> lock(server_cv_mtx_);
        server_cv_.wait(lock);
    }
    for (int i = 0; i < num_nodes_; i++)
    {
        if (i != id_)
        {
            rpc_client_[i] = new rpc_client("127.0.0.1", nodes_[i] + num_nodes_);
        }
    }
    thread_pool_->enqueue(&KVStore::apply, this);
    thread_pool_->enqueue(&KVStore::start, this);
    transition(follower);
    server_thread.join();
}

KVStore::~KVStore()
{
    delete thread_pool_;
}

void KVStore::start()
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "run tcp server on: " << port_ << std::endl;
    }
    Server::start();
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
        std::future<int> th[num_nodes_];
        for (int i = 0; i < num_nodes_; i++)
            if (i != id_)
                th[i] = thread_pool_->enqueue_ret([this](int i)
                                                  { return heartbeat_timeout(i); },
                                                  i);
        int ret;
        for (int i = 0; i < num_nodes_; i++)
            if (i != id_)
                ret = th[i].get();
        return ret;
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
    }
}

void KVStore::start_heartbeat(int id)
{
    thread_pool_->enqueue([this, id]()
                          { send2other(id, "append"); });
}

void KVStore::send2other(int id, std::string func)
{
    rpc_client_[id]->connect();
    if (func == "vote")
    {
        while (1)
        {
            {
                std::shared_lock<std::shared_mutex> lock(state_mtx_);
                if (state_ != candidate)
                    break;
            }
            try
            {
                int log_index;
                {
                    std::shared_lock<std::shared_mutex> lock(log_mtx_);
                    log_index = log_.size() - 1;
                }
                int log_term = log_[log_index].term;
                auto recv = rpc_client_[id]->call<RequestVoteRet>("vote", id_, term_, log_index, log_term);

                if (recv.vote_granted)
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
                else
                {
                    if (recv.term > term_)
                    {
                        std::unique_lock<std::shared_mutex> lock(state_mtx_);
                        state_ = follower;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }

            return;
        }
    }
    else if (func == "append")
    {
        LogEntry log_tosent;
        log_tosent.term = 0;
        if (next_index_[id] < log_.size())
        {
            log_tosent = log_[next_index_[id]];
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << "id:  " << id << std::endl;
            std::cout << "next_index_[id]:  " << next_index_[id] << std::endl;
            std::cout << "log size:  " << log_.size() << std::endl;
            std::cout << "log_tosent term:  " << log_tosent.term << std::endl;
        }
        int prev_log_index = next_index_[id] - 1;
        int prev_log_term = 0;
        if (prev_log_index >= 0)
            prev_log_term = log_[prev_log_index].term;

        // {
        //     std::lock_guard<std::mutex> lock(print_mtx);
        //     std::cout << "id:  " << id << std::endl;
        //     std::cout << "index: " << prev_log_index << "  term: " << prev_log_term << std::endl;
        //     std::cout << "log2sent: " << log_tosent.term << std::endl;
        //     std::cout << "log size:  " << log_.size() << std::endl;
        // }
        auto recv = rpc_client_[id]->call<AppendEntriesRet>("append", term_, id_, prev_log_index, prev_log_term, log_tosent, commit_index_);
        if (log_tosent.term)
        {
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << "recv:  " << recv.success << std::endl;
        }

        if (!recv.success)
        {
            if ([&]()
                {   std::shared_lock<std::shared_mutex> lock(term_mtx_);
                        return recv.term > term_; }())
            {
                {
                    std::unique_lock<std::shared_mutex> lock(state_mtx_);
                    state_ = follower;
                }

                {
                    std::unique_lock<std::shared_mutex> lock(term_mtx_);
                    term_ = recv.term;
                }
            }
            else
            {
                next_index_[id]--;
            }
        }
        else if (log_tosent.term)
        {
            {
                std::lock_guard<std::mutex> lock(log_cnt_mtx_);
                log_[next_index_[id]].recv_cnt++;
                if (log_[next_index_[id]].recv_cnt == (num_nodes_ / 2 + num_nodes_ % 2))
                {
                    commit_index_++;
                    {
                        std::lock_guard<std::mutex> lock(print_mtx);
                        std::cout << "commit:  " << commit_index_ << std::endl;
                        std::cout << "log size:  " << log_.size() << std::endl;
                        std::cout << "index:  " << next_index_[id] << std::endl;
                    }
                    std::lock_guard<std::mutex> lock(apply_cv_mtx_);
                    apply_cv_.notify_one();
                }
            }
            {
                std::lock_guard<std::mutex> lock(print_mtx);
                std::cout << "next_index of " << id << ": ++!!" << std::endl;
                std::cout << "log_tosent:  " << log_tosent.term << std::endl;
                std::cout << "log size:  " << log_.size() << std::endl;
                std::cout << "index:  " << next_index_[id] << std::endl;
                std::cout << "cnt:  " << log_[next_index_[id]].recv_cnt << "|" << (num_nodes_ / 2 + num_nodes_ % 2) << std::endl;
            }
            match_index_[id] = next_index_[id];
            next_index_[id]++;
        }
    }
}

RequestVoteRet KVStore::vote(rpc_conn conn, int id, int term, int log_index, int log_term)
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

        {
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << term_ << "|" << term << std::endl;
            std::cout << lindex << "|" << log_index << std::endl;
            std::cout << lterm << "|" << log_term << std::endl;
        }

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
    ret.term = term_;
    return ret;
}

AppendEntriesRet KVStore::append(rpc_conn conn, int term, int lid, int prev_log_index, int prev_log_term, LogEntry entry, int lcommit)
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
        if (entry.func_name == "")
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
        else if (entry.term) // 添加日志
        {
            if (prev_log_term != log_[prev_log_index].term)
            {
                ret.success = false;
            }
            else
            {
                int idx = prev_log_index + 1;
                std::unique_lock<std::shared_mutex> lock(log_mtx_);
                if (idx < log_.size())
                    log_[idx] = entry;
                else
                    log_.emplace_back(entry);
                ret.success = true;
            }
        }
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

int KVStore::heartbeat_timeout(int id)
{
    hstart_[id] = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point end = hstart_[id];
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
            start_heartbeat(id);
            {
                std::unique_lock<std::mutex> lock(hmutex_);
                hstart_[id] = std::chrono::system_clock::now();
            }
            end = hstart_[id] + hduration_;
        }
    }
}

void KVStore::apply()
{
    while (1)
    {
        // {
        //     std::lock_guard<std::mutex> lock(print_mtx);
        //     std::cout << last_applied_ << "|" << commit_index_ << std::endl;
        // }
        if (last_applied_ == commit_index_)
        {
            std::unique_lock<std::mutex> lock(apply_cv_mtx_);
            apply_cv_.wait(lock);
        }
        // {
        //     std::lock_guard<std::mutex> lock(print_mtx);
        //     std::cout << "start apply" << std::endl;
        // }
        for (int i = last_applied_ + 1; i <= commit_index_ && i < log_.size(); i++)
        {
            auto t = log_[++last_applied_];
            {
                std::lock_guard<std::mutex> lock(print_mtx);
                std::cout << id_ << " apply:  " << t.func_name << std::endl;
                std::cout << "key:  " << t.key << std::endl;
                std::cout << "value:  " << t.set_value << std::endl;
            }
            if (t.func_name == "SET")
                kv_store_[t.key] = t.set_value;
            else if (t.func_name == "DEL")
            {
                num_del_ = 0;
                for (auto x : t.del_key)
                {
                    if (kv_store_.find(x) != kv_store_.end())
                    {
                        kv_store_.erase(x);
                        num_del_++;
                    }
                }
            }
        }

        std::lock_guard<std::mutex> lock(req_cv_mtx_);
        req_cv_.notify_one();
    }
}

void KVStore::handle_client(int client_socket, const char *buffer)
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
    if (leader_id_ != id_)
    {
        std::string response_str = "127.0.0.1:" + std::to_string(nodes_[leader_id_]);
        send2client(client_socket, response_str);
        return;
    }
    thread_pool_->enqueue([this, buffer, client_socket]()
                          { handle_all(client_socket, buffer); });
}

void KVStore::handle_all(int client_socket, const char *buffer)
{
    ClientReq request = decode(buffer);
    std::string response_str;
    if (request.success)
    {
        LogEntry log = request.log_info;
        log.term = term_;
        log.recv_cnt = 1;
        {
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << "request:  " << log.func_name << std::endl;
        }
        if (log.func_name == "GET")
        {
            if (kv_store_.find(log.key) == kv_store_.end())
            {
                response_str = "*1\r\n$3\r\nnil\r\n";
            }
            else
            {
                std::string value = kv_store_[log.key];
                std::string resp;
                int start = 0;
                int cnt = 0;
                while (start < value.size())
                {
                    size_t pos1 = value.find(' ', start);
                    if (pos1 == std::string::npos)
                        pos1 = value.size();
                    resp.append("$" + std::to_string(pos1 - start));
                    resp.append("\r\n" + value.substr(start, pos1 - start) + "\r\n");
                    start = pos1 + 1;
                    cnt++;
                }
                response_str = "*" + std::to_string(cnt) + resp;
            }
        }
        else
        {
            {
                std::unique_lock<std::shared_mutex> lock(log_mtx_);
                log_.emplace_back(log);
            }
            {
                std::unique_lock<std::mutex> lock(req_cv_mtx_);
                req_cv_.wait(lock);
            }
            if (log.func_name == "SET")
                response_str = "+OK\r\n";
            else
                response_str = ":" + std::to_string(num_del_) + "\r\n";
        }
    }
    else
        response_str = "-ERROR\r\n";
    send2client(client_socket, response_str);
}

void KVStore::send2client(int client_socket, std::string response_str)
{
    if (send(client_socket, response_str.c_str(), response_str.size(), 0) == -1)
    {
        perror("Send response to client failed");
    }
    close(client_socket);
}

ClientReq KVStore::decode(const char *buffer)
{
    ClientReq ret;
    if (buffer[0] != '*')
    {
        std::cerr << "format is wrong!!!";
        ret.success = false;
        return ret;
    }
    std::string str(buffer);
    int pos1 = str.find("\r\n");
    int num_string = std::atoi(str.substr(1, pos1 - 1).c_str());
    int start = pos1 + 1;
    for (int i = 0; i < num_string; i++)
    {
        int pos2 = str.find("$", start);
        start = pos2 + 1;
        int pos3 = str.find("\r\n", pos2);
        int len = std::atoi(str.substr(pos2 + 1, pos3 - pos2 - 1).c_str());
        if (i == 0)
        {
            ret.log_info.func_name = str.substr(pos3 + 2, len);
        }
        else
        {
            if (ret.log_info.func_name != "DEL")
            {
                if (i == 1)
                    ret.log_info.key = str.substr(pos3 + 2, len);
                else if (i == 2)
                    ret.log_info.set_value.append(str.substr(pos3 + 2, len));
                else
                    ret.log_info.set_value.append(" " + str.substr(pos3 + 2, len));
            }
            else
            {
                ret.log_info.del_key.push_back(str.substr(pos3 + 2, len));
            }
        }
    }
    ret.success = true;
    return ret;
}
