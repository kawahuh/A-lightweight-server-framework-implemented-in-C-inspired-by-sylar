#include "fiber.h"
#include "macro.h"
#include "scheduler.h"
#include "log.h"
#include <atomic>

namespace sylar {

static std::atomic<uint64_t> s_fiber_id {0};
static std::atomic<uint64_t> s_fiber_count {0};

static thread_local Fiber* t_fiber = nullptr;
static thread_local Fiber::ptr t_threadFiber = nullptr;     // the main fiber

class MallocStackAllocator {
    public:
        static void* Alloc(size_t size) {
            return malloc(size);
        }

        static void Dealloc(void* vp, size_t size) {
            return free(vp);
        }
};

using StackAllocator = MallocStackAllocator;
// 返回当前运行协程的 id
uint64_t Fiber::GetFiberId() {
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}
// 不带参构造协程, 获取上下文
// 将此协程设置为当前 线程空间内 正在运行的协程
// 也没有设置栈, 只用作保存线程的上下文
// 这个协程的主要作用是为了避免无法切换回原进程执行
// 当作工具线程吧
Fiber::Fiber() {
    m_state = EXEC;
    // 设置当前运行协程为此协程
    SetThis(this);
    // 获取线程运行的上下文
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    // 增加协程数
    ++s_fiber_count;
    loginfo2("create a fiber with no param");
}
// 带参构造
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)
        :m_id(++s_fiber_id)
        ,m_cb(cb) {
    // 增加协程数
    ++s_fiber_count;
    // 设置栈大小
    m_stacksize = stacksize ? stacksize : 128 * 1024;
    // 调用库函数分配栈空间
    m_stack = StackAllocator::Alloc(m_stacksize);
    // 获取上下文
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    // 设置上下文中的栈和栈空间
    // uc.link 为 nullptr 表示结束后不会自动切换
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    // 为线程绑定上下文的入口函数
    if (!use_caller) {
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    } else {
        makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
    }
    loginfo2("create a fiber with param");
}

Fiber::~Fiber() {
    --s_fiber_count;
    if (m_stack) {
        SYLAR_ASSERT(m_state == TERM || m_state == INIT || m_state == EXCEPT);
        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else {
        SYLAR_ASSERT(!m_cb);
        SYLAR_ASSERT(m_state == EXEC);

        Fiber* cur = t_fiber;
        if (cur == this) {
            SetThis(nullptr);
        }
    }
    loginfo2("delete a fiber, id: " + std::to_string(m_id));
}
// 重设协程需要执行的函数
void Fiber::reset(std::function<void()> cb) {
    // 确定栈存在
    SYLAR_ASSERT(m_stack);
    // 确认协程的状态
    SYLAR_ASSERT(m_state == TERM || m_state == INIT || m_state == EXCEPT);
    // 绑定需要执行的函数
    m_cb = cb;
    // 获取上下文
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    // 设置栈空间
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    // 设置上下文的入口函数
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    // 协程状态为初始化
    m_state = INIT;
}

// 个人理解, 一个是用于单个线程空间内的协程切换, 一个是用于多线程情况下的调度器协程切换
void Fiber::call() {
    // 设置运行的协程为当前协程
    SetThis(this);
    // 执行态
    m_state = EXEC;
    // 切换上下文执行, 
    // 将当前上下文保存至 t_threadFiber->m_ctx
    // 执行此协程对象的 m_ctx
    if (swapcontext(&t_threadFiber->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

void Fiber::back() {
    // 设置运行的协程为 t_threadFiber
    SetThis(t_threadFiber.get());
    // 将当前的上下文保存至此协程对象的 m_ctx中
    // 继续执行 t_threadFiber->m_ctx 的上下文
    if (swapcontext(&m_ctx, &t_threadFiber->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

void Fiber::swapIn() {
    // 设置运行的协程为此协程对象
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC);
    // 执行态
    m_state = EXEC;
    // 保存当前上下文至调度器的调度协程
    // 执行此协程对象的上下文
    if (swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext in");
    }
} 

void Fiber::swapOut() {
    // 设置运行的协程为调度器的调度协程
    SetThis(Scheduler::GetMainFiber());
    // 保存当前上下文至此协程对象的 m_ctx
    // 继续执行调度器的调度协程的上下文
    if (swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}
// 设置运行的协程为 Fiber* f
void Fiber::SetThis(Fiber *f) {
    t_fiber = f;
}

Fiber::ptr Fiber::GetThis() {
    // 若当前有协程在运行, 返回协程
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }
    // 无协程运行则创建协程
    // 设置 t_threadFiber 为创建的协程, 作为主协程, 避免无法切换回原进程的上下文
    loginfo2("use GetThis to new a fiber");
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_threadFiber = main_fiber;
    // 返回创建的协程, 也就是当前运行中的协程
    return t_fiber->shared_from_this();
}
// 将当前运行的协程切换至就绪态
void Fiber::YieldToReady() {
    Fiber::ptr cur = GetThis();
    cur->m_state = READY;
    cur->swapOut();
}
// 将当前运行的协程切换至持有态
void Fiber::YieldToHold() {
    Fiber::ptr cur = GetThis();
    cur->m_state = HOLD;
    cur->swapOut();
}
// 返回所有线程中的总协程数
uint64_t TotalFibers() {
    return s_fiber_count;
}
// 普通协程绑定的上下文
void Fiber::MainFunc() {
    // 获取当前运行的协程
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    // 运行协程需要执行的函数
    try {
        cur->m_cb();
        // 执行完置为空, 状态为结束态
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        std::cout << "Fiber_id: " << cur->getId() << std::endl;
        std::cout << "Fiber except" << ex.what() << std::endl;
    } catch (...) {
        cur->m_state = EXCEPT;
        std::cout << "Fiber except" << std::endl;
    }
    // 获取到协程的指针
    // 结束此协程的运行, 切换回调度器的调度线程的上下文
    auto raw_ptr = cur.get();
    // 减少引用计数, 计数为0时析构对象
    cur.reset();
    raw_ptr->swapOut();
}

void Fiber::CallerMainFunc() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        std::cout << "Fiber Except: " << ex.what()
            << " Fiber id: " << cur->getId() 
            << std::endl;
    } catch(...) {
        cur->m_state = EXCEPT;
        std::cout << "Fiber Except: "
            << " Fiber id: " << cur->getId() 
            << std::endl;
    }

    auto raw_ptr = cur.get();
    // 减少引用计数, 计数为0时析构对象
    cur.reset();
    // 与上文同理, 不同的是这里切换回运行线程的上下文
    raw_ptr->back();
}

}