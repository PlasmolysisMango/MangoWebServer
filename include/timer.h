#ifndef TIMER_H
#define TIMER_H

#include <queue>
#include <vector>
#include <chrono>
#include <list>
#include "httpconn.h"

// 定时器触发的事件间隔，由系统的ARAM事件保证
#define TIMESLOT 2

class TimerHandler;
class HTTPConn;
void alarm_start(TimerHandler &hdr, size_t timeslot);

// 使用struct封装连接类的智能指针以及超时参数，args参数意义可自定义
struct timer_type {
    using size_t = unsigned;
    // weak_ptr避免循环引用
    std::weak_ptr<HTTPConn> conn;
    std::vector<size_t> args;
    TimerHandler *hdr = nullptr;
    timer_type() {}
    timer_type(std::shared_ptr<HTTPConn> _conn, const std::vector<size_t> &_arg, TimerHandler *_hdr)
                :conn(_conn), args(_arg), hdr(_hdr) {}
    // 重载<小于号，用于后续需要排序的定时器设计中
    bool operator < (const timer_type &rhs) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] > rhs.args[i])
                return true;
        }
        return false;
    }
    void set_args(const std::vector<size_t> &_vec) {
        args = _vec;
    }
};

class TimerHandler
{
protected:
    using size_t = unsigned;
    using duration = std::chrono::seconds;
    using time_point = std::chrono::time_point<std::chrono::system_clock, duration>;

public:
    TimerHandler(){}
    virtual ~TimerHandler() {}
    // 添加、删除和执行回调
    virtual void add_timer(std::shared_ptr<timer_type> _timer) = 0;
    virtual void mod_timer(std::shared_ptr<timer_type> _timer, size_t timeout) = 0;
    virtual void delete_timer(std::shared_ptr<timer_type> _timer) = 0;
    virtual void callback(std::shared_ptr<timer_type> _timer) = 0;
    // 返回符合定义的timer_type类型
    virtual std::shared_ptr<timer_type> get_timer(std::shared_ptr<HTTPConn> _conn, size_t timeout) {
        return std::make_shared<timer_type>(_conn, std::vector<size_t>{timeout}, this);
    }
    // 一次计时器定时事件
    virtual void tick() = 0;
private:
    virtual void set_timer(timer_type &timer, size_t timeout) = 0;
};

class TimerWheelHandler: public TimerHandler {
    // timer_type的args数据域分别代表剩余轮转数和所在时间槽
    using container_type = std::list<std::shared_ptr<timer_type>>;

public:
    // 时间轮的槽总数，轮转时间间隔（以秒计）
    TimerWheelHandler() {
        m_slots.resize(60);
    }
    TimerWheelHandler(size_t _size, size_t _si): m_si(_si) {
        m_slots.resize(_size);
    }
    void add_timer(std::shared_ptr<timer_type> _timer) override;
    void mod_timer(std::shared_ptr<timer_type> _timer, size_t timeout) override;
    void delete_timer(std::shared_ptr<timer_type> _timer) override;
    void callback(std::shared_ptr<timer_type> _timer) override;
    std::shared_ptr<timer_type> get_timer(std::shared_ptr<HTTPConn> _conn, size_t timeout) override;
    // 时间槽滚动一个间隔，并执行到期的定时任务
    void tick() override;

private:
    // 使用vector来存储时间槽，时间槽使用list组织
    std::vector<container_type> m_slots;
    // 标识时间槽滚动的时间间隔，以一个duration计（秒）
    size_t m_si = 1;
    // 时间轮的当前槽
    size_t m_curslot = 0;
    // 用于处理timer属性的私有函数
    void set_timer(timer_type &timer, size_t timeout) override;
};

// class TimerHeapHandler: public TimerHandler {
// public:
    
// private:
    
// };

#endif