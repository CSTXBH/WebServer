
#pragma once
#ifndef MYSQL_CONN_H
#define MYSQL_CONN_H

#include <mysql/mysql.h>
#include <thread>
#include <cstring>
#include <sstream>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <map>
#include "../threadpool/locker.h"

using namespace std;

class mysql_conn
{
private:
    /* data */
    
    const char *host = "127.0.0.1";
    // const char * host = "192.168.0.203";
    const char *user = "root";
    const char *pwd = "123456";
    const char *db = "test";

    int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
	locker lock;
	list<MYSQL *> connList; //连接池
	sem reserve;

public:
    MYSQL m_mysql;

    string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名

    // static mysql_conn* m_connPool; 

public:
    mysql_conn(/* args */);
    // {
    //     mysql_init(&m_mysql);
    //     if (!mysql_real_connect(&m_mysql, host, user, pwd, db, 3306, 0, CLIENT_MULTI_STATEMENTS))
    //     {
    //         printf ("mysql connect failed %s\n",  mysql_error(&m_mysql) );
    //     }
    //     else
    //     {
    //         printf ("mysql connect succeed %s\n",  host);
    //     }
    // }
    ~mysql_conn();
    // {
    //     mysql_close(&m_mysql);
    //     mysql_library_end();
    // }


    MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式
	static mysql_conn *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn); 

};

class connectionRAII{

public:
	connectionRAII(MYSQL **con, mysql_conn *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	mysql_conn *poolRAII;
};

#endif