
#pragma once
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <exception>
#include <list>
#include <cstdio>
#include "locker.h"

//线程池类，定义成模板类是为了代码的复用， 模板参数T是任务类
template<typename T>
class threadpool
{
private:
    /* data */
    
    //线程的数量
    int m_thread_number;

    //线程池数组，大小为 m_thread_number
    pthread_t * m_threads;

    //请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    std::list< T*> m_work_queue;

    //互斥锁
    locker m_queuelocker;

    //信号量用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;

public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    
    ~threadpool();
    
    bool append(T* request);

private:
    static void* worker(void* args);
    void run();
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests ):
m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL){
    
        if(thread_number <= 0 || max_requests <= 0) {
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];
        if(!m_threads){
            throw std::exception();
        }

        //创建m_thread_number个线程， 并将它们设置为线程脱离
        for(int i=0; i<thread_number; i++){
            printf("create  thread%d\n",i);

            if(pthread_create(m_threads+i, NULL, worker, this) != 0){
                delete []m_threads;
                throw std::exception();
            }

            if(pthread_detach(m_threads[i]) != 0){
                delete []m_threads;
                throw std::exception();
            }
        }
    }

template<typename T>
threadpool<T>::~threadpool(){
    delete []m_threads;
    m_threads = NULL;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();
    if(m_work_queue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_work_queue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){

    threadpool* pool = (threadpool*) arg;

    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_work_queue.empty()){
            m_queuelocker.unlock();
            return;
        }

        T* request = m_work_queue.front();
        m_work_queue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        request->process(); 
    }
}

#endif