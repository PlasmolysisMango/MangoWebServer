#ifndef LOG_H
#define LOG_H

#include <fstream>
#include <iostream>
#include <cstdio>
#include <future>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

class Log {
public:
    using size_t = unsigned;
    enum LOG_LEVEL {ERROR, WARN, DEBUG, INFO};
    static Log &getInstance()
    {
        static Log logger;
        return logger;
    }
    // 保证析构时缓冲区的日志均已经输出
    ~Log();
    Log(const Log &) = delete;
    Log &operator=(const Log &) = delete;
    Log(Log &&) = delete;
    Log &operator=(Log &&) = delete;

    // 用于配置日志属性
    void config(const std::string &dir, const std::string &name, bool isAsync, size_t maxitems,
                bool split, size_t maxlength, size_t cachetime, size_t cachesize, bool print);
    // 设置异步日志，只能在异步线程启动前进行配置
    bool setAsync() {
        if (m_async)
            return false;
        m_async = true;
        m_thread = std::thread(asyncWork);
        return true;
    }
    // 设置打印到标准输出
    void setPrint() noexcept {
        m_isPrint = true;
    }
    // 写日志函数
    template <typename... Args>
    void write_log(LOG_LEVEL lv, const std::string &fmt, Args &&...args);

    // 重载<<运算符

private:
    Log() { }
    // 保存日志文件，另开线程保证一直在工作
    static void asyncWork();
    void asyncSave();
    void syncSave(const std::string &);

    std::mutex m_mutex;
    std::condition_variable m_cv;

    // 日志的路径和文件名
    std::string m_dir = "./";
    std::string m_name = "log";
    // 异步开关
    bool m_async = false;
    // 单文件存储的日志条目数量
    size_t m_maxlogitems = 100;
    // 单条日志的最大长度
    size_t m_maxlength = 256;
    // 是否分文件存储
    bool m_isSplit = false;
    // 日志写入的最大缓存数和缓存时间
    size_t m_cachesize = 5;
    size_t m_cachetime = 30;
    // 日志缓冲区
    std::vector<std::string> m_cache;
    // 是否同步输出到标准输出
    bool m_isPrint = false;
    // 异步保存线程
    std::thread m_thread;
    bool m_shutdown = false;
    // 目前打开的日志文件及已经写入的日志条目数
    std::fstream m_curfile;
    // 当前日志的条目数和分文件的编号
    size_t m_curlogidx = 0;
    size_t m_curfileidx = 1;
};

template <typename... Args>
void LOG_ERROR(const std::string &fmt, Args &&...args) {
    Log::getInstance().write_log(Log::ERROR, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void LOG_INFO(const std::string &fmt, Args &&...args)
{
    Log::getInstance().write_log(Log::INFO, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void LOG_WARN(const std::string &fmt, Args &&...args)
{
    Log::getInstance().write_log(Log::WARN, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void LOG_DEBUG(const std::string &fmt, Args &&...args)
{
    Log::getInstance().write_log(Log::DEBUG, fmt, std::forward<Args>(args)...);
}

void Log::config(const std::string &dir, const std::string &name, bool isAsync,
                 size_t maxitems, bool split, size_t maxlength, 
                 size_t cachetime, size_t cachesize, bool print)
{
    m_dir = dir;
    m_name = name;
    m_async = isAsync;
    m_maxlogitems = maxitems;
    m_isSplit = split;
    m_maxlength = maxlength;
    m_cachetime = cachetime;
    m_cachesize = cachesize;
    m_isPrint = print;
    m_cache.reserve(m_cachesize + 5);
}

template <typename... Args>
void Log::write_log(Log::LOG_LEVEL lv, const std::string &fmt, Args &&...args)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    // 若缓冲区满，则加入当前日志，并唤醒保存线程腾空缓冲区
    char tmp[m_maxlength];
    sprintf(tmp, fmt.c_str(), args...);
    std::string t(tmp);
    std::string pre;
    switch(lv) {
        case(INFO): {
            pre = "[Info] ";
            break;
        }
        case(WARN): {
            pre = "[Warn] ";
            break;
        }
        case(ERROR): {
            pre = "[Error] ";
            break;
        }
        case(DEBUG): {
            pre = "[Debug] ";
            break;
        }
    }
    time_t now = time(nullptr);
    tm* tmt = localtime(&now);
    char curtime[50];
    sprintf(curtime, "%d/%d/%d %02d:%02d:%02d", tmt->tm_year + 1900, tmt->tm_mon + 1,
            tmt->tm_mday, tmt->tm_hour, tmt->tm_min, tmt->tm_sec);
    t = pre + curtime + " " + t;
    if (m_isPrint)
        std::cout << t << std::endl;
    if (!m_async) {
        syncSave(t);
    }
    else {
        m_cache.push_back(std::move(t));
        if (m_cache.size() >= m_cachesize) 
            m_cv.notify_one();
    }
}

void Log::asyncWork()
{
    getInstance().asyncSave();
}

void Log::syncSave(const std::string &s)
{
    if (!m_curfile.is_open() || 
    (m_isSplit && m_curlogidx >= m_maxlogitems)) {
        time_t now = time(nullptr);
        tm* tmt = localtime(&now);
        char curtime[50];
        sprintf(curtime, "%d_%d_%d", tmt->tm_year + 1900, tmt->tm_mon + 1, tmt->tm_mday);
        std::string log_path = m_dir + m_name + "_" + curtime;
        if (m_isSplit)
            log_path += "_" + std::to_string(m_curfileidx++);
        log_path += ".txt";
        m_curfile.open(log_path, std::fstream::out | std::fstream::app);
        if (!m_curfile) {
            std::cout << "Open logfile failed!" << std::endl;
            return;
        }
        m_curlogidx = 0;
    }
    m_curfile << s << std::endl;
    if (++m_curlogidx == m_maxlogitems && m_isSplit)
        m_curfile.close();
}

void Log::asyncSave()
{
    while (true) {
        std::unique_lock<std::mutex> ulk(m_mutex);
        while (m_cache.empty())
        {
            if (m_shutdown)
                return;    
            m_cv.wait(ulk);
        }
        for (const auto &s : m_cache)
        {
            syncSave(s);
        }
        m_cache.clear();
    }
}

Log::~Log()
{
    if (m_async) {
        m_shutdown = true;
        m_cv.notify_all();
        m_thread.join();
    }
    if (m_curfile.is_open())
        m_curfile.close();
}

#endif