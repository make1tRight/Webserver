#include "webserver.h"

WebServer::WebServer() {
    // 创建http_conn类对象
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);   //获取当前工作目录的绝对路径 -> 保存到server_path
    char root[6] = "/root";     //用来配合strcat拼接到工作目录后面(这个文件夹里面放的是网页的文件)
    m_root = (char*) malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);//工作路径保存在m_root中
    strcat(m_root, root);       //拼接

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete[] m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName,
        int log_write, int opt_linger, int trigmode, int sql_name,
        int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_users = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_name;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::thread_pool() {
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::sql_pool() {
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_users, m_passWord,
                     m_databaseName, m_port, m_sql_num, m_close_log);

    /**
     * 初始化数据库读取表
     * 这里就开始取出线程池中的线程 -> 建立连接了
     */
    users->initmysql_result(m_connPool);
}

void WebServer::log_write() {
    if (0 == m_close_log) {     //确认开启了写日志模式
        if (1 == m_log_write) { //异步写日志 -> 设置阻塞队列长度
            Log::get_instance()->init("./ServerLog", m_close_log,
                 2000, 800000, 800);
        } else {                //同步写日志 -> 将阻塞队列长度设置为0
            Log::get_instance()->init("./ServerLog", m_close_log,
                 2000, 800000, 0);
        }
    }
}

void WebServer::trig_mode() {
    if (0 == m_TRIGMode) {          //LT + LT
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    } else if (1 == m_TRIGMode) {   //LT + ET
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    } else if (2 == m_TRIGMode) {   //ET + LT
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    } else if (3 == m_TRIGMode) {   //ET + ET
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::eventListen() {
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);   //创建socket -> IPv4, 流服务(TCP), 都是0
    assert(m_listenfd >= 0);                        //创建socket成功会返回文件描述符

    // 优雅关闭连接 <- setsockopt设置socket选项来实现
    // getsockopt和setsockopt -> 读取或设置socket文件描述符属性的方法
    if (0 == m_OPT_LINGER) {            //不等待未发送的数据
        struct linger tmp = {0, 1};     //不启用SO_LINGER, 延迟关闭时间为1s
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (1 == m_OPT_LINGER) {     //最多等待1s来发送剩余数据
        struct linger tmp = {1, 1};     //启用SO_LINGER, 延迟关闭时间为1s
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);    //host to network long
    address.sin_port = htons(m_port);               //host to network short

    // 启用地址复用 - 端口在TIME_WAIT状态的时候能够立即绑定该端口 -> 快速重启
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));  //强制使用处于TIME_WAIT状态的socket地址
    ret = bind(m_listenfd, (struct sockaddr*)& address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);    //backlog = 5 -> 连接数量超过backlog不受理新的连接
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);    //oneshot=false对应LT模式
    http_conn::m_epollfd = m_epollfd;   //在其他地方也可以访问m_epollfd实例

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);                  //写端非阻塞
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);      //读端阻塞

    utils.addsig(SIGPIPE, SIG_IGN);                     //忽略SIGPIPE -> 防止向已关闭的数据连接读写导致崩溃
    utils.addsig(SIGALRM, utils.sig_handler, false);    //注册定时器信号
    utils.addsig(SIGTERM, utils.sig_handler, false);    //注册终止信号  

    alarm(TIMESLOT);                                    //每TIMESLOT触发一次SIGALRM

    // 工具类 -> 在其他地方也可使用这些资源
    Utils::u_epollfd = m_epollfd;
    Utils::u_pipefd = m_pipefd;
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        // 根据epoll_wait返回的事件类型进行不同处理
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd) {
                bool flag = dealclientdata();
                if (false == flag) {
                    continue;           //处理失败 -> 跳过
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务器端关闭连接，移除对应的定时器
                // HUP -> 读写方向都不能用了
                // RDHUP -> 客户端主动关闭了写方向
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            } else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {   //处理信号
                bool flag = dealwithsignal(timeout, stop_server);   //为什么要处理信号？
                if (false == flag) {
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            } else if (events[i].events & EPOLLIN) {    //处理客户连接上收到的读数据
                dealwithread(sockfd);                   //读取socket的数据
            } else if (events[i].events & EPOLLOUT) {   //响应客户端请求
                dealwithwrite(sockfd);                  //往客户端连接写入数据
            }
        }

        if (timeout) {                                  //如果有定时器超时
            utils.timer_handler();                      //处理定时器相关任务
            LOG_INFO("%s", "timer tick");
            timeout = false;                            //重置超时标志位
        }
    }
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    // 初始化用户信息
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode,
                    m_close_log, m_users, m_passWord, m_databaseName);
    
    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer* timer = new util_timer;             //初始化定时器
    timer->user_data = &users_timer[connfd];        //关联定时器与用户数据
    timer->cb_func = cb_func;                       //定时器超时 -> 调用回调函数(关闭连接)
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;             //设置定时器过期的时间
    users_timer[connfd].timer = timer;              //将设置好的定时器给对应的user
    utils.m_timer_lst.add_timer(timer);             //将定时器添加到链表
}

