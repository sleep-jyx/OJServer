#include "http_conn.h"

//定义HTTP相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";
//网站资源根目录
const char *doc_root = "/home/jyx/文档/unix/newJiaGou/root";

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//静态变量
int http_conn::m_user_count = 0; //客户连接数量
int http_conn::m_epollfd = -1;   //统一的内核事件表
//int http_conn::cgiPipe[2] = {};                    //cgi使用的管道描述符，因为在静态信号处理函数中使用，故而为静态变量
//char *http_conn::m_dynamic_page_address = nullptr; //cgi生成的动态页面的起始地址
//int http_conn::m_dynamic_page_len = 0;             //cgi生成的动态页面的长度

char *m_file_address;    //客户请求的目标文件被mmap到内存中的起始位置
struct stat m_file_stat; //目标文件的状态，判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
//采用writev执行集中写操作。writev(int fd,const struct iovec* vector,int count);
struct iovec m_iv[2];
int m_iv_count;

char *m_post_data;            //post请求携带的数据
                              //无奈之举，信号函数没法传需要的参数，只能写在类里，从而获取如管道描述符等信息。又因为信号处理函数是回调函数，不能和类有关系？改成静态，既然信号处理函数改了静态，其内部用到的cgiPipe也得改为静态
int cgiPipe[2];               //调用cgi采用fork and execute模式，该管道用于与cgi程序间的数据传输
char *m_dynamic_page_address; //cgi生成的动态页面的起始地址
int m_dynamic_page_len;       //cgi生成的动态页面的长度
char jsonDataBuf[2000];       //cgi返回的json格式的数据的缓冲区

//关闭与客户的连接
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd); //将该客户连接移出epoll
        m_sockfd = -1;
        m_user_count--; //客户连接数量-1
    }
}

//初始化与客户的连接
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    //printf("--debug--:http_conn对象init(sockfd,addr)\n");
    m_sockfd = sockfd;
    m_address = addr;
    //下面这几行做了什么处理？
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //将该客户连接加入epoll，只能有一个线程触发
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init(); //不是递归，是下面这个初始化的重载
}

void http_conn::init()
{
    //printf("--debug--:重置http_conn状态，客户连接数量=%d\n", m_user_count);
    m_check_state = CHECK_STATE_REQUESTLINE; //主状态机方法
    m_linger = false;                        //http请求是否保持连接

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
    memset(m_real_file, '\0', FILENAME_LEN); //请求目标文件的完整路径
    //释放post相关
    if (m_dynamic_page_len != 0)
    {
        delete m_dynamic_page_address;
        m_dynamic_page_len = 0;
        printf("释放动态页面缓冲区\n");
    }
    m_dynamic_page_len = 0;
    memset(jsonDataBuf, '\0', 2000);
}

//解析请求的一行
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    //从当前分析位置分析到已读入数据的最后一个字节之前的位置
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];

        if (temp == '\r')
        { //如果最后一个字符是'\r'，那么它应该还有其他数据，进入LINE_OPEN状态
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            //如果正好读完一行，以'\r\n'结尾，进入LINE_OK状态
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //否则转入LINE_BAD状态
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    //运行到这，说明不是上述的情况，数据还需要再读
    return LINE_OPEN;
}

//非阻塞读，循环读客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    { //如果读入的数据超出了缓冲区的大小，返回false
        return false;
    }

    int bytes_read = 0;
    while (true)
    {
        //从客户连接sockfd读取请求数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        //开始下一次的读循环
        m_read_idx += bytes_read;
    }

    //只有发生无数据可读才会运行到这，返回true
    return true;
}

//解析HTTP请求行，获得请求方法、目标URL，一级HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //请求数据第一行长这样： GET    /   HTTP/1.1,分为三部分，分别是请求方法、url、协议
    m_url = strpbrk(text, " \t"); //返回第一次匹配到' \t'之后的字符串
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; //以'\0'分隔开GET和url，char*类型遇到'\0'截断

    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t"); //匹配掉方法和url之间的空格
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    /*
    要实现页面跳转，就改m_url
    手动路由，如果没指定访问的url，默认访问index.html
    */

    if (strcasecmp(m_url, "/") == 0)
        strcpy(m_url, "/index.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析除请求行的HTTP头部
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_method == HEAD)
        {
            return GET_REQUEST;
        }

        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        //printf("--debug--:没有内容可以处理了\n");
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
        // printf("--debug--:解析出连接状态\n");
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text); //字符串转数字，post请求所携带的数据长度
        printf("post请求携带的数据长度=%d\n", m_content_length);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
        // printf("--debug--:解析出host\n");
    }
    else
    {
        int sep = strcspn(text, ":");
        //printf("--debug--:sep=%d\n", sep);
        *(text + sep) = '\0';
        //printf("--debug-- 尚未实现的头部字段%s的解析\n", text);
    }

    return NO_REQUEST;
}

