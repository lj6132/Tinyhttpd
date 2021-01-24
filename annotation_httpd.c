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

//isspace���ж��Ƿ��ǿո����ǿ������ת��Ϊint���ǳ�������
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
	//������������client��socket
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

	//��ȡHTTP�ĵ�һ�У���\0��ֹ�������� URL �汾\r\n��
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;

	//��ȡ�����еķ�������ΪHTTP�ĵ�һ�нṹ�ǡ����� URL �汾\r\n��
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

	//�Ȳ���GETҲ����POST�Ļ����ͷ��ؾ�̬��Ϣ��Ŀǰ����δʵ�֣�ֻ���ع̶������ݣ�������Ϊ�˷����Ժ󷽷���չ��
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

	//��ȡ��Դ��λ��
    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

	//�����GET������ȡ��ǰ���ַ�����ǰ����URL��������ǲ���
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

	//ָ��ֻ��htdocs�ļ�������
    sprintf(path, "htdocs%s", url);
	//���δָ����ȡ�ļ�����ʹ��Ĭ���ļ�
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
	//stat������ȡ��ָ���ļ����ļ����ԣ��ļ����Դ洢�ڽṹ��stat��ɹ�ʱ����0��ʧ��ʱ����-1��
	//�����������û������ļ�������not_found����δ�ҵ��ı��ġ�
    if (stat(path, &st) == -1) {
		//һֱ�������Ϊ����ʵ�����tcp buffer����Ϊ����ļ��Ҳ�������������ݶ�û��Ҫ���ˡ�
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
		//S_IFMT���ļ�����0xF0000,S_IFDIR��ʾ���Ǹ�Ŀ¼0x4000
		//���URL��������Ǽ�/����ִ������жϣ�������Ĭ���ļ���
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
		//S_IXUSR����ǰ�û�������Ȩ��
		//S_IXGRP�����û��ж�Ȩ��
		//S_IXOTH�������û�������Ȩ��
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    )
            cgi = 1;
		//����ļ����߱���������Ȩ�ޣ�Ҳ����POST������Ҳ���Ǵ�������GET�������Ǿ�ֱ�ӷ����ļ���ȥ
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
	//�����GET��һֱ��������headers�����tcp buffer
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numchars = get_line(client, buf, sizeof(buf));
		//һֱ������ͷ��ֱ���ҵ�Content-Length���ڵ���һ�У���ȡ���е�content_length������Ȼ��һֱ�������tcp buffer��
        while ((numchars > 0) && strcmp("\n", buf))
        {
			//����ÿһ������ͷ��ֱ�ӽ�buf[15]��Ϊ\0��ʵ������Ϊ����ȡÿ��ǰ14���ַ�������\0�����ַ����ſ���ʹ��strcasecmp���бȽϡ�
            buf[15] = '\0';
			//����POST���ģ�Content-Length����ͷ�Ǳ���ģ����Կ���ֱ��д����
			//������Content-Length:������ʱ��Content-Length:�ܹ�14���ַ���Ȼ��ǰ��������15������\0���պù����ַ���"Content-Length:"��
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
	//�ܵ���filedes[0]�����ܵ���filedes[1]���д�ܵ���pipe�����ɹ������򷵻�0��ʧ�ܷ���-1��
	//���Թܵ�����д��ʱ�����д�������С��128K���Ƿ�ԭ�ӵģ��������128K���һֱд��ܵ���ֱ��ȫ�����ݱ����꣬���û�˶��ͻ�������

    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

	//fork�����ӽ��̣����ظ�ֵ��ʾ����ʧ�ܡ�����ʧ�ܵ�ԭ��������ڴ治�㣬Ҳ�����ǽ������ﵽϵͳ�����ˡ�
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }

	//����client�����յ���Ϣ�ˡ�
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

	//�ӽ��̵�fork����ֵ��0��
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

		//#include <unistd.h>
		//int dup2(int oldfd, int newfd);
		/*dup2������newfd����ļ�������ָ��oldfd���ļ������newfdԭ����ָ����ļ�����ر�ԭ�ļ���
		��֮���Ƶ���dup(int oldfd)�����������ص�ǰ���̿��õ���С�ļ���������ָ����oldfd��ͬ���ļ���
		dup2�����ڱ�׼����������ض���dup�ɺ�dup2����ʹ�ã�����dup���±�׼����������ļ���dup2�����ض���󣬽���dup���ݵ��ļ������������ض���ĸ�ԭ��
		*/

		
        dup2(cgi_output[1], STDOUT);//��cgi_output��д�ܵ��󶨵�STDOUT������ԭ�����������׼��������ݣ����ڻ������cgi_output��д�ܵ��
        dup2(cgi_input[0], STDIN);	//��cgi_input�Ķ��ܵ��󶨵�STDIN������ԭ�������뵽��׼��������ݣ����ڻ����뵽cgi_input�Ķ��ܵ��
        close(cgi_output[0]);		//�ر��ӽ���cgi_output��д�ܵ���
        close(cgi_input[1]);		//�ر��ӽ���cgi_input�Ķ��ܵ��������������ܵ�Ҳ���ļ����븸���̹��������̹�һ�ˣ��ӽ��̹���һ�ˣ��������ӽ���֮��Ϳ���ʵ�ֵ����ͨ�š�
        
		//���ø��ֻ����������������䡿
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

		//ִ��������ָ��·�����ļ���NULL��ʾ���贫���ˡ�
        execl(path, NULL);
        exit(0);
    } else {    /* parent */
		//�ӽ��̹���һ�߹ܵ��������̹���һ�ߡ�
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
			//һ���ֽ�һ���ֽڵأ�����������ͨ�������̴����ӽ��̡�
			//�������̡�cgi_input[1]д�ܵ�����>���ӽ��̡�cgi_input[0]���ܵ�
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }

		//����Ϣ������ӽ���֮�󣬵��ӽ��̴�����������ӽ���ִ������ʱ���κ������STDOUT�Ķ���������ͨ���ض���֮��������ܵ��
		//���ӽ��̡�����������������������STDOUT���Ѿ��ض���cgi_outputд�ܵ��ˣ�����>�������̡���cgi_output���ܵ������������>��������ظ�client
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);	//���ӽ��̽���
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

	//buf�����п��࣬������������û��\n�������ֹ��\0
	//buf�����п��࣬��������������\n������¼\n�ٲ�һ����ֹ��\0
	//buf���������������ֹ��
    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
			//�����ǰ�������з��������滻Ϊ�س����š�if ((n > 0) && (c == '\n'))���ж���Ϊ�˽�\t\n�ĳ������Ҳ����һ���س�������
            if (c == '\r')
            {
				//��1���ַ���MSG_PEEK��ʾ��tcp buffer�ж�ȡ���ݵ��ǲ����tcp buffer�������´ο����ٴζ�������ݡ�
				//������������0�������֮�󻹻��tcp buffer��ɾ���Ѷ�����
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
	//�ڶ�ȡ���ݵ����һ����ֹ��������whileѭ������С��size-1������С��size����Ϊ���Ҫ��һ���س��Ŀռ䡣
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

	//һֱ������ͷ��Ϊ�����tcp buffer��headers�����д���
    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
		//���ع̶�����
        headers(client, filename);
		//���ļ����ݷ���ȥ
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
    name.sin_port = htons(*port);               //ָ������˶˿�Ϊport = 4000
    name.sin_addr.s_addr = htonl(INADDR_ANY);   //INADDR_ANY ��ϵͳ��������õ�IP��ַ���൱����ȡ��ǰ���Ե�IP��ַ��

	//setsockopt������socket���Եĺ���������޴������᷵��0�����򷵻�SOCKET_ERROR��
	/*SO_REUSEADDR��������Ѿ�����ESTABLISHED״̬�µ�socket����close��һ�㲻�������رն�����TIME_WAIT�Ĺ��̣����ȴ�4���ӡ�
	�������ڿ����ظ�����������е��ԣ���Ҫ���ø�socket��������Ҫ����SO_REUSEADDRģʽ*/
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
    {
        error_die("setsockopt failed");
    }

	//����������socket�ṹ�壨��¼�˶˿ں�IP��Ϣ�ˣ��Ͷ˿ڰ�����
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);	//�����ֽ��򣨴�ˣ�ת���Ե��ֽ���s��ʾ16�ֽ�
    }

	//���������������
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

    server_sock = startup(&port);       //�������˵�ocket���󶨶˿ڣ�����listen״̬
    printf("httpd running on port %d\n", port);

    while (1)
    {
        //while client connecting
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        accept_request(&client_sock);       //����������ȡ������GET or POST��������URL����ȫĬ���ļ����ָ�������֣���ִ���ļ���ȡ����/ִ��cgi����
											//����Ƕ�ȡ�ļ�������ֱ���ɵ�ǰ���̶�ȡ�ļ������ͻ�client��
											//�����ִ��cgi���򣺴����ӽ��̣��������ӽ��̼�Ĺܵ����ӽ��̱�׼��������ض���ִ�г��򣬸�����ͨ���ܵ����cgi����������ݲ����ͻ�client��
        //if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            //perror("pthread_create");
    }

    close(server_sock);

    return(0);
}

