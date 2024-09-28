#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include "MySocket.h"
#include "InetAddress.h"
#include "MyEpoll.h"
#include "Channel.h"


static const int READ_BUFFER_SIZE = 1024;   //加static只在当前源文件中有效

void dealwithread(int sockfd);

int main() {
    InetAddress* server_address = new InetAddress("127.0.0.1", 8888);
    
    MySocket* sockfd = new MySocket();
    sockfd->bind(server_address);
    sockfd->listen();
    sockfd->setnonblocking();

    MyEpoll* m_epollfd = new MyEpoll();
    Channel* server_channel = new Channel(m_epollfd, sockfd->getfd());
    // m_epollfd->addfd(sockfd->getfd(), EPOLLIN | EPOLLET);
    server_channel->enableReading();

    while (true) {
        std::vector<Channel*> activeChannels = m_epollfd->myEpoll_rdylist();
        int number = activeChannels.size();

        for (int i = 0; i < number; ++i) {
            int chnnfd = activeChannels[i]->getfd();
            if (chnnfd == sockfd->getfd()) {
                InetAddress* client_address = new InetAddress();
                MySocket* connfd = new MySocket(sockfd->accept(client_address));

                printf("new client fd %d! IP: %s Port: %d\n", connfd, inet_ntoa(client_address->addr.sin_addr), ntohs(client_address->addr.sin_port));

                connfd->setnonblocking();
                Channel* client_channel = new Channel(m_epollfd, connfd->getfd());
                client_channel->enableReading();
                // m_epollfd->addfd(connfd->getfd(), EPOLLIN | EPOLLET);

            } else if (activeChannels[i]->getRevents() & EPOLLIN) {
                dealwithread(activeChannels[i]->getfd());
            } else {
                std::cout << "something else happened" << std::endl;
            }
        }
    }
    delete server_address;
    delete sockfd;
    return 0;
}

void dealwithread(int sockfd) {
    char m_read_buf[READ_BUFFER_SIZE];
    while (true) {
        bzero(&m_read_buf, sizeof(m_read_buf));
        int bytes_read = 0;
        bytes_read = recv(sockfd, m_read_buf, sizeof(m_read_buf), 0);

        if (bytes_read > 0) {
            std::cout << "message from client fd" <<
                sockfd << ": " << m_read_buf << std::endl;
            send(sockfd, m_read_buf, sizeof(m_read_buf), 0); 
        } else if (bytes_read == 0) {
            std::cout << "EOF, client fd:" << sockfd << " disconnected" << std::endl;
            close(sockfd);
            break;
        } else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::cout << "finish reading once, errno: " << errno << std::endl;
            break;
        } else if (bytes_read == -1 && errno == EINTR) {
            std::cout << "continue reading" << std::endl;
            continue;
        }
    }
}
