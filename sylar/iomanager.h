#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "scheduler.h"
#include "timer.h"

namespace sylar {

class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;
    // 事件类型
    enum Event : uint32_t {
        NONE    = 0x0,
        READ    = 0x1,
        WRITE   = 0x4,
    };

private:
    struct FdContext {
        typedef Mutex MutexType;
        struct EventContext {
            // 调度器
            Scheduler* scheduler = nullptr;
            // 协程
            Fiber::ptr fiber;
            // 回调函数
            std::function<void()> cb;
        };
        // 根据事件类型返回事件上下文
        EventContext& getContext(Event event);
        // 指定的事件上下文清空
        void resetContext(EventContext& ctx);
        void triggerEvent(Event event);

        int fd = 0;
        EventContext read;
        EventContext write;
        Event events = NONE;
        MutexType mutex;
    };

public:
    IOManager(size_t threads = 1, bool use_caller = true, 
        const std::string name = "");
    ~IOManager();

    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);
    bool cancelAll(int fd);

    static IOManager* GetThis();

protected:
    void tickle() override;
    bool stopping() override;
    bool stopping(uint64_t& timeout);
    void idle() override;
    void onTimerInsertedAtFront() override;

    void contextResize(size_t size);
private:
    int m_epfd = 0;
    int m_tickleFds[2];

    std::atomic<size_t> m_pendingEventCount = {0};
    RWMutexType m_mutex;
    std::vector<FdContext*> m_fdContexts;
};

}

#endif
