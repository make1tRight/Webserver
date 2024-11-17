#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H
#include <iostream>
#include <mysql/mysql.h>
#include <list>
#include <stdio.h>
#include <string>
#include <error.h>
#include "../log/log.h"
using namespace std;

class connection_pool { 
public:
    MYSQL* GetConncetion();                 //获取数据库连接
    bool ReleaseConnection(MYSQL* conn);    //释放连接
    int GetFreeConn();                      //获取连接
    void DestroyPool();                     //销毁所有连接

    void saveGameData(const std::string& , const std::string&, int);
 
    /**
     * 让所有线程共享同一个线程池 -> 提高资源的利用率
     * 单例模式确保一个类只有一个实例 -> 这里提供了唯一一个全局访问点
     */
    static connection_pool* GetInstance();

    // 构造初始化函数
    void init(string url, string User, string PassWord,
             string DataBaseName, int Port, int MaxConn, int close_log);
    

private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;          //最大连接数
    int m_CurConn;          //已使用连接数
    int m_FreeConn;         //空闲连接数
    locker lock;
    list<MYSQL*> connList;  //连接池
    sem reserve;

public:
    string m_url;           //主机地址
    string m_Port;          //数据库端口号
    string m_User;          //数据库用户名
    string m_PassWord;      //数据登录密码
    string m_DatabaseName;  //数据库名
    int m_close_log;        //日志开关
};

// 不需要手动释放资源 -> 发生异常情况会自动释放资源
// -> 不需要考虑哪些地方可能退出、可能释放资源, 因为离开作用域会自动释放资源
class connectionRAII {
public:
    connectionRAII(MYSQL** con, connection_pool* connPool); //**可在构造函数内部修改外部指针值
    ~connectionRAII();
private:
    MYSQL* conRAII;
    connection_pool* poolRAII;
};
#endif