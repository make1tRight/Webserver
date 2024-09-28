#ifndef MYSOCKET_H
#define MYSOCKET_H
#include <sys/socket.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include "InetAddress.h"

class MySocket {
private:
    int sockfd;
    int ret;
public:
    MySocket();
    ~MySocket();
    MySocket(int _fd);
    
    void bind(InetAddress* addr);
    void listen();
    int setnonblocking();

    int accept(InetAddress* addr);
    int getfd();
};
#endif