#ifndef __SYLAR_THREAD_H__
#define __SYLAR_THREAD_H__

#pragma once
#include "mutex.h"
#include "noncopyable.h"

#include <string>

namespace sylar {

class Thread : public Noncopyable {
    public: 
        // 定义当前类的智能指针
        typedef std::shared_ptr<Thread> ptr;
        // 构造函数, 绑定函数与名称
        Thread(std::function<void()> cb, const std::string& name);
        // 析构
        ~Thread();
    
        // 获取当前线程id
        pid_t getId() const { return m_id; }
        // 获取当前线程名称
        const std::string getName() const { return m_name; }

        // 等待线程结束运行
        void join();
        // 返回正在运行的线程
        static Thread* GetThis();
        static const std::string& GetName(); // return the running thread name
        static void SetName(const std::string& name);   // set the running name
    private:
        static void* run(void* arg);
    private:
        // 线程 id
        pid_t m_id = -1;
        // 线程的标识符
        pthread_t m_thread = 0;
        // 线程需要执行的函数
        std::function<void()> m_cb;
        // 线程名称
        std::string m_name;
        // 线程的信号量
        Semaphore m_semaphore;
};

}

#endif