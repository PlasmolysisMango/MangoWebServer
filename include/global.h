#ifndef GLOBAL_H
#define GLOBAL_H
#include <mutex>
#include <condition_variable>

class Semaphore {
public:
    Semaphore() {}
    Semaphore(int n): count(n) {}
    bool wait() 
    {
        std::unique_lock<std::mutex> ulk(mtx);
        if (--count < 0) // 挂起线程
        {
            cv.wait(ulk);
        }
        return true;
    }

    bool post()
    {
        std::unique_lock<std::mutex> ulk(mtx);
        if (++count <= 0) // 唤醒线程
        {
            cv.notify_one();
        }
        return true;
    }

private:
    std::mutex mtx;
    int count = 0;
    std::condition_variable cv;
};



#endif