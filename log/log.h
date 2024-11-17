#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#include <string>
#include <stdarg.h>
#include <iostream>
#include <cstring>
#include <sys/time.h>
#include "block_queue.h"


using namespace std;

class Log {
public:
    // 公有的实例获取方法
    // 整个系统只需要一个日志对象来记录日志 -> 使用单例模式
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }

    // 初始化日志文件
    // 可选择参数：日志文件、日志缓冲区大小、最大行数和最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size, 
                int split_lines = 5000000, int max_queue_size = 0);
    
    // 异步写日志共有方法 -> 调用私有方法async_write_log
    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
    }

    // 将输出内容按照标准格式整理
    void write_log(int level, const char* format, ...);

    // 强制刷新缓冲区
    void flush(void);
private:
    Log();
    virtual ~Log();

    // 异步写日志方法
    void* async_write_log() {
        string singe_log;
        // 从阻塞队列中取出一条日志内容写入
        while (m_log_queue->pop(singe_log)) {   //不断从日志队列中取出日志内容 -> 存放到single_log
            m_mutex.lock(); //这里是用的c++11风格的互斥锁
            fputs(singe_log.c_str(), m_fp); //将single_log转换为C风格的字符串 -> 写入到m_fp
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128];                 //路径名
    char log_name[128];                 //log文件名
    int m_split_lines;                  //日志最大行数
    int m_log_buf_size;                 //日志缓冲区大小
    long long m_count;                  //日志行数记录
    int m_today;                        //记录当天的时间，按天区分日志文件
    FILE* m_fp;                         //打开log的文件指针
    char* m_buf;                        //要输出的内容(指向日志缓冲区)
    block_queue<string>* m_log_queue;   //阻塞队列
    bool m_is_async;                    //是否同步标志位
    locker m_mutex;                     //同步类
    int m_close_log;                    //是否关闭日志
};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) if (0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush(); } //##允许在不传入可变参数的情况下自动移除前面的逗号; 调用flush刷新日志缓冲区 -> 将日志内容立即写到文件或输出设备中
#endif
