# 头文件

* ~~**lock.h**: 使用RAII封装Linux提供的信号量、互斥锁和条件变量。~~已改用C++11提供的std::mutex和std::condition_variable。
* **httpconn.h**: 使用有限状态机解析http请求，目前支持GET请求。
* **threadpool.h**: 半同步/半反应堆线程池。
* **log.h**: 异步/同步日志，提供四种日志级别。
* **timer.h**: 时间堆/时间轮定时器管理类。
