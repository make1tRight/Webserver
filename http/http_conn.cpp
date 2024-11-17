#include "http_conn.h"
#include <mysql/mysql.h>

// 定义http响应的状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char *error_403_title = "Forbidden";
const char *error_404_title = "Not Found";
const char *error_500_title = "Internal Error";

const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/**
 * 这个初始化负责重置内部状态, 与外部资源无关
 * 初始化新接受的连接, check_state默认是分析请求行的状态
 */
void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}



bool http_conn::read_once() {       //向内核缓冲区读取数据, 注意:不管这里面的数据是否内核已经处理好, 都是放在内核缓冲区里的 -> 所以proactor和reactor模型都要调用这个函数
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;

    if (0 == m_TRIGMode) {          //LT读取数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);         
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    } else {                        //ET读数据
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);            
            if (bytes_read == -1) {
                /**
                 * 要一直读到返回EWOULDBLOCK
                 * 确保所有数据已经读完, 因为ET模式只会提醒一次
                 */
                if (errno == EAGAIN || errno == EWOULDBLOCK) 
                    break;
                return false;
            } else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

/*--------epoll相关代码----------*/

//非阻塞模式
int setnonblockting(int fd) {
    int old_option = fcntl(fd, F_GETFL);     //获取fd的状态标志
    int new_option = old_option | O_NONBLOCK;//设置为非阻塞的
    fcntl(fd, F_SETFL, new_option);          //设置fd的状态标志
    return old_option;                       //返回旧的状态标志以便复原
}

//内核事件表注册新事件 -> 让内核监控和管理各种系统事件和资源的状态变化
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd; //用于配置要注册的事件类型和相关fd

    // 引入变量TRIGMode可以在程序运行过程中动态调整触发模式
    if (1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot) { //启用oneshot模式 -> 防止相同事件被重复触发
        /**
         * 保证每个socket在任意时刻都只会被一个线程服务
         * 一旦epoll通知应用程序某事发生 -> 文件描述符的监控被暂停 -> 直到程序重新启用
         * 用一次 -> 重置 -> 再次使用
         */
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblockting(fd);
}

// 初始化外部资源
void http_conn::init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode, int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    ++m_user_count;

    // 当浏览器出现连接重置 -> 可能是网页根目录出错/http响应格式出错或者访问的文件内容为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 向内核事件表删除事件
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);  //手动释放文件描述符资源
}

