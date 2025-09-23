#include "scheduler.h"
#include "macro.h"
#include "log.h"
#include "hook.h"

#include <iostream>

namespace sylar {

static thread_local Scheduler* t_schedular = nullptr;
static thread_local Fiber* t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string name) 
        :m_name(name) {
    SYLAR_ASSERT(threads > 0);

    if (use_caller) {
        // 若主线程也加入调度
        // 先创建一个主协程保存进程的上下文
        sylar::Fiber::GetThis();
        --threads;
        // 保证当前进程空间内无调度器
        SYLAR_ASSERT(GetThis() == nullptr); 
        // 设置当前的调度器为运行中的调度器
        t_schedular = this;

        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        sylar::Thread::SetName(m_name);

        // 设置调度协程为当前线程空间内的根协程
        // 实测可以不设, 没用上
        // 但是这样代码很好看, 全面
        t_scheduler_fiber = m_rootFiber.get();
        // 获取线程id
        m_rootThread = sylar::GetThreadId();
        // 加入线程id池
        m_threadIds.push_back(m_rootThread);
    } else {
        m_rootThread = -1;
    }
    // record threads count
    m_threadCount = threads;
}

Scheduler::~Scheduler() {
    SYLAR_ASSERT(m_stopping);
    loginfo2("delete a scheduler here");
    if (GetThis() == this) {
        t_schedular = nullptr;
    }
}

Scheduler* Scheduler::GetThis() {
    return t_schedular;
}
// 也没用上
Fiber* Scheduler::GetMainFiber() {
    return t_scheduler_fiber;
}

// 启动调度
void Scheduler::start() {
    // 这里加锁是为了修改线程池
    // 但如果测试的时候只有一个调度器的话可以不加
    MutexType::Lock lock(m_mutex);
    // m_stopping is initialized of true
    if (!m_stopping) {
        return ;
    }
    m_stopping = false;
    SYLAR_ASSERT(m_threads.empty());    // no threads now
    // 重设线程池大小
    m_threads.resize(m_threadCount);
    // 创建线程加入池
    for (size_t i = 0; i < m_threadCount; ++i) {    // all threads bind run
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this)
                            , m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    lock.unlock();

    // if (m_rootFiber) {
    //     m_rootFiber->call();
    //     loginfo2("call out");
    // }
}

void Scheduler::stop() {
    m_autoStop = true;
    // 判断特殊情况, 只有一个协程且执行完了
    if (m_rootFiber 
            && m_threadCount == 0
            // 感觉这里逻辑判断错了, 是不是一定为真
            // 也可能代码抄错了...
            && (m_rootFiber->getState() == Fiber::TERM
                || m_rootFiber->getState() == Fiber::INIT)) {
        std::cout << this;
        loginfo2(" stopped");
        m_stopping = true;
        
        if (stopping()) {
            return ;
        }
    }
    // 若 use_caller 为 true , 保证当前调度器为主进程的调度器 
    if (m_rootThread != -1) {
        SYLAR_ASSERT(GetThis() == this);
    } else {
    // 当前程序结构下似乎也没用到这个
    // 应该是不能自己停止自己吧, 需要是主线程调用
    // 在 run 函数中调用了 SetThis()
        SYLAR_ASSERT(GetThis() != this);
    }

    m_stopping = true;
    // for (size_t i = 0; i < m_threadCount; ++i) {
    //     tickle();
    // }

    // if (m_rootFiber) {
    //     tickle();
    // }

    // 也不知道有什么用
    // 若不能停止的话继续运行
    if (m_rootFiber) {
        if (!stopping()) {
            m_rootFiber->call();
        }
    }
    // 等待线程池结束运行
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }

    for(auto& i : thrs) {
        i->join();
    }
}

void Scheduler::setThis() {
    t_schedular = this;
}

// 暂时没用, 用于唤醒任务
void Scheduler::tickle() {
    loginfo2("tickle func");
    loginfo2(std::to_string(sylar::GetThreadId()));
}

bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    return m_autoStop && m_stopping 
        && m_fibers.empty() && m_activeThreadCount == 0;
}

// 在 IOManager 中重写了这个函数
void Scheduler::idle() {
    loginfo2("the idle fiber has no task to do");
    while (!stopping()) {
        // 让出执行权, 回到run的循环继续检查有无任务
        sylar::Fiber::YieldToHold();
    }
}

