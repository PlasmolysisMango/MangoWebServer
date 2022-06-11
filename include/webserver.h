#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/epoll.h>

#include "threadpool.h"
#include "httpconn.h"
#include "log.h"
#include "signalhandler.h"
// #include "timer.h"

class WebServer
{
public:
    using size_t = unsigned;
    // 默认ip和端口为：192.168.8.8:5005
    WebServer(const char *ip = "192.168.8.8", int port = 5005) {
        init(ip, port);
    }
    ~WebServer() {
        if (m_listenfd != -1) {
            LOG_INFO("Close listenfd: %d", m_listenfd);
            close(m_listenfd);
        }
    }
    void run();

private:
    // 最大连接数
    const size_t MAX_FD = 40000;

    // 标识停止服务器
    bool m_stopserver = false;
    // 标识定时事件
    bool m_timeout = false;

    // 监听fd和当前事件fd
    int m_listenfd = -1;
    int m_eventfd = -1;
    // epoll就绪数组
    epoll_event *m_events;

    // 组件对象：线程池、信号处理、epoll管理
    ThreadPool &m_pool = ThreadPool::getInstance();
    SigHandler &m_sighdr = SigHandler::getInstance();
    EpollControl &m_epoller = EpollControl::getInstance();
    // 连接管理类
    ConnHandler &m_connhdr = ConnHandler::getInstance();

    // 初始化的函数
    // Log类
    void init_log() {
        Log::getInstance().setAsync();
        Log::getInstance().setPrint();
    }
    // Signal类
    void init_signal() {
        m_sighdr.init(m_epoller);
    }
    // 初始化连接
    void init(const char *ip, int port);

    // 处理listen事件，如果成功则返回accpet的fd
    int handle_listen();
    // 处理epollin事件，即读取数据
    void handle_read();
    // 处理epollout事件，即发送数据
    void handle_write();
    // 处理信号事件
    void handle_signal();
    
    // 关闭一个文件描述符并移除对应的所有资源
    void closefd(int fd);
};

#endif 
