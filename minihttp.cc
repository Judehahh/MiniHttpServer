#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_PORT 8088

static int debug = 1;

int get_line(int sock, char *buf, int size);
void do_http_request(int client_sock);
void do_http_response(int client_sock, const char* path);
void bad_request(int client_sock);       //400
void not_found(int client_sock);        //404
void inner_error(int client_sock);      //500
void unimplemented(int client_sock);    //501
int headers(int client_sock, FILE* resource, const char* statu);
void cat(int client_sock, FILE* resource);

int main() {
    
    //1. 创建信箱
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    //2. 清空标签，写上地址和端口号
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;   //协议族Ipv4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);    //监听本地所有IP地址
    server_addr.sin_port = htons(SERVER_PORT);  //绑定端口号
    
    //socket命名
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    //监听socket
    listen(sock, 128);

    //等待来信
    printf("等待客户端的连接\n");


    int done = 1;

    while(done) {
        struct sockaddr_in client;  //socket地址结构体
        int client_sock, len, i;
        char client_ip[64];
        char buf[256];

        socklen_t client_addr_len = sizeof(client);     //client地址的长度
        client_sock = accept(sock, (struct sockaddr *)&client, &client_addr_len);

        //打印客户端IP地址和端口号
        printf("client ip: %s\t port : %d\n",
                inet_ntop(AF_INET, &client.sin_addr.s_addr, client_ip, sizeof(client_ip)),
                ntohs(client.sin_port));

        //处理http请求
        do_http_request(client_sock);
        
        //执行http响应
        //do_http_response(client_sock);

        close(client_sock);
    }

    close(sock);
    return 0;

}


//读取客户端发送的http请求
void do_http_request(int client_sock) {
    
    int len = 0;
    char buf[256];
    char path[256];

    struct stat st;

    //1. 读取请求行
    len = get_line(client_sock, buf, sizeof(buf));
    
    //读到了请求行
    if(len > 0) {
        //获取url
        char* url = strpbrk(buf, " \t");
        *url++ = '\0';
        url += strspn(url, " \t");

        //获取请求方法
        char* method = buf;
        if(debug) printf("request method: %s\n", method);
        //只处理get请求
        if(strcasecmp(method, "GET") == 0) {
            if(debug) printf("The request method is GET\n");
        }
        else {
            fprintf(stderr, "WARNING! Other Method!");
            do {
                len = get_line(client_sock, buf, sizeof(buf));
                if(debug) printf("read: %s\n", buf);
            }while(len > 0);
            unimplemented(client_sock); //请求未实现
            
            return ;
        }

        //获取协议版本
        char* version = strpbrk(url, " \t");
        *version++ = '\0';
        version += strspn(version, " \t");
        //只支持HTTP/1.1
        if(strcasecmp(version, "HTTP/1.1") == 0) {
            if(debug) printf((char*)"The http version is: %s\n", version);
        }

        if(debug) printf("url is: %s\n", url);
        

        //定位服务器本地的html文件
        
        //处理url中的问号
        char* pos = strchr(url, '?');
        if(pos) {
            *pos = '\0';
            printf("real url: %s\n", url);    
        }

        sprintf(path, "./html_docs%s", url);
        if(debug) printf("path: %s\n", path);

        //执行http响应
        //判断文件是否存在，如果存在就响应200 OK，同时发送相应的html文件，如果不存在，就响应404 NOT FOUND
        if(stat(path, &st) == -1) {       //文件不存在或出错
            fprintf(stderr, "stat %s failed. reason: %s\n", path, strerror(errno));
            not_found(client_sock);
            return;
        }
        else {     //文件存在

            if(S_ISDIR(st.st_mode)) {   //为一个目录
                strcat(path, "/index.html");
            }

            do_http_response(client_sock, path);
        }

        
    } else {
        //400 Bad Request
        bad_request(client_sock);
        return;
    }

    //2. 读取头部字段，直到遇到空行
    do{
        len = get_line(client_sock, buf, sizeof(buf));
        if(debug) printf("read: %s\n", buf);
    } while (len > 0);

}

//响应http请求
void do_http_response(int client_sock, const char* path) {

    FILE* resource = fopen(path, "r");

    if(resource == NULL) {
        not_found(client_sock);
        return ;
    }

    //1. 发送http头部
    int ret = headers(client_sock, resource, "200 OK");

    //2. 发送http body
    if(!ret) cat(client_sock, resource);

    fclose(resource);
}

/****************************
 *返回关于响应文件信息的http 头部
 *输入： 
 *     client_sock - 客服端socket 句柄
 *     resource    - 文件的句柄 
 *返回值： 成功返回0 ，失败返回-1
******************************/
int headers(int client_sock, FILE* resource, const char* statu) {
    struct stat st;
    char tmp[64];
    char buf[1024] = {0};
    char status[64];
    sprintf(status, "HTTP/1.1 %s\r\n", statu);
    strcpy(buf, status);
    strcat(buf, "Server: Jude Server\r\n");
    strcat(buf, "Content-Type: text/html\r\n");
    strcat(buf, "Connection: Close\r\n");

    if(fstat(fileno(resource), &st) == -1) {
        //内部出错
        inner_error(client_sock);
        return -1;
    }

    snprintf(tmp, 64, "Content_Length: %ld\r\n\r\n", st.st_size);
    strcat(buf, tmp);

    if(debug) fprintf(stdout, "header: %s\n", buf);

    if(send(client_sock, buf, strlen(buf), 0) < 0) {
        fprintf(stderr, "send failed. data: %s, reason: %s\n", buf, strerror(errno));
        return -1;
    }
    
    return 0;
}

