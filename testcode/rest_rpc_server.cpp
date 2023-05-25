#include "rest_rpc.hpp"
using namespace rest_rpc;
using namespace rpc_service;
#include <fstream>

struct LogEntry
{
    int term;
    std::string func;
    std::string key;
    std::string value;

    MSGPACK_DEFINE(term, func, key, value);
};
int mytest(rpc_conn conn, std::vector<LogEntry> log)
{
    for (int i = 0; i < 10; i++)
    {
        std::cout << log[i].term << "|" << log[i].func << "|" << log[i].key << std::endl;
    }
    return log.size();
}

int main()
{
    //  benchmark_test();
    rpc_server server(9000, std::thread::hardware_concurrency());
    server.register_handler("mytest", mytest);

    server.run();
}