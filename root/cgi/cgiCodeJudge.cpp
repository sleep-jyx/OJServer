#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <wait.h>
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

//将url的ISO-8859-1编码解码为UTF-8
string urlDecode(const string &SRC)
{
    string ret;
    char ch;
    int i, ii;
    for (i = 0; i < SRC.length(); i++)
    {
        if (int(SRC[i]) == 37)
        {
            sscanf(SRC.substr(i + 1, 2).c_str(), "%x", &ii); //每次都正则匹配 %x 即%加一个十六进制数
            ch = static_cast<char>(ii);
            ret += ch;
            i = i + 2;
        }
        else if (SRC[i] == '+') //不用担心真正的运算符+被转换掉，因为实际上运算符+被转义了，而剩下的+都是空格
        {
            ret += " ";
        }
        else
        {
            ret += SRC[i];
        }
    }
    return (ret);
}
//比较两个文件内容是否相同，用于比较代码输出和标准答案
bool compareFile(char *file1, char *file2)
{
    if (strlen(file1) == 0 || strlen(file2) == 0)
        return false;
    std::string t, file1Content, file2Content;
    freopen(file1, "r", stdin);
    char c;
    while (scanf("%c", &c) != EOF)
        file1Content += c;
    fclose(stdin);
    freopen(file2, "r", stdin);
    while (scanf("%c", &c) != EOF)
        file2Content += c;
    fclose(stdin);
    if (file1Content.size() != file2Content.size())
        return false;
    for (int i = 0; i < file1Content.size(); i++)
    {
        if (file1Content[i] != file2Content[i])
            return false;
    }
    return true;
}

//统计目录下文件数量，注意每个目录都含有. 和.. 目录，要减去
int fileNum(const char *dirname)
{
    DIR *dp;
    struct dirent *dirp;
    if ((dp = opendir(dirname)) == NULL)
        printf("无法打开该目录!\n");
    int num = 0;
    while ((dirp = readdir(dp)) != NULL)
        num++;
    closedir(dp);
    return num - 2;
}

//启动一个子进程编译代码
bool compile()
{
    pid_t childpid;
    char *ARGV[10];
    for (int i = 0; i < 10; i++)
        ARGV[i] = new char[30]; //依次分配内存空间
    if ((childpid = fork()) == 0)
    {
        strcpy(ARGV[0], "g++");
        strcpy(ARGV[1], "code.cpp");
        strcpy(ARGV[2], "-o");
        strcpy(ARGV[3], "code");
        ARGV[4] = nullptr;
        int errorfd = open("errorStatus.txt", O_CREAT | O_RDWR, 0664);
        ftruncate(errorfd, 0);        //清空内容
        lseek(errorfd, 0, SEEK_SET);  //重置偏移
        dup2(errorfd, STDERR_FILENO); //如果接下来的编译发生了错误，会将错误信息写入errorStatux.txt
        execvp(ARGV[0], ARGV);
    }
    wait(NULL);
    //查看errorStatux.txt文件，判断编译是否出问题

    char errorContent[500];
    freopen("errorStatus.txt", "r", stdin);
    int i = 0;
    while (scanf("%c", &errorContent[i]) != EOF && i < 500)
        i++;
    fclose(stdin);
    if (i == 0) //没有错误信息，说明编译成功
        return true;
    else
    {
        printf("%s", errorContent);
        return false;
    }
}

//运行样例输入
void run(int problemId)
{
    char problemIdStr[10];
    sprintf(problemIdStr, "%d", problemId); //数字转字符数组

    char dirName[100];
    strcpy(dirName, "./root/judge/samples/");
    strcat(dirName, problemIdStr);
    int n = fileNum(dirName) / 2; //每个题目的样例目录都有输入和输出，所以除2
    int acNum = 0;                //通过样例数
    for (int i = 1; i <= n; i++)
    {
        char numStr[10];
        sprintf(numStr, "%d", i);
        //拼接样例输入文件路径
        char inFile[100];
        strcpy(inFile, dirName);
        strcat(inFile, "/In");
        strcat(inFile, numStr);
        strcat(inFile, ".txt");
        //拼接样例输出文件路径
        char outFile[100];
        strcpy(outFile, dirName);
        strcat(outFile, "/Out");
        strcat(outFile, numStr);
        strcat(outFile, ".txt");
        //拼接代码运行结果路径
        char codeOutFile[100];
        strcpy(codeOutFile, "./root/judge/codeOut/");
        strcat(codeOutFile, problemIdStr);
        strcat(codeOutFile, "/codeOut");
        strcat(codeOutFile, numStr);
        strcat(codeOutFile, ".txt");
        //测试样例输入，得到代码运行结果
        if (fork() == 0)
        {
            int caseInfd = open(inFile, O_CREAT | O_RDWR, 0664);       //打开样例输入文件描述符
            ftruncate(caseInfd, 0);                                    //清空内容
            lseek(caseInfd, 0, SEEK_SET);                              //重置偏移
            dup2(caseInfd, STDIN_FILENO);                              //将输入文件重定向到标准输入，这样下面运行代码时，代码内部就是直接从文件获取输入
            int codeOutfd = open(codeOutFile, O_CREAT | O_RDWR, 0664); //打开一个输出文件描述符
            ftruncate(codeOutfd, 0);                                   //清空内容
            lseek(codeOutfd, 0, SEEK_SET);                             //重置偏移
            dup2(codeOutfd, STDOUT_FILENO);                            //将该文件重定向到标准输出，代码运行的输出结果直接输出到文件中
            execl("./code", "", NULL);
            exit(0);
        }
        wait(NULL);
        if (compareFile(outFile, codeOutFile))
            acNum++;
    }
    printf("通过用例%d/%d\n", acNum, n);
    //评测完要清空用户的输出结果文件，其实这是一个临界区，应该上锁的
}
//列出当前目录下文件，看当前工作目录是什么，为运行时提供参考
void myls()
{
    pid_t childpid;
    char *ARGV[10];
    for (int i = 0; i < 10; i++)
        ARGV[i] = new char[30]; //依次分配内存空间
    if ((childpid = fork()) == 0)
    {
        strcpy(ARGV[0], "ls");
        strcpy(ARGV[1], ".");
        ARGV[2] = nullptr;
        execvp(ARGV[0], ARGV);
    }
    wait(NULL);
}

int main(int argc, char *argv[])
{
    //提交代码评测的表单有三个元素：题目号、代码后缀、代码，格式为: problemId=xx&suffix=xx&code=xx(解码后的)
    string decodedStr = urlDecode(argv[0]); //textarea默认是ISO-8859-1编码，要进行解码
    //解析题号和后缀
    int problemId; //提交的代码是对应的哪一题，以便查找响应的目录找样例输入
    char suffix[20];
    if (sscanf(decodedStr.c_str(), "problemId=%d&suffix=%[^&]&", &problemId, suffix) == 2) //提取suffix时，不要使用%s，因为会把&也看做字符串
    {
        //printf("解析所得题号=%d 后缀=%s\n", problemId, suffix);
    }

    std::ofstream OsWrite("code.cpp", std::ofstream::out); //out是覆盖写模式，app是添加模式
    int pos = decodedStr.find("code=");
    if (pos == string::npos)
        return 0;
    OsWrite << decodedStr.substr(pos + 5);
    OsWrite.close();

    //编译代码
    if (compile() == true)
    { //编译成功才会运行样例,生成用户代码输出结果文件并评判
        run(problemId);
    }

    return 0;
}