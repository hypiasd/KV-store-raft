#include <iostream>
#include <ratio> //表示时间单位的库
#include <chrono>
#include <ctime>
#include <thread>
using namespace std;
using namespace std::chrono;

int main()
{

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now(); // 获取当前时间点
    std::time_t now_c = std::chrono::system_clock::to_time_t(start);
    std::cout << "当前时间点： " << now_c << std::endl;
    std::chrono::system_clock::duration duration = std::chrono::milliseconds(100); // 设置定时器持续时间为 5 秒
    std::chrono::system_clock::time_point end = start + duration;                  // 计算定时器结束的时间点
    while (std::chrono::system_clock::now() < end)
    {                                                                // 循环等待定时器触发
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 暂停 100 毫秒
    }
    std::chrono::system_clock::time_point end1 = std::chrono::system_clock::now();
    std::time_t now_c1 = std::chrono::system_clock::to_time_t(end1);
    std::cout << "当前时间点： " << now_c1; // 输出时间戳
    return 0;
}