void Scheduler::run() {
    set_hook_enable(true);
    setThis();

    // 若当前线程为被调度的线程
    if (sylar::GetThreadId() != m_rootThread) {
        // 设置调度协程为线程空间内唯一的主协程
        t_scheduler_fiber = Fiber::GetThis().get();
    }
    // 分配一个空闲时的协程
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    // 分配一个执行函数用的协程
    Fiber::ptr cb_fiber;
    // 分配一个任务
    FiberAndThread ft;
    // 接下来一直运行这个 while 循环
    // 直到空闲时的协程为结束态
    while (true) {
        ft.reset();
        //bool tickle_me = false;
        bool is_active = false;
        {
            // 加锁寻找需要调度执行的任务
            MutexType::Lock lock(m_mutex);
            auto it = m_fibers.begin();
            // 遍历任务列表
            while (it != m_fibers.end()) {
                // it->thread 为 -1 表示任意线程, 可以执行
                // 不为当前线程 id, 表示不属于当前线程, 跳过
                // ++it 在指定线程 id 的情况下生效
                if (it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    ++it;
                    //tickle_me == true;
                    continue;
                }
                // 判断是否有任务
                SYLAR_ASSERT(it->fiber || it->cb);
                // 若是协程且已在执, 跳过
                if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    ++it;
                    continue;
                }
                // 绑定任务
                ft = *it;
                m_fibers.erase(it);
                ++m_activeThreadCount;
                is_active = true;
                break;
            }
            //tickle_me |= it != m_fibers.end();
        }

        // if (tickle_me) {
        //     tickle();
        // }

        // 若需要执行的任务为协程
        // 这个逻辑判断是不是一直为真
        if (ft.fiber && (ft.fiber->getState() != Fiber::TERM
                        || ft.fiber->getState() != Fiber::EXCEPT)) {
            // 进入协程上下文执行
            ft.fiber->swapIn();
            // 执行完毕
            --m_activeThreadCount;
            // 若为就绪态, 接着调度
            if (ft.fiber->getState() == Fiber::READY) {
                schedule(ft.fiber);
            } else if (ft.fiber->getState() != Fiber::TERM
                        && ft.fiber->getState() != Fiber::EXCEPT) {
                ft.fiber->m_state = Fiber::HOLD;
            }
            // 初始化为空
            ft.reset();
        } else if (ft.cb) {
            // 需要执行的是函数
            if (cb_fiber) {
                // 协程重新绑定函数
                cb_fiber->reset(ft.cb);
            } else {
                // 新建线程执行
                cb_fiber.reset(new Fiber(ft.cb));
            }
            // 任务重设为空
            ft.reset();
            // 进入绑定了函数的上下文执行
            cb_fiber->swapIn();
            // 执行完毕
            --m_activeThreadCount;
            // 同上
            if (cb_fiber->getState() == Fiber::READY) {
                schedule(cb_fiber);
                cb_fiber.reset();
            } else if (cb_fiber->getState() == Fiber::EXCEPT
                        || cb_fiber->getState() == Fiber::TERM) {
                cb_fiber->reset(nullptr);
            } else {//if (cb_fiber->getState() != Fiber::TERM) {
                cb_fiber->m_state = Fiber::HOLD;
                cb_fiber.reset();
            }
        } else {
            // 若为活跃态且无任务可执行
            // is_active 在每次循环都会被初始化为 false
            if (is_active) {
                --m_activeThreadCount;
                continue;
            }
            // 若空闲进程运行结束, 也就是收到了 stop 信号
            if (idle_fiber->getState() == Fiber::TERM) {
                loginfo2("idle fiber is term, this thread is about to exit");
                break;
            }
            // 进程空闲, 进入等待
            ++m_idleThreadCount;
            // idle_fiber 会持续等待
            // 在退出后, 因为 Fiber 对象绑定的上下文, 会自动设置状态为 TERM
            idle_fiber->swapIn();
            --m_idleThreadCount;
            if (idle_fiber->getState() != Fiber::TERM
                    && idle_fiber->getState() != Fiber::EXCEPT) {
                idle_fiber->m_state = Fiber::HOLD;
            }
        }
    }
}

}