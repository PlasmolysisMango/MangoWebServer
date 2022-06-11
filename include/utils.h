#ifndef UTILS_H
#define UTILS_H
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

// 两种事件处理模式，Proactor为同步模拟
enum ACTOR_MODE { REACTOR, PROACTOR };
// 两种触发模式
enum TRI_MODE { LT, ET };

// 将socket设置为非阻塞的函数，返回sock的原配置
int setnonblocking(int fd);

// 采用RAII的Epoll操作类

// 最大epoll就绪队列容量
const unsigned MAX_EVENT_NUMBER = 10000;

class EpollControl {
public:
    static EpollControl &getInstance() {
        static EpollControl epoller;
        return epoller;
    }

    EpollControl(const EpollControl &) = delete;
    EpollControl(EpollControl &&) = delete;
    EpollControl &operator=(const EpollControl &) = delete;
    EpollControl &operator=(EpollControl &&) = delete;

    ~EpollControl() {
        if (m_epollfd != -1) {
            close(m_epollfd);
        }
    }
    // 向epoll对象中添加fd, oneshot属性默认启用
    void addfd(int fd, TRI_MODE tmode, bool oneshot = true);
    // 修改fd，主要用于重置oneshot状态，只用在启用了oneshot模式的ET，ev为EPOLLIN或者EPOLLOUT
    void reset_oneshot(int fd, int ev);
    // 移除某个fd
    void removefd(int fd);
    // 获取本来的epollfd
    int getfd() const { return m_epollfd; }
    // 等待并获取就绪数组，返回就绪数组的大小
    int wait(epoll_event **events);

private:
    EpollControl():m_epollfd(epoll_create(5)) {}
    int m_epollfd = -1;
    epoll_event m_events[MAX_EVENT_NUMBER];
};

#endif
