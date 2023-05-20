#include "kvstore.h"

std::mutex print_mtx;
int main()
{
    std::vector<int> tmp({12340, 12341, 12342, 12343, 12344});
    // std::vector<int> tmp({12340});
    int num = tmp.size();
    std::vector<std::thread> threads;
    for (int i = 0; i < num; ++i)
    {
        threads.emplace_back([&tmp, i]()
                             { KVStore kv(i, tmp); });
    }
    for (auto &thread : threads)
    {
        if (thread.joinable())
            thread.join();
    }
    // for (int i = 0; i < num; i++)
    // {
    //     std::cout << i << std::endl;
    //     th[i] = std::thread([i, &tmp]()
    //                         { KVStore kv(i, tmp); });
    // }
    // for (int i = 0; i < num; i++)
    // {
    //     th[i].join();
    // }
}