// 向内核事件表修改事件
void modfd(int epollfd, int fd, int ev, int TRIGMode) {   //ev是原来的事件类型
    epoll_event event;
    event.data.fd = fd;
// #ifdef ET
//     event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;//重置ONESHOT
// #endif
// #ifdef LT
//     event.events = ev | EPOLLONESHOT | EPOLLRDHUP;  //RDHUP是用来看对面是否关闭连接的
// #endif
    if (1 == TRIGMode) {    //设置触发模式为ET
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    } else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
/**
 * 浏览器端发出HTTP请求
 * 服务器端构建HTTP对象 -> 将数据读取到buffer中
 * 将任务加入到请求队列
 * 工作线程将任务从请求队列中取出 -> 使用process函数进行处理
 */
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {   //请求不完整 -> 继续接收请求数据
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    //调用process_write完成报文响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    //注册并监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

/**
 * 主状态机
 * 主状态机的三种状态m_check_state
 * 1. CHECK_STATE_REQUESTLINE -> 解析请求行
 * 2. CHECK_STATE_HEADER -> 解析请求头
 * 3. CHECK_STATE_CONTENT -> 解析消息体(仅用于解析POST请求)
 */
http_conn::HTTP_CODE http_conn::process_read() {

    //初始化从状态机的状态、http请求解析的结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    //parse_line在从状态机的具体实现
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
            ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        //m_start_line是每一个数据行在m_read_buf中的起始位置
        m_start_line = m_checked_idx;   //m_checked_idx是从状态机在m_read_buf中读取的位置
        LOG_INFO("%s", text);
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE: {         //解析请求行
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER: {              //解析请求头
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {    //完整解析GET请求以后 -> 跳转到报文响应函数
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT: {             //解析消息体
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;            //完成报文解析 -> 更新line_status为LINE_OPEN避免再次进入循环
                break;
            }

            default: {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

/**
 * 从状态机 -> 用于分析一行内容
 * 返回值是行的读取状态LINE_OK LINE_OPEN LINE_BAD
 * LINE_OK -> 读取到了完整一行
 * LINE_OPEN -> 行不完整要继续接收
 * LINE_BAD -> 请求有误
 */
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {       //m_read_idx指向的是缓冲区m_read_buf数据末尾的下一个字节
        temp = m_read_buf[m_checked_idx];                       //跳到要分析的那个字节
        if (temp == '\r') {                                     //当前是\r   \r\n是行末的标志 说明有可能读完一整行
            if ((m_checked_idx + 1) == m_read_idx) {            //下一个字节就是buffer末尾 -> 这行没读完
                return LINE_OPEN;                               //返回open -> 标识没有读完
            } else if (m_read_buf[m_checked_idx + 1] == '\n') { //\r\n是行末
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;                                 //读取到了完整的一行
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {   //第一个条件是防止空行的时候
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';             //这里改完是到了m_checked_idx的下一个位置
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;   //没有找到\r\n -> 要继续接收
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    //请求行中包含要访问的资源、HTTP协议版本号，两者用\t或者空格分割
    m_url = strpbrk(text, " \t");  //查找字符串中第一个匹配上字符集的字符
    //string pointer break -> 任意字符第一次出现的位置

    if (!m_url) {
        return  BAD_REQUEST;
    }

    *m_url++ = '\0';    //用于将前面的数据取出
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {
        return BAD_REQUEST;
    }

    //让指针m_url跳过" \t"
    m_url += strspn(m_url, " \t");  //返回有多少个" \t"
    //string span -> 包含" \t"的最大长度
    //判断版本号
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {   //适用于比较完整的字符串
        return BAD_REQUEST;
    }

    //对请求资源的前7个字符进行判断
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        //string character -> 字符第一次出现的位置
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {    //单独的/也可以访问资源
        return BAD_REQUEST;
    }

    if (strlen(m_url) == 1) {           //当url=/的时候，显示欢迎页面//在www.xxx.com后面这个斜杠加上judge.html
        //string concatenate -> 将"judge.html"连接到目标字符串的末尾
        strcat(m_url, "judge.html");
    }

    //请求处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的header
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    
    //判断是空行还是请求头
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;    //只有在消息体确定不为0的情况下才有必要去解析消息体
            return NO_REQUEST;  //否则在结果是不需要每一个http请求都去解析消息体的
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;

        //跳过空格和\t
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        LOG_INFO("oop! unknow header: %s\n", text);
    }

    return NO_REQUEST;  //代表请求不完整，还需要继续接收数据
}

http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) { //已经读取了完整的请求体
        text[m_content_length] = '\0';  //用于终止字符串
        m_string = text;    //保存用户名和密码
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// const char* doc_root = "/root/workspace/TinyWebServer/root";
http_conn::HTTP_CODE http_conn::do_request() {
    //将初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    //找到m_url中的/位置
    //string reverse character -> 查找'/'最后一次出现的位置
    const char* p = strrchr(m_url, '/');    //

    // 实现登录和注册校验
    // http://localhost/2?user=alice&password=secret123 问号前的2用于跳转到对应页面
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断 -> 登录检测 or 注册检测
        // 分配200字节的内存 sizeof(char) = 1字节
        char* m_url_real = (char*) malloc(sizeof(char) * 200);   //指向字符数组的指针(后面是x)
        strcpy(m_url_real, "/");            //开头是根目录
        // m_url+2 -> ?user=alice&password=secret123
        strcat(m_url_real, m_url + 2);      //将m_url第二个字符开始的字符串拼接到m_url_real后面 
        // 将m_url_real拼接到m_real_file的最末尾 -> 形成一个完整的文件路径  
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);                   //释放内存 -> 防止内存泄漏

        // 提取用户名和密码
        char name[100], password[100];
        int i;

        // 以&为分隔符, user=123&password=123
        for (i = 5; m_string[i] != '&'; ++i) {  //跳过user=
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0'; //结尾加终止符

        // 以&为分隔符, 后面是密码
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j) {   //跳过password=
            password[j] = m_string[i];
        }
        password[j] = '\0'; //结尾加终止符

        if (*(p + 1) == '3') {
            // 如果是注册 -> 检查数据库中是否有重名
            // 如果没有重名 -> 增加数据
            // sql_insert用于装载一个SQL语句
            char* sql_insert = (char*) malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);   //创建成功 -> 0; 失败-> 返回错误信息
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res) { //如果创建成功 -> 跳转登录界面
                    strcpy(m_url, "/log.html");
                } else {    //校验失败 -> 跳转注册失败页面
                    strcpy(m_url, "/registerError.html");
                }
            } else {        //已经有重复的用户了 -> 跳转注册失败页面
                strcpy(m_url, "/registerError.html");
            }
            
        } else if (*(p + 1) == '2') {
            // 如果是登录 -> 直接判断
            // 如果浏览器端的用户和密码可以查到 -> 返回1; 否则返回0
            if (users.find(name) != users.end() && users[name] == password) {   //有这个用户
                strcpy(m_url, "/welcome.html");
                /**记录游戏时长 */
                char duration[100];
                saveGameData(mysql, name, duration);
                /************* */
            } else {    //没有这个用户
                strcpy(m_url, "/logError.html");
            }
            
        }
    }

    //如果请求资源是/0 -> 跳转注册界面
    if (*(p + 1) == '0') {
        char* m_url_real = (char*) malloc(sizeof(char)* 200);
        strcpy(m_url_real, "/register.html");

        //将网站目录和/register.html进行拼接 -> 更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '1') {   //请求资源是/1 -> 跳转登陆界面
        char* m_url_real = (char*) malloc(sizeof(char)* 200);   //动态分配200字节的内存
        strcpy(m_url_real, "/log.html");

        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '5') {   
        char* m_url_real = (char*) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);

        /***添加数据库保存游戏记录的逻辑 */
        // **解析游戏数据并保存到数据库**
        // 获取排行榜数据   
        // auto leaderboard = getLeaderboard(mysql);
        // std::ofstream outfile(m_real_file);
        // outfile << "<html><body><h1>Leaderboard</h1><table border=\"1\">";
        // outfile << "<tr><th>Username</th><th>Date</th><th>Duration (s)</th></tr>";
        // for (const auto& entry : leaderboard) {
        //     outfile << "<tr><td>" << std::get<0>(entry) << "</td><td>"
        //             << std::get<1>(entry) << "</td><td>"
        //             << std::get<2>(entry) << "</td></tr>";
        // }
        // outfile << "</table></body></html>";
        // outfile.close();
        /**************************** */

    } else if (*(p + 1) == '6') {   
        char* m_url_real = (char*) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '7') {   
        char* m_url_real = (char*) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else {
        //既不是登录界面又不是注册界面 -> 跳转到welcome界面
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    //通过stat获取请求资源文件信息 -> 将信息更新到m_file_stat结构体中
    //失败返回NO_RESOURCE状态 -> 表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0) {
        return  NO_RESOURCE;
    }

    //判断文件权限 -> 不可读 -> 返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH)) { //Is Read permission for OTHers
        return FORBIDDEN_REQUEST;
    }

    //判断文件类型 -> 是目录 -> 返回BAD_REQUEST(表示请求报文有误)
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    //以只读方式获取fd, 通过mmap将文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    //避免文件描述符的占用和浪费
    close(fd);

    //表示请求文件存在 -> 可访问
    return FILE_REQUEST;
}

//根据响应报文格式生成对应8个部分 -> 由do_request()调用
bool http_conn::add_response(const char* format, ...) {
    //写入的内容超过了m_write_buf缓冲区的大小
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }

    //定义可变参数列表
    va_list arg_list;
    //将变量arg_list初始化传入参数
    va_start(arg_list, format);

    //将数据format从可变参数列表写入缓冲区中 -> 返回摄入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, 
        WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    //写入的数据长度超过了缓冲区剩余空间 -> 报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);   //结束对可变参数列表的访问
        return false;
    }

    //更新m_write_idx的位置/
    m_write_idx += len;
    //清空可变参数列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

//添加状态行
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//添加消息报头，具体地，添加文本长度、连接状态、空行
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) &&
            add_linger() &&
            add_blank_line();
}

