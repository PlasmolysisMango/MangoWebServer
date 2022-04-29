#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

// 读缓冲区大小
#define BUFFER_SIZE 4096
// 主状态机的两种状态：分析请求行，分析头部字段
enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER};
// 从状态机分析行的三种状态：读取完成，行数据错误和行不完整。
enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
// 处理结果：请求不完整， 获取到完整请求，错误请求，权限错误，服务器内部错误，客户端关闭连接
enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, 
INTERNAL_ERROR, CLOSED_CONNECTION};
// 简化的返回信息
const char * szret[] = {"OK", "WRONG"};

// 从状态机
// 解析某行信息
LINE_STATUS parse_line(char *buffer, int &check_index, int &read_index) 
{
    // check_index为需要解析的第一个字符位置，read_index为最后一个需要解析的数据的下个字符
    char temp;
    for (; check_index < read_index; ++check_index) {
        temp = buffer[check_index];
        // 若检查到回车符
        if (temp == '\r') {
            // 若为最后一个字符，说明行不完整，只有\r\n才是完整行的结尾
            if (check_index == read_index - 1) 
                return LINE_OPEN;
            else if (buffer[check_index+1] == '\n') {
                buffer[check_index++] = '\0';
                buffer[check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // 也有可能直接检查到换行符，此时需要考虑前一个字符情况
        else if (temp == '\n') {
            if (check_index > 1 && buffer[check_index-1] == '\r') {
                buffer[check_index-1] = '\0';
                buffer[check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 即还没有收到完整的一行
    return LINE_OPEN;
}

// 解析请求行
HTTP_CODE parse_requestline(char *temp, CHECK_STATE &checkstate) 
{
    // 请求行形如：GET /562f25980001b1b106000338.jpg HTTP/1.1
    char *url = strpbrk(temp, " \t"); // 找出最先含有搜索字符串中任一字符的位置并返回
    // 若没有空格或者\t，则请求有问题
    if (!url) 
        return BAD_REQUEST;
    *url++ = '\0'; // 分割字符串
    char *method = temp;
    // 仅支持GET方法
    if (strcasecmp(method, "GET") == 0) { // 忽略大小写
        printf("The request method is GET\n");
    }
    else {
        return BAD_REQUEST;
    }
    url += strspn(url, " \t"); //返回第一个不在指定字符串中出现的下标
    char *verison = strpbrk(url, " \t");
    if (!verison) 
        return BAD_REQUEST;
    *verison++ = '\0';
    verison += strspn(verison, " \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(verison, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 检查URL是否合法
    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;
        url = strchr(url, '/'); // 查找给定字符的第一个匹配之处
    }
    if (!url || url[0] != '/')
        return BAD_REQUEST;
    printf("The request URL is: %s\n", url);
    // 状态转移到头部字段分析
    checkstate = CHECK_STATE_HEADER;
    return NO_REQUEST;
}    

// 解析头部字段
HTTP_CODE parse_headers(char *temp) 
{
    // 说明解析到了请求末尾，完整解析了请求(buffer初始化为'\0')
    if (temp[0] == '\0') 
        return GET_REQUEST;
    else if (strncasecmp(temp, "Host:", 5) == 0) {
        temp += 5;
        temp += strspn(temp, " \t");
        printf("The request host is: %s\n", temp);
    }
    else {
        printf("Can't handle this header\n");
    }
    return NO_REQUEST;
}

// 分析HTTP请求的入口函数
HTTP_CODE parse_content(char *buffer, int &check_index, CHECK_STATE &checkstate, 
int &read_index, int &start_line) 
{
    LINE_STATUS linestatus = LINE_OK;
    HTTP_CODE retcode = NO_REQUEST;
    // 主状态机, 从buffer中取出所有的行
    while ((linestatus = parse_line(buffer, check_index, read_index)) == LINE_OK) {
        // startline为行在buffer中的开始位置
        char *temp = buffer + start_line;
        start_line = check_index;
        switch (checkstate) {
            // 分析请求行
            case CHECK_STATE_REQUESTLINE: {
                retcode = parse_requestline(temp, checkstate);
                if (retcode == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            // 分析头部字段
            case CHECK_STATE_HEADER: {
                retcode = parse_headers(temp);
                if (retcode == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (retcode == GET_REQUEST) 
                    return GET_REQUEST;
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

int main(int argc, char *argv[])
{
    if (argc <= 2) {
        printf("usage: %s ip_address port\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof address);
    assert(ret != -1);
    ret = listen(listenfd, 5); // 半连接队列大小为5
    assert(ret != -1);
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof client_address;
    int fd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
    if (fd < 0)
        printf("errno is %d\n", errno);
    else {
        char buffer[BUFFER_SIZE];
        memset(buffer, '\0', BUFFER_SIZE);
        int data_read = 0;
        int read_index = 0;
        int check_index = 0;
        int start_line = 0;
        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
        while (true) {
            data_read = recv(fd, buffer + read_index, BUFFER_SIZE - read_index, 0);
            if (data_read == -1) {
                printf("reading failed\n");
                break;
            }
            else if (data_read == 0) {
                printf("remote client has closed the connection\n");
                break;
            }
            read_index += data_read;
            HTTP_CODE result = parse_content(buffer, check_index, checkstate,
                                             read_index, start_line);
            if (result == NO_REQUEST)
                continue;
            else if (result == GET_REQUEST) {
                send(fd, szret[0], strlen(szret[0]), 0);
                break;
            }
            else {
                send(fd, szret[1], strlen(szret[1]), 0);
                break;
            }
        }
        close(fd);
    }
    close(listenfd);
    return 0;
}