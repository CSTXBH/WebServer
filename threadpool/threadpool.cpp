
// #include "threadpool.h"

// template<typename T>
// threadpool<T>::threadpool(int thread_number, int max_requests ):
// m_thread_number(thread_number), m_max_requests(max_requests),
//     m_stop(false), m_threads(NULL){
    
//         if(thread_number <= 0 || max_requests <= 0) {
//             throw std::exception();
//         }

//         m_threads = new pthread_t[m_thread_number];
//         if(!m_threads){
//             throw std::exception();
//         }

//         //创建m_thread_number个线程， 并将它们设置为线程脱离
//         for(int i=0; i<thread_number; i++){
//             printf("create  thread%d\n",i);

//             if(pthread_create(m_threads+i, NULL, worker, this) != 0){
//                 delete []m_threads;
//                 throw std::exception();
//             }

//             if(pthread_detach(m_threads[i]) != 0){
//                 delete []m_threads;
//                 throw std::exception();
//             }
//         }
//     }

// template<typename T>
// threadpool<T>::~threadpool(){
//     delete []m_threads;
//     m_threads = NULL;
//     m_stop = true;
// }

// template<typename T>
// bool threadpool<T>::append(T* request){
//     m_queuelocker.lock();
//     if(m_work_queue.size() > m_max_requests){
//         m_queuelocker.unlock();
//         return false;
//     }

//     m_work_queue.push_back(request);
//     m_queuelocker.unlock();
//     m_queuestat.post();
//     return true;
// }

// template<typename T>
// void* threadpool<T>::worker(void* arg){

//     threadpool* pool = (threadpool*) arg;

//     pool->run();
//     return pool;
// }

// template<typename T>
// void threadpool<T>::run(){
//     while(!m_stop){
//         m_queuestat.wait();
//         m_queuelocker.lock();
//         if(m_work_queue.empty()){
//             m_queuelocker.unlock();
//             return;
//         }

//         T* request = m_work_queue.front();
//         m_work_queue.pop_front();
//         m_queuelocker.unlock();

//         if(!request){
//             continue;
//         }

//         request->process(); 
//     }
// }