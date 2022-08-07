
#pragma once
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <pthread.h>
#include <exception>
#include <list>
#include <cstdio>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>
#include <map>

#include "../threadpool/locker.h"
#include "../threadpool/threadpool.h"
#include "../mysql_conn/mysql_conn.h"
#include "../timer/lst_timer.h"


class http_conn
{
private:
    /* data */
public:
    static int m_epollfd;                      // 所有的socket上的事件都被注册到同一个epollfd上
    static int m_user_count;                   //统计用户的数量
    static const int READ_BUFFER_SIZE = 2048;  //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; //写缓冲区的大小
    static const int FILE_NAME_LEN = 200;


    // http请求方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    /*
        解析客户端请求时， 主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析请求头
        CHECK_STATE_CONTENT:当前正在分析请求体
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    // 从状态机的三种可能状态， 即行的读取状态， 分别表示
    // 1. 读取到一个完整的行 2. 行出错 3. 行数据尚且不完整
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    /*
        服务器处理HTTP请求的可能结果， 报文解析的结果
        NO_REQUEST		    :	请求不完整，需要继续读取客户数据
        GET_REQUEST		    :	表示获得了一个完整的客户请求
        BAD_REQUEST		    :	表示客户请求语法错误
        NO_RESOURCE		    :	表示服务器没有资源
        FORBIDDEN_REQUEST	:	表示客户对资源没有足够的访问权限
        FILE_REQUEST		:	文件请求，获取文件成功
        INTERNAL_ERROR		:	表示服务器内部错误
        CLOSED_CONNECTION	:	表示客户端已经关闭连接了
    */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

public:
    http_conn(/* args */) {}
    ~http_conn() {}
    
    void init(int sockfd, const sockaddr_in &addr, mysql_conn *connPool);
    void init_mysql(mysql_conn *connPool);
    void process();    //处理客户端的请求
    void close_conn(); //关闭连接
    bool read();       //非阻塞地读
    bool write();      //非阻塞地写

private:

    void init_status(); //初始化 状态  
    HTTP_CODE process_read();                 //解析HTTP请求
    bool process_write(HTTP_CODE ret);      // 填充HTTP应答


    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char *text); //解析HTTP请求行
    HTTP_CODE parse_header(char *text);       //解析HTTP请求头
    HTTP_CODE parse_content(char *text);      //解析HTTP请求体
    char *get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();
    HTTP_CODE do_request();

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_status_line(int status, const char* title);
    bool add_response( const char* format, ... );
    bool add_headers( int content_len);
    bool add_content( const char* content );
    bool add_content_length(int content_len);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();




public:
    int m_sockfd;                      //该http连接的socket fd
    sockaddr_in m_address;             //通信的socket地址
    //
    MYSQL* mysql;
    util_timer* timer;
    client_data *users_timer;

private:
    char m_read_buf[READ_BUFFER_SIZE]; //读缓冲区
    int m_read_index;                  //标识都缓冲区中以及读入的客户端数据的最后一个字节的下一个位置, 也就是当前的长度
    int m_checked_index; //当前正在分析的字符在缓冲区的位置
    int m_start_line;    //当前正在解析的行的起始位置
    
    CHECK_STATE m_check_state; //主状态机当前所处的状态
    METHOD m_method;   //请求方法

    char *m_url;         //请求目标url
    char* m_version;                        // HTTP协议版本号，我们仅支持HTTP1.1
    char *m_host;        //主机名
    char *m_string; //存储请求头数据
    bool m_linger;       //HTTP请求是否要保持连接
    int m_content_length;    // HTTP请求的消息总长度

    char m_real_file[ FILE_NAME_LEN ];  // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    struct stat m_file_stat;// 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置

    char  m_write_buf[WRITE_BUFFER_SIZE];// 写缓冲区
    int m_write_idx;    // 写缓冲区中待发送的字节数
    struct iovec m_iv[2];    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;

    int bytes_to_send;  // 将要发送的数据的字节数
    int bytes_have_send;            // 已经发送的字节数

    int cgi;    

    std::map<std::string, std::string> m_users;
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];

    

};



#endif