#include "utils.h"

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void EpollControl::addfd(int fd, TRI_MODE tmode, bool oneshot)
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = fd;
    // 关心收到数据和对方关闭连接事件，设定ET模式
    if (tmode == ET) {
        event.events |= EPOLLET;
    }
    if (oneshot) {
        // 只会触发一次，除非使用epoll_ctl函数重置该文件描述符上注册的EPOLLONESHOT事件
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(getfd(), EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void EpollControl::removefd(int fd) 
{
    epoll_ctl(getfd(), EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void EpollControl::reset_oneshot(int fd, int ev) 
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(getfd(), EPOLL_CTL_MOD, fd, &event);
}

int EpollControl::wait(epoll_event **events)
{
    int num = epoll_wait(getfd(), m_events, MAX_EVENT_NUMBER, -1);
    *events = m_events;
    return num;
}
