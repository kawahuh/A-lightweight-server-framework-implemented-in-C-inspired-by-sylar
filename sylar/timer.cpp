#include "timer.h"
#include "util.h"
#include "log.h"

namespace sylar {
// 要满足 右 > 左
// 用于 set 中定时器的比较
bool Timer::Comparator::operator()(const Timer::ptr& lhs
                                    ,const Timer::ptr& rhs) const {
    if (!lhs && !rhs) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (!rhs) {
        return false;
    }
    if (lhs->m_next < rhs->m_next) {
        return true;
    }
    if (lhs->m_next > rhs->m_next) {
        return false;
    }
    return lhs.get() < rhs.get();
}

// 构造
// 是否循环, 周期, 执行的函数, 是否循环, 管理器
Timer::Timer(uint64_t ms, std::function<void()> cb,
                bool recurring, TimerManager* manager)
            :m_recurring(recurring)
            ,m_ms(ms)
            ,m_cb(cb)
            ,m_manager(manager) {
    m_next = sylar::GetCurrentMS() + m_ms;
}

Timer::Timer(uint64_t next)
                :m_next(next) {

}

// 取消定时器
// 在 set 中 erase
bool Timer::cancel() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

// 刷新定时器的时间
bool Timer::refresh() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    // 先清除再插入是为了保证时间的有序性
    m_manager->m_timers.erase(it);
    m_next = sylar::GetCurrentMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

// 指定时间和是否从现在开始的刷新
bool Timer::reset(uint64_t ms, bool from_now) {
    if (ms == m_ms && !from_now)  {
        return true;
    }
    // 上写锁修改
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    // 先 erase 再 insert 是为了保证 set 的有序性
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if (from_now) {
        start = sylar::GetCurrentMS();
    } else {
        // 初次设置定时器时的时间
        start = m_next - m_ms;
    }
    // 更新周期
    m_ms = ms;
    // 初次时间的基础上更新下次执行时间
    m_next = start + m_ms;
    // 注意这里传的锁是引用
    m_manager->addTimer(shared_from_this(), lock);
    return true;
}

TimerManager::TimerManager() {
    m_previousTime = sylar::GetCurrentMS();
}

TimerManager::~TimerManager() {

}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
                        , bool recurring) {
    // 创建 timer 
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    // 通过虚指针尝试获取, 若成功获取则执行 cb
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
                                , std::weak_ptr<void> weak_cond
                                , bool recurring) {
    // OnTimer的第一次参数为weak_cond, 第二个为cb, 返回一个新的可调用对象
    // 到时间后若满足条件(可获取到 weak_cond)则执行 cb
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

// 获取最近要发生的定时器的触发时间
uint64_t TimerManager::getNextTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    if (m_timers.empty()) {
        return ~0ull;
    }

    const Timer::ptr& next = *m_timers.begin();
    uint64_t now_ms = sylar::GetCurrentMS();
    // 若已经超时, 返回 0
    if (now_ms >= next->m_next) {
        return 0;
    } else {
        // 若还未发生, 返回最近的时间
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<Timer::ptr> expired;
    // 判断是否有定时器
    // 没有直接返回
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_timers.empty()) {
            return ;
        }
        lock.unlock();
    }
    RWMutexType::WriteLock lock(m_mutex);
    // 检测服务器时间是否被调后了
    bool rollover = detectClockRollover(now_ms);
    if (!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return ;
    }

    // 新建一个当前时间的 timer
    Timer::ptr now_timer(new Timer(now_ms));
    // 获取到超过当前时间的 timer 的迭代器(set的有序性)
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    
    while (it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }
    // 插入所有已超时的定时器
    expired.insert(expired.begin(), m_timers.begin(), it);
    // 删除已超时的定时器
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    // 遍历超时的定时器
    for (auto& timer : expired) {
        // 添加进函数列表
        cbs.push_back(timer->m_cb);
        // 需要循环的插回
        if (timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
} 

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
    auto it = m_timers.insert(val).first;
    // 插入头部且未被唤醒
    bool at_front = (it == m_timers.begin()) && !m_tickled;
    if (at_front)
        m_tickled = true;
    lock.unlock();

    // 若在头部且已经超时
    if (at_front) {
        // 在 IOManager 中重写了
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    if (now_ms < m_previousTime &&
            now_ms < (m_previousTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previousTime = now_ms;
    return rollover;
}   

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}