// 若有数据传输 -> 重新计算超时时间
// 调整定时器在链表中的位置
void WebServer::adjust_timer(util_timer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

// 关闭连接 + 移除对应的定时器
void WebServer::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);   //调用回调函数关闭连接
    if (timer) {
        utils.m_timer_lst.del_timer(timer); //关闭连接需要移除对应的定时器
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

// 处理用户数据 - 创建并连接用户、分配定时器
bool WebServer::dealclientdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode) {    //LT模式, 
        int connfd = accept(m_listenfd, (struct sockaddr*)& client_address, &client_addrlength);
        if (connfd < 0) {                               //创建连接失败
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) {        //超过了最大连接数
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }

        timer(connfd, client_address);                  //分配逻辑单元
    } else {                        //ET模式
        while (1) {
            int connfd = accept(m_listenfd, (struct sockaddr*)& client_address, &client_addrlength);
            if (connfd < 0) {                               //创建连接失败
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD) {        //超过了最大连接数
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

// 处理客户端读端传来的信号
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];

    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {        //出错
        return false;
    } else if (ret == 0) {  //返回的数据长度为0 <- 可能是对端已经关闭了连接
        return false;
    } else {                //读取信号
        for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

// 处理读事件
void WebServer::dealwithread(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    if (1 == m_actormodel) {                            //reactor
        if (timer) {
            adjust_timer(timer);
        }

        // 若监测到读事件 -> 将事件放入请求队列
        m_pool->append(users + sockfd, 0);              //users+sockfd是指向用户的指针
        while (true) {
            if (1 == users[sockfd].improv) {            //需要进一步处理
                if (1 == users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;               //表示处理完成
                break;
            }
        }
    } else {                                            //proactor
        if (users[sockfd].read_once()) {                //向内核缓冲区读取数据
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));                               //记录用户的IP地址
            /**
             * 监测到读事件 -> 将事件放入请求队列
             * 不需要区分01读写事件的原因在于 -> proactor得到的数据是内核已经处理好的
             * 内核或操作系统已经完成了IO操作 -> 程序只需要处理结果
             */
            m_pool->append_p(users + sockfd);

            if (timer) {
                adjust_timer(timer);
            }
        } else {                                        //没有数据可读取
            deal_timer(timer, sockfd);
        }
    }
}

// 处理写数据
void WebServer::dealwithwrite(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    if (1 == m_actormodel) {                            //reactor
        if (timer) {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true) {
            if (1 == users[sockfd].improv) {            //需要进一步处理
                if (1 == users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;               //表示处理完成
                break;
            }
        }
    } else {                                            //proactor
        if (users[sockfd].write()) {                    //写数据成功
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));                               //记录用户的IP地址

            if (timer) {
                adjust_timer(timer);
            }
        } else {                                        //没有数据可读取
            deal_timer(timer, sockfd);
        }
    }
}