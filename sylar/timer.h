#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <set>
#include "thread.h"

namespace sylar {

class TimerManager;    
class Timer : public std::enable_shared_from_this<Timer> {
friend class TimerManager;
public:
    // 定义此类的智能指针
    typedef std::shared_ptr<Timer> ptr;
    // 取消定时
    bool cancel();
    // 刷新定时
    bool refresh();
    // 重设定时
    bool reset(uint64_t ms, bool from_now);
private:
    // 执行间隔, 回调函数, 是否循环, 定时器管理器
    Timer(uint64_t ms, std::function<void()> cb,
            bool recurring, TimerManager* manager);
    Timer(uint64_t next);
private:
    // 是否循环
    bool m_recurring = false;     
    // 执行周期
    uint64_t m_ms = 0;             
    // 下次执行的精确时间
    uint64_t m_next = 0; 
    // 回调函数          
    std::function<void()> m_cb;
    // 定时器管理器
    TimerManager* m_manager = nullptr;
private:
    // 比较定时器的智能指针大小(执行时间排序)
    struct Comparator {
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
};

class TimerManager {
friend class Timer;
public:
    typedef RWMutex RWMutexType;

    TimerManager();
    virtual ~TimerManager();

    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb
                        , bool recurring = false);
    // 添加条件定时器
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb
                                , std::weak_ptr<void> weak_cond
                                , bool recurring = false);
    uint64_t getNextTimer();
    // 获取需要执行的回调函数的列表
    void listExpiredCb(std::vector<std::function<void()> >& cbs);
    // 是否有定时器
    bool hasTimer();
protected:
    // 有定时器插入到定时器的首部时, 执行该函数
    virtual void onTimerInsertedAtFront() = 0;
    // 添加定时器到管理器中
    // 注意这里传的是锁的引用
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);
private:
    // 检测服务器时间是否被调后了
    bool detectClockRollover(uint64_t now_ms);
private:
    RWMutexType m_mutex;
    // 定时器集合
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    // 是否触发 onTimerInsertedAtFront()
    bool m_tickled = false;
    // 上次执行时间
    uint64_t m_previousTime = 0;
};

}

#endif