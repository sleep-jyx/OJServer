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
#include <string>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <wait.h>
#include <iostream>
#include "locker.h"
#include <google/template.h>
#include "json.hpp"

using json = nlohmann::json;

class http_conn
{
public:
    //文件名最大长度
    static const int FILENAME_LEN = 200;
    //读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;
    //HTTP请求方法，暂时仅支持GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };
    //解析客户请求时，主状态机所处的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, //处理请求
        CHECK_STATE_HEADER,          //处理头部
        CHECK_STATE_CONTENT          //处理内容
    };
    //服务器处理HTTP请求的可能结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,       //错误的请求
        NO_RESOURCE,       //资源不存在
        FORBIDDEN_REQUEST, //资源禁止访问
        FILE_REQUEST,      //请求文件(经过检验是存在且可以发送的)
        INTERNAL_ERROR,    //内部错误
        CLOSED_CONNECTION, //关闭连接
        POST_REQUEST       //post请求
    };
    //行的读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr); //初始化新接受的连接
    void close_conn(bool real_close = true);        //关闭连接
    void process();                                 //处理客户请求
    bool read();                                    //非阻塞读
    bool write();                                   //非阻塞写

private:
    void init();                       //初始化连接
    HTTP_CODE process_read();          //解析HTTP请求
    bool process_write(HTTP_CODE ret); //填充HTTP应答

    //分析HTTP请求
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    //下面一组函数被process_write调用以填充HTTP应答
    void unmap();
    bool add_response(const char *format, ...);          //核心
    bool add_content(const char *content);               //post请求才用到？
    bool add_status_line(int status, const char *title); //添加响应头
    bool add_headers(int content_length);                //包裹了下面三个函数
    bool add_content_length(int content_length);         //添加内容长度
    bool add_linger();                                   //添加连接状态
    bool add_blank_line();                               //添加“\r\n”

public:
    static int m_epollfd;    //所有socket上的事件都注册到同一个epoll内核事件表中，所以设置为静态变量
    static int m_user_count; //用户数量总和

private:
    //该HTTP连接的socket和其socket地址
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];   //读缓冲区
    int m_read_idx;                      //已读入数据的最后一个字节的下一个位置
    int m_checked_idx;                   //正在分析的字符位置
    int m_start_line;                    //正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE]; //写缓冲区,用于填充响应头部
    int m_write_idx;                     //写缓冲区中待发送的字节数

    CHECK_STATE m_check_state; //主状态机状态
    METHOD m_method;           //HTTP请求方法

    char m_real_file[FILENAME_LEN]; //客户请求的目标文件的完整路径
    char *m_url;                    //客户请求的目标文件文件名
    char *m_version;                //HTTP协议版本号，仅支持HTTP/1.1
    char *m_host;                   //主机名
    int m_content_length;           //HTTP请求的消息体的长度
    bool m_linger;                  //HTTP请求是否保持连接

    void postRespond(); //使用cgi处理post请求
};

#endif