/****************************
 *说明：实现将html文件的内容按行
        读取并送给客户端
 ****************************/
void cat(int client_sock, FILE* resource) {
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    
    if(debug) fprintf(stdout, "*****sending html*****\n");
    while(!feof(resource)) {
        int len = write(client_sock, buf, strlen(buf));
        
        if(len < 0) {   //发送body的过程中出现问题
            fprintf(stderr, "send body error. reason: %s\n", strerror(errno));
            break;
        }

        if(debug) fprintf(stdout, "%s", buf);
        fgets(buf, sizeof(buf), resource);
    }
    if(debug) fprintf(stdout, "*****sending html*****\n");
}


void do_http_response(int client_sock) {

    const char* main_header = "HTTP/1.1 200 OK\r\n\
                               Server: Jude Server\r\n\
                               Content-Type: text/html\r\n\
                               Connection: Close\r\n";    

    const char* welcome_content = "<!DOCTYPE html>\n\
                                   <html>\n\
                                   <head>\n\
                                   <meta charset=\"UTF-8\">\n\
                                   <title>Hello World</title>\n\
                                   </head>\n\
                                   <body>\n\
                                   <p>Hello World!!!</p>\n\
                                   </body>\n\
                                   </html>";

    //1. 送main_header
    int len = write(client_sock, main_header, strlen(main_header));

    if(debug) fprintf(stdout, "---do_http_response...");
    if(debug) fprintf(stdout, "write[%d]: %s\n", len, main_header);

    //2. 生成Content_Length行并发送
    char send_buf[64];
    int wc_len = strlen(welcome_content);
    len = snprintf(send_buf, 64, "Content_Length: %d\r\n\r\n", wc_len);
    len = write(client_sock, send_buf, len);

    if(debug) fprintf(stdout, "write[%d]: %s", len, send_buf);

    //3. 发送html内容
    len = write(client_sock, welcome_content, wc_len);
    
    if(debug) fprintf(stdout, "write[%d]: %s\n", len, welcome_content);

}


//返回值： -1表示读取出错，0表示读取到空行，大于0表示成功读取一行
int get_line(int sock, char *buf, int size) {
    int count = 0;  //已经读取字符个数
    char ch = '\0';
    int len = 0;

    while((count < size - 1) && ch != '\n') {
        len = read(sock, &ch, 1);

        //读取成功
        if(len == 1) {
            //读取到回车，继续往下读
            if(ch == '\r') {
                continue;
            }
            //读取到换行，跳出循环
            else if(ch == '\n') {
                //buf[count] = '\0';
                break;
            }

            //处理一般的字符，存入buf中
            buf[count++] = ch;
        }
        //读取出错
        else if(len == -1) {
            perror("read failed");
            count = -1;
            break;
        }
        //客户端关闭socket连接，返回0
        else {
            fprintf(stderr, "client close\n");
            count = -1;
            break;
        }
    }

    if(count >= 0)
        buf[count] = '\0';

    return count;
}

void bad_request(int client_sock) {
    const char* reply = "HTTP/1.0 400 BAD REQUEST\r\n\
                        Content-Type: text/html\r\n\
                        \r\n\
                        <HTML>\
                        <HEAD>\
                        <TITLE>BAD REQUEST</TITLE>\
                        </HEAD>\
                        <BODY>\
                            <P>Your browser sent a bad request! \
                        </BODY>\
                        </HTML>";

    int len = write(client_sock, reply, strlen(reply));

    if(len <= 0) {
        fprintf(stderr, "send reply failed. reason: %s\n", strerror(errno));
    }
}

void not_found(int client_sock) {

    FILE* resource = fopen("./html_docs/404.html", "r");

    if(resource == NULL) {
        const char* reply = "HTTP/1.1 404 NOT FOUND\r\n\
                        Content-Type: text/html\r\n\
                        \r\n\
                        <HTML>\
                        <HEAD>\
                        <TITLE>NOT FOUND</TITLE>\
                        </HEAD>\
                        <BODY>\
                            <P>The server could not fulfill your request because the resource specified is unavailable or nonexistent.\
                        </BODY>\
                        </HTML>";

        int len = write(client_sock, reply, strlen(reply));

        if(len <= 0) {
            fprintf(stderr, "send reply failed. reason: %s\n", strerror(errno));
        }
        return ;
    }

    //1. 发送http头部
    int ret = headers(client_sock, resource, "404 NOT FOUND");

    //2. 发送http body
    if(!ret) cat(client_sock, resource);

    fclose(resource);

}

void inner_error(int client_sock) {
    const char* reply = "HTTP/1.1 500 Internal Sever Error\r\n\
                        Content-Type: text/html\r\n\
                        \r\n\
                        <HTML>\
                        <HEAD>\
                        <TITLE>Inner Error</TITLE>\
                        </HEAD>\
                        <BODY>\
                            <P>服务器内部出错\
                        </BODY>\
                        </HTML>";

    int len = write(client_sock, reply, strlen(reply));

    if(len <= 0) {
        fprintf(stderr, "send reply failed. reason: %s\n", strerror(errno));
    }
}

void unimplemented(int client_sock) {
    const char* reply = "HTTP/1.1 501 Method Not Implemented\r\n\
                        Content-Type: text/html\r\n\
                        \r\n\
                        <HTML>\
                        <HEAD>\
                        <TITLE>Method Not Implemented</TITLE>\
                        </HEAD>\
                        <BODY>\
                            <P>HTTP request method not supported.\
                        </BODY>\
                        </HTML>";

    int len = write(client_sock, reply, strlen(reply));

    if(len <= 0) {
        fprintf(stderr, "send reply failed. reason: %s\n", strerror(errno));
    }
}