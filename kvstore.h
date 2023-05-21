#ifndef __KVSTORE_H__
#define __KVSTORE_H__

#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>
#include "buttonrpc.hpp"
#include <thread>
#include <condition_variable>

struct RequestVoteRet
{
    int term;          // 当前任期号，以便于候选人去更新自己的任期号
    bool vote_granted; // 候选人赢得了此张选票时为真
};
struct AppendEntriesRet
{
    int term;     // 当前任期，对于领导人而言 它会更新自己的任期
    bool success; // 如果跟随者所含有的条目和 prevLogIndex 以及 prevLogTerm 匹配上了，则为 true
};
struct LogEntry
{
    int term;
    std::string func_name;
    std::string key;
    int value;
};
class KVStore
{
public:
    enum state
    {
        follower,
        candidate,
        leader
    };
    KVStore(int id, std::vector<int> &info);
    void transition(state st);                                                                                                 // 状态机切换状态
    void follower_func();                                                                                                      // 跟随者状态逻辑
    void candidate_func();                                                                                                     // 候选者状态逻辑
    void leader_func();                                                                                                        // 领导者状态逻辑
    void start_timer();                                                                                                        // 开启计时器
    void start_election();                                                                                                     // 成为候选者，开始参加竞选
    void start_heartbeat();                                                                                                    // 成为领导者，开始发送心跳
    void send2other(int id, std::string func);                                                                                 // 给其他节点发送信息
    RequestVoteRet vote(int lid, int term, int last_log_index, int last_log_term);                                             // 服务器投票函数
    AppendEntriesRet append(int term, int lid, int prev_log_index, int prev_log_term, vector<LogEntry> &entries, int lcommit); // 追加条目，由领导人调用，用于日志条目的复制，同时也被当做心跳使用
    void election_timeout();
    void heartbeat_timeout();

private:
    //// 状态
    // 所以服务器持久性状态
    int term_ = 1;              // 当前最新任期
    int voted_for_ = -1;        // 投票给哪个节点，-1 表示没有投票
    std::vector<LogEntry> log_; // 日志
    // 所有服务器易失性状态
    int commit_index_ = 0; // 已知提交的最高的日志条目的索引
    int last_applied_ = 0; // 已被应用到状态机的最高的日志条目的索引
    // 领导人上的易失性状态
    std::vector<int> next_index_;  // 对于每一台服务器，发送到该服务器的下一个日志条目的索引（初始值为领导人最后的日志条目的索引+1）
    std::vector<int> match_index_; // 对于每一台服务器，已知的已经复制到该服务器的最高日志条目的索引（初始值为0，单调递增）

    buttonrpc server_;                                      // rpc服务器
    buttonrpc client_[100];                                 // rpc客户端， 用于发送信息
    std::unordered_map<std::string, std::string> kv_store_; // 键值对
    int id_;                                                // 节点id
    int leader_id_ = -1;                                    // 领导者id， 不存在则为-1
    state state_ = follower;                                // 当前身份
    int votes_received_ = 0;                                // 收到的投票数
    int num_nodes_;                                         // 总节点数
    bool etimeout_ = false;                                 // 选举超时
    bool htimeout_ = false;                                 // 心跳超时
    std::chrono::system_clock::time_point estart_;          // 开始选举计时时间
    std::chrono::system_clock::duration eduration_;         // 选举计时超时时间
    std::chrono::system_clock::time_point hstart_;          // 开始心跳计时时间
    std::chrono::system_clock::duration hduration_;         // 心跳计时超时时间
    std::mutex emutex_;                                     // 选举时间锁
    std::mutex hmutex_;                                     // 心跳时间锁
    std::condition_variable cv_;                            // 条件变量
    std::mutex cv_mtx_;                                     // 互斥锁
    std::mutex state_mtx_;                                  // 状态锁
    std::mutex votes_mtx_;                                  // 票数锁

    std::thread etimer_thread_; // 选举定时器线程
    std::thread htimer_thread_; // 心跳定时器线程
    std::vector<int> nodes_;    // 所有节点的信息
};

#endif