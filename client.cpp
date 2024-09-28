#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <iostream>
#include <unistd.h>
#include <stdio.h>

const int READ_BUFFER_SIZE = 1024;
const int WRITE_BUFFER_SIZE = 1024;

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(8888);
    
    int ret = 0;
    ret = connect(sockfd, (sockaddr*)& server_address, sizeof(server_address));
    assert(ret >= 0);
    
    while (true) {
        char m_write_buf[WRITE_BUFFER_SIZE];
        bzero(&m_write_buf, sizeof(m_write_buf));
        // scanf("%s", m_write_buf);
        std::cin >> m_write_buf;
        
        int bytes_write = 0;
        bytes_write = send(sockfd, m_write_buf, sizeof(m_write_buf), 0);
        // ssize_t bytes_write = write(sockfd, m_write_buf, sizeof(m_write_buf));
        if (bytes_write == -1) {
            std::cout << "socket already disconnected, can't write any more!" << std::endl;
            break;
        }
        char m_read_buf[READ_BUFFER_SIZE];
        bzero(&m_read_buf, sizeof(m_read_buf));
        int bytes_read = 0;
        bytes_read = recv(sockfd, m_read_buf, sizeof(m_read_buf), 0);
        // ssize_t bytes_read = read(sockfd, m_write_buf, sizeof(m_write_buf));
        if (bytes_read > 0) {
            std::cout << "message from server: " << m_read_buf << std::endl;
        } else if (bytes_read == 0) {
            std::cout << "socket already disconnected!" << std::endl;
            break;
        } else if (bytes_read == -1) {
            close(sockfd);
            std::cout << "socket read error" << std::endl;
        }
    }
    close(sockfd);
    return 0;
}
