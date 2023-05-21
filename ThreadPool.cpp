#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t num_threads)
{
    for (size_t i = 0; i < num_threads; ++i)
    {
        threads_.emplace_back([this]
                              {
                while (true) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cond_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) {
                        return;
                    }
                    std::function<void()> task = std::move(tasks_.front());
                    tasks_.pop();
                    lock.unlock();
                    task();
                } });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cond_.notify_all();
    for (auto &thread : threads_)
    {
        thread.join();
    }
}
