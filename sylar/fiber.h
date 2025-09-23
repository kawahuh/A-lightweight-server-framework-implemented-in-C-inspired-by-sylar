#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <memory>
#include <functional>
#include <ucontext.h>
#include "thread.h"

namespace sylar {

uint64_t TotalFibers();
class Scheduler;

class Fiber : public std::enable_shared_from_this<Fiber> {
friend class Scheduler;

public:
    // 定义当前类的智能指针
    typedef std::shared_ptr<Fiber> ptr;
    // 协程的几种状态, 初始化, 持有, 运行中, 结束运行, 就绪, 意外
    enum State {
        INIT,
        HOLD,
        EXEC,
        TERM,
        READY,
        EXCEPT,
    };
private:
    // 不带参的构造, 用于记录主线程的上下文(main函数的)
    Fiber();
public:
    // 需要运行的函数, 栈大小, 是否将主线程也加入调度
    Fiber(std::function<void()> cb, size_t stacksize = 0, 
            bool use_caller = false);
    ~Fiber();
    // 重新绑定协程需要执行的函数
    void reset(std::function<void()> cb);
    // 普通的协程使用 swapIn 和 swapOut, 由调度器进行管理
    // 切换进协程的上下文执行
    void swapIn();
    // 从协程的上下文切换回调度器的上下文
    void swapOut();
    // 若 use_caller 为 true, 由调用线程管理, 需要返回线程的主协程
    // 线程的主协程 t_threadFiber
    // 切换至主协程的执行
    void call();
    // 从主协程切换回原协程
    void back();
    // 获取此对象协程的 id
    uint64_t getId() const { return m_id;}
    // 获取此协程对象的状态
    State getState() const { return m_state;}
public:
    // 设置当前运行的协程 Fiber* f
    static void SetThis(Fiber* f);
    // 返回当前正在运行的协程
    // 若当前无协程, 则创建协程并将此协程设置为主协程 t_threadFiber
    static Fiber::ptr GetThis();
    // 切换当前协程至就绪态
    static void YieldToReady();
    // 切换当前协程至持有态
    static void YieldToHold();
    // 返回所有线程创建的协程总数
    static uint64_t TotalFibers();
    // 普通协程需要绑定上下文的入口函数
    // 在上下文中执行 m_cb 需要执行的函数
    static void MainFunc();
    // use_caller 为 true 时需要绑定主协程上下文的入口函数
    // 在上下文中执行 m_cb 需要执行的函数
    static void CallerMainFunc();
    // 获取当前运行协程的 id
    static uint64_t GetFiberId();
    // 协程状态
    State m_state = INIT;
private:
    // 协程 id
    uint64_t m_id;
    // 协程的栈大小
    uint32_t m_stacksize;
    // 协程的上下文
    ucontext_t m_ctx;
    // 协程的栈
    void* m_stack = nullptr;
    // 协程需要执行的函数(上下文的入口函数)
    std::function<void()> m_cb;
};

}

#endif