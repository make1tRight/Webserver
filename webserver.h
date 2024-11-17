#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <sys/socket.h>
#include <netinet/in.h> //网络编程相关
#include <arpa/inet.h>  //IP地址的转换与解析
#include <stdio.h>
#include <unistd.h>     //POSIX操作系统API, 如read, write, close
#include <errno.h>  
#include <fcntl.h>      //文件控制操作
#include <stdlib.h>     //内存管理, 程序退出
#include <cassert>      //调试时验证程序状态
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passWord, string databaseName,
            int log_write, int opt_linger, int trigmode, int sql_name,
            int thread_num, int close_log, int actor_model);
    
    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer* timer);
    void deal_timer(util_timer* timer, int sockfd);
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
    
private:
    // 基础变量
    int m_port;
    char* m_root;
    int m_log_write;        //指定日志写入的模式 -> console or 日志文件
    int m_close_log;
    int m_actormodel;       //指定服务器的工作模式
    
    int m_pipefd[2];
    int m_epollfd;
    http_conn* users;

    // 数据库相关
    connection_pool* m_connPool;    
    string m_users;
    string m_passWord;
    string m_databaseName;
    int m_sql_num;

    // 线程池相关
    threadpool<http_conn>* m_pool;      //指向一个threadpool<http_conn>对象
    int m_thread_num;

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    // 定时器相关
    client_data* users_timer;
    Utils utils;
};
#endif