#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>

bool testSearch(char *_username, char *_password)
{
    bool flag;
    //可能会发生sql注入攻击
    char SelectAllExec[200] = "select * from user where username=";
    strcat(SelectAllExec, "\'");
    strcat(SelectAllExec, _username);
    strcat(SelectAllExec, "\' and passwd=");
    strcat(SelectAllExec, "\'");
    strcat(SelectAllExec, _password);
    strcat(SelectAllExec, "\';");

    int ResultNum = 0;
    MYSQL_RES *Result;
    MYSQL_ROW Row;
    MYSQL ConnectPointer;
    mysql_init(&ConnectPointer);
    mysql_real_connect(&ConnectPointer, "127.0.0.1", "root", "muou123", "webServer", 0, NULL, 0);

    if (&ConnectPointer)
    {
        ResultNum = mysql_query(&ConnectPointer, SelectAllExec); //查询成功返回0
        if (ResultNum != 0)
            flag = false;
        Result = mysql_store_result(&ConnectPointer); //获取结果集
        if (Result == NULL)
            flag = false;
        if ((Row = mysql_fetch_row(Result)) == NULL) //遍历结果集，但因为这是查询语句，所以只看有无一行数据
            flag = false;
        else
            flag = true;
    }
    else
        flag = false;

    mysql_close(&ConnectPointer); //关闭数据库连接
    return flag;
}

int main(int argc, char *argv[])
{
    char username[20];
    char password[20];
    int statusNum;
    char statusStr[200];
    if (sscanf(argv[0], "username=%[^&]&password=%s", username, password) != 1)
    {
        if (testSearch(username, password))
            statusNum = 1;
        else
            statusNum = 2;
    }

    //响应头
    //printf("HTTP/1.1 200 OK\r\nContent-type:hello\r\n\r\n");
    //页面主体
    printf("<!DOCTYPE html>");
    printf("<html lang=\"en\">\n");
    printf("<head>\n");
    printf("<title>CGI Check Program</title>\n");
    //printf("<meta http-equiv=\"refresh\" content=\"3;url=/index.html\">");//变成html有用，cgi渲染的没用
    if (statusNum == 1)
        printf("<script>window.setTimeout(\"location.href = '../index.html'\", 3000);</script>\n"); //可行
    else if (statusNum == 2)
        printf("<script>window.setTimeout(\"location.href = '../log.html'\", 3000);</script>\n");
    printf("<meta charset=\"UTF-8\" />");
    printf("</head>\n");
    printf("<body>\n");
    if (statusNum == 1)
    {
        printf("<h2>hello %s</h2>", username);
        printf("<h3>登录成功，3秒后自动跳转到主页，或点击下方链接跳转</h3>");
    }
    else if (statusNum == 2)
        printf("<h3>登录失败，3秒后返回登录页</h3>");

    printf("<a href=\"/index.html\">主页</a>");
    printf("</body>\n");
    printf("</html>\n");

    return 0;
}