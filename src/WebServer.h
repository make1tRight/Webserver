#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <functional>
#include "MySocket.h"
#include "Channel.h"
#include "EventLoop.h"

class WebServer {
private:
    EventLoop* eventloop;
public:
    WebServer(EventLoop* _eventloop);
    ~WebServer();

    void dealwithread(int fd);
    void eventListen(MySocket* sockfd);
};
#endif