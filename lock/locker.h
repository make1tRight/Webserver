#ifndef LOCKER_H
#define LOCKER_H

#include <exception>    //异常处理+多线程编程
#include <pthread.h>    //提供POSIX线程接口 -> 创建、同步、管理线程
#include <semaphore.h>  //POSIX信号量的接口 -> 线程间的同步和互斥

class sem {
public:
    sem() {     //构造函数
        if (sem_init(&m_sem, 0, 0) != 0) {      //初始化信号量
            throw std::exception();
        }
    }

    sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0) {    //0只在同一进程内共享, num是信号量的初始值
            throw std::exception();
        }
    }
    ~sem() {    //析构函数
        sem_destroy(&m_sem);    //销毁信号量
    }

    bool wait() {   //P
        return sem_wait(&m_sem) == 0;
    }

    bool post() {   //V
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

class locker {  //互斥锁
public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get() {    //传递指针只需要4 or 8字节
        // 使用引用可以避免复制 <- 结构体的复制开销大
        // 而且复制也无法修改原对象只能修改副本
        return &m_mutex; 
    }
private:
    pthread_mutex_t m_mutex;    //pthread_mutex_t是一个结构体
};

class cond {
public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) == 0) {
            throw std::exception();
        }
    }

    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t* m_mutex) {   //m_mutex是用于保护条件变量的互斥锁 -> 保证操作原子性
        int ret = 0;
        /**
         * 用于阻塞等待目标条件变量 -> 直到目标条件被满足并被通知
         * 1. 调用pthread_cond_wait函数之前一定要加锁
         * 2. 将调用线程放入条件变量的等待队列中 -> 将互斥锁解锁 -> 允许自己被挂起时其他线程能获得锁
         * (调用wait的线程被放入条件变量等待队列这段时间内, 没有其他线程能修改条件变量)
         * 3. pthread_cond_wait函数执行成功返回0 -> 成功返回后再次上锁
         */
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }
    
    bool timewait(pthread_mutex_t* m_mutex, struct timespec t) {    //具有超时机制
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t); //执行成功 or 超时返回0
        return ret == 0;
    }

    bool signal() {     //唤醒一个等待目标条件变量的线程
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast() {  //以广播形式唤醒所有等待目标条件的线程
        return pthread_cond_broadcast(&m_cond) == 0;
    }


private:
    pthread_cond_t m_cond;
};
#endif