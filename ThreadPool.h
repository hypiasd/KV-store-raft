#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class ThreadPool
{
public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();
    template <typename F, typename... Args>
    auto enqueue_ret(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock{mutex_};
            tasks_.emplace([task]()
                           { (*task)(); });
        }

        cond_.notify_one();

        return result;
    }

    template <typename F, typename... Args>
    void enqueue(F &&f, Args &&...args)
    {
        auto task = std::make_shared<std::function<void()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        {
            std::unique_lock<std::mutex> lock{mutex_};
            tasks_.emplace([task]()
                           { (*task)(); });
        }

        cond_.notify_one();
    }

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_ = false;
};
#endif // __THREADPOOL_H__