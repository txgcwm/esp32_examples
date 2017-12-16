#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>



// 需要实现的接口
unsigned long os_get_timestamp();
int os_net_open(const char* host, unsigned short port);
int os_net_close();
int os_net_send(const char* buffer, int len);


//==========================================================


void (*s_on_receive)(const char* buf, int len);
int s_sockfd = -1;
struct sockaddr_in s_addr;
int s_exit_flag = 0;
pthread_t s_thread;


static void* os_net_thread(void*);
static void hexdump(const char* title, const char* buf, int len);


int os_net_init(void (*on_receive)(const char* buf, int len))
{
    printf("os_net_init()\n");
    s_on_receive = on_receive;
    return 0;
}

int os_net_open(const char* host, unsigned short port)
{
    printf("os_net_open() host(%s) port(%d)\n", host, (int)port);
    s_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    printf("socket fd(%d)\n", s_sockfd);

    /* 填写sockaddr_in*/
    bzero(&s_addr, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);
    s_addr.sin_addr.s_addr = inet_addr(host);

    s_exit_flag = 0;
    pthread_create(&s_thread, NULL, os_net_thread, 0);

    return 0;
}

int os_net_close()
{
    printf("os_net_close()\n");
    s_exit_flag = 1;
    //shutdown(s_sockfd, SHUT_RDWR);
    close(s_sockfd);
    pthread_join(s_thread, NULL);
    //close(s_sockfd);
    return 0;
}

int os_net_send(const char* buffer, int len)
{
    int payload_offset = (buffer[0] & 0xF) ? 4 : 1;
    printf("os_net_send() len(%d) payload(%s)\n", len, &buffer[payload_offset]);
    //for (int i = 0; i < len; ++i) printf("buffer[%d]: %02x '%c'\n", i, buffer[i], buffer[i]);
    return (int)sendto(s_sockfd, buffer, len, 0, (struct sockaddr *)&s_addr, sizeof(s_addr));
}

static int os_net_recv(char* buffer, int len)
{
    printf("os_net_recv()\n");
    /* 接收server端返回的字符串*/
    return (int)recv(s_sockfd, buffer, len, 0);
}

static void* os_net_thread(void* v)
{
    printf("os_net_thread() begin\n");
    int ret = -1;
    int buflen = 2048;
    char* buf = (char*)malloc(buflen);
    while (!s_exit_flag)
    {
        ret = os_net_recv(buf, buflen);
        printf("net recv ret(%d) buf(%s), buflen(%d)\n", ret, buf, buflen);
        hexdump("net received", buf, ret);
        if (ret < 0)
        {
            usleep(200000);
        }

        s_on_receive(buf, ret);
    }

    free(buf);

    printf("os_net_thread() end\n");
    return NULL;
}

static void hexdump(const char* title, const char* buf, int len)
{
    printf("%s: ", title);
    for (int i = 0; i < len; ++i)
    {
        printf("%02x ", (unsigned char)buf[i]);
    }
    printf("\n");
}

