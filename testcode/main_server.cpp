#include <string>
#include <iostream>
#include "buttonrpc.hpp"
#include <thread>
#include <time.h>

#define buttont_assert(exp)                                                                   \
    {                                                                                         \
        if (!(exp))                                                                           \
        {                                                                                     \
            std::cout << "ERROR: ";                                                           \
            std::cout << "function: " << __FUNCTION__ << ", line: " << __LINE__ << std::endl; \
            system("pause");                                                                  \
        }                                                                                     \
    }

// 测试例子
void foo_1(int arg1)
{
    time_t currentTime = time(NULL);

    // 将当前时间转换为本地时间
    struct tm *localTime = localtime(&currentTime);

    // 格式化时间为字符串
    char timeString[100];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", localTime);

    // 打印当前时间
    printf("当前时间: %s\n", timeString);
    std::cout << "start:  " << arg1 << std::endl;
    sleep(5);
}

void foo_2(int arg1)
{
    buttont_assert(arg1 == 10);
}

int foo_3(int arg1)
{
    buttont_assert(arg1 == 10);
    return arg1 * arg1;
}

int foo_4(int arg1, std::string arg2, int arg3, float arg4)
{
    buttont_assert(arg1 == 10);
    buttont_assert(arg2 == "buttonrpc");
    buttont_assert(arg3 == 100);
    buttont_assert((arg4 > 10.0) && (arg4 < 11.0));
    return arg1 * arg3;
}

class ClassMem
{
public:
    int bar(int arg1, std::string arg2, int arg3)
    {
        buttont_assert(arg1 == 10);
        buttont_assert(arg2 == "buttonrpc");
        buttont_assert(arg3 == 100);
        return arg1 * arg3;
    }
};

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

PersonInfo foo_5(PersonInfo d, int weigth)
{
    buttont_assert(d.age == 10);
    buttont_assert(d.name == "buttonrpc");
    buttont_assert(d.height == 170);

    PersonInfo ret;
    ret.age = d.age + 10;
    ret.name = d.name + " is good";
    ret.height = d.height + 10;
    return ret;
}

int main()
{
    buttonrpc server;
    server.as_server(5555);
    std::cout << "run rpc server on: " << 5555 << std::endl;
    server.bind("foo_1", foo_1);
    server.bind("foo_2", foo_2);
    server.bind("foo_3", std::function<int(int)>(foo_3));
    server.bind("foo_4", foo_4);
    server.bind("foo_5", foo_5);

    ClassMem s;
    server.bind("foo_6", &ClassMem::bar, &s);
    server.run();

    // std::cout << "run rpc server on: " << 5555 << std::endl;
    // server.run();

    return 0;
}