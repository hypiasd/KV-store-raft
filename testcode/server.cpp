#include "rpc.h"

int main()
{
    std::vector<int> tmp({12340, 12341, 12342, 12343, 12344});
    // std::vector<int> tmp({12340});
    int num = tmp.size();
    std::vector<std::thread> threads;
    // Rpc kv(0, tmp, 5);
    // kv.start();
    for (int i = 0; i < num; ++i)
    {
        threads.emplace_back([i, &tmp]()
                             { 
        Rpc kv(i, tmp, 5);kv.start(); });
    }
    for (auto &thread : threads)
    {
        if (thread.joinable())
            thread.join();
    }
}