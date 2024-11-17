#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <fstream>
#include <map>
#include <string>
#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"
// #define MAX_FD 20
using namespace std;

class http_conn {
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    enum HTTP_CODE {
        NO_REQUEST,         //请求不完整 -> 需要继续请求报文数据
        GET_REQUEST,        //获得了完整的请求
        BAD_REQUEST,        //语法有错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,     //服务器内部错误
        CLOSED_CONNECTION
    };

    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}
public:
    void init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode,
            int close_log, string user, string passwd, string sqlname);//初始化socket
    void process();                           
    bool read_once();                           //读取浏览器端发来的全部数据
    void close_conn(bool real_close = true);    //关闭http连接
    bool write();                               //响应报文写入函数

    sockaddr_in* get_address() {
        return &m_address;
    }
    // std::vector<std::tuple<std::string, std::string, int>> getLeaderboard(MYSQL*);
    void saveGameData(MYSQL*, char *, char *);

    void initmysql_result(connection_pool* connPool);
    int timer_flag;
    int improv;

private:
    void init();
    HTTP_CODE process_read();                   //从m_read_buf中读取 -> 处理请求报文
    bool process_write(HTTP_CODE ret);          //向m_write_buf写入响应报文数据
    HTTP_CODE parse_request_line(char* text);   //主状态机解析报文中的请求行数据
    HTTP_CODE parse_headers(char* text);        //主状态机解析报文中的请求头数据
    HTTP_CODE parse_content(char* text);        //主状态机解析报文中的请求内容
    HTTP_CODE do_request();                     //生成响应报文

    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();                   //从状态机读取一行 -> 分析是请求报文的哪一个部分

    void unmap();

    //根据响应报文格式生成对应8个部分 -> 由do_request()调用
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL* mysql;
    int m_state;    //读为0, 写为1
    /***数据库相关 */
    // void handle_game_record(const char* p);
    // void parse_game_data(const std::string& request_data, char* userId, char* gameId, int& score, int& time);
    // int save_or_update_game_record(const char* userId, const char* gameId, int score, int time);

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];  //存储读取的请求报文数据
    int m_read_idx;                     //缓存区中m_read_buf中数据的最后一个字节下一个位置
    int m_checked_idx;                  //m_read_buf读取的位置 
    int m_start_line;                   //m_read_buf已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];//存储发出的响应报文数据
    int m_write_idx;                    //buffer中的长度

    CHECK_STATE m_check_state;          //主状态机的状态
    METHOD m_method;                    //请求方法

    //解析报文请求中对应的6个变量
    char m_real_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;

    char* m_file_address;               //读取服务器上的文件地址
    struct stat m_file_stat;            //文件的属性
    struct iovec m_iv[2];               //io向量机制iovec -> 就是内存的起始地址和长度
    int m_iv_count;
    int cgi;                            //是否启用POST
    char* m_string;                     //储存请求头数据
    int bytes_to_send;                  //剩余发送字节数
    int bytes_have_send;                //已发送字节数
    char* doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;
    char sql_user[100];                 //数据库用户名
    char sql_passwd[100];               //数据库密码
    char sql_name[100];                 //数据库名称
};
#endif