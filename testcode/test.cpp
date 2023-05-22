#include <iostream>
#include <chrono>
#include <unistd.h>
#include <thread>

int main()
{
    while (1)
    {
        // 获取当前时间点
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        auto hduration_ = std::chrono::milliseconds(50); // 50ms发送一次心跳
        auto end = now + hduration_;
        // 将时间点转换为毫秒（以 std::chrono::milliseconds 表示）
        auto duration = now.time_since_epoch();
        auto duration1 = end.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        auto ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(duration1).count();
        // 打印毫秒数
        std::cout << "当前时间（毫秒）：" << ms << "||" << ms1 << "||" << (std::chrono::system_clock::now() >= end) << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
