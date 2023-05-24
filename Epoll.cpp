#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include "Epoll.h"

Epoll::Epoll(int max_events)
{
    // 创建 epoll 实例
    epollfd = epoll_create1(0);
    if (epollfd == -1)
    {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        return;
    }

    // 初始化maxevents
    maxevents = max_events;
    // 初始化events_arr
    events_arr = new epoll_event[maxevents];
    // 初始化数组size
    size = 0;
}

Epoll::~Epoll()
{
    if (epollfd != -1)
    {
        close(epollfd);
    }
    delete events_arr;
}

bool Epoll::Add(int fd, uint32_t events)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | events;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        std::cerr << "Failed to add file descriptor to epoll instance: "
                  << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool Epoll::Remove(int fd)
{
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        std::cerr << "Failed to remove file descriptor from epoll instance: "
                  << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

int Epoll::Wait(int timeout_ms)
{
    int num_events = epoll_wait(epollfd, events_arr, maxevents, timeout_ms);
    if (num_events < 0)
    {
        std::cerr << "Failed to wait on epoll file descriptor: " << strerror(errno) << "\n";
        exit(EXIT_FAILURE);
    }
    return num_events;
}
