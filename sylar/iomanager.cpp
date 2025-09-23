#include "iomanager.h"
#include "log.h"
#include "macro.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

namespace sylar {
// 根据指定事件返回上下文
IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
    switch (event) {
        case IOManager::READ:   return read;
        case IOManager::WRITE:  return write;
        default:                SYLAR_ASSERT2(false, "getcontext");
    }
}

// 重设上下文
void IOManager::FdContext::resetContext(EventContext& ctx) {
    ctx.cb = nullptr;
    ctx.fiber.reset();
    ctx.scheduler = nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event event){
    // 确保当前有此类型事件
    SYLAR_ASSERT(events & event);
    // 取消存储的事件, 因为即将触发
    events = (Event)(events & ~event);
    // 获取指定事件的上下文
    EventContext& ctx = getContext(event);
    // 有函数调度函数, 没函数调度协程
    if (ctx.cb) {
        ctx.scheduler->schedule(&ctx.cb);
    } else {
        ctx.scheduler->schedule(&ctx.fiber);
    }
    // 调度完毕, 退出
    ctx.scheduler = nullptr;
    return ;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string name) 
    :Scheduler(threads, use_caller, name) {
    m_epfd = epoll_create(5000);
    SYLAR_ASSERT(m_epfd > 0);

    int rt = pipe(m_tickleFds);
    SYLAR_ASSERT(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    // 读事件, 边缘触发
    event.events = EPOLLIN | EPOLLET;
    // 将管道读端加入监听, 其他线程向管道写数据时, epoll会通知iomanager
    // 设置管道的作用是用于唤醒 epoll
    event.data.fd = m_tickleFds[0];
    
    // 管道读端设置为非阻塞模式
    // fcntl 系统调用设置
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!rt);
    // 使用epoll_ctl将管道读加入epoll实例中
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);

    // 在这里初始化fdContexts
    contextResize(32);

    start();
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (!m_fdContexts[i]) {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    // 新建上下文
    FdContext* fd_ctx = nullptr;
    // 上读锁, 判断是否需要扩充
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 当前的事件中应当不含将添加的事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLICKLY(fd_ctx->events & event)) {
        std::cout << "addEvent assert fd=" << fd
                << " event=" << event
                << " fd_ctx.event=" << fd_ctx->events << std::endl;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    // 修改 or 添加
    // 对 epoll 结构体进行设置
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    // 边缘触发, 原事件 + 新事件
    epevent.events = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    // 通过调用 epoll_ctl 更新
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        std::cout << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return -1;
    }

    // 增加事件
    ++m_pendingEventCount;
    // 更新上下文中的事件记录
    fd_ctx->events = (Event)(fd_ctx->events | event);
    // 根据指定事件获取上下文
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    SYLAR_ASSERT(!event_ctx.scheduler
                    && !event_ctx.fiber
                    && !event_ctx.cb);
    // 更新上下文内容
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) {
        event_ctx.cb.swap(cb);
    } else {
        event_ctx.fiber = Fiber::GetThis();
        SYLAR_ASSERT(event_ctx.fiber->getState() == Fiber::EXEC);
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!fd_ctx->events & event) {
        return false;
    }

    // 和 addEvent 的操作类似
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    --m_pendingEventCount;
    fd_ctx->events = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    fd_ctx->resetContext(event_ctx);
    return true;
}

// 和 delEvecnt 不同的地方在于, cancel 会在返回前触发事件, 用于执行回调函数或者 fiber
bool IOManager::cancelEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!fd_ctx->events & event) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return false;
    }
    
    fd_ctx->triggerEvent(event);
    --m_pendingEventCount;
    
    return true;
}

// 取消指定 fd 的所有事件, 同样也会触发回调
bool IOManager::cancelAll(int fd) {
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!fd_ctx->events) {
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }
    
    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager* IOManager::GetThis() {
    // 下转型
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

// 通过管道唤醒任务
void IOManager::tickle() {
    if (!hasIdleThreads()) {
        return ;
    }
    int rt = write(m_tickleFds[1], "T", 1);
    SYLAR_ASSERT(rt == 1);
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

bool IOManager::stopping(uint64_t& timeout) {
    timeout = getNextTimer();
    return timeout == ~0ull 
        && m_pendingEventCount == 0 
        && Scheduler::stopping();
}

// 重写了 Scheduler::idle
void IOManager::idle() {
    const uint64_t MAX_EVENTS = 256;
    // 用于接收 epoll_wait 触发的 fd
    epoll_event* events = new epoll_event[MAX_EVENTS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr) {
        delete[] ptr;
    });

    while(true) {
        // 是否结束
        uint64_t next_timeout = 0;
        if (stopping(next_timeout)) {
            if (next_timeout == ~0ull) {
                std::cout << "name=" << getName() << "iomanager idle stopped" << std::endl;
                break;
            }
        }

        int rt = 0;
        do {
            // 超时时间 3000ms
            static const int MAX_TIMEOUT = 3000;
            // 有自定义用自定义的时间
            if (next_timeout != ~0ull) {
                next_timeout = (int)next_timeout > MAX_TIMEOUT ? 
                                MAX_TIMEOUT : next_timeout;
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            // 调用 epoll_wait 等待时间触发
            rt = epoll_wait(m_epfd, events, MAX_EVENTS, (int)next_timeout);

            if (rt < 0 && errno == EINTR) {

            } else {
                break;
            }
        } while (true);

        // 这里用了 TimerManager
        // 检查是否有超时的定时器
        std::vector<std::function<void()> > cbs;
        // 列出需要调度的任务
        listExpiredCb(cbs);
        if (!cbs.empty()) {
            // 从头调到尾
            schedule(cbs.begin(), cbs.end());
            cbs.clear();
        }

        // 遍历处理触发的 fd
        for (int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];
            // 如果是管道(用于唤醒任务), 单独处理
            if (event.data.fd == m_tickleFds[0]) {
                uint8_t dummy[256];
                while (read(m_tickleFds[0], &dummy, sizeof(dummy)) == 1) ;
                continue;
            }

            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            // 如果是错误或者挂断, 强制触发已注册的时间
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            // 新建变量 real_event 记录需要触发的事件
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 更新上下文中的事件
            int left_events = (fd_ctx->events & ~real_events);
            // 更新 epoll 结构体
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            // epoll_ctl 
            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                std::cout << "epoll_ctl(" << m_epfd << ", "
                    << op << "," << fd_ctx->fd << "," << event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
                continue;
            }

            // 触发事件
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        }

        // 处理完切换出去(到Scheduler::run中)
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        raw_ptr->swapOut();
    }
}

// 昂, 就是 tickle 了一下
void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}