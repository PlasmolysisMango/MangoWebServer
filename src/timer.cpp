#include "timer.h"

void TimerWheelHandler::set_timer(std::shared_ptr<timer_type> _timer)
{
    unsigned ticks;
    if (_timer->internal < m_si)
        ticks = 1;
    else
        ticks = _timer->internal / m_si;
    unsigned size = m_slots.size();
    // 计算需要待插入的定时器需要在时间轮转动多少圈后触发
    unsigned rotation = ticks / size;
    // 待插入的计时器需要插入的槽
    unsigned ts = (m_curslot + ticks % size) % size;
    // 存储触发前需要转动的圈数和存储槽
    _timer->setArg({"rotation", rotation});
    _timer->setArg({"ts", ts});
}

void TimerWheelHandler::add_timer(std::shared_ptr<timer_type> _timer)
{
    set_timer(_timer);
    auto ts = _timer->getArg("ts");
    m_slots[ts].emplace_back(_timer);
}

void TimerWheelHandler::mod_timer(std::shared_ptr<timer_type> _timer, size_t internal)
{
    delete_timer(_timer);
    _timer->internal = internal;
    _timer->valid = true;
    add_timer(_timer);
}

void TimerWheelHandler::delete_timer(std::shared_ptr<timer_type> _timer)
{
    auto ts = _timer->getArg("ts");
    auto &slot = m_slots[ts];
    slot.erase(std::find(slot.begin(), slot.end(), _timer));
}

void TimerWheelHandler::tick()
{
    auto &curlist = m_slots[m_curslot];
    for (auto it = curlist.begin(); it != curlist.end();)
    {
        if (!(*it)->valid) {
            continue;
        }
        // 若剩余轮转数大于0，则-1
        if ((*it)->getArg("rotation") > 0)
            --(*it)->getArg("rotation");
        else {
            // 到期后运行回调
            (*it)->runCallback();
            auto tmp = it++;
            if ((*tmp)->loop) {
                add_timer(*tmp);
            }
            curlist.erase(tmp);
        }
    }
    m_curslot = ++m_curslot % m_slots.size();
}