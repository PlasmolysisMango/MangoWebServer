#ifndef TIMER_H
#define TIMER_H

#include <queue>
#include <vector>
#include <chrono>
#include <functional>
#include <list>
#include <future>

#define TIME_SLOT 5

// 使用struct封装连接类的智能指针以及超时参数，args参数意义可自定义
class TimerData {
    // 全局递增id
    static unsigned global_id;
public:
    template<class F, class... Args>
    static auto createTimerData(int _in, bool _va, bool _lo, F &&f, Args&&... args) -> std::pair<std::shared_ptr<TimerData>, std::future<decltype(f(args...))>>;
private:
    // 唯一标识id
    unsigned id;
    // 时间间隔
    unsigned internal = TIME_SLOT;
    bool valid = true;
    // 是否循环
    bool loop = false;
    // 下次启动的时间戳（可选）
    // unsigned next_timestamp = 0;
    std::function<void()> callback;
public:
    TimerData(int _in, bool _va, bool _lo): id(global_id++), internal(_in), valid(_va), loop(_lo) {}
    ~TimerData() {}
    // 设置回调
    template<class F, class... Args>
	auto setCallback(F &&f, Args&&... args) -> std::future<decltype(f(args...))>;
    // 重载<小于号，用于后续需要排序的定时器设计中
    // bool operator < (const TimerData &rhs) {
    //     // TODO: 
    //     return false;
    // }
};

unsigned TimerData::global_id = 0;

template<class F, class... Args>
inline auto TimerData::setCallback(F &&f, Args&&... args) -> std::future<decltype(f(args...))> 
{
    using ret_type = std::future< decltype(f(args...))>; //typename 此处加不加均可以的，下面同
	std::function< decltype(f(args...))()> func =
		std::bind(std::forward<F>(f), std::forward<Args>(args)...); // 连接函数和参数定义，特殊函数类型，避免左右值错误

	auto task = std::make_shared< std::packaged_task< decltype(f(args...))()> >(
		func
		);

	callback = [task]() {(*task)(); };
	ret_type res = task->get_future();
	return res;
}

template<class F, class... Args>
inline auto TimerData::createTimerData(int _in, bool _va, bool _lo, F &&f, Args&&... args) 
-> std::pair<std::shared_ptr<TimerData>, std::future<decltype(f(args...))>>
{
    auto timer = std::make_shared<TimerData>(_in, _va, _lo);
    auto result = timer->setCallback(std::forward<F>(f), std::forward<Args>(args)...);
    return {timer, std::move(result)};
}

#endif