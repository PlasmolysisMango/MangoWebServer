#pragma once
#include<vector>
#include<queue>
#include<memory>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<future>
#include<functional>
#include<stdexcept>
#include<type_traits>
#include <utility>

class ThreadPool
{
public:
	ThreadPool();
	ThreadPool(int num);
	~ThreadPool();

	void CreateThread(void);

	//template<class F, class...Args>
	//auto enqueue(F&& f, Args&&...args)->std::future<typename std::_Forced_result_type<F(Args...)>::type>;
	template<class F, class... Args>
	auto enqueue(F &&f, Args&&... args)
	->std::future<decltype(f(args...))>;
private:
	std::vector <std::thread> workers; //thread array

	std::queue<std::function<void()>>tasks; //task queue

	std::mutex queue_mutex;
	std::condition_variable cond;
	bool stop;
};

inline ThreadPool::ThreadPool()
{
}

inline void ThreadPool::CreateThread(void)
{
	for (;;)
	{
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(this->queue_mutex);
			this->cond.wait(lock, [this] {
				return this->stop || !this->tasks.empty();//stop 或许任务队列不为空时唤醒。
				}
			);

			if (stop && tasks.empty())
				return;
			task = std::move(tasks.front());
			tasks.pop();//有点类似bfs的思路
		}

		task();
	}
}

ThreadPool::ThreadPool(int num) :stop(false)
{
	for (size_t i = 0; i < num; i++)
	{
		auto thread = std::bind(&ThreadPool::CreateThread,this);//&，this 不能丢
		workers.emplace_back(thread);

		//或者直接下面
		//workers.emplace_back(std::bind(&ThreadPool::CreateThread, this));

	}
}

inline ThreadPool::~ThreadPool()
{
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		stop = true;
	}
	cond.notify_all();

	for (auto& worker : workers)
	{
		worker.join();
	}
}


template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
->std::future<decltype(f(args...))>
{
	using ret_type = std::future< decltype(f(args...))>; //typename 此处加不加均可以的，下面同
	std::function< decltype(f(args...))()> func =
		std::bind(std::forward<F>(f), std::forward<Args>(args)...); // 连接函数和参数定义，特殊函数类型，避免左右值错误

	auto task = std::make_shared< std::packaged_task< decltype(f(args...))()> >(
		func
		);

	std::function<void()> warpper_func = [task]() {(*task)(); };
	ret_type res = task->get_future();
	if (stop)
		throw std::runtime_error("enqueue on stopped ThreadPool");
	std::unique_lock<std::mutex> lock(queue_mutex);
	tasks.emplace(warpper_func);
	cond.notify_one();
	return res;
}