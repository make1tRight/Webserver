#include "log.h"

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    if (m_fp != NULL) { //有打开文件 -> 把文件关闭
        fclose(m_fp);
    }
}


// 异步需要设置阻塞队列长度，同步不需要设置
bool Log::init(const char* file_name, int close_log, int log_buf_size, 
    int split_lines, int max_queue_size) {
    
    // 如果设置了max_queue_size -> 设置为异步
    // 异步可以让主线程只需要将日志数据放入队列后就去执行其他任务
    // 不需要等待写入完成再去执行另外的任务
    if (max_queue_size >= 1) {  //说明启用了阻塞队列 -> 主线程可以把日志数据放在这里
        // 设置写入方式flag
        m_is_async = true;

        // 创建并设置阻塞队列长度
        m_log_queue = new block_queue<string> (max_queue_size);
        pthread_t tid;

        // flush_log_thread为回调函数 -> 创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    // 输出内容的长度
    m_log_buf_size = log_buf_size;  //设置日志缓冲区的大小
    m_buf = new char[m_log_buf_size];   //分配日志缓冲区
    memset(m_buf, '\0', sizeof(m_buf)); //初始化缓冲区的内容

    m_split_lines = split_lines;

    time_t t = time(NULL);  //返回Unix纪元(1970.1.1)开始的描述
    struct tm* sys_tm = localtime(&t);  //localtime将time_t转换为本地时间的结构体tm
    // localtime返回的是一个指向struct tm的指针, 而不是一个值或者对象
    struct tm my_tm = *sys_tm;  //保留当前时间信息

    // 从后往前找到第一个/的位置
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};  //用来存储完整的日志文件路径

    // 自定义日志名称
    // 如果输出的文件名没有/ -> 直接将时间+文件名作为日志名
    // 日志文件名不包含目录, 直接写
    if (p == NULL) {    //目标字符缓冲区, 要写入的最大字符数, 格式, 可变参数
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", 
            dir_name, 
            my_tm.tm_year + 1900, 
            my_tm.tm_mon + 1, 
            my_tm.tm_mday, 
            log_name);
    } else {            //有目录 -> 写在规定的目录下
        // 将/的位置向后移动一个位置 -> 复制到logname中
        strcpy(log_name, p + 1);    //获取要写日志的文件名
        // p - file_name + 1是文件所在路径文件夹的长度
        // dir_name 相当于./
        // **获取日志所在文件夹路径**
        strncpy(dir_name, file_name, p - file_name + 1);    //将目录部分复制到dir_name

        // 后面的参数和format有关
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",
            dir_name,
            my_tm.tm_year + 1900,
            my_tm.tm_mon + 1,
            my_tm.tm_mday,
            log_name);
    }

    m_today = my_tm.tm_mday;            //记录m_today -> 判断后续是否需要创建新的日志文件
    m_fp = fopen(log_full_name, "a");   //以追加模式a打开文件

    if (m_fp == NULL) { //打开文件失败
        return false;   //初始化失败
    }
    return true;
}

