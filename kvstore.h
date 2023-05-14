#ifndef __KVSTORE_H__
#define __KVSTORE_H__

#include <unordered_map>
#include "tcp_server.h"

class KVStore : public Server
{
public:
    void handle_client(int client_socket, const char *buffer) override;

private:
    std::unordered_map<std::string, std::string> kv_store_;
};

#endif