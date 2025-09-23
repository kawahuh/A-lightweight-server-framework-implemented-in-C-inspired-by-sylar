#include "thread.h"
#include "util.h"
#include "mutex.h"
#include <iostream>

namespace sylar {
    // thread_local的作用就是让每个线程空间内独自拥有一个变量, 多线程时不会互相干扰, C++11的特性
    static thread_local Thread* t_thread = nullptr;    
    static thread_local std::string t_thread_name = "SomeDay"; // the runnning name

    Thread* Thread::GetThis() {
        return t_thread;
    }

    const std::string& Thread::GetName() {
        return t_thread_name;
    }

    void Thread::SetName(const std::string& name) {
        // 若当前有线程在运行, 则为其 m_name 赋值
        if (t_thread) {
            t_thread->m_name = name;
        }
        // 否则修改线程空间内唯一的名称 t_thread_name
        // 应该在未初始化线程的时候使用此语句
        t_thread_name = name;
    }

    Thread::Thread(std::function<void()> cb, const std::string& name) 
        :m_cb(cb)
        ,m_name(name) {
        // 若未声明名称, 使用默认名称
        if (name.empty()) {
            m_name = "SomeDay";
        }
        // 调用库函数 pthread_create 创建线程
        // m_thread 是线程的唯一标识符
        // 第二个参数用于设置线程的属性, nullptr 表默认
        // &Thread::run 是绑定的函数, 线程创建后既开始调用
        // this 是传递给线程启动函数(&Thread::run)的参数
        int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
        // rt 作为句柄判断线程是否创建成功
        if (rt) {
            std::cout << "pthread_create failed, rt = " << rt 
                << "name = " << name;
        }
        // 确保线程完全启动并初始化完成后 主线程 继续执行构造线程函数之后的代码
        // 在绑定的 &Thread::run 函数中还有一系列的初始化操作
        // 在 &Thread::run 函数初始化完成之后会调用 m_semaphore.notify() 通知线程结束等待
        m_semaphore.wait();
    }

    Thread::~Thread() {
        // 调用 m_thread 作为线程唯一存在的标识符进行销毁
        // 其他针对线程的操作也要基于 m_thread 去操作, 是库级别的
        if (m_thread) {
            pthread_detach(m_thread);
        }
    }

    void Thread::join() {
        if (m_thread) {
            // 调用库函数等待线程结束运行
            // 第二个参数用于接收线程返回值
            int rt = pthread_join(m_thread, nullptr);
            if (rt) {
                std::cout << "pthread_create failed, rt = " << rt 
                    << "name = " << m_name;
            }
            m_thread = 0;
        }
    }

    void* Thread::run(void* arg) {
        // 接收 pthread_create 中传递的参数
        Thread* thread = (Thread*)arg;
        // 线程空间中当前正在运行的线程 thread_local t_thread 指向此被初始化的线程
        t_thread = thread;
        // 同时为 t_thread_name 赋值
        t_thread_name = thread->m_name;
        // 通过系统调用获取线程 id
        thread->m_id = sylar::GetThreadId();
        // 线程取名不能超过 15 个字符, 为系统中线程赋名
        pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());
        // 安全的获取并清空线程需要执行的函数 m_cb, 在构造线程对象时已经传递赋值
        // 直接交换, 避免了拷贝开销
        // 交换后 thread->m_cb 不再持有函数, 避免了重复执行
        std::function<void()> cb;
        cb.swap(thread->m_cb);
        // 初始化完成, 通知主线程继续执行
        thread->m_semaphore.notify();
        // 调用在线程构造时传递的 m_cb 函数, 也就是线程实际要执行的函数, 由主线程确定
        cb();
        return 0;
    }
}