#ifndef LST_TIMER_H
#define LST_TIMER_H
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/uio.h>
#include <time.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>
#include "../http/http_conn.h"

class util_timer;
struct client_data {
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// 定时器类
class util_timer {
public:
    util_timer(): prev(NULL), next(NULL) {}

public:
    time_t expire;                      //超时时间
    void (* cb_func)(client_data*);     //回调函数 -> 函数指针声明
    client_data* user_data;             //连接资源
    util_timer* prev;                   //前向定时器
    util_timer* next;                   //后继定时器
};

class sort_timer_lst {
public:
    sort_timer_lst(): head(NULL), tail(NULL) {}
    ~sort_timer_lst() {
        util_timer* tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(util_timer* timer);
    void adjust_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    void tick();

private:
    void add_timer(util_timer* timer, util_timer* lst_head);

    util_timer* head;
    util_timer* tail;
}; 


class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //将fd设置为非阻塞
    int setnonblocking(int fd);

    //向内核时间表注册读事件、ET模式、开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以并不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data* user_data);   //函数声明
#endif