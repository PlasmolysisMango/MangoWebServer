#include "httpconn.h"
#include "log.h"

// 网站根目录
static RepInfo ok_200 = RepInfo("2333");
static RepInfo error_400("Bad Request", "Your request has bad syntax or is inherently impossible to satisfy.\n");
static RepInfo error_403("Forbidden", "You do not have permission to get file from this server.\n");
static RepInfo error_404("Not Found", "The requested file was not found on this server.\n");
static RepInfo error_500("Internal Error", "There was an unusual problem serving the requested file.\n");
const char *doc_root = "../root";

int EpollControl::setnonblocking(int fd)
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
    if (tmode == ET)
        event.events |= EPOLLET;
    if (oneshot) {
        // 只会触发一次事件
        // 除非使用epoll_ctl函数重置该文件描述符上注册的EPOLLONESHOT事件
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

void EpollControl::modfd(int fd, int ev) 
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(getfd(), EPOLL_CTL_MOD, fd, &event);
}

int HTTPConn::m_user_count = 0;
EpollControl HTTPConn::m_epoller = EpollControl();

void HTTPConn::close_conn(bool real_close) 
{
    if (real_close && (m_sockfd != -1)) {
        m_epoller.removefd(m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void HTTPConn::init(int sockfd, const sockaddr_in &addr, ACTOR_MODE amode, TRI_MODE tmode)
{
    m_sockfd = sockfd;
    m_address = addr;
    // 下面的代码是用于取消关闭连接时的TIME_WAIT状态
    #ifdef DEBUG_MODE
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    #endif
    m_actor_mode = amode;

    m_epoller.addfd(sockfd, tmode);
    m_user_count++;
    init();
}

void HTTPConn::init() 
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

HTTPConn::LINE_STATUS HTTPConn::parse_line()
{
    // m_checked_idx为需要解析的第一个字符位置，m_read_idx为最后一个需要解析的数据的下个字符
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        // 若检查到回车符
        if (temp == '\r') {
            // 若为最后一个字符，说明行不完整，只有\r\n才是完整行的结尾
            if (m_checked_idx == m_read_idx - 1) 
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx+1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // 也有可能直接检查到换行符，此时需要考虑前一个字符情况
        else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r') {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 即还没有收到完整的一行
    return LINE_OPEN;
}

// 对应epoll的oneshot模式，循环读取直到无数据可读
bool HTTPConn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    // LT模式，只读取一次数据
    if (m_actor_mode == REACTOR) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read <= 0)
            return false;
        m_read_idx += bytes_read;
        return true;
    }
    // ET模式，循环读取直到全部读取完成
    else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                            READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
                return false;
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析HTTP请求行
HTTPConn::HTTP_CODE HTTPConn::parse_request_line(char *text) 
{
    // 请求行形如：GET /562f25980001b1b106000338.jpg HTTP/1.1
    m_url = strpbrk(text, " \t"); // 找出最先含有搜索字符串中任一字符的位置并返回
    // 若没有空格或者\t，则请求有问题
    if (!m_url) 
        return BAD_REQUEST;
    *m_url++ = '\0'; // 分割字符串
    char *method = text;
    // 仅支持GET方法
    if (strcasecmp(method, "GET") == 0) { // 忽略大小写
        m_method = GET;
    }
    else {
        return BAD_REQUEST;
    }
    // 即返回开头字符中连续在指定字符串中存在的数量
    m_url += strspn(m_url, " \t"); //返回第一个不在指定字符串中出现的下标
    m_version = strpbrk(m_url, " \t");
    if (!m_version) 
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 检查URL是否合法
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/'); // 查找给定字符的第一个匹配之处
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 状态转移到头部字段分析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析一行头部信息headers
HTTPConn::HTTP_CODE HTTPConn::parse_headers(char *text)
{
    // 空行说明解析到了请求末尾，完整解析了请求(buffer初始化为'\0')
    if (text[0] == '\0') {
        // 若后续还有消息体，则还需要继续解析m_content_length长度
        if (m_content_length != 0) {
            // 状态转移至解析消息体
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 若无消息体，则说明解析完全
        return GET_REQUEST;
    }
    // 处理其他头部字段
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        // 长连接
        if (strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_WARN("Can't handle this header: %s", text);
    }
    return NO_REQUEST;
}

// 解析请求体，但只是检测是否完整读入
HTTPConn::HTTP_CODE HTTPConn::parse_content(char *text) 
{
    // 如果读取数据的末尾位置大于根据请求体长度标识的末尾位置
    // 说明请求体完全读入了
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        // text仅仅指示当前处理行开头位置的指针
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HTTPConn::HTTP_CODE HTTPConn::process_read()
{
    LINE_STATUS linestatus = LINE_OK;
    HTTP_CODE retcode = NO_REQUEST;
    char *text = 0;
    // 主状态机, 从buffer中取出所有的行
    while (((m_check_state == CHECK_STATE_CONTENT) && (linestatus = LINE_OK))
        || ((linestatus = parse_line()) == LINE_OK)) {
        // startline为行在buffer中的开始位置
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("http line: %s", text);

        switch (m_check_state) {
            // 分析请求行
            case CHECK_STATE_REQUESTLINE: {
                retcode = parse_request_line(text);
                if (retcode == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            // 分析头部字段
            case CHECK_STATE_HEADER: {
                retcode = parse_headers(text);
                if (retcode == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (retcode == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                retcode = parse_content(text);
                if (retcode == GET_REQUEST)
                    return do_request();
                linestatus = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    // 若读取行不完整
    if (linestatus == LINE_OPEN)
        return NO_REQUEST;
    else 
        return BAD_REQUEST;
}

// 如果请求的文件存在、可读且不是目录，则将其内存映射
HTTPConn::HTTP_CODE HTTPConn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 拼接请求的URL和网站根目录
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH)) // 其他用户组的读权限
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ,
                                  MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 取消内存映射
void HTTPConn::unmap()
{
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = NULL;
    }
}

// 写HTTP响应
bool HTTPConn::write() 
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        // 即发送完成
        m_epoller.modfd(m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 若写缓存满，等待下一轮EPOLLOUT事件
            if (errno == EAGAIN) {
                m_epoller.modfd(m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        // bytes_to_send <= bytes_have_send 为什么这么写？
        if (bytes_to_send <= 0) { 
            // 发送HTTP响应成功，根据HTTP请求中的长连接属性决定连接关闭
            unmap();
            if (m_linger) {
                init();
                m_epoller.modfd(m_sockfd, EPOLLIN);
                return true;
            }
            else {
                m_epoller.modfd(m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

// 向写缓存中写入待发送的数据
bool HTTPConn::add_response(const char *format, ...) 
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    // 一定要...之前的那个参数，根据内存偏移找到可变参数地址
    va_start(arg_list, format); 
    // vsnprintf会自动添加\0结束符，故需要-1
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    if (len > (WRITE_BUFFER_SIZE - 1 - m_write_idx))
        return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool HTTPConn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HTTPConn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool HTTPConn::add_content_length(int content_length)
{
    return add_response("Content-Length: %d\r\n", content_length);
}

bool HTTPConn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

bool HTTPConn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool HTTPConn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool HTTPConn::add_content_type() 
{
    return add_response("Content-Type: %s\r\n", "text/html");
}

// 根据服务器处理请求的结果返回给客户端
bool HTTPConn::process_write(HTTP_CODE ret)
{
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500.title);
            add_headers(strlen(error_500.form));
            if (!add_content(error_500.form))
                return false;
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400.title);
            add_headers(strlen(error_400.form));
            if (!add_content(error_400.form))
                return false;
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404.title);
            add_headers(strlen(error_404.form));
            if (!add_content(error_404.form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403.title);
            add_headers(strlen(error_403.form));
            if (!add_content(error_403.form))
                return false;
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200.title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
            break;
        }
        default: {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 处理HTTP请求的入口函数，有线程池的工作线程调用
void HTTPConn::process()
{
    HTTP_CODE read_ret = process_read();
    // 若未读取完整，则重新注册EPOLLIN事件继续检测其输入事件
    if (read_ret == NO_REQUEST) {
        m_epoller.modfd(m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn(); // 默认是断开连接并关闭socket
    }
    m_epoller.modfd(m_sockfd, EPOLLOUT);
}

bool HTTPReq::do_request() 
{
    if (m_httpconn->m_actor_mode == ACTOR_MODE::REACTOR) {
        if (m_state == READ) {
            if (m_httpconn->read()) {
                m_httpconn->process();
            }
        }
        else if (m_state == WRITE){
            m_httpconn->write();
        }
    }
    else {
        m_httpconn->process();
    }
    return true;
}