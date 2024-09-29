#ifndef EVENTLOOP_H
#define EVENTLOOP_H
#include "MyEpoll.h"
#include "Channel.h"

class MyEpoll;
class Channel;
class EventLoop {
private:
    MyEpoll* m_epollfd;
    bool stop_server;
public:
    EventLoop();
    ~EventLoop();

    void loop();
    void updateChannel(Channel* channel);
};

#endif