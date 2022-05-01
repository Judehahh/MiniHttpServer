#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#define SERVER_PORT 8088

static int debug = 1;

int get_line(int sock, char *buf, int size);
void do_http_request(int client_sock);

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
            //501 Method Not Implement
            fprintf(stderr, "WARNING! Other Method!");
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
    }
    else {
        //400 Bad Request
        //bad_request(cline_sock);
    }

    //2. 读取头部字段，直到遇到空行
    do{
        len = get_line(client_sock, buf, sizeof(buf));
        if(debug) printf("read: %s\n", buf);
    } while (len > 0);

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
