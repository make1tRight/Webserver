#include "lst_timer.h"

// 定时器回调函数 -> 超时则关闭与客户的连接
void cb_func(client_data* user_data) {
    // 删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);

    // 关闭文件描述符
    close(user_data->sockfd);

    // 减少连接数
    http_conn::m_user_count--;
}

// 信号处理函数
void Utils::sig_handler(int sig) {
    // 为了保证函数ud可重入性 -> 保留原来的errno
    // 可重入性表示中断后再次进入这个函数 -> 环境变量不改变 -> 不会丢失数据
    int save_errno = errno;
    int msg = sig;

    // 将信号值从管道写端写入 -> 传输字符类型而非整形
    send(u_pipefd[1], (char*)& msg, 1, 0);
    //还原原来的errno
    errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    //创建sigaction结构体
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    //信号处理函数中仅仅发送信号值，不做对应的逻辑处理
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    
    // 将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);

    // 执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 添加定时器，内部可调用私有成员add_timer
void sort_timer_lst::add_timer(util_timer* timer) {
    // 没有要添加的定时器直接返回
    if (!timer) {
        return;
    }
    // 链表是空的就加timer，head和tail都是他，因为只有一个
    if (!head) {
        head = tail = timer;
        return;
    }

    // 超时时间小于最前面的事件 -> 调换位置 
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 否则调用私有成员 -> 调整内部节点
    add_timer(timer, head);
}

// 私有成员add_timer，可被公有成员add_timer和adjust_timer调用
// 主要作用是调整链表内部结点
void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head) {
    util_timer* prev = lst_head;
    util_timer* tmp = prev->next;

    // 遍历当前结点之后的链表，按照超时时间找到目标定时器对应位置
    // 执行常规双向链表插入操作
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    // 如果目标定时器需要放在尾节点处
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

// 调整定时器
void sort_timer_lst::adjust_timer(util_timer* timer) {
    if (!timer) {
        return;
    }

    util_timer* tmp = timer->next;
    // 如果被调整的定时器在链表的尾部
    // 或者说定时器的超时时间还是比下一个短 -> 不调整
    if (!tmp || timer->expire < tmp->expire) {
        return;
    }

    if (timer == head) {    // 被调整定时器是链表的头节点 -> 将定时器取出后重新插入链表
        head = head->next;
        head->prev = NULL;

        timer->next = NULL;
        add_timer(timer, head);    
    } else {                // 被调整定时器在内部 -> 取出后重新插入链表
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        add_timer(timer, head);
    }
}

// 删除定时器
void sort_timer_lst::del_timer(util_timer* timer) {
    if (!timer) {
        return;
    }

    // 链表中只有一个定时器的情况
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }

    // 被删除的结点是头节点
    if (timer == head) {
        head = head->next;
        head->prev = NULL;

        delete timer;
        return;
    }

    // 被删除结点是尾节点
    if (timer == tail) {
        tail = tail->prev;
        tail->next = NULL;

        delete timer;
        return;
    }

    // 被删除timer在链表内部
    timer->next->prev = timer->prev;
    timer->prev->next = timer->next;
    delete timer;
}

// 定时任务处理函数
void sort_timer_lst::tick() {
    if (!head) {
        return;
    }

    // 获取当前时间
    time_t cur = time(NULL);
    util_timer* temp = head;

    // 遍历定时器链表
    while (temp) {
        // 链表是升序排列的 -> 最早到期的在最前面
        // 如果当前的时间连当前节点都没有达到，后面的timer也不会过期
        if (cur < temp->expire) {
            break;
        }

        // 当前定时器到期 -> 调用回调函数 -> 执行定时事件
        temp->cb_func(temp->user_data);

        // 将处理后的timer从链表中删除, 并重置头节点
        head = temp->next;
        if (head) {
            head->prev = NULL;
        }
        delete temp;
        temp = head;
    }
}

//定时处理任务，重新定时以并不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

// 多任务、处理网络连接等高并发场景都需要设置为非阻塞模式
// 将fd设置为非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 同样有助于高并发的场景（ET模式会让读写尽量多）
// 向内核事件表注册读事件、ET模式、开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;  //设置超时时间
}


void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);    //往connfd上写入数据(报错), flag可发送带外数据
    close(connfd);  //关闭连接
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;