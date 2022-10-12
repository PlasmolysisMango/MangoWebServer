#ifndef TIMER_H
#define TIMER_H

#include <queue>
#include <vector>
#include <chrono>
#include <functional>
#include <list>
#include <future>

// 可由TIME_SLOT自定义触发时间
#ifndef TIME_SLOT
#define TIME_SLOT 5
#endif

// 使用struct封装连接类的智能指针以及超时参数，args参数意义可自定义
class TimerData {
    // 全局递增id
    static unsigned global_id;
public:
    template<class F, class... Args>
    static auto createTimerData(int _in, bool _va, bool _lo, F &&f, Args&&... args) -> std::shared_ptr<TimerData>;
public:
    // 唯一标识id
    unsigned id;
    // 触发的时间间隔
    unsigned internal = TIME_SLOT;
    bool valid = true;
    // 是否循环
    bool loop = false;
    std::function<void()> callback = nullptr;
private:
    // 可选参数，以键值对形式存储
    std::unordered_map<std::string, int> argsMap;
public:
    TimerData(int _in, bool _va, bool _lo): id(global_id++), internal(_in), valid(_va), loop(_lo) {}
    ~TimerData() {}
    // 设置回调
    template<class F, class... Args>
	auto setCallback(F &&f, Args&&... args) -> void;
    void runCallback() {
        callback();
    }
    // 设置和获取可选参数
    int& getArg(const std::string &key) {
        return argsMap[key];
    }
    void setArg(const std::pair<const std::string, int> &pi) {
        argsMap[pi.first] = pi.second;
    }
};

inline unsigned TimerData::global_id = 0;

template<class F, class... Args>
inline auto TimerData::setCallback(F &&f, Args&&... args) -> void 
{
    using ret_type = std::future< decltype(f(args...))>; //typename 此处加不加均可以的，下面同
	std::function< decltype(f(args...))()> func =
		std::bind(std::forward<F>(f), std::forward<Args>(args)...); // 连接函数和参数定义，特殊函数类型，避免左右值错误
	callback = [func]() {func(); };
}

template<class F, class... Args>
inline auto TimerData::createTimerData(int _in, bool _va, bool _lo, F &&f, Args&&... args) 
-> std::shared_ptr<TimerData>
{
    auto timer = std::make_shared<TimerData>(_in, _va, _lo);
    timer->setCallback(std::forward<F>(f), std::forward<Args>(args)...);
    return timer;
}


// 定时器管理类
class TimerHandler
{
public:
    using timer_type = TimerData;

    TimerHandler(){}
    virtual ~TimerHandler() {}
    // 添加、删除和调整timer
    virtual void add_timer(std::shared_ptr<timer_type> _timer) = 0;
    virtual void mod_timer(std::shared_ptr<timer_type> _timer, size_t internal) = 0;
    virtual void delete_timer(std::shared_ptr<timer_type> _timer) = 0;
    // 一次计时器定时事件
    virtual void tick() = 0;
};

// 时间轮管理器
class TimerWheelHandler: public TimerHandler {
    // timer_type的args数据域分别代表剩余轮转数和所在时间槽
    using container_type = std::list<std::shared_ptr<TimerData>>;

public:
    // 时间轮的槽总数，轮转时间间隔（以秒计）
    TimerWheelHandler() {
        m_slots.resize(60);
    }
    TimerWheelHandler(size_t _size, size_t _si): m_si(_si) {
        m_slots.resize(_size);
    }
    void add_timer(std::shared_ptr<timer_type> _timer) override;
    void mod_timer(std::shared_ptr<timer_type> _timer, size_t internal) override;
    void delete_timer(std::shared_ptr<timer_type> _timer) override;
    // 时间槽滚动一个间隔，并执行到期的定时任务
    void tick() override;

private:
    // 使用vector来存储时间槽，时间槽使用list组织
    std::vector<container_type> m_slots;
    // 标识时间槽滚动的时间间隔，以一个duration计（秒）
    size_t m_si = 1;
    // 时间轮的当前槽
    size_t m_curslot = 0;
    // 根据TimerData的数据设置或重置
    void set_timer(std::shared_ptr<timer_type> _timer);
};

#endif