//处理请求头的数据，post的数据放在content里面
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        m_post_data = text;
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    //printf("--debug--:preocess_read()处理客户请求\n");
    LINE_STATUS line_status = LINE_OK; //行状态
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    //进入循环条件：(主状态机正在处理内容 且 当前行状态ok) 或者 下一行状态ok就继续读下一行。
    //跳出循环条件：(主状态机在处理头部或请求，或当前行不OK) 且 下一行状态不OK
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        //printf("got 1 http line:%s\n", text);
        // printf("%s\n", text);

        switch (m_check_state) //主状态机
        {
        case CHECK_STATE_REQUESTLINE:
        {
            //printf("--debug--:处理requestLine状态\n");
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            //printf("--debug--:处理header状态\n");
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                //处理请求
                //printf("--debug--:已经解析完了需要的信息，转dorequest()开始处理请求\n");
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            //printf("--debug--:处理content状态\n");
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                //printf("--debug--:已经解析完了需要的信息，转dorequest()开始处理请求\n");
                return do_request();
            }
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

void cgiSigHandler0(int sig)
{ //获取题单的消息处理函数
    if (sig == SIGCHLD)
    { //如果是子进程结束的信号：cgi处理完成
        int ret = recv(cgiPipe[0], jsonDataBuf, sizeof(jsonDataBuf), 0);

        m_dynamic_page_len = strlen(jsonDataBuf);
        m_dynamic_page_address = new char[m_dynamic_page_len]; //非常关键，一定要分配内存，否则出现段错误
        strcpy(m_dynamic_page_address, jsonDataBuf);
    }
}

void cgiSigHandler1(int sig)
{ //获取题单的消息处理函数
    if (sig == SIGCHLD)
    { //如果是子进程结束的信号：cgi处理完成
        int ret = recv(cgiPipe[0], jsonDataBuf, sizeof(jsonDataBuf), 0);
        //使用json库解析
        json parseRes = json::parse(jsonDataBuf);
        //http模板
        google::TemplateDictionary dict("example");
        dict.SetValue("table_name", "");

        for (int i = 0; i < parseRes.size(); ++i)
        {
            google::TemplateDictionary *table1_dict;
            table1_dict = dict.AddSectionDictionary("TABLE1");
            table1_dict->SetValue("problemId", parseRes[i]["problemId"].get<std::string>());
            table1_dict->SetValue("problemName", parseRes[i]["problemName"].get<std::string>());
            table1_dict->SetValue("problemLevel", parseRes[i]["problemLevel"].get<std::string>());
        }

        std::string output;
        google::Template *tpl;
        tpl = google::Template::GetTemplate("./root/OJPage/OJProblemSet.html", google::DO_NOT_STRIP);
        tpl->Expand(&output, &dict);

        //以下三行是一体的
        m_dynamic_page_len = output.size();
        m_dynamic_page_address = new char[m_dynamic_page_len]; //非常关键，一定要分配内存，否则出现段错误
        strcpy(m_dynamic_page_address, output.c_str());
    }
}

void cgiSigHandler2(int sig)
{ //获取单个题目详细信息的cgi
    if (sig == SIGCHLD)
    { //如果是子进程结束的信号：cgi处理完成
        int ret = recv(cgiPipe[0], jsonDataBuf, sizeof(jsonDataBuf), 0);
        //使用json库解析
        //printf("在消息处理函数中出错了\n");
        json parseRes = json::parse(jsonDataBuf);
        //printf("cgi2:%s\n", jsonDataBuf);
        //http模板
        google::TemplateDictionary dict("example");
        dict.SetValue("problemId", parseRes["problemId"].get<std::string>());
        dict.SetValue("description", parseRes["description"].get<std::string>());
        dict.SetValue("caseIn", parseRes["caseIn"].get<std::string>());
        dict.SetValue("caseOut", parseRes["caseOut"].get<std::string>());

        std::string output;
        google::Template *tpl;
        tpl = google::Template::GetTemplate("./root/OJPage/OJProblem.html", google::DO_NOT_STRIP);
        tpl->Expand(&output, &dict);

        m_dynamic_page_len = output.size();
        m_dynamic_page_address = new char[m_dynamic_page_len]; //非常关键，一定要分配内存，否则出现段错误
        strcpy(m_dynamic_page_address, output.c_str());
        printf("当前渲染出的动态页面大小=%d\n", output.size());
        printf("动态页面缓冲区大小=%d\n", strlen(m_dynamic_page_address));
        close(cgiPipe[1]);
        close(cgiPipe[0]);
    }
}

void cgiSigHandler3(int sig)
{ //评测提交代码的消息处理函数
    if (sig == SIGCHLD)
    {
        recv(cgiPipe[0], jsonDataBuf, sizeof(jsonDataBuf), 0);
        //printf("%s\n", jsonDataBuf);

        //http模板渲染
        google::TemplateDictionary dict("example");
        dict.SetValue("runReult", jsonDataBuf);
        std::string output;
        google::Template *tpl;
        tpl = google::Template::GetTemplate("./root/OJPage/OJRunResult.html", google::DO_NOT_STRIP);
        tpl->Expand(&output, &dict);

        m_dynamic_page_len = output.size();
        m_dynamic_page_address = new char[m_dynamic_page_len]; //非常关键，一定要分配内存，否则出现段错误
        strcpy(m_dynamic_page_address, output.c_str());
        printf("当前渲染出的动态页面大小=%d\n", output.size());
        printf("动态页面缓冲区大小=%d\n", strlen(m_dynamic_page_address));
        close(cgiPipe[1]);
        close(cgiPipe[0]);
    }
}

//使用cgi处理post请求
void http_conn::postRespond()
{
    int pos = strcspn(m_real_file, "."); // 原url为 /xx/xx/xx.cgi，匹配到.
    while (m_real_file[pos] != '/')
        pos--;
    char *cgiName = m_real_file + pos + 1; //提取出最后的cgi程序名字，以区别对待
    printf("%s\n", cgiName);

    pid_t childpid;
    int piperet = socketpair(PF_UNIX, SOCK_STREAM, 0, cgiPipe); //创建双向管道，0端父进程用，1端子进程用
    assert(piperet != -1);
    if ((childpid = fork()) == 0)
    {
        int ret = dup2(cgiPipe[1], STDOUT_FILENO); //此后进入到cgi处理程序中，向标准输出输出就是向双向管道的1端写入数据，父进程读取0端获取
        execl(m_real_file, m_post_data, NULL);
    }
    if (childpid > 0)
    {
        if (strcasecmp(cgiName, "cgiMysql.cgi") == 0) //登录验证cgi
            signal(SIGCHLD, cgiSigHandler0);
        else if (strcasecmp(cgiName, "cgiGetProblemSet.cgi") == 0) //获取题目列表cgi
            signal(SIGCHLD, cgiSigHandler1);
        else if (strcasecmp(cgiName, "cgiGetProblem.cgi") == 0) //获取单个题目详细信息cgi
            signal(SIGCHLD, cgiSigHandler2);
        else if (strcasecmp(cgiName, "cgiCodeJudge.cgi") == 0) //评测提交代码的cgi
            signal(SIGCHLD, cgiSigHandler3);
    }
    wait(NULL);
}

http_conn::HTTP_CODE http_conn::do_request()
{

    //根路径+url拼接为完整路径
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    //printf("--debug--:请求文件完整路径%s\n", m_real_file);
    //判断文件是否存在
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    //权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    //文件是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    //post截胡
    if (m_method == POST)
    {
        // printf("--debug--:post请求数据为 %s,return\n", m_post_data);
        return POST_REQUEST;
    }

    //真的是文件，以只读方式获取文件描述符
    int fd = open(m_real_file, O_RDONLY);
    //将磁盘文件映射到内存进程空间
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // printf("--debug--:将磁盘文件映射到内存空间\n");
    close(fd);
    return FILE_REQUEST;
}

//删除映射
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        //集中写，m_iv是结构体数组，{内存地址,内存块大小}[m_iv_count]。写入客户socket连接
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        // printf("--debug--:write()成功将内存块集中写入客户socket连接\n");
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send)
        {
            unmap();
            if (m_linger)
            { //即使保持连接，还是要init()，因为http是有连接但无状态的协议
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;           //解决变参问题
    va_start(arg_list, format); //对arg_list进行初始化，指向可变参数的第一个参数
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    // printf("--debug--:m_write_idx = %d\n", m_write_idx);
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{ //暂时来说，ret只用到两种：FILE_REQUEST和POST_REQUEST，前者客户端通过get方法请求服务端上的静态资源(.html和图片等)
    //printf("--debug--:preocess_write()\n");
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    }
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        //printf("--debug--:preocess_write(),请求文件\n");
        add_status_line(200, ok_200_title);
        //add_response("justForTest: %s\r\n", "hello"); //要在加blank_line之前加上，否则就不是加入头部而是加入主体了
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size); //这个参数用于填充首部字段content，易混淆，因为add_headers有多个字段，没头没脑的传一个长度参数干什么
            //写缓冲区保存的是响应头部字段,注意其中字段content-Length是请求的文件长度，不包含响应头的长度
            //有点二级映射的意思
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //客户连接请求的磁盘文件的内存映射
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
            {
                return false;
            }
        }
    }
    case POST_REQUEST:
    {
        postRespond(); //填充动态页面进第二个分散块
        add_status_line(200, ok_200_title);
        add_headers(m_dynamic_page_len);

        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        //客户连接请求的磁盘文件的内存映射
        m_iv[1].iov_base = m_dynamic_page_address;
        m_iv[1].iov_len = m_dynamic_page_len;
        m_iv_count = 2;
        return true;
    }
    default:
    {
        return false;
    }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void http_conn::process()
{
    //printf("--debug--:process()\n");
    HTTP_CODE read_ret = process_read();
    //printf("--debug--:process()解析HTTP请求完毕，开始填充HTTP应答\n");
    if (read_ret == NO_REQUEST)
    { //没有请求就返回
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
