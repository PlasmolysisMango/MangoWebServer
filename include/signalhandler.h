#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>
#include "log.h"
#include "utils.h"

const unsigned MAX_SIGNALNUM = 1024;

inline void send_error(int connfd, const char *info) 
{
    LOG_ERROR("%s", info);
    send(connfd, info, strlen(info), 0);
}

class SigHandler {
public:
    static SigHandler& getInstance() {
        static SigHandler hdr;
        return hdr;
    }
    ~SigHandler() {
        close(m_sockpair.first);
        close(m_sockpair.second);
    }
    std::pair<int, int> get_sockpair() const{
        return m_sockpair;
    }

    int get_sockread() const {
        return m_sockpair.first;
    }

    int get_sockwrite() const {
        return m_sockpair.second;
    }

    static void send_sig(int sig) {
        // LOG_INFO("Send signal: %d", sig);
        int save_errno = errno;
        assert(send(getInstance().m_sockpair.second, (char*)&sig, 1, 0) != -1);
        errno = save_errno;
    }
    // 添加一个延后处理的信号
    void delaysig(int sig) {
        struct sigaction sa;
        memset(&sa, '\0', sizeof sa);
        sa.sa_handler = send_sig;
        // 接收到信号时，重新发起被打断的系统调用
        sa.sa_flags |= SA_RESTART;
        sigfillset(&sa.sa_mask);
        assert(sigaction(sig, &sa, NULL) != -1);
    }

    // 为一个信号添加处理函数
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
    // 获取延后处理的信号数组
    int get_signals(char **signals) {
        int ret = recv(m_sockpair.first, m_signals, sizeof m_signals, 0);
        if (ret == -1) {
            LOG_ERROR("%s", "Recv signal error");
            return -1;
        }
        *signals = m_signals;
        return ret;
    }
    void init(EpollControl &epoller) {
        epoller.addfd(m_sockpair.first, TRI_MODE::ET, false);
        setnonblocking(m_sockpair.second);
    }

private:
    SigHandler() {
        int pipefd[2];
        socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
        m_sockpair = {pipefd[0], pipefd[1]};
    }
    std::pair<int, int> m_sockpair;
    char m_signals[MAX_SIGNALNUM];
};

#endif