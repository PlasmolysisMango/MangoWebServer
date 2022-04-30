#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <stdarg.h>
#include <errno.h>
#include "threadpool.h"

struct RepInfo {
    const char *title;
    const char *form;
    RepInfo(const char *_t, const char *_f = nullptr) : title(_t), form(_f) {}
};

// 两种事件处理模式，Proactor为同步模拟
enum ACTOR_MODE { REACTOR, PROACTOR };
// 两种触发模式
enum TRI_MODE { LT, ET };

// 将socket设置为非阻塞的函数，返回sock的原配置
int setnonblocking(int fd);

// 采用RAII的Epoll操作类
class EpollControl {
public:
    EpollControl():m_epollfd(epoll_create(5)) {}
    ~EpollControl() {
        if (m_epollfd != -1) {
            close(m_epollfd);
        }
    }
    // 向epoll对象中添加fd, oneshot属性只影响ET，默认启用
    void addfd(int fd, TRI_MODE tmode, bool oneshot = true);
    // 修改fd，主要用于重置oneshot状态，只用在启用了oneshot模式的ET
    void modfd(int fd, int ev);
    // 移除某个fd
    void removefd(int fd);
    // 获取本来的epollfd
    int getfd() const { return m_epollfd; }
    // 设置某个fd为非阻塞
    static int setnonblocking(int fd);

private:
    int m_epollfd = -1;
};

class HTTPConn
{
public:
    // 支持的文件名最大长度
    static const int FILENAME_LEN = 200;
    // 读写缓冲区
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP各种请求
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS,
                CONNECT, PATCH };
    // 主状态机：分析请求行，分析头部字段，分析请求体
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, 
                        CHECK_STATE_CONTENT };
    // 从状态机分析行的三种状态：读取完成，行数据错误和行不完整。
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
    // 处理结果：请求不完整， 获取到完整请求，错误请求，权限错误，服务器内部错误，客户端关闭连接，资源不存在，获取文件资源成功
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, 
    INTERNAL_ERROR, CLOSED_CONNECTION, NO_RESOURCE, FILE_REQUEST};

public:
    HTTPConn() {}
    ~HTTPConn() {}

public:
    // 初始化新接收的连接
    void init(int sockfd, const sockaddr_in &addr, ACTOR_MODE amode = REACTOR, TRI_MODE tmode = ET);
    // 关闭连接
    void close_conn(bool real_close = true);
    // 处理请求
    void process();
    // 非阻塞读写
    bool read();
    bool write();

private:
    // 初始化连接
    void init();
    // 解析HTTP
    HTTP_CODE process_read();
    // 填充HTTP应答
    bool process_write(HTTP_CODE ret);

    // 有限状态机分析HTTP请求，用于process_read()
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() 
    {
        return m_read_buf + m_start_line;
    }
    LINE_STATUS parse_line();

    // 填充HTTP应答，用于process_write()
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    // epoll文件描述符
    // static int m_epollfd;
    static EpollControl m_epoller;
    // 用户数量
    static int m_user_count;
    // int m_state; // PROACTOR模式下的读写状态，0为读，1为写
    ACTOR_MODE m_actor_mode;

private:
    // 当前连接的socket和对应地址信息
    int m_sockfd;
    struct sockaddr_in m_address;
    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 标识解析状态的参数
    int m_read_idx = 0;
    int m_checked_idx = 0;
    int m_start_line = 0;
    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 标识写缓冲区中待发送的数据
    int m_write_idx = 0;

    // 主状态机状态标识
    CHECK_STATE m_check_state;
    METHOD m_method;

    // 客户请求的完整路径
    char m_real_file[FILENAME_LEN];
    // 客户请求URL
    char *m_url = NULL;
    // HTTP版本号
    char *m_version = NULL;
    // 主机名
    char *m_host = NULL;
    // 消息体长度
    int m_content_length;
    // 保持连接Keep_alive
    bool m_linger;
    
    // 客户请求文件的内存映射地址
    char *m_file_address = NULL;
    // 目标文件的状态
    struct stat m_file_stat;
    // writev函数需要的数据结构，后者指示被写内存块的数量
    struct iovec m_iv[2];
    int m_iv_count = 0;
};

class HTTPReq: public WorkRequest {
public:
    enum STATE{ READ, WRITE, NONE};
    HTTPReq(std::shared_ptr<HTTPConn> _httpcon, STATE _state = NONE) 
    : m_httpconn(_httpcon), m_state(_state) {}
    bool do_request() override;
private:
    std::shared_ptr<HTTPConn> m_httpconn;
    STATE m_state = NONE;
};

#endif