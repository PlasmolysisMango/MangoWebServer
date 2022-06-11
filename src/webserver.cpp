#include "webserver.h"

void WebServer::init(const char *ip, int port)
{
    LOG_INFO("%s", "Initializing");
    init_log();
    // 信号处理相关
    init_signal();
    // 向某个已被关闭或未连接的socket发送数据时，产生SIGPIPE信号
    // 默认处理方式是终止进程，在这里忽略掉该信号
    m_sighdr.addsig(SIGPIPE, SIG_IGN);
    // 定时器信号处理方式，添加到延后处理队列中
    m_sighdr.delaysig(SIGALRM);
    // 键盘退出事件处理方式，添加到延后处理队列中
    m_sighdr.delaysig(SIGINT);
    // kill命令处理方式，添加到延后处理队列中
    m_sighdr.delaysig(SIGTERM);
    // 初始化定时器管理类，这里采用时间轮
    // TimerWheelHandler timer_hdr;

    LOG_INFO("Binding server @%s:%d", ip, port);
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof address);
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);
    // struct linger tmp = {1, 0}; // 强制退出模式，非优雅关闭
    // setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof tmp);
    
    // 用于调试，取消对应socket的time_wait状态
    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof address);
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);
    // listenfd关闭oneshot模式，否则每accept一个连接就需要重置
    LOG_INFO("Listening at sockfd: %d", m_listenfd);
    m_epoller.addfd(m_listenfd, TRI_MODE::ET, false);
}

void WebServer::run()
{
    while (!m_stopserver) {
        int num = m_epoller.wait(&m_events);
        // 对异常进行处理，避免因为信号中断导致epoll失败
        if ((num < 0) && (errno != EINTR)) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        // 遍历处理epoll事件
        for (int i = 0; i < num; ++i) {
            m_eventfd = m_events[i].data.fd;
            LOG_INFO("handle sockfd: %d", m_eventfd);
            if (m_eventfd == m_listenfd)
            {
                int connfd = handle_listen();
                if (connfd == -1) {
                    LOG_ERROR("%s", "Listen error");
                    continue;
                }
                if (HTTPConn::m_user_count >= MAX_FD) {
                    LOG_ERROR("More than MAXFD: %d", MAX_FD);
                    send_error(connfd, "Internal server busy");
                    close(connfd);
                    continue;
                }
                LOG_INFO("Connect with sock: %d", connfd);
                // 添加事件，ET且关闭oneshot
                // m_epoller.addfd(connfd, TRI_MODE::ET, false);
                auto conn = m_connhdr.add_conn(connfd);
                conn->init(connfd, ACTOR_MODE::PROACTOR, TRI_MODE::ET, true);
            }
            else if (m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 出错，关闭当前处理的fd并释放所有资源
                LOG_ERROR("EpollError, sockfd: %d, relese rec", m_eventfd);
                closefd(m_eventfd);
            }
            else if (m_eventfd == m_sighdr.get_sockread() && (m_events[i].events & EPOLLIN))
            {
                handle_signal();
            }
            else if (m_events[i].events & EPOLLIN) {
                handle_read();
            }
            else if (m_events[i].events & EPOLLOUT) {
                handle_write();
            }
            if (m_timeout) {
                LOG_INFO("%s", "Handle time");
            }
        }
    }
}

int WebServer::handle_listen() 
{
    LOG_INFO("Listen sock: %d", m_listenfd);
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof client_address;
    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address,
                        &client_addrlength);
    if (connfd < 0) {
        LOG_ERROR("errno is %d. connfd is invalid.", errno);
        return -1;
    }

    return connfd;
}

void WebServer::handle_read()
{
    LOG_INFO("%s", "Handle read");
    auto conn = m_connhdr.find_conn(m_eventfd);
    if (conn->m_actor_mode == PROACTOR)
    {
        // 若读成功，则加入工作队列处理（缓存区读写）
        if (conn->read()) {
            auto workreq = std::make_shared<HTTPReq>(conn);
            m_pool.appendReq(workreq);
            // 更新定时器
            // timer_hdr.mod_timer(conns[sockfd]->m_timer, 3 * TIMESLOT);
        }
        // 若读错误，则关闭连接并移除定时器
        else {
            // auto timer = conns[sockfd]->m_timer;
            // conns[sockfd]->close_conn();
            // timer_hdr.delete_timer(timer);
        }
    }
    else if (conn->m_actor_mode == REACTOR) {
        auto workreq = std::make_shared<HTTPReq>(conn, HTTPReq::READ);
        m_pool.appendReq(workreq);
    }
}

void WebServer::handle_write()
{
    LOG_INFO("%s", "Handle write");
    auto conn = m_connhdr.find_conn(m_eventfd);
    if (conn->m_actor_mode == PROACTOR) {
        // 工作线程中直接完成缓存区读写，不需要再添加任务
        if (!conn->write()) {
            conn->close_conn();
        }
    }
    else if (conn->m_actor_mode == REACTOR) {
        auto workreq = std::make_shared<HTTPReq>(conn, HTTPReq::WRITE);
        m_pool.appendReq(workreq);
    }
}

void WebServer::handle_signal()
{
    // 处理信号
    LOG_INFO("%s", "Handle signal");
    char *signals;
    int ret = m_sighdr.get_signals(&signals);
    for (int j = 0; j < ret; ++j)
    {
        int sig = signals[j];
        switch (sig) {
            case (SIGALRM): {
                m_timeout = true;
                break;
            }
            case (SIGTERM): {
                m_stopserver = true;
                break;
            }
            case (SIGINT): {
                m_stopserver = true;
                break;
            }
        }
    }
}

void WebServer::closefd(int fd)
{
    auto conn = m_connhdr.find_conn(fd);
    if (conn) {
        conn->close_conn();
    }
    m_connhdr.delete_conn(fd);
    m_epoller.removefd(fd);
    close(fd);
}
