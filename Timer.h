#ifndef __TIMER_H__
#define __TIMER_H__

#include <chrono>
#include <thread>

class Timer
{
public:
    Timer(int ms = 200) : timeout_ms(ms) {}
    void start()
    {
        is_running_ = true;
        thread_ = std::thread(&Timer::run, this);
    }
    void stop()
    {
        is_running_ = false;
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

private:
    int timeout_ms;
    bool is_running_ = false;
    std::thread thread_;

    void run()
    {
        while (is_running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        }
    }
};

#endif // __TIMER_H__