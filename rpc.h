#ifndef __RPC_H__
#define __RPC_H__

#include "tcp_server.h"
#include "ThreadPool.h"

class Rpc : public Server
{
public:
    Rpc(int id, std::vector<int> &info, size_t num_thread);
    ~Rpc();
    void start() override;
    void handle_client(int client_socket, const char *buffer) override;
    void handle_all(int client_socket, const char *buffer);
    void sendmsg(int client_socket);
    // virtual bool decode(const char *buffer) = 0;
    // virtual void handle() = 0;
    // virtual void encode() = 0;
    bool decode(const char *buffer);
    void handle();
    void encode();

protected:
    int client_socket_[100]; // 客户端socket
    int num_nodes_;
    int id_;
    std::vector<int> nodes_;
    ThreadPool *thread_pool_;
    std::string response_str_;
    std::string info_;
    std::string func_name_;
    std::string key_;
    std::string value_;
    std::vector<std::string> del_keys_;
};

#endif // __RPC_H__