//添加Content-Length -> 表示响应报文的长度
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}

// 添加文本类型
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加连接状态
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

//添加空行
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

//添加文本
bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;

                // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;

                m_iv_count = 2;
                // 发送全部数据为响应报文头部和文件大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default: {
            return false;
        }
    }

    //除FILE_REQUEST状态之外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::write() {   //写数据
    int temp = 0;

    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1) {
        //writev函数返回写入fd的字节数
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0) {
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }        

        if (bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

// 从数据库中获取所有用户名和密码 -> 存储在服务器的map中
void http_conn::initmysql_result(connection_pool* connPool) {
    // 从连接池中取一个连接
    MYSQL* mysql = NULL;
    connectionRAII mysqlconn(&mysql, connPool); //利用构造函数自动调用GetConnection -> 调用连接

    // 在user表中检索username, passwd数据, 浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {   //这里是SQL语句
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES* result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组 -> 字段名、类型等
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行 -> 将对应用户名和密码存到map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {   //逐行获取数据并加入到map
        string temp1(row[0]);
        string temp2(row[1]);

        users[temp1] = temp2;   //这个是我们的map
    }
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//关闭http连接
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {   //有正在进行的socket
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        --m_user_count;
    }
}


void http_conn::saveGameData(MYSQL* conn, char *username, char *duration) {
    char *sql_insert = (char*) malloc(sizeof(char) * 200);
    /**
     * INSERT INTO leaderboard(username, date, duration) VALUES(
     * 'username', NOW(), 'duration');
     */

    strcpy(sql_insert, "INSERT INTO leaderboard(username, date, duration) VALUES(");
    strcat(sql_insert, "'");
    strcat(sql_insert, username);
    strcat(sql_insert, "', NOW(), ");
    strcat(sql_insert, duration);
    strcat(sql_insert, "')");

    // if (mysql_query(conn, sql_insert)) {
    //     std::cerr << "Error inserting game data: " << mysql_error(conn) << std::endl;
    // }
    m_lock.lock();
    int res = mysql_query(mysql, sql_insert);
    m_lock.unlock();
    if (!res) {
        std::cerr << "insert game date error" << std::endl;
    }

}

// std::vector<std::tuple<std::string, std::string, int>> http_conn::getLeaderboard(MYSQL* conn) {
//     std::vector<std::tuple<std::string, std::string, int>> leaderboard;

//     // 查询排行榜数据
//     if (mysql_query(conn, "SELECT username, date, duration FROM leaderboard ORDER BY duration DESC LIMIT 10;")) {
//         std::cerr << "Error fetching leaderboard: " << mysql_error(conn) << std::endl;
//         return leaderboard;
//     }

//     MYSQL_RES* res = mysql_store_result(conn);
//     MYSQL_ROW row;
    
//     while ((row = mysql_fetch_row(res))) {
//         leaderboard.push_back(std::make_tuple(row[0], row[1], std::stoi(row[2])));
//     }
//     mysql_free_result(res);

//     return leaderboard;
// }