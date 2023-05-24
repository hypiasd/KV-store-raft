#ifndef __EPOLL_H__
#define __EPOLL_H__

#include <iostream>
#include <sys/epoll.h>

class Epoll
{
public:
    Epoll(int max_events = 1000);
    ~Epoll();

    bool Add(int fd, uint32_t events = 0);
    bool Remove(int fd);

    int Wait(int timeout_ms = -1);

    int maxevents;
    int size;
    epoll_event *events_arr;

private:
    int epollfd;
};

#endif // __EPOLL_H__