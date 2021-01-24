/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
//#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

//isspace：判断是否是空格符，强制类型转换为int型是常规做法
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg)
{
	//传参是连接了client的socket
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
    char *query_string = NULL;

	//读取HTTP的第一行，以\0终止：【方法 URL 版本\r\n】
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;

	//提取报文中的方法。因为HTTP的第一行结构是【方法 URL 版本\r\n】
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

	//既不是GET也不是POST的话，就返回静态信息。目前功能未实现，只返回固定的内容，这里是为了方便以后方法拓展。
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

	//读取资源定位符
    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

	//如果是GET请求。提取？前的字符。？前的是URL，？后的是参数
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

	//指定只在htdocs文件夹下找
    sprintf(path, "htdocs%s", url);
	//如果未指定获取文件，就使用默认文件
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
	//stat函数：取得指定文件的文件属性，文件属性存储在结构体stat里。成功时返回0，失败时返回-1。
	//如果服务器上没有这个文件，调用not_found返回未找到的报文。
    if (stat(path, &st) == -1) {
		//一直读到最后，为的其实是清空tcp buffer。因为这个文件找不到，后面的内容都没必要看了。
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
		//S_IFMT是文件掩码0xF0000,S_IFDIR表示这是个目录0x4000
		//如果URL的最后忘记加/，就执行这个判断，并加上默认文件名
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
		//S_IXUSR：当前用户有运行权限
		//S_IXGRP：组用户有读权限
		//S_IXOTH：其他用户有运行权限
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    )
            cgi = 1;
		//如果文件不具备以上任意权限，也不是POST方法，也不是带参数的GET方法，那就直接发送文件回去
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
        const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
	//如果是GET，一直读，无视headers，清空tcp buffer
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numchars = get_line(client, buf, sizeof(buf));
		//一直读请求头，直到找到Content-Length所在的那一行，提取其中的content_length参数。然后一直读，清空tcp buffer。
        while ((numchars > 0) && strcmp("\n", buf))
        {
			//对于每一行请求头，直接将buf[15]置为\0。实际上是为了提取每行前14个字符，补个\0构成字符串才可以使用strcasecmp进行比较。
            buf[15] = '\0';
			//对于POST报文，Content-Length请求头是必须的，所以可以直接写死。
			//当遇到Content-Length:所在行时，Content-Length:总共14个字符，然后前面在索引15处放了\0，刚好构成字符串"Content-Length:"。
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
    {
    }

	/*#include<unistd.h>
	int pipe(int filedes[2]);*/
	//管道，filedes[0]会存读管道，filedes[1]会存写管道。pipe函数成功调用则返回0，失败返回-1。
	//当对管道进行写入时，如果写入的数据小于128K则是非原子的，如果大于128K则会一直写入管道，直到全部数据被读完，如果没人读就会阻塞。

    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

	//fork创建子进程，返回负值表示创建失败。创建失败的原因可能是内存不足，也可能是进程数达到系统上限了。
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }

	//告诉client，我收到消息了。
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

	//子进程的fork返回值是0。
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

		//#include <unistd.h>
		//int dup2(int oldfd, int newfd);
		/*dup2函数将newfd这个文件描述符指向oldfd的文件。如果newfd原本有指向的文件，则关闭原文件。
		与之类似的有dup(int oldfd)函数，它返回当前进程可用的最小文件描述符，指向与oldfd相同的文件。
		dup2常用于标准输入输出的重定向，dup可和dup2搭配使用，先用dup存下标准输入输出的文件，dup2进行重定向后，借助dup备份的文件描述符进行重定向的复原。
		*/

		
        dup2(cgi_output[1], STDOUT);//将cgi_output的写管道绑定到STDOUT，就是原本会输出到标准输出的数据，现在会输出到cgi_output的写管道里。
        dup2(cgi_input[0], STDIN);	//将cgi_input的读管道绑定到STDIN，就是原本会输入到标准输入的数据，现在会输入到cgi_input的读管道里。
        close(cgi_output[0]);		//关闭子进程cgi_output的写管道。
        close(cgi_input[1]);		//关闭子进程cgi_input的读管道。常规做法，管道也是文件，与父进程共享。父进程关一端，子进程关另一端，这样父子进程之间就可以实现单向的通信。
        
		//配置各种环境变量。【待补充】
		sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);			
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

		//执行请求中指定路径的文件，NULL表示不需传参了。
        execl(path, NULL);
        exit(0);
    } else {    /* parent */
		//子进程关了一边管道，父进程关另一边。
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
			//一个字节一个字节地，将请求主体通过父进程传给子进程。
			//【父进程】cgi_input[1]写管道——>【子进程】cgi_input[0]读管道
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }

		//将消息传完给子进程之后，等子进程处理请求。如果子进程执行请求时有任何输出到STDOUT的东西，都会通过重定向之后输出到管道里。
		//【子进程】处理请求，如果有内容输出到STDOUT（已经重定向到cgi_output写管道了）——>【父进程】从cgi_output读管道接收输出——>将输出返回给client
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);	//等子进程结束
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

	//buf容量有空余，并且输入的最后没有\n：最后补终止符\0
	//buf容量有空余，并且输入的最后是\n：最后记录\n再补一个终止符\0
	//buf容量不够：最后补终止符
    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
			//如果提前读到换行符，将它替换为回车符号。if ((n > 0) && (c == '\n'))的判断是为了将\t\n的常见组合也当成一个回车来处理。
            if (c == '\r')
            {
				//读1个字符。MSG_PEEK表示从tcp buffer中读取数据但是不清空tcp buffer，这样下次可以再次读这个数据。
				//如果这个参数是0，则读完之后还会从tcp buffer中删除已读数据
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
	//在读取内容的最后补一个终止符。所以while循环里是小于size-1而不是小于size，因为最后要留一个回车的空间。
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

	//一直读到尽头，为了清空tcp buffer。headers不进行处理
    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
		//返回固定内容
        headers(client, filename);
		//将文件内容发过去
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    //generate server sockaddr
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);               //指定服务端端口为port = 4000
    name.sin_addr.s_addr = htonl(INADDR_ANY);   //INADDR_ANY ：系统中任意可用的IP地址（相当于提取当前电脑的IP地址）

	//setsockopt是设置socket属性的函数，如果无错误发生会返回0，否则返回SOCKET_ERROR。
	/*SO_REUSEADDR：如果在已经处于ESTABLISHED状态下的socket调用close，一般不会立即关闭而经历TIME_WAIT的过程，最长需等待4分钟。
	这里由于可能重复启动程序进行调试，需要重用该socket，所以需要设置SO_REUSEADDR模式*/
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
    {
        error_die("setsockopt failed");
    }

	//将服务器的socket结构体（记录了端口和IP信息了）和端口绑定起来
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);	//网络字节序（大端）转电脑的字节序，s表示16字节
    }

	//监听，最多监听五个
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t  client_name_len = sizeof(client_name);
    //pthread_t newthread;

    server_sock = startup(&port);       //构造服务端的ocket，绑定端口，进入listen状态
    printf("httpd running on port %d\n", port);

    while (1)
    {
        //while client connecting
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        accept_request(&client_sock);       //解析请求。提取方法（GET or POST），处理URL（补全默认文件，分割参数部分），执行文件读取操作/执行cgi程序。
											//如果是读取文件操作，直接由当前进程读取文件并发送回client。
											//如果是执行cgi程序：创建子进程，构建父子进程间的管道，子进程标准输入输出重定向并执行程序，父进程通过管道获得cgi程序输出数据并发送回client。
        //if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            //perror("pthread_create");
    }

    close(server_sock);

    return(0);
}

