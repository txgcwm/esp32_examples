#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "srpp_client.h"


/*********************************************************/

// 定时器模拟函数:

unsigned long g_timeout = 0;

unsigned long os_get_timestamp()
{
    // struct timespec ts;
    // clock_gettime(CLOCK_MONOTONIC, &ts);
    // return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

    return 0;
}

int os_set_timeout(unsigned long expired_time)
{
    g_timeout = expired_time;
    return 0;
}


/*********************************************************/


void on_timeout()
{
    srpp_client_on_timeout();
}

void on_net_receive(const char* buf, int len)
{
    srpp_client_on_receive(buf, len);
}

void on_srpp_message(const char* payload, int len)
{
    printf("example on message, payload(%s) len(%d)\n", payload, len);
}

void on_srpp_request(unsigned char request_id, const char* payload, int len)
{
    printf("example on request, request_id(%d) payload(%s) len(%d)\n", request_id, payload, len);
}

void on_srpp_response(unsigned char request_id, const char* payload, int len)
{
    printf("example on response, request_id(%d) payload(%s) len(%d)\n", request_id, payload, len);
}

/*********************************************************/


int os_net_init(void (*on_receive)(const char* buf, int len));
void example_task();

int srpp_main(char* server, int port)
{
    int res = -1;
    int loop_times = 10000;
    const char* conn_info = "hello\nworld\n";

    printf("hello\n");

    os_net_init(on_net_receive);

    srpp_client_init(on_srpp_message, on_srpp_response, on_srpp_request);
    srpp_client_connect(server, port, conn_info, strlen(conn_info));

    printf("enter loop ...\n");
    while (loop_times-- > 0)
    {
        // 模拟定时器处理
        if (g_timeout > 0 && os_get_timestamp() > g_timeout)
        {
            g_timeout = 0;
            on_timeout();
        }

        example_task();
        usleep(1000);
    }

    printf("exit loop\n");
    srpp_client_colse();

    return 0;
}

void example_task()
{
    char buf[] = "123456";
    static int last_tickcount = 0;
    unsigned long now = os_get_timestamp();
    if (now > last_tickcount + 300)
    {
        printf("test send\n");
        //srpp_client_request(buf, sizeof(buf));
        last_tickcount = now;
    }
}

