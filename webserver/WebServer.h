
#pragma once
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <time.h>

#include "../http_conn/http_conn.h"
#include "../threadpool/locker.h"
#include "../threadpool/threadpool.h"
#include "../timer/lst_timer.h"
#include "../mysql_conn/mysql_conn.h"

using namespace std;

#define MAX_FD 65535 //最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 10


class WebServer
{
private:
    /* data */
public:
    WebServer(/* args */);
    ~WebServer();
    void init(int port , string user, string passWord, string databaseName,int sql_num,int thread_num );
    
    void thread_pool();
    void sql_pool();

public:
    //基础
    int m_port;
    char *m_root;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //数据库相关
    mysql_conn *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    
    //定时器相关
    client_data *users_timer;
    Utils utils;
};


#endif