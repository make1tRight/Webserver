#include "MySocket.h"

MySocket::MySocket(): sockfd(-1) {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sockfd != -1);
}

MySocket::MySocket(int _fd): sockfd(_fd) {
    assert(sockfd != -1);
}

MySocket::~MySocket() {
    if (sockfd != -1) {
        close(sockfd);
        sockfd = -1;
    }
}

void MySocket::bind(InetAddress* addr) {
    ret = 0;
    ret = ::bind(sockfd, (sockaddr*)& addr->addr, addr->addrlength);
    assert(ret >= 0);
}

void MySocket::listen() {
    ret = 0;
    ret = ::listen(sockfd, SOMAXCONN);
    assert(ret >= 0);
}

int MySocket::setnonblocking() {
    int old_option = fcntl(sockfd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(sockfd, F_SETFL, new_option);
    return old_option;
}

int MySocket::accept(InetAddress* addr) {
    int connfd = ::accept(sockfd, (sockaddr*)& addr->addr, &addr->addrlength);
    assert(connfd >= 0);
    return connfd;
}

int MySocket::getfd() {
    return sockfd;
}