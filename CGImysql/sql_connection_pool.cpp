#include "sql_connection_pool.h"

connection_pool* connection_pool::GetInstance() {
    // 设置成静态局部变量 -> 不会重复创建(单例模式的特点)
    // 只有在第一次调用的时候才会创建静态局部变量(懒汉模式的特点)
    // 如果是饿汉模式 -> 直接在private里面初始化就行了
    static connection_pool connPool;    
    return &connPool;
}

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

void connection_pool::DestroyPool() {
    lock.lock();
    if (connList.size() > 0) {
        list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL* con = *it;   //迭代器解引用就是connList里面所储存的对象, 类型为MYSQL*
            mysql_close(con);   //关闭mysql
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();       //清除装有MYSQL*对象的list
    }
    lock.unlock();
}

void connection_pool::init(string url, string User, string PassWord,
             string DataBaseName, int Port, int MaxConn, int close_log) {
    m_url = url;                    //数据库的主机名或ip
    m_Port = Port;                  //数据库端口
    m_User = User;                  //连接数据库的用户名
    m_PassWord = PassWord;          //连接数据库的密码
    m_DatabaseName = DataBaseName;  //连接数据库名称
    m_close_log = close_log;        //数据库打开状况

    // 按设置的最大连接数来创建数据库连接池
    for (int i = 0; i < MaxConn; ++i) {
        MYSQL* con = NULL;
        con = mysql_init(con);      //mysql_init和mysql_real_connect都是封装在mysql.h头文件里的函数
        if (con == NULL) {  //检查myql_init是否成功
            LOG_ERROR("MySQL Error");
            exit(1);    //1表示程序异常终止或发生错误, 0表示正常结束
        }


        // NULL代表使用TCP连接
        // 0是客户端标志
        // c_str() -> 获取c风格字符串
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(),
                                DataBaseName.c_str(), Port, NULL, 0);
        if (con == NULL) {  //检查mysql_real_connect是否成功
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        connList.push_back(con);
        ++m_FreeConn;
    }

    reserve = sem(m_FreeConn);  //创建信号量 -> 初始值设置为可用的数据库连接数
    m_MaxConn = m_FreeConn;     //刚开始创建SQL的时候全部连接都是可用的
}

// 取出连接后信号量原子-1 -> 当连接池内没有连接则阻塞等待
MYSQL* connection_pool::GetConncetion() {
    MYSQL* con = NULL;

    if (0 == connList.size()) { //没有则直接返回
        return NULL;
    }

    // wait是操作信号量的一种方法 -> 获取信号量
    // 信号量的值 > 0 -> wait使得信号量的值-1
    // 信号量的值 = 0 -> wait会阻塞当前线程, 直到信号量的值变大
    reserve.wait();         //取出连接 -> 信号量原子-1, 0是等待(其实就是P操作)
    lock.lock();            //操作连接池之前要上锁

    con = connList.front(); //获取连接
    connList.pop_front();

    --m_FreeConn;           //空闲的少一个
    ++m_CurConn;            //已使用连接数多一个

    lock.unlock();
    return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL* con) {
    if (NULL == con) {
        return false;
    }

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();
    reserve.post(); //V操作
    return true;
}


connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool) {    //构造函数
    *SQL = connPool->GetConncetion();   //(RAII)在对象创建的时候自动调用构造函数 -> 自动获取连接
    
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() { //RAII机制 -> 在析构函数里面释放资源
    poolRAII->ReleaseConnection(conRAII);   //发生异常情况的时候或超出作用域 -> 自动释放资源
}