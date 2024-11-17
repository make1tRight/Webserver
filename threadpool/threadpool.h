#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <pthread.h>
#include <exception>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../http/http_conn.h"

template <typename T>
class threadpool {
public:
    threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request, int state);    //给用户接口 -> 将任务加入到线程池中
    bool append_p(T* request);

private:
    static void* worker(void* arg); //取出任务并执行的函数
    void run();                     //线程池执行任务的过程不需要被外部访问

private:
    int m_thread_number;        //线程数
    pthread_t* m_threads;       //线程是个指针
    int m_max_requests;         //允许的最大请求数
    std::list<T*> m_workqueue;  //工作队列
    locker m_queuelocker;       //互斥锁
    sem m_queuestat;            //表示队列状态
    bool m_stop;                //停止标志
    connection_pool* m_connPool;//数据库连接池
    int m_actor_model;          //模型切换
};

// 线程池的创建
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number, int max_requests):
    m_actor_model(actor_model), m_connPool(connPool), 
    m_thread_number(thread_number), m_threads(NULL), 
    m_max_requests(max_requests) {
    
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];    //线程id初始化
    if (!m_threads) {   //当m_threads为NULL的时候
        throw std::exception(); //delete[] 对NULL是没有效果的，所以这里不用加delete
    }

    for (int i = 0; i < thread_number; ++i) {
        /**
         * 线程池创建的线程数量应该和CPU的数量差不多
         * pthread_create第三个参数是线程的启动函数
         * 必须是一个静态函数, 也就是不能有this指针
         * 但是我们又需要在线程中访问类的动态成员函数, 于是将this指针(类的对象)主动转递给静态函数
         * 这样我们就可以让静态函数调用对象的动态方法
         */
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads; //释放动态分配的内存
            throw std::exception();
        }
        /**
         * 线程分离 -> 脱离与进程中其他线程的同步 -> 脱离线程可以在退出时自动释放占用的系统资源
         * 不需要主线程或其他线程显式调用pthread_join进行回收 -> 提升并发性能
         */
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 线程池的回收
template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*) arg;   //为了能够使用run, 因为run是动态函数
    pool->run();
    return pool;    //为了满足pthread_create的函数签名要求
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait();                 //一直等待直到工作队列中有任务到来
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) {
            continue;
        }

        if (1 == m_actor_model) {           //reactor
            if (0 == request->m_state) {    //如果是读
                if (request->read_once()) { //调用read_once从内核缓冲区中读取来自socket的数据
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {                        //如果是写
                if (request->write()) {     //执行写入操作
                    request->improv = 1;    //标记请求需要进一步处理
                } else {
                    request->improv = 1;    
                    request->timer_flag = 1;//需要定时处理
                }
            }
        } else {                            //proactor
            /**
             * proactor模型在dealwithread中也调用了read_once
             * 但是read_once是在内核缓冲区中读取内核已经处理好的数据
             * 下面proactor是在初始化sql了, 没有处理读写事件
             * 取数据库连接池中的连接
             */
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

template <typename T>
bool threadpool<T>::append(T* request, int state) { //可以设置请求状态
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) {  //超出了队列可容纳的最大请求数量
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;   //http_conn里m_state读为0, 写为1
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    /**
     * 原子操作的方式将信号量+1 -> 提醒有任务要处理
     * 正在调用sem_wait等待信号量的线程会被唤醒
     */
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T* request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
#endif