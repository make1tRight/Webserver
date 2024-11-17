#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include "../lock/locker.h"



template <typename T>
class block_queue {
public:
    // 初始化私有成员
    block_queue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);   //使程序立即终止
        }

        m_max_size = max_size;
        m_array = new T[m_max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue() {
        m_mutex.lock();

        if (m_array != NULL) {
            delete[] m_array;
        }

        m_mutex.unlock();
    }

    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    /**
     * push是生产者
     * 往队列添加元素，需要将所有使用队列的线程唤醒
     * 如果有元素push进队列 -> 生产者生产了一个元素
     * 如果当前没有线程等待条件变量 -> 唤醒无意义
     */
    bool push(const T& item) {
        // 操作共享数据前先加互斥锁 -> 防止其他线程一起访问
        m_mutex.lock();
        if (m_size >= m_max_size) { //检查队列是否已满
            // 发送所有等待该条件变量的线程 -> 唤醒对应线程
            m_cond.broadcast();
            // 加不了了直接解锁
            m_mutex.unlock();
            return false;
        }

        // 将新增数据放在循环数组的对应位置
        // 循环队列 -> 如果m_back到了m_max_size -> +1回到起点
        m_back = (m_back + 1) % m_max_size;
        // 存放在尾部
        m_array[m_back] = item;
        m_size++;

        // 发送所有等待该条件变量的线程 -> 通知队列中有新元素可用
        // 如果有线程在等待队列非空 -> 唤醒这些线程
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    /**
     * pop是消费者
     * 如果当前队列没有元素，那么pop会等待条件变量
     */
    bool pop(T& item) {
        m_mutex.lock();

        // 如果有多个消费者的时候
        while (m_size <= 0) {
            // 当重新抢到互斥锁，pthread_cond_wait返回0
            if (!m_cond.wait(m_mutex.get())) {
                // 不是0说明没有抢到
                m_mutex.unlock();
                return false;
            }
        }

        // 取出队首元素
        m_front = (m_front + 1) % m_max_size;   //其实这个是遍历的过程
        item = m_array[m_front];
        --m_size;
        m_mutex.unlock();
        return true;
    }

    // 增加了超时处理
    bool pop(T& item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();

        if (m_size <= 0) {                              //检查队列是否为空
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {   //等待条件变量直到超时
                m_mutex.unlock();                       //超时或被中断返回false
                return false;
            }
        }

        if (m_size <= 0) {                              //再次检查队列是否为空, 防备其他线程在上个等待期间修改了队列
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        --m_size;
        m_mutex.unlock();
        return true;
    }
    
    int size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    int max_size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;        
    }

    // 判断队列是否满了
    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 判断队列是否为空
    bool empty() {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T& value) {
        m_mutex.lock();

        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T& value) {
        m_mutex.lock();

        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;
    
    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};
#endif