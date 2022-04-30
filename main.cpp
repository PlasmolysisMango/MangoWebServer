#define DEBUG_MODE

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

// #include "global.h"
#include "threadpool.h"
#include "httpconn.h"
#include "log.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = handler;
    // 接收到信号时，重新发起被打断的系统调用
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char *info) 
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    Log::getInstance().setAsync();
    Log::getInstance().setPrint();
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    LOG_INFO("Binding server @%s:%d", ip, port);
    // 向某个已被关闭或未连接的socket发送数据时，产生SIGPIPE信号
    // 默认处理方式是终止进程，在这里忽略掉该信号
    addsig(SIGPIPE, SIG_IGN);
    // 获取单例ThreadPool
    auto &pool = ThreadPool::getInstance();
    LOG_INFO("%s", "ThreadPool initializing");

    // 预分配HTTPConn对象，直接使用fd下标访问
    // 后续会使用结合定时器的连接管理类，当连接满时断开LRU连接
    auto conns = std::vector<std::shared_ptr<HTTPConn>>(MAX_FD, std::make_shared<HTTPConn>());
    LOG_INFO("Preprocessing connects[%d]", MAX_FD);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {1, 0}; // 强制退出模式，非优雅关闭
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof tmp);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof address);
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof address);
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 预分配epoll就绪数组
    epoll_event events[MAX_EVENT_NUMBER];
    // epoll监听的最大文件描述符数量，在新版本linux中该参数被忽略
    // int epollfd = epoll_create(5);
    // assert(epollfd >= 0);
    // // 对于listenfd，关闭oneshot模式
    HTTPConn::m_epoller.addfd(listenfd, TRI_MODE::LT);
    // 所有HTTPConn实例共用的epollfd
    // HTTPConn::m_epollfd = epollfd;

    while (true) {
        int number = epoll_wait(HTTPConn::m_epoller.getfd(), events, MAX_EVENT_NUMBER, -1);
        // 对异常进行处理，避免因为信号中断导致epoll失败
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof client_address;
                int connfd = accept(listenfd, (struct sockaddr *)&client_address,
                                    &client_addrlength);
                if (connfd < 0) {
                    printf("errno is %d\nconnfd is invalid.\n", errno);
                    continue;
                }
                if (HTTPConn::m_user_count >= MAX_FD) {
                    show_error(connfd, "Internal server busy.\n");
                    continue;
                }
                conns[connfd]->init(connfd, client_address, PROACTOR, LT);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 若有异常，关闭连接
                conns[sockfd]->close_conn();
            }
            else if (events[i].events & EPOLLIN) {
                if (conns[sockfd]->m_actor_mode == PROACTOR) {
                    // 若读成功，则加入工作队列处理（缓存区读写）
                    if (conns[sockfd]->read()) {
                        // pool->append(users + sockfd);
                        auto workreq = std::make_shared<HTTPReq>(conns[sockfd]);
                        pool.appendReq(workreq);
                    }
                    else {
                        conns[sockfd]->close_conn();
                    }
                }
                else if (conns[sockfd]->m_actor_mode == REACTOR) {
                    auto workreq = std::make_shared<HTTPReq>(conns[sockfd], HTTPReq::READ);
                    pool.appendReq(workreq);
                }
            }
            else if (events[i].events & EPOLLOUT) {
                if (conns[sockfd]->m_actor_mode == PROACTOR) {
                    // 工作线程中直接完成缓存区读写，不需要再添加任务
                    if (!conns[sockfd]->write()) {
                        conns[sockfd]->close_conn();
                    }
                }
                else if (conns[sockfd]->m_actor_mode == REACTOR) {
                    auto workreq = std::make_shared<HTTPReq>(conns[sockfd], HTTPReq::WRITE);
                    pool.appendReq(workreq);
                }
            }
        }
    }
    close(listenfd);
    return 0;
}
