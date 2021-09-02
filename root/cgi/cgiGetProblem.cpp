#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <iostream>
#include "../../json.hpp"

using json = nlohmann::json;

int main(int argc, char *argv[])
{
    //argv[0]：1=0&2=0&3=1&4=0&5=0&6=0&7=0&8=0&10=0&11=0
    int key, value;
    while (sscanf(argv[0], "%d=%d&", &key, &value) == 2)
    {
        //printf("key=%d value=%d\n", key, value);
        if (value == 1)
            break;
        int pos = strcspn(argv[0], "&");
        argv[0] += pos + 1;
    }
    std::cerr << "key=" << key;

    char problemId[20];     //题号
    char description[1000]; //问题描述
    char caseIn[500];       //样例输入
    char caseOut[500];      //样例输出

    bool flag = true;
    char SelectAllExec[200] = "select * from problem where problemId=";
    sprintf(problemId, "%d", key); //数字转字符串
    strcat(SelectAllExec, problemId);
    strcat(SelectAllExec, ";");

    int ResultNum = 0;
    MYSQL_RES *Result;
    MYSQL_ROW Row;
    MYSQL ConnectPointer;
    mysql_init(&ConnectPointer);
    mysql_real_connect(&ConnectPointer, "127.0.0.1", "root", "muou123", "webServer", 0, NULL, 0);
    int num = 0;

    if (&ConnectPointer)
    {
        mysql_query(&ConnectPointer, "set names utf8;");         //csdn真好，保证从MySQL查过来的数据也是UTF8编码
        ResultNum = mysql_query(&ConnectPointer, SelectAllExec); //查询成功返回0
        if (ResultNum != 0)
            flag = false;
        Result = mysql_store_result(&ConnectPointer); //获取结果集
        if (Result == NULL)
            flag = false;
        else
        {
            while ((Row = mysql_fetch_row(Result))) //循环读取结果集
            {
                // strcpy(problemId, Row[0]);
                strcpy(description, Row[1]);
                strcpy(caseIn, Row[2]);
                strcpy(caseOut, Row[3]);
                num++;
            }
            //printf("已经进入cgiGetProblemSet.cgi,循环获取结果集成功\n");
        }
    }
    else
        flag = false;

    if (flag != false)
    {
        json res; //将原始的返回数据构造为json格式
        res["problemId"] = problemId;
        res["description"] = description;
        res["caseIn"] = caseIn;
        res["caseOut"] = caseOut;
        //fflush(stdin);
        std::cout << res; //我怀疑就是之前多放了一个回车，还是会出错，看来不是
    }

    mysql_close(&ConnectPointer); //关闭数据库连接
    return 0;
}