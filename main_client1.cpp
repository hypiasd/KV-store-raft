#include <string>
#include <iostream>
#include <ctime>
#include "buttonrpc.hpp"

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

int main()
{
    buttonrpc client;

    int callcnt = 0;
    while (1)
    {
        std::cout << "current call count: " << ++callcnt << std::endl;

        int port;
        std::cout << "port: ";
        std::cin >> port;
        client.as_client("127.0.0.1", port);

        client.set_timeout(2000);

        client.call<void>("foo_1");

        sleep(1);
    }

    return 0;
}