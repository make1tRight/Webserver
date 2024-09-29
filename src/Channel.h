#ifndef CHANNEL_H
#define CHANNEL_H
#include <sys/epoll.h>
#include <functional>
#include "MyEpoll.h"
#include "EventLoop.h"

class EventLoop;
class Channel {
private:
    // MyEpoll* m_epollfd;
    EventLoop* eventloop;
    int sockfd;

    uint32_t events;
    uint32_t revents;
    bool inEpoll;

    std::function<void()> callback;

public:
    Channel(EventLoop* _eventloop, int _fd);
    ~Channel();

    void handleEvent();
    void enableReading();
    
    int getfd();
    uint32_t getEvents();
    uint32_t getRevents();
    bool getInEpoll();
    void setInEpoll();

    void setRevents(uint32_t _ev);
    void setCallback(std::function<void()>);
};
#endif