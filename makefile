server:http_conn.cpp http_conn.h  threadpool.h webServer-v2.cpp locker.h
	g++ -o server webServer-v2.cpp http_conn.cpp  http_conn.h threadpool.h  locker.h -lpthread  -lctemplate

objects = cgiMysql cgiGetProblemSet cgiGetProblem cgiCodeJudge

cgi: $(objects)

cgiMysql:./root/cgi/cgiMysql.cpp
	g++ -o ./root/cgi/cgiMysql.cgi ./root/cgi/cgiMysql.cpp  -lmysqlclient

cgiGetProblemSet:./root/cgi/cgiGetProblemSet.cpp
	g++ -o ./root/cgi/cgiGetProblemSet.cgi ./root/cgi/cgiGetProblemSet.cpp  -lmysqlclient

cgiGetProblem:./root/cgi/cgiGetProblem.cpp
	g++ -o ./root/cgi/cgiGetProblem.cgi ./root/cgi/cgiGetProblem.cpp  -lmysqlclient

cgiCodeJudge:./root/cgi/cgiCodeJudge.cpp
	g++ -o ./root/cgi/cgiCodeJudge.cgi ./root/cgi/cgiCodeJudge.cpp 

clean:
	rm  server