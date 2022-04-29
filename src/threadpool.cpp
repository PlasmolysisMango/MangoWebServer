#include "threadpool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t thread_num, size_t max_reqs)
: m_thread_num(thread_num), m_max_requests(max_reqs)
{
    m_threads.reserve(m_thread_num);
    for (int i = 0; i < m_thread_num; ++i) {
        std::thread tt(worker);
        // tt.detach(); 
        // 采用析构时join的方式，确保线程正确释放
        m_threads.push_back(std::move(tt));
    }
}

bool ThreadPool::appendReq(std::shared_ptr<WorkRequest> req)
{
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_workqueue.size() >= m_max_requests) 
    {
        return false;
    }
    m_workqueue.push(req);
    m_cv.notify_one();
    return true;
}

void ThreadPool::worker()
{
    auto &pool = getInstance();
    pool.run();
}

void ThreadPool::run()
{
    while (true)
    {
        std::unique_lock<std::mutex> ulk(m_mtx);
        while (m_workqueue.empty()) {
            if (m_shutdown)
                return;
            m_cv.wait(ulk);
        }
        auto req = m_workqueue.front();
        m_workqueue.pop();
        ulk.unlock();
        if (!req)
            continue;
        req->do_request();
    }
}

ThreadPool::~ThreadPool() 
{
    m_shutdown = true;
    for (auto &t : m_threads) {
        m_cv.notify_all();
        t.join();
    }
}