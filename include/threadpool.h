#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <condition_variable>
// #include "global.h"

class WorkRequest;

class ThreadPool {
public:
    using size_t = unsigned;
    static ThreadPool &getInstance(size_t thread_num = 8, size_t max_requests = 10000)
    {
        static ThreadPool pool(thread_num, max_requests);
        return pool;
    }
    // 两种反应堆模式下的添加任务方法，不同处理方式由请求类保证
    bool appendReq(std::shared_ptr<WorkRequest> req);
    // 保证所有任务执行完成后析构
    ~ThreadPool();
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

private:
    ThreadPool(size_t thread_num, size_t max_request_num);
    // 工作线程函数
    static void worker();
    void run();

    // MODE m_actor_mode; // IO模型
    size_t m_thread_num; // 线程数
    size_t m_max_requests; // 请求队列的最大容量
    std::vector<std::thread> m_threads; // 线程池数组
    std::queue<std::shared_ptr<WorkRequest>> m_workqueue; // 请求队列
    std::mutex m_mtx; // 请求队列互斥量
    std::condition_variable m_cv; // 条件变量，用于阻塞和唤醒线程
    bool m_shutdown = false;
};

// 用于线程池中工作队列，需要继承并重写do_request函数
class WorkRequest {
public:
    WorkRequest() = default;
    virtual ~WorkRequest() {}
    virtual bool do_request() = 0;
};

#endif