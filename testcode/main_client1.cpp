#include <string>
#include <iostream>
#include <ctime>
#include "buttonrpc.hpp"
#include <thread>

#ifdef _WIN32
#include <Windows.h> // use sleep
#else
#include <unistd.h>
#endif

#define buttont_assert(exp)                                                                   \
    {                                                                                         \
        if (!(exp))                                                                           \
        {                                                                                     \
            std::cout << "ERROR: ";                                                           \
            std::cout << "function: " << __FUNCTION__ << ", line: " << __LINE__ << std::endl; \
            system("pause");                                                                  \
        }                                                                                     \
    }

struct PersonInfo
{
    int age;
    std::string name;
    float height;

    // must implement
    friend Serializer &operator>>(Serializer &in, PersonInfo &d)
    {
        in >> d.age >> d.name >> d.height;
        return in;
    }
    friend Serializer &operator<<(Serializer &out, PersonInfo d)
    {
        out << d.age << d.name << d.height;
        return out;
    }
};
struct ClientReqRet
{
    std::string info;
    std::string value;
    int leader_id;

    // must implement
    friend Serializer &operator>>(Serializer &in, ClientReqRet &d)
    {
        in >> d.info >> d.value >> d.leader_id;
        return in;
    }
    friend Serializer &operator<<(Serializer &out, ClientReqRet d)
    {
        out << d.info << d.value << d.leader_id;
        return out;
    }
};
int main()
{
    buttonrpc client[3];

    // int callcnt = 0;
    // for (int i = 0; i < 1; i++)
    // {
    //     client[i].as_client("127.0.0.1", 5555);
    //     client[i].set_timeout(2000);
    //     std::thread([&]()
    //                 { client[i].call<void>("foo_1", i); })
    //         .detach();
    // }
    client[0].as_client("127.0.0.1", 12343);
    client[0].set_timeout(2000);
    auto x = client[0].call<ClientReqRet>("request", "set", "abc", "123");
    std::cout << x.error_msg() << std::endl;
    std::cout << x.val().leader_id << std::endl;
    return 0;
}