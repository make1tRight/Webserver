#include "WebServer.h"

static const int READ_BUFFER_SIZE = 1024;   //加static只在当前源文件中有效

WebServer::WebServer(EventLoop* _eventloop): eventloop(_eventloop) {
    MySocket* sockfd = new MySocket();
    InetAddress* server_address = new InetAddress("127.0.0.1", 8888);
    sockfd->bind(server_address);
    sockfd->listen();
    sockfd->setnonblocking();

    Channel* server_channel = new Channel(eventloop, sockfd->getfd());
    std::function<void()> cb = std::bind(&WebServer::eventListen, this, sockfd);    //要使用this指针就必须正确绑定WebServer类中的成员函数 -> 加WebServer::
    server_channel->setCallback(cb);
    server_channel->enableReading();/
}

WebServer::~WebServer() {

}

void WebServer::dealwithread(int sockfd) {
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

void WebServer::eventListen(MySocket* sockfd) {
    InetAddress* client_address = new InetAddress();
    MySocket* connfd = new MySocket(sockfd->accept(client_address));

    std::cout << "new client fd: " << connfd->getfd() << "! IP: " 
    << inet_ntoa(client_address->addr.sin_addr) << " Port: "
    << ntohs(client_address->addr.sin_port) << std::endl;

    connfd->setnonblocking();
    Channel* client_channel = new Channel(eventloop, connfd->getfd());
    std::function<void()> cb = std::bind(&WebServer::dealwithread, this, connfd->getfd());

    client_channel->setCallback(cb);
    client_channel->enableReading();
}