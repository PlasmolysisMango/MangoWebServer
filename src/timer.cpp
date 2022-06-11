#include "timer.h"

void alarm_start(TimerHandler &hdr, size_t timeslot)
{
    hdr.tick();
    alarm(TIMESLOT);
}

void TimerWheelHandler::set_timer(timer_type &timer, size_t timeout)
{
    size_t ticks;
    if (timeout < m_si)
        ticks = 1;
    else
        ticks = timeout / m_si;
    size_t size = m_slots.size();
    // 计算需要待插入的定时器需要在时间轮转动多少圈后触发
    size_t rotation = ticks / size;
    // 待插入的计时器需要插入的槽
    size_t ts = (m_curslot + ticks % size) % size;
    // 修改timer
    timer.set_args({rotation, ts});
}

void TimerWheelHandler::mod_timer(std::shared_ptr<timer_type> _timer, size_t timeout) 
{
    printf("mod timer\n");

    set_timer(*_timer, timeout);
}

void TimerWheelHandler::add_timer(std::shared_ptr<timer_type> _timer)
{
    // 指示经过几次tick后被触发定时器事件
    size_t ts = _timer->args[1];
    m_slots[ts].push_back(_timer);
}

void TimerWheelHandler::delete_timer(std::shared_ptr<timer_type> _timer)
{
    auto curlist = m_slots[_timer->args[1]];
    for (auto it = curlist.begin(); it != curlist.end(); ) {
        if (*it == _timer) {
            auto tmp = it++;
            curlist.erase(tmp);
        }
        else
            it++;
    }
}

void TimerWheelHandler::callback(std::shared_ptr<timer_type> _timer)
{
    _timer->conn.lock()->close_conn();
}

void TimerWheelHandler::tick()
{
    auto &curlist = m_slots[m_curslot];
    for (auto it = curlist.begin(); it != curlist.end();)
    {
        // 若剩余轮转数大于0，则-1
        if ((*it)->args[0] > 0)
            --(*it++)->args[0];
        else {
            // 到期后断开连接，此处即为对应执行任务的代码区域
            callback(*it);
            // 记录当前需要删除的迭代器，list后续节点迭代器不会失效
            auto tmp = it++;
            curlist.erase(tmp);
        }
    }
    m_curslot = ++m_curslot % m_slots.size();
}

std::shared_ptr<timer_type> TimerWheelHandler::get_timer(std::shared_ptr<HTTPConn> _conn, size_t timeout)
{
    auto timer = std::make_shared<timer_type>();
    timer->conn = _conn;
    timer->hdr = this;
    set_timer(*timer, timeout);
    return timer;
}