void Log::write_log(int level, const char* format, ...) {

    // 获取当前时间 -> 保存到my_tm中去
    // timeval结构体一般包含tv_sec(秒)tv_usec(微秒)这2个成员
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);           //获取当前的系统时间储存在now变量中, 第二个参数是指定时区
    time_t t = now.tv_sec;              //提取tv_sec给遍历t, tv_sec是从1970.1.1到当前时间的秒数
    struct tm* sys_tm = localtime(&t);  //赋值给sys_tm
    struct tm my_tm = *sys_tm;          //复制到my_tm变量中
    
    char s[16] = {0};                   //创建字符数组, 用来存储格式化后的时间字符串
    switch (level) {
        case 0: {
            strcpy(s, "[debug]:");
            break;
        }
        case 1: {
            strcpy(s, "[info]:");
            break;
        }
        case 2: {
            strcpy(s, "[warn]:");
            break;
        }
        case 3: {
            strcpy(s, "[error]:");
            break;
        }
        default: {
            strcpy(s, "[info]:");
            break;
        }
    }
    
    /*=====================打开文件=====================*/
    m_mutex.lock();
    ++m_count;

    // 日志不是今天的 或者 日志行数是最大行数的倍数 -> touch新的日志文件继续记录
    // m_split_lines是最大行数
    // tail -> 时间戳(YYYY_MM_DD_)
    // new_log
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fp);   //强迫将缓冲区内的的数据写回参数指定的文件中 -> 避免下一个数据加入到输出缓冲区使得两个文件的数据混在一起
        fclose(m_fp);   //关闭旧的日志文件 -> 维护新的日志文件
        char tail[16] = {0};

        // 格式化日志名中的时间部分 -> YYYY_MM_DD_
        snprintf(tail, 16, "%d_%02d_%02d_",
            my_tm.tm_year + 1900,
            my_tm.tm_mon + 1,
            my_tm.tm_mday);

        // 如果时间不是今天 -> 创建今天的日志
        // 更新m_today 和m_count
        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s",    //new_log是新的日志文件路径, 包含目录名, 当天日期, 日志名称
            dir_name,   //目录名称
            tail,       //时间戳
            log_name);  //new_log = dir_name/2024_08_24_log_name
            m_today = my_tm.tm_mday;            //更新变量m_today为当天的日期
            m_count = 0;                        //因为创造了一个新的日志文件, 所以行号清零
        } else {    //如果是今天 -> 创建新的
            // 超过了最大行数 -> 在之前的日志名基础上加后缀, m_count / m_split_lines
            snprintf(new_log, 255, "%s%s%s.%lld", 
            dir_name,
            tail,
            log_name,
            m_count / m_split_lines);   //同一天的第几个日志文件, dir_name/2024_08_24_log_name.1
        }
        // 追加模式"a"打开新的日志文件 -> 继续记录新的日志内容
        // 如果新创建了日志文件(不管是不同天的日志, 还是同一天的另一个日志文件), 则在新的日志文件编辑
        // 如果没有创建新日志文件 -> 那么就在原来的日志文件末尾编辑, 追加模式不会覆盖原有的内容
        m_fp = fopen(new_log, "a"); 
    }

    m_mutex.unlock();   //文件操作和格式化字符串的操作是比较耗时的 -> 先解锁让其他线程先执行
    /*=====================写日志=====================*/
    // 将传入的format参数赋值给valst -> 便于格式化输出
    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    // 写入内容的格式: 时间+内容
    // 时间格式化, snprintf成功返回写字符的总数, 不包括结尾的null字符
    // snprintf函数限定了可以写入字符数组的最大字符数(size_t size = 48) -> 防止缓冲区溢出
    // 返回的是成功写入字符串的字符总数 -> 如果大于限定的size表示空间不够了
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                    my_tm.tm_year + 1900,
                    my_tm.tm_mon + 1,
                    my_tm.tm_mday,
                    my_tm.tm_hour,
                    my_tm.tm_min,
                    my_tm.tm_sec,
                    now.tv_usec,
                    s); //这行代码的效果是(时间+日志等级)2024-09-13 15:24:35.123456 [info]: 
    
    // 内容格式化 -> 向字符串中打印数据, 数据格式用户自定义
    // 返回到写入到字符数组str中的字符个数(不包括终止符)
    // vsnprintf用于处理变参数列表 -> 在有可变参数的情况下格式化输出
    // snprintf 可以直接传入参数 <- 因为日志每行开头格式都是固定的, 所以可以直接写好传进去
    // vsnprintf 需要预处理参数列表, 这里是valst <- 需要通过参数传入format, 和具体要写入的valst
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';

    log_str = m_buf;    //将完整的日志内容复制给log_str -> 让log_str进行异步或同步写入
    m_mutex.unlock();   //这里就表示写完了 

    // 如果m_is_async == true -> 异步写日志, 否则默认是同步
    // 如果是异步的话 -> 将日志信息加入到阻塞队列中, 同步就先加锁再写
    if (m_is_async && !m_log_queue->full()) {
        // 放入队列中 -> 等待线程进行处理
        // 这里不加锁的原因是, 阻塞队列本身就会自己上锁
        m_log_queue->push(log_str);
    } else {    //如果是同步写, 那么没有队列, 加锁自己写
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);   //加锁后直接将日志写到文件中
        m_mutex.unlock();
    }

    va_end(valst);  //写完了 -> 清理可变参数列表
}

void Log::flush(void) {
    m_mutex.lock();

    // 强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}