/*
服务器端 CGI 程序接收信息有三种途径：环境变量、命令行和标准输入。
其中环境变量是指 CGI 定义一组环境变量，通过环境变量可传递数据。服务器收到来自浏览器的数据，调用 CGI 脚本，CGI 脚本将收到的数据转换成环境变量并从中取出所需要的内容。
<form>标签的 METHOD 属性来决定具体使用哪一种方法。在“METHOD=GET”时，向 CGI 传递表单编码信息的是通过命令来进行的。
表单编码信息大多数是通过环境变量 QUERY_STRING 来传递的。若“METHOD=POST”，表单信息通过标准输入来读取。
还有一种不使用表单就可以向 CGI 传送信息的方法，那就是把信息直接附在 URL 地址后面，信息和URL 之间用问号（?）来进行分隔。
GET 方法是对数据的一个请求，被用于获得静态文档。GET 方法通过将发送请求信息附加在 URL 后面的参数。当 GET 方法被使用时，CGI 程序将会从环境变量 QUERY_STRING获取数据。
为了正确的响应客户端发来的请求，CGI 必须对 QUERY_STRING 中的字符串进行分析。
当用户需要从服务器获取数据，但服务器上的数据不得改变时，应该用 GET 方法；但是如果请求中的字符串超过了一定长度，通常是 1024 字节，那么这时，只能用 POST 方法。
POST 方法：浏览器将通过填写表单将数据传给服务器时一般采用POST 方法。在发送的数据超过 1024 字节时必须采用 POST 方法。
当 POST 方法被使用时，Web 服务器向CGI 程序的标准输入 STDIN 传送数据。环境变量 CONTENT_LENGTH 存放着发送的数据长度。
CGI 程序必须检查环境变量 REQUEST_METHOD 以确定有没有采用了 POST 方法，并决定是否要读取标准输入STDIN。
*/
