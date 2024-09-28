#ifndef CHANNEL_H
#define CHANNEL_H
#include <sys/epoll.h>
#include "MyEpoll.h"

class MyEpoll;
class Channel {
private:
    MyEpoll* m_epollfd;
    int sockfd;

    uint32_t events;
    uint32_t revents;
    bool inEpoll;

public:
    Channel(MyEpoll* _ep, int _fd);
    ~Channel();

    void enableReading();
    
    int getfd();
    uint32_t getEvents();
    uint32_t getRevents();
    bool getInEpoll();
    void setInEpoll();

    void setRevents(uint32_t _ev);
};
#endif