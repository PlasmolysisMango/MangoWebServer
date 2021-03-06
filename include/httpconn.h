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
#include <unordered_map>
#include "threadpool.h"
#include "utils.h"

struct RepInfo {
    const char *title;
    const char *form;
    RepInfo(const char *_t, const char *_f = nullptr) : title(_t), form(_f) {}
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
    void init(int sockfd, ACTOR_MODE amode = PROACTOR, TRI_MODE tmode = ET, bool oneshot = true);
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
    static EpollControl &m_epoller;
    // 用户数量
    static int m_user_count;

    // 事件处理模式
    ACTOR_MODE m_actor_mode;
    // 触发模式
    TRI_MODE m_tri_mode;
    // oneshot模式
    bool m_oneshot = false;

private:
    // 当前连接的socket和对应地址信息
    int m_sockfd;
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

// 继承自线程池任务类的网络连接任务类，重写接口
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

// 连接管理类
class ConnHandler
{
public:
    static ConnHandler &getInstance() {
        static ConnHandler connhdr;
        return connhdr;
    }

    ~ConnHandler() {}
    ConnHandler(const ConnHandler &) = delete;
    ConnHandler(ConnHandler &&) = delete;
    ConnHandler &operator=(const ConnHandler &) = delete;
    ConnHandler &operator=(ConnHandler &&) = delete;
    // 添加一个连接，成功返回对应智能指针，失败返回nullptr
    std::shared_ptr<HTTPConn> add_conn(int connfd);
    // 查找一个连接，成功返回对应智能指针，不存在则返回nullptr
    std::shared_ptr<HTTPConn> find_conn(int connfd);
    // 删除一个连接
    bool delete_conn(int connfd);

    // 维护一个删除列表，在主线程统一关闭并释放所有资源
    std::vector<int> removed_fdlist;

private:
    ConnHandler() {}
    std::unordered_map<int, std::shared_ptr<HTTPConn>> m_conns;
};

#endif