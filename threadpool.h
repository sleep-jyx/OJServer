#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include <string.h>

#include "locker.h"

template <typename T>
class threadpool
{
public:
    //构造函数:创建线程池，默认8个线程，最多有10000个客户连接
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *request); //往请求队列中添加任务

private:
    //静态函数
    static void *worker(void *arg); //工作线程运行的函数，不断从工作队列中取出任务并执行
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //线程池标识符数组
    std::list<T *> m_workqueue; //请求队列，模板参数类，双向队列
    locker m_queuelocker;       //保护请求队列的互斥锁，一把锁适用所有线程，因为所有线程都是访问的同一个请求队列
    sem m_queuestat;            //信号量
    bool m_stop;                //结束线程
};

//构造函数
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL)
{
    //printf("--debug--:线程池构造函数...\n");
    //printf("--debug--:线程池中的线程数:%d\n", m_thread_number);
    if ((thread_number <= 0) || (max_requests <= 0))
    {
        throw std::exception();
    }
    //线程池标识符数组
    m_threads = new pthread_t[m_thread_number];

    if (!m_threads)
    {
        throw std::exception();
    }
    //创建thread_number个线程，将它们都设置为脱离线程
    for (int i = 0; i < thread_number; ++i)
    {
        // printf("create the %dth thread\n", i);
        //启动worker()工作线程
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        { //创建线程失败
            // printf("--debug--:创建线程失败\n");
            delete[] m_threads;
            throw std::exception();
        }
        // printf("--debug--:创建的线程标识符=%d\n", (int)*(m_threads + i));
        int ret;
        if (ret = pthread_detach(m_threads[i])) //脱离线程不必使用pthread_join回收，在线程函数退出或pthread_exit时自动释放资源
        {                                       //将线程设置为脱离线程失败
            //printf("--debug--:设置为脱离线程失败\n");
            //printf("--debug--:ret=%d\n", ret);
            if (ret != 0)
            {
                printf("pthread_detach error:%s\n", strerror(ret));
                return;
            }
            delete[] m_threads;
            throw std::exception();
        }
    }
}

//析构函数
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

//向工作队列中添加请求
template <typename T>
bool threadpool<T>::append(T *request)
{ //操纵工作队列时要加锁
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    { //如果请求队列已经满了，不允许再添加任务
        printf("--debug--:工作队列已满\n");
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); //工作队列有任务了，信号量增加
    printf("--debug--:工作队列来请求了,队列任务数=%d\n", (int)m_workqueue.size());
    return true;
}

//工作线程
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    //拷贝构造函数？
    //worker()线程的线程池依然是当前池？
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();   //等待请求队列有任务处理
        m_queuelocker.lock(); //请求队列上锁
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        //取出请求队列最前面的任务进行处理
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
        { //如果请求为空，进入下一个循环
            continue;
        }
        //处理请求
        printf("--debug--:某个线程竞争到了该任务，调用http状态机进行解析处理\n");
        request->process(); //核心
    }
}

#endif