/*
�������� CGI ���������Ϣ������;�������������������кͱ�׼���롣
���л���������ָ CGI ����һ�黷��������ͨ�����������ɴ������ݡ��������յ���������������ݣ����� CGI �ű���CGI �ű����յ�������ת���ɻ�������������ȡ������Ҫ�����ݡ�
<form>��ǩ�� METHOD ��������������ʹ����һ�ַ������ڡ�METHOD=GET��ʱ���� CGI ���ݱ�������Ϣ����ͨ�����������еġ�
��������Ϣ�������ͨ���������� QUERY_STRING �����ݵġ�����METHOD=POST��������Ϣͨ����׼��������ȡ��
����һ�ֲ�ʹ�ñ��Ϳ����� CGI ������Ϣ�ķ������Ǿ��ǰ���Ϣֱ�Ӹ��� URL ��ַ���棬��Ϣ��URL ֮�����ʺţ�?�������зָ���
GET �����Ƕ����ݵ�һ�����󣬱����ڻ�þ�̬�ĵ���GET ����ͨ��������������Ϣ������ URL ����Ĳ������� GET ������ʹ��ʱ��CGI ���򽫻�ӻ������� QUERY_STRING��ȡ���ݡ�
Ϊ����ȷ����Ӧ�ͻ��˷���������CGI ����� QUERY_STRING �е��ַ������з�����
���û���Ҫ�ӷ�������ȡ���ݣ����������ϵ����ݲ��øı�ʱ��Ӧ���� GET ������������������е��ַ���������һ�����ȣ�ͨ���� 1024 �ֽڣ���ô��ʱ��ֻ���� POST ������
POST �������������ͨ����д�������ݴ���������ʱһ�����POST �������ڷ��͵����ݳ��� 1024 �ֽ�ʱ������� POST ������
�� POST ������ʹ��ʱ��Web ��������CGI ����ı�׼���� STDIN �������ݡ��������� CONTENT_LENGTH ����ŷ��͵����ݳ��ȡ�
CGI ��������黷������ REQUEST_METHOD ��ȷ����û�в����� POST �������������Ƿ�Ҫ��ȡ��׼����STDIN��
*/
