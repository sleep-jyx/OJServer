### 一、在线访问地址

http://www.tinyoj.website

### 二、背景和技术点

- 这是一个仿照常见 OJ 网站的服务器，用户可以访问该服务器获取题目并提交代码，服务器后台基于 CGI 对用户提交的代码进行评测，返回代码运行结果。
- 项目技术点：
  - 使用线程池处理 HTTP 连接，采用互斥锁实现线程间互斥
  - 采用 epoll 和 Reactor 并发模型高效处理事件源
  - 使用主从状态机解析 HTTP 请求报文，支持解析 GET 和 POST 请求
  - 使用 CGI 处理 POST 请求，CGI 访问 MySQL 获取原始数据后封装为 json 格式返回服务器，服务器解析 CGI 返回的 json 格式数据，并使用 ctemplate 动态生成 HTML 页面
  - 代码评测模块使用 dup2 重定向用例输入和代码输出，对比用户代码输出和标准答案判断是否通过用例
  - 大量使用进程间通信机制如 socket、管道、信号;多处使用进程控制如 fork、exec、wait
- 具体描述见 csdn:https://blog.csdn.net/qq_45424267/article/details/120076100

### 三、项目配置

1.  解压后，进入项目目录
2.  修改 http_conn.cpp 文件 14 行对应的网站根目录为您本地的绝对路径如，"/home/xx/yy/OJServer-master/root";
3.  安装 mysql 的 C/C++库函数 sudo apt-get install libmysqlclient-dev
4.  创建 mysql 数据库和数据表

    ````
    //创建数据库
    create database webServer;
    // 创建 user 表
    USE webServer;
    CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
    )
    default charset=utf8;
    //插入 user 数据
    INSERT INTO user(username, passwd) VALUES('admin', '123456');

    //创建problemSet表
    CREATE TABLE `webServer`.`problemSet` (
    `problemId` INT NOT NULL,
    `problemName` VARCHAR(45) NULL,
    `problemLevel` VARCHAR(45) NULL,
    PRIMARY KEY (`problemId`)
    )
    default charset=utf8;
    //插入数据
    insert into problemSet(problemId,problemName,problemLevel)
    values
    (2,'替换空格','简单'),
    (3,'从尾到头打印链表','简单'),
    (6,'旋转数组的最小数字','简单'),
    (7,'斐波那契数列','入门')

    // 题目详细信息表
    CREATE TABLE `webServer`.`problem` (
    `problemId` INT NOT NULL,
    `description` VARCHAR(400) NULL,
    `caseIn` VARCHAR(200) NULL,
    `caseOut` VARCHAR(200) NULL,
    PRIMARY KEY (`problemId`))
    default charset=utf8;

    insert into problem(problemId,description,caseIn,caseOut)
    values
    (2,'请实现一个函数，将一个字符串中的每个空格替换成“%20”。例如，当字符串为We Are Happy.则经过替换之后的字符串为We%20Are%20Happy。','"We Are Happy"','"We%20Are%20Happy"'),
    (3,'输入一个链表的头节点，按链表从尾到头的顺序返回每个节点的值（用数组返回）。0 <= 链表长度 <= 10000','{1,2,3}','[3,2,1]'),
    (6,'把一个数组最开始的若干个元素搬到数组的末尾，我们称之为数组的旋转。输入一个非递减排序的数组的一个旋转，输出旋转数组的最小元素。NOTE：给出的所有元素都大于0，若数组大小为0，请返回0。','[3,4,5,1,2]','1'),
    (7,'大家都知道斐波那契数列，现在要求输入一个整数n，请你输出斐波那契数列的第n项（从0开始，第0项为0，第1项是1）。n≤39','4','3')
    ```

    ````

5.  修改 /root/cgi/cgiMysql.cpp 第 23 行的数据库信息
    修改/root/cgi/cgiGetProblemSet.cpp 第 25 行的数据库信息
    修改/root/cgi/cgiGetProblem.cpp 第 40 行的数据库信息
6.  确保在 OJServer-master 目录下，执行

    ```bash
    $ make
    $ make cgi
    ```

7.  启动服务器

    ```bash
    $ ./server 127.0.0.1 8089
    ```

8.  浏览器访问 url
    127.0.0.1:8089/log.html

> 您可能还需要配置 ctenplate 库，其 github 地址为： https://github.com/OlafvdSpek/ctemplate

9. 要编写测试用例，只要在 root/judge/samples/题号 下编写样例，注意要同时有 In 和 Out，命令应该顺序增长，不要跳数字，可以参照 root/judge/samples/2 目录下的用例写法
