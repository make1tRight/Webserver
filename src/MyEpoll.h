#ifndef MYEPOLL_H
#define MYEPOLL_H
#include <sys/epoll.h>
#include <vector>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <stdlib.h>
#include "Channel.h"
static const int MAX_EVENT_NUMBER = 10000;

class Channel;
class MyEpoll {
private:
    int m_epollfd;
    epoll_event* events;
public:
    MyEpoll();
    ~MyEpoll();
    
    void addfd(int fd, uint32_t op);
    void updateChannel(Channel* channel);
    std::vector<Channel*> myEpoll_rdylist(int timeout = -1);
};
#endif