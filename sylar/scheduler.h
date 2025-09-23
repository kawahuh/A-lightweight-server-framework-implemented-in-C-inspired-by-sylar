#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include "fiber.h"
#include "thread.h"

namespace sylar {

class Scheduler {
public:
    // 为当前对象设置智能指针
    typedef std::shared_ptr<Scheduler> ptr;
    // 设置锁
    typedef Mutex MutexType;
    // 带参构造
    Scheduler(size_t threads = 2, bool use_caller = true, 
                const std::string name = "");
    virtual ~Scheduler();
    // 获取调度器名称
    const std::string& getName() { return m_name; }
    // 获取当前调度器
    static Scheduler* GetThis();
    // 获取调度器的调度协程
    static Fiber* GetMainFiber();
    // 启动调度
    void start();
    // 停止调度
    void stop();
    // 模板类, 用于调度新的协程或者函数
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            // 在这里上锁, 就不需要在加入调度时上锁了
            MutexType::Lock lock(m_mutex);
            // 将协程或者函数加入调度
            // 这里的 thread 都默认为-1, 表示任意线程均可执行
            // 也是后续优化的地方
            need_tickle = scheduleNoLock(fc, thread);
        }

        // if (need_tickle) {
        //     tickle();
        // }
    }
    // 模板类, 用于批量添加需要调度的内容(协程或者函数)
    template<class InputIterator>
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            while (begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1);
                ++begin;
            }
        }
        // if (need_tickle) {
        //     tickle();
        // }
    }
protected:
    // 调度的核心函数, 在 run 中进行一系列的调度操作
    void run();
    // 唤醒函数, 目前没发现有什么用, 可后续优化
    virtual void tickle();
    // 判断是否需要停止调度
    virtual bool stopping();
    // 没有任务需要调度, 空闲时执行的函数
    virtual void idle();
    // 将运行中的调度器设置为当前调度器对象
    void setThis();
    // 是否有空闲的线程, 若 有 返回 真 
    bool hasIdleThreads() { return m_idleThreadCount > 0;}
private:
    // 模板类, 添加需要调度的函数或者协程
    template<class FiberOrCb> 
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        // need_tickle 不知道什么作用
        // 判断任务池是否为空
        bool need_tickle = m_fibers.empty();
        // 创建一个任务对象
        FiberAndThread ft(fc, thread);
        if (ft.fiber || ft.cb) {
            // 将任务对象添加进任务池中
            m_fibers.push_back(ft);
        }
        return need_tickle;
    }
    // 针对添加的任务是协程或者函数自动分配
    // 基于不同的函数签名
    struct FiberAndThread {
        Fiber::ptr fiber;
        std::function<void()> cb;
        int thread;
        // 协程
        FiberAndThread(Fiber::ptr f, int thr) 
            :fiber(f), thread(thr) { 
        }
        // 协程指针
        FiberAndThread(Fiber::ptr* f, int thr) 
            :thread(thr) {
            fiber.swap(*f);
        }
        // 函数
        FiberAndThread(std::function<void()> f, int thr)
            :cb(f), thread(thr) {
        }
        // 函数指针
        FiberAndThread(std::function<void()>* f, int thr)
            :thread(thr) {
            cb.swap(*f);
        }

        FiberAndThread()
            :thread(-1) {
        }

        void reset() {
            // 交给智能指针自动析构
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
private:
    // 锁
    MutexType m_mutex;
    // 进程池
    std::vector<Thread::ptr> m_threads;
    // 任务池
    std::list<FiberAndThread> m_fibers;
    // 根协程
    Fiber::ptr m_rootFiber;
    // 调度器名称
    std::string m_name;
protected:
    // 线程id池
    std::vector<int> m_threadIds;
    // 线程总数
    size_t m_threadCount = 0;
    // 活跃的线程数
    std::atomic<size_t> m_activeThreadCount = {0};
    // 空闲的线程数
    std::atomic<size_t> m_idleThreadCount = {0};
    // 是否需要停止
    bool m_stopping = true;
    // 是否需要自动停止
    bool m_autoStop = false;
    // 根线程id
    int m_rootThread = 0;
};

}

#endif