
#include"http_conn.h"

int http_conn::m_epollfd = -1;   // 所有的socket上的事件都被注册到同一个epollfd上
int http_conn::m_user_count = 0;

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "400 Bad Request! Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "403 Forbidden! You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "404 Not Found! The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

locker m_lock;
mysql_conn  m_mysql;
const char * doc_root = "/home/ljc/projects/webserver/root";

void setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//添加文件描述符到epoll中
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd =fd;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;

    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}

//从epoll中删除文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符,重置socket上epolloneshot事件， 以确保下一次可读时， EPOLLIN能被触发
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化连接
void http_conn::init(int sockfd, const sockaddr_in & addr,mysql_conn *connPool)
{
    m_sockfd = sockfd;
    m_address = addr;

    //设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //添加文件描述符到epoll中
    addfd(m_epollfd, m_sockfd, true);
    //总用户数加1
    m_user_count++;

    init_status();
    init_mysql(connPool);
}

void http_conn::init_status(){

    bytes_to_send = 0;
    bytes_have_send = 0;
    mysql = NULL;

    m_check_state = CHECK_STATE_REQUESTLINE;    //初始化 状态为解析请求首行
    m_linger = false;   // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;
    m_checked_index = 0;    
    m_start_line = 0;
    m_read_index = 0;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_string = 0;
    m_content_length = 0;
    m_write_idx = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILE_NAME_LEN);
}

void http_conn::init_mysql(mysql_conn *connPool){
    mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        printf("SELECT error:%s\n", mysql_error(mysql));
    }
    
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        m_users[temp1] = temp2;
    }
}

//关闭连接
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        //总用户数-1
        m_user_count--;
    }
}

// 循环读取客户数据，直到读好或者对方关闭连接
bool http_conn::read(){
    // printf("一次性读完所有数据\n");
    if(m_read_index >= READ_BUFFER_SIZE){
        return false;
    }

    // 读取到的字节
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index,READ_BUFFER_SIZE - m_read_index, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 没有数据
                break;
            }else{
                return false;
            }
        }else if(bytes_read == 0){
            //对方关闭链接
            return false;
        }
        m_read_index += bytes_read;
    }
    // printf("read data: %s\n", m_read_buf);
    return true;
}

//主状态机, 解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read()   
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret= NO_REQUEST;

    char *text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
    || (line_status = parse_line()) == LINE_OK)
    {
        //解析到了一行完整的数据,或者解析到了请求体,也是完整的数据
        /* code */
        text = get_line();
        m_start_line = m_checked_index;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                //解析请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
                
            case CHECK_STATE_HEADER:
            {
                //解析请求头
                ret = parse_header(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if( ret == GET_REQUEST){
                    //作为get请求 则需要跳转到报文响应函数
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                //解析消息体
                ret = parse_content(text);
                //对于post请求 跳转到报文响应函数
                if(ret ==  GET_REQUEST){
                    return do_request();
                }
                //更新 跳出循环 代表解析完了消息体
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    
    return NO_REQUEST;
}

//解析HTTP请求行, 获得请求方法，目标url， http版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    //GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");

    //GET\0/index.html HTTP/1.1
    *m_url++ = '\0';

    char * method = text;
    if( strcasecmp(method, "GET") == 0){
        m_method = GET;
    }else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
     else {
        return BAD_REQUEST;
    }
    
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++ = '\0';

    // /index.html\0HTTP/1.1
    if( strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    
    // http://192.168.0.1:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7; //192.168.0.1:10000/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }

    //https的情况
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    //不符合规则的报文
    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    //当url为/时，显示欢迎界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    m_check_state = CHECK_STATE_HEADER;     //主状态机检查状态变成检查请求头

    return NO_REQUEST;
}   

//解析HTTP请求头
http_conn::HTTP_CODE http_conn::parse_header(char* text){
    //遇到空行，表示头部字段解析完毕
    if( text[0] == '\0'){
        //如果HTTP请求有请求体，则还需要读取m_content_length字节的消息体
        //状态机转移到CHECK_STATE_CONTENT
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        
        //否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }else if ( strncasecmp( text, "Connection:", 11) == 0){
        //处理Connection 头部字段， Connection： keep-alive
        text += 11;
        text += strspn(text, " \t");
        if( strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }else if ( strncasecmp( text, "Content-Length:", 15) == 0){
        //处理Content-Length头部字段， Content-Length： 1000
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }else if ( strncasecmp( text, "Host:", 5) == 0){
        //处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }else{
        // printf("unkown header %s\n", text);
    }
    return NO_REQUEST;
}

//解析HTTP请求体
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    //判断是否读取了消息体
    if( m_read_index >= (m_content_length + m_checked_index) ){
        text[ m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}   

//解析一行, 判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line(){

    char temp;
    for(; m_checked_index < m_read_index; m_checked_index++){
        temp = m_read_buf[m_checked_index];
        if( temp == '\r'){
            if(m_checked_index + 1 == m_read_index){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_index+1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if(temp == '\n'){
            if(m_checked_index > 1 && (m_read_buf[m_checked_index-1] == '\r')){
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}



http_conn::HTTP_CODE http_conn::do_request(){

     //将初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //找到m_url中/的位置
    //POST /2CGISQL.cgi HTTP/1.1
    printf("%s\n",m_url);
    const char *p = strrchr(m_url, '/');

    //处理cgi
    //实现登录和注册校验
     if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILE_NAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (m_users.find(name) == m_users.end())
            {
                m_lock.lock();
                int res = mysql_query(&m_mysql.m_mysql, sql_insert);
                m_users.insert(std::pair<std::string, std::string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (m_users.find(name) != m_users.end() && m_users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }
    //如果请求资源为/0，表示跳转注册界面
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        //将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果请求资源为/1，表示跳转登录界面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        //将网站目录和/log.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }    
    else
        strncpy( m_real_file + len, m_url, FILE_NAME_LEN - len - 1);

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if(stat( m_real_file , & m_file_stat) < 0){
        return NO_RESOURCE;
    }

    // 判断访问权限
    if( ! (m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE,fd, 0);
    close(fd);

    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...){
    if( m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE -1 -m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){
        return false;
    }
    m_write_idx += len;
    va_end( arg_list);
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_length(int content_len)
{
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_content_type(){
    return add_response( "Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers( strlen(error_400_form));
            if( ! add_content(error_400_form)){
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers( strlen(error_404_form));
            if( ! add_content(error_404_form)){
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers( strlen(error_403_form));
            if( ! add_content(error_403_form)){
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers( m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;
            
            return true;
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::write(){
    // printf("一次性写完所有数据\n");

    int temp = 0;

    if(bytes_to_send == 0){
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN);
        init_status();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN){
                modfd( m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if(m_linger){
                init_status();
                return true;
            }else{
                return false;
            }
        }
        
    }
    

}
//由线程池中的工作线程调用，这是处理http请求的入口函数
void http_conn::process(){

    // 解析http请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // printf("parse request, create response\n");

    // 生成响应
    bool write_ret = process_write( read_ret);
    if(!write_ret){
        printf("write file no resource\n");
        // close_conn();
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT);
}