#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <iostream>
#include "../../json.hpp"

using json = nlohmann::json;

int main(int argc, char *argv[])
{

    char problemId[20][10];
    char problemName[20][100];
    char problemLevel[20][20];

    bool flag = true;
    char SelectAllExec[200] = "select * from problemSet limit 12";

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
                strcpy(problemId[num], Row[0]);
                strcpy(problemName[num], Row[1]);
                strcpy(problemLevel[num], Row[2]);
                num++;
            }
            //printf("已经进入cgiGetProblemSet.cgi,循环获取结果集成功\n");
        }
    }
    else
        flag = false;

    if (flag != false)
    {
        json res;
        for (int i = 0; i < num; ++i)
        {
            //printf("%s %s %s\n", problemId[i], problemName[i], problemLevel[i]);
            res[i]["problemId"] = problemId[i];
            res[i]["problemName"] = problemName[i];
            res[i]["problemLevel"] = problemLevel[i];
        }
        std::cout << res.dump(4) << "\n";
    }

    mysql_close(&ConnectPointer); //关闭数据库连接
    return 0;
}