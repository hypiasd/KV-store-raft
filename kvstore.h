#ifndef __KVSTORE_H__
#define __KVSTORE_H__

#include <iostream>
#include <unordered_map>
#include "tcp_server.h"
#include <vector>
#include <chrono>
#include <mutex>

enum state
{
    follower,
    candidate,
    leader
};
class KVStore : public Server
{
public:
    KVStore(int id, std::vector<int> &info, int election_ms, int heartbeat_ms);
    void start_timer();                                                                                                            // 启动计时线程
    void run_timer(std::chrono::system_clock::time_point &start, std::chrono::system_clock::duration duration, std::mutex &mutex); // 开始计时
    void handle_client(int client_socket, const char *buffer) override;
    void reset_election_timer();
    void reset_heartbeat_timer();

private:
    std::unordered_map<std::string, std::string> kv_store_; // 键值对
    int id_;                                                // 节点id
    int term_ = 0;                                          // 任期
    int leader_id_;                                         // 领导者id
    state state_ = follower;                                // 当前身份
    int votes_received_;                                    // 收到的投票数
    int voted_for_;                                         // 投票给哪个节点，-1 表示没有投票
    int num_nodes_;                                         // 总节点数
    bool etimeout_ = false;                                 // 选举超时
    bool htimeout_ = false;                                 // 心跳超时
    std::chrono::system_clock::time_point estart_;          // 开始选举计时时间
    std::chrono::system_clock::duration eduration_;         // 选举计时超时时间
    std::chrono::system_clock::time_point hstart_;          // 开始心跳计时时间
    std::chrono::system_clock::duration hduration_;         // 心跳计时超时时间
    std::mutex emutex_;                                     // 选举时间锁
    std::mutex hmutex_;                                     // 心跳时间锁

    std::thread etimer_thread_; // 选举定时器线程
    std::thread htimer_thread_; // 心跳定时器线程
    std::vector<int> nodes_;    // 所有节点的信息
    std::vector<LogEntry> log_; // 日志
};

#endif