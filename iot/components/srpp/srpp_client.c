#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "srpp_queue.h"
#include "srpp_client.h"
#include "srpp_serialize.h"


#define LOG printf
//#define LOG(...)


unsigned long os_get_timestamp();
int os_set_timeout(unsigned long expired_time);

int os_net_open(const char* host, unsigned short port);
int os_net_close();
int os_net_send(const char* buffer, int len);


//==========================================================


#define SRPP_CONNECT_TRY_INTERVAL   3000        // 5s, 连接失败，重试连接间隔
#define SRPP_CONNACK_TIMEOUT        8000        // 8s, 等待连接回复的时间间隔
#define SRPP_REQUEST_TRY_INTERVAL   3000
#define SRPP_PING_TRY_INTERVAL      2000        // 2s, PING包发送失败时，重试发送时间
#define SRPP_PING_INTERVAL          60000       // 1min, PING包正常发送间隔
#define SRPP_ALIVE_INTERVAL         120000      // 2min, 超过此间隔未收到包则认为对方断开连接


enum srpp_client_status
{
    SCS_INIT            = 0,
    SCS_RETRY_OPEN_MAIN,
    SCS_RETRY_CONNECT,
    SCS_WAIT_CONNACK,
    SCS_RETRY_OPEN_SUB,
    SCS_TRANSPORT,
    SCS_CLOSE,
};

struct srpp_client_context
{
    void (*on_message)(const char* payload, int len);
    void (*on_response)(unsigned char request_id, const char* payload, int len);
    void (*on_request)(unsigned char request_id, const char* payload, int len);
    char host[32];
    unsigned short port0;
    unsigned short port1;
    enum srpp_client_status status;
    unsigned long next_try_time;
    unsigned long next_ping_time;
    unsigned long check_alive_time;
    struct srpp_queue send_queue;
    char conn_payload[1024];
    int conn_payload_len;
    char send_buf[1288];
};

struct srpp_client_context s_client_context = {};


static int srpp_client_update_timer(struct srpp_client_context* ctx);
static int srpp_client_open_main_action(struct srpp_client_context* ctx);
static int srpp_client_connect_action(struct srpp_client_context* ctx);
static int srpp_client_connack_action(struct srpp_client_context* ctx, const char* buf, int len);
static int srpp_client_open_sub_action(struct srpp_client_context* ctx);
static int srpp_client_send_ping_action(struct srpp_client_context* ctx);
static int srpp_client_send_request_action(struct srpp_client_context* ctx, const char* payload, int len);
static int srpp_client_receive_request_action(struct srpp_client_context* ctx, const char* buf, int len);
static int srpp_client_send_response_action(struct srpp_client_context* ctx, unsigned char request_id, const char* payload, int len);
static int srpp_client_receive_response_action(struct srpp_client_context* ctx, const char* buf, int len);
static int srpp_client_send_message_action(struct srpp_client_context* ctx, const char* payload, int len);
static int srpp_client_receive_message_action(struct srpp_client_context* ctx, const char* buf, int len);
static int srpp_client_resend_request_action(struct srpp_client_context* ctx);
static int srpp_client_teardown_action(struct srpp_client_context* ctx, int errcode);
static int srpp_client_close_action(struct srpp_client_context* ctx, int errcode);
static int srpp_client_mark_alive_action(struct srpp_client_context* ctx);



//==========================================================


int srpp_client_init(srpp_on_message on_message, srpp_on_response on_response, srpp_on_request on_request)
{
    struct srpp_client_context* ctx = &s_client_context;
    LOG("TRACE srpp_client_init()\n");
    memset(ctx, 0, sizeof(*ctx));
    ctx->on_message = on_message;
    ctx->on_response = on_response;
    ctx->on_request = on_request;
    return 0;
}

int srpp_client_connect(const char* host, unsigned short port, const char* payload, int len)
{
    struct srpp_client_context* ctx = &s_client_context;
    int ret = -1;
    LOG("TRACE srpp_client_connect()\n");
    if (len > sizeof(ctx->conn_payload))
    {
        return -EINVAL;
    }

    ctx->port0 = port;
    strncpy(ctx->host, host, sizeof(ctx->host));
    memcpy(ctx->conn_payload, payload, len);
    ctx->conn_payload_len = len;

    switch (ctx->status)
    {
    case SCS_INIT:
    case SCS_CLOSE:
    case SCS_RETRY_OPEN_MAIN:
    case SCS_RETRY_OPEN_SUB:
        ret = srpp_client_open_main_action(ctx);
        break;

    case SCS_RETRY_CONNECT:
    case SCS_WAIT_CONNACK:
        ret = srpp_client_connect_action(ctx);
        break;

    case SCS_TRANSPORT:
        ret = EBUSY;
        break;
    }

    return ret;
}

int srpp_client_colse()
{
    struct srpp_client_context* ctx = &s_client_context;
    int ret = -1;
    LOG("TRACE srpp_client_colse()\n");

    switch (ctx->status)
    {
    case SCS_INIT:
    case SCS_RETRY_OPEN_MAIN:
    case SCS_RETRY_OPEN_SUB:
    case SCS_RETRY_CONNECT:
    case SCS_WAIT_CONNACK:
        ret = srpp_client_close_action(ctx, 0);
        break;

    case SCS_TRANSPORT:
        srpp_client_teardown_action(ctx, 0);
        ret = srpp_client_close_action(ctx, 0);
        break;

    case SCS_CLOSE:
        ret = 0;
        break;
    }

    return ret;
}

/// 发送请求，需要回复结果
int srpp_client_send_request(const char* payload, int len)
{
    struct srpp_client_context* ctx = &s_client_context;
    int ret = -1;
    LOG("TRACE srpp_client_send_request()\n");
    switch (ctx->status)
    {
    case SCS_TRANSPORT:
        ret = srpp_client_send_request_action(ctx, payload, len);
        break;

    case SCS_INIT:
    case SCS_RETRY_OPEN_MAIN:
    case SCS_RETRY_CONNECT:
    case SCS_WAIT_CONNACK:
    case SCS_RETRY_OPEN_SUB:
    case SCS_CLOSE:
        ret = EBUSY;
        break;
    }

    return ret;
}

/// 回复应答包
int srpp_client_send_response(unsigned char request_id, const char* payload, int len)
{
    struct srpp_client_context* ctx = &s_client_context;
    LOG("TRACE srpp_client_send_response()\n");
    int ret = -1;
    switch (ctx->status)
    {
    case SCS_TRANSPORT:
        ret = srpp_client_send_response_action(ctx, request_id, payload, len);
        break;

    case SCS_INIT:
    case SCS_RETRY_OPEN_MAIN:
    case SCS_RETRY_CONNECT:
    case SCS_WAIT_CONNACK:
    case SCS_RETRY_OPEN_SUB:
    case SCS_CLOSE:
        ret = EBUSY;
        break;
    }

    return ret;
}

/// 只上传，不回复
int srpp_client_send_message(const char* payload, int len)
{
    struct srpp_client_context* ctx = &s_client_context;
    int ret = -1;
    LOG("TRACE srpp_client_send_message()\n");
    switch (ctx->status)
    {
    case SCS_TRANSPORT:
        ret = srpp_client_send_message_action(ctx, payload, len);
        break;

    case SCS_INIT:
    case SCS_RETRY_OPEN_MAIN:
    case SCS_RETRY_CONNECT:
    case SCS_WAIT_CONNACK:
    case SCS_RETRY_OPEN_SUB:
    case SCS_CLOSE:
        ret = EBUSY;
        break;
    }

    return ret;
}

int srpp_client_on_receive(const char* buf, int len)
{
    struct srpp_client_context* ctx = &s_client_context;
    int ret = -1;
    short command = -1;
    short param_a = -1;
    long param_bc = -1;

    LOG("TRACE srpp_client_on_receive()\n");
    command = srpp_parse_command(buf, len);
    switch (ctx->status)
    {
    case SCS_INIT:
    case SCS_RETRY_OPEN_MAIN:
    case SCS_RETRY_CONNECT:
        break;

    case SCS_WAIT_CONNACK:
        switch (command)
        {
        case SRPP_CMD_CONNACK:
            ret = srpp_client_connack_action(ctx, buf, len);
            break;

        default:
            LOG("WARNING srpp_client_on_receive(): unexpect ack! command(%d)\n", command);
            break;
        }

        srpp_client_mark_alive_action(ctx);
        break;

    case SCS_TRANSPORT:
        switch (command)
        {
        case SRPP_CMD_REQUEST:
            ret = srpp_client_receive_request_action(ctx, buf, len);
            break;

        case SRPP_CMD_RESPONSE:
            ret = srpp_client_receive_response_action(ctx, buf, len);
            break;

        case SRPP_CMD_MESSAGE:
            ret = srpp_client_receive_message_action(ctx, buf, len);
            break;

        case SRPP_CMD_PONG:
            break;

        default:
            LOG("WARNING srpp_client_on_receive(): unexpect ack! command(%d)\n", command);
            break;
        }

        srpp_client_mark_alive_action(ctx);
        break;

    case SCS_RETRY_OPEN_SUB:
    case SCS_CLOSE:
        ret = EBUSY;
        break;
    }

    return ret;
}

static int srpp_client_process_try_timeout(struct srpp_client_context* ctx, unsigned long now)
{
    int ret = -1;
    // 判断超时是否有效
    if (ctx->next_try_time == 0 || now < ctx->next_try_time)
    {
        LOG("WARNING srpp_client_process_try_timeout(): no timeout, now(%lu) next_try_time(%lu)\n", now, ctx->next_try_time);
        return -1;
    }

    switch (ctx->status)
    {
    case SCS_RETRY_OPEN_MAIN:
        ret = srpp_client_open_main_action(ctx);
        break;

    case SCS_RETRY_CONNECT:
    case SCS_WAIT_CONNACK:
        ret = srpp_client_connect_action(ctx);
        break;

    case SCS_RETRY_OPEN_SUB:
        ret = srpp_client_open_sub_action(ctx);
        break;

    case SCS_TRANSPORT:
        ret = srpp_client_resend_request_action(ctx);
        break;

    case SCS_INIT:
    case SCS_CLOSE:
        ret = 0;
        break;
    }

    return ret;
}

static int srpp_client_process_ping_timeout(struct srpp_client_context* ctx, unsigned long now)
{
    int ret = -1;
    // 判断超时是否有效
    if (ctx->next_ping_time == 0 || now < ctx->next_ping_time)
    {
        //LOG("WARNING srpp_client_process_ping_timeout(): no ping timeout, now(%lu) next_ping_time(%lu)\n", now, ctx->next_ping_time);
        return 0;
    }

    if (ctx->status == SCS_TRANSPORT)
    {
        ret = srpp_client_send_ping_action(ctx);
    }

    return ret;
}

static int srpp_client_check_alive_timeout(struct srpp_client_context* ctx, unsigned long now)
{
    int ret = -1;
    // 判断超时是否有效
    if (ctx->check_alive_time == 0 || now < ctx->check_alive_time)
    {
        //LOG("WARNING srpp_client_check_alive_timeout(): check alive not timeout, now(%lu) next_ping_time(%lu)\n", now, ctx->check_alive_time);
        return 0;
    }

    if (ctx->status == SCS_TRANSPORT)
    {
        // 从头开始，重新连接
        ret = srpp_client_open_main_action(ctx);
    }

    return ret;
}

int srpp_client_on_timeout()
{
    struct srpp_client_context* ctx = &s_client_context;
    unsigned long now = os_get_timestamp();
    LOG("TRACE srpp_client_on_timeout(): now(%lu)\n", now);
    srpp_client_process_try_timeout(ctx, now);
    srpp_client_process_ping_timeout(ctx, now);
    srpp_client_check_alive_timeout(ctx, now);
    return 0;
}

/******************************************************************/

static int srpp_client_update_timer(struct srpp_client_context* ctx)
{
    unsigned long min = ctx->next_try_time;
    if (ctx->status == SCS_TRANSPORT)
    {
        if (min == 0 || (ctx->next_ping_time > 0 && ctx->next_ping_time < min))
        {
            min = ctx->next_ping_time;
        }
        if (min == 0 || (ctx->check_alive_time > 0 && ctx->check_alive_time < min))
        {
            min = ctx->check_alive_time;
        }
    }

    LOG("INFO srpp_client_update_timer(): now(%lu) min timeout(%lu)\n", os_get_timestamp(), min);
    return os_set_timeout(min);
}

/******************************************************************/

static int srpp_net_send(struct srpp_client_context* ctx, unsigned char cmd, unsigned char pa, const char* payload, int payload_len)
{
    int ret = -1;
    int len = 0;
    len = srpp_serialize(cmd, pa, payload, payload_len, ctx->send_buf, sizeof(ctx->send_buf));
    if (len < 0)
    {
        LOG("ERROR srpp_net_send(): serialize data failed! ret(%d)\n", ret);
        return len;
    }

    ret = os_net_send(ctx->send_buf, len);
    if (ret < 0)
    {
        LOG("ERROR srpp_net_send(): send data failed! ret(%d)\n", ret);
        return ret;
    }

    return 0;
}

static int srpp_send_connect(struct srpp_client_context* ctx, const char* payload, int len)
{
    //printf("srpp_send_connect(): payload(%s) len(%d)\n", payload, len);
    return srpp_net_send(ctx, SRPP_CMD_CONNECT, 0, payload, len);
}

static int srpp_send_message(struct srpp_client_context* ctx, const char* payload, int len)
{
    return srpp_net_send(ctx, SRPP_CMD_MESSAGE, 0, payload, len);
}

static int srpp_send_request(struct srpp_client_context* ctx, const char* payload, int len)
{
    return srpp_net_send(ctx, SRPP_CMD_REQUEST, 0, payload, len);
}

static int srpp_send_response(struct srpp_client_context* ctx, unsigned char request_id, const char* payload, int len)
{
    return srpp_net_send(ctx, SRPP_CMD_RESPONSE, 0, payload, len);
}

static int srpp_send_ping(struct srpp_client_context* ctx)
{
    return srpp_net_send(ctx, SRPP_CMD_PING, 0, 0, 0);
}

static int srpp_client_send_teardown(struct srpp_client_context* ctx)
{
    return srpp_net_send(ctx, SRPP_CMD_TEARDOWN, 0, 0, 0);
}

/******************************************************************/

static int srpp_client_open_main_action(struct srpp_client_context* ctx)
{
    int ret = -1;
    LOG("TRACE srpp_client_open_main_action()\n");

    ctx->next_try_time = 0;
    os_net_close();
    srpp_queue_clear(&ctx->send_queue);

    ret = os_net_open(ctx->host, ctx->port0);
    if (ret != 0)
    {
        ctx->status = SCS_RETRY_OPEN_MAIN;
        ctx->next_try_time = os_get_timestamp() + SRPP_CONNECT_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        return ret;
    }

    ret = srpp_send_connect(ctx, ctx->conn_payload, ctx->conn_payload_len);
    if (ret != 0)
    {
        ctx->status = SCS_RETRY_CONNECT;
        ctx->next_try_time = os_get_timestamp() + SRPP_CONNECT_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        return ret;
    }

    ctx->status = SCS_WAIT_CONNACK;
    ctx->next_try_time = os_get_timestamp() + SRPP_CONNACK_TIMEOUT;
    srpp_client_update_timer(ctx);

    return 0;
}

static int srpp_client_connect_action(struct srpp_client_context* ctx)
{
    int ret = -1;
    LOG("TRACE srpp_client_connect_action()\n");
    ctx->next_try_time = 0;
    ret = srpp_send_connect(ctx, ctx->conn_payload, ctx->conn_payload_len);
    if (ret != 0)
    {
        ctx->status = SCS_RETRY_CONNECT;
        ctx->next_try_time = os_get_timestamp() + SRPP_CONNECT_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        return ret;
    }

    ctx->status = SCS_WAIT_CONNACK;
    ctx->next_try_time = os_get_timestamp() + SRPP_CONNACK_TIMEOUT;
    srpp_client_update_timer(ctx);

    return 0;
}

static int srpp_client_connack_action(struct srpp_client_context* ctx, const char* buf, int len)
{
    int ret = -1;
    short param_a = -1;
    long param_bc = -1;
    unsigned long now = 0;

    LOG("TRACE srpp_client_connack_action()\n");
    ctx->next_try_time = 0;
    srpp_client_update_timer(ctx);

    param_a = srpp_parse_param_a(buf, len);
    param_bc = srpp_parse_param_bc(buf, len);
    LOG("I param a(%d) bc(%d)\n", (int)param_a, (int)param_bc);
    if (param_a != 0 || param_bc < 0)
    {
        LOG("INFO srpp_client_connack_action(): conn ack errcode(%d)\n", param_a);
        if (param_a == EACCES)
        {
            // 鉴权错误则关闭客户端
            ctx->next_try_time = 0;
            ctx->next_ping_time = 0;
            srpp_client_update_timer(ctx);
            os_net_close();
            ctx->status = SCS_CLOSE;
            return param_a;
        }

        // 下次再试
        ctx->status = SCS_RETRY_CONNECT;
        ctx->next_try_time = os_get_timestamp() + SRPP_CONNECT_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        ret = param_a == 0 ? (int)param_bc : param_a;
        return ret;
    }

    // 关闭主连接
    os_net_close();
    // 打开子连接
    ctx->port1 = (unsigned short)param_bc;
    ret = os_net_open(ctx->host, ctx->port1);
    if (ret != 0)
    {
        ctx->status = SCS_RETRY_OPEN_SUB;
        ctx->next_try_time = os_get_timestamp() + SRPP_CONNECT_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        return ret;
    }

    now = os_get_timestamp();
    ctx->status = SCS_TRANSPORT;
    ctx->next_try_time = 0;
    ctx->next_ping_time = now + SRPP_PING_INTERVAL;
    ctx->check_alive_time = now + SRPP_ALIVE_INTERVAL;
    srpp_client_update_timer(ctx);
    return 0;
}

static int srpp_client_open_sub_action(struct srpp_client_context* ctx)
{
    int ret = -1;
    unsigned long now = 0;
    ctx->next_try_time = 0;
    LOG("TRACE srpp_client_open_sub_action()\n");
    // 打开子连接
    ret = os_net_open(ctx->host, ctx->port1);
    if (ret != 0)
    {
        ctx->status = SCS_RETRY_OPEN_SUB;
        ctx->next_try_time = os_get_timestamp() + SRPP_CONNECT_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        return ret;
    }

    now = os_get_timestamp();
    ctx->status = SCS_TRANSPORT;
    ctx->next_try_time = 0;
    ctx->next_ping_time = now + SRPP_PING_INTERVAL;
    ctx->check_alive_time = now + SRPP_ALIVE_INTERVAL;
    srpp_client_update_timer(ctx);
    return 0;
}

/// 上行消息处理
static int srpp_client_send_message_action(struct srpp_client_context* ctx, const char* payload, int len)
{
    LOG("TRACE srpp_client_send_message_action()\n");
    return srpp_send_message(ctx, payload, len);
}

static int srpp_client_send_response_action(struct srpp_client_context* ctx, unsigned char request_id, const char* payload, int len)
{
    LOG("TRACE srpp_client_send_response_action()\n");
    return srpp_send_response(ctx, request_id, payload, len);
}

static int srpp_client_send_request_action(struct srpp_client_context* ctx, const char* payload, int len)
{
    unsigned long now = 0;
    unsigned long min_time = 0;
    int min_index = -1;
    int ret = -1;

    LOG("TRACE srpp_client_send_request_action()\n");
    // 发送并更新 ping timeout
    ret = srpp_send_request(ctx, payload, len);
    if (ret == 0)
    {
        now = os_get_timestamp();
        ctx->next_ping_time = now + SRPP_PING_INTERVAL;
        srpp_client_update_timer(ctx);
    }

    // 插入队列等待回复
    ret = srpp_queue_insert(&ctx->send_queue, payload, len, now + SRPP_REQUEST_TRY_INTERVAL);
    if (ret != 0)
    {
        LOG("ERROR srpp_queue_insert(): insert queue failed! ret(%d)\n", ret);
        return ret;
    }

    // 添加定时器
    min_index = srpp_queue_find_min(&ctx->send_queue);
    min_time = srpp_queue_get_try_time(&ctx->send_queue, min_index);
    if (min_time > now)
    {
        ctx->next_try_time = min_time;
        srpp_client_update_timer(ctx);
    }

    return 0;
}

static int srpp_client_resend_request_action(struct srpp_client_context* ctx)
{
    unsigned long now = 0;
    unsigned long min_time = 0;
    int min_index = -1;
    const char* payload = NULL;
    int len = 0;
    int ret = -1;

    LOG("TRACE srpp_client_resend_request_action()\n");
    now = os_get_timestamp();
    while (1)
    {
        // 从发送队列中找出需要重发的数据
        min_index = srpp_queue_find_min(&ctx->send_queue);
        if (min_index < 0)
        {
            break;
        }

        min_time = srpp_queue_get_try_time(&ctx->send_queue, min_index);
        if (min_time <= now)
        {
            ret = srpp_queue_get_payload(&ctx->send_queue, &payload, &len);
            if (ret != 0)
            {
                LOG("ERROR srpp_client_resend_action(): get payload from queue failed! ret(%d)\n", ret);
                continue;
            }

            ret = srpp_send_request(ctx, payload, len);
            if (ret == 0)
            {
                // 推迟 ping 包发送时间
                ctx->next_ping_time = now + SRPP_PING_INTERVAL;
                srpp_client_update_timer(ctx);
            }

            // 重新设定超时时间
            srpp_queue_set_try_time(&ctx->send_queue, now + SRPP_REQUEST_TRY_INTERVAL);
        }
    }

    // 重新设定超时定时器
    min_index = srpp_queue_find_min(&ctx->send_queue);
    min_time = srpp_queue_get_try_time(&ctx->send_queue, min_index);
    ctx->next_try_time = (min_time > now) ? min_time : 0;
    srpp_client_update_timer(ctx);

    return ret;
}

/// 命令应答包处理
static int srpp_client_receive_response_action(struct srpp_client_context* ctx, const char* buf, int len)
{
    short param_a = -1;
    long param_bc = -1;
    unsigned char request_id = 0;
    int data_offset = -1;
    const char* payload = NULL;
    int payload_len = 0;
    int index = -1;

    LOG("TRACE srpp_client_receive_response_action()\n");
    param_a = srpp_parse_param_a(buf, len);
    if (param_a < 0)
    {
        LOG("ERROR srpp_client_response_action(): unknown request id(%d)\n", param_a);
        return -1;
    }

    request_id = (unsigned char)param_a;
    data_offset = srpp_parse_data_offset(buf, len);
    payload = data_offset > 0 ? buf + data_offset : NULL;
    param_bc = srpp_parse_param_bc(buf, len);
    payload_len = param_bc > 0 ? (int)param_bc : 0;

    index = srpp_queue_find_by_request_id(&ctx->send_queue, request_id);
    srpp_queue_remove(&ctx->send_queue, index);

    ctx->on_response(request_id, payload, payload_len);

    return 0;
}

/// 下行消息处理
static int srpp_client_receive_message_action(struct srpp_client_context* ctx, const char* buf, int len)
{
    long param_bc = -1;
    int data_offset = -1;
    const char* payload = NULL;
    int payload_len = 0;

    LOG("TRACE srpp_client_receive_message_action()\n");
    data_offset = srpp_parse_data_offset(buf, len);
    payload = data_offset > 0 ? buf + data_offset : NULL;
    param_bc = srpp_parse_param_bc(buf, len);
    payload_len = param_bc > 0 ? (int)param_bc : 0;

    ctx->on_message(payload, payload_len);

    return 0;
}

static int srpp_client_receive_request_action(struct srpp_client_context* ctx, const char* buf, int len)
{
    short param_a = -1;
    long param_bc = -1;
    unsigned char request_id = 0;
    int data_offset = -1;
    const char* payload = NULL;
    int payload_len = 0;

    LOG("TRACE srpp_client_receive_request_action()\n");
    param_a = srpp_parse_param_a(buf, len);
    if (param_a < 0)
    {
        LOG("WARNING srpp_client_receive_request_action(): unknown request id(%d)\n", param_a);
    }

    request_id = (unsigned char)param_a;
    data_offset = srpp_parse_data_offset(buf, len);
    payload = data_offset > 0 ? buf + data_offset : NULL;
    param_bc = srpp_parse_param_bc(buf, len);
    payload_len = param_bc > 0 ? (int)param_bc : 0;

    ctx->on_request(request_id, payload, payload_len);

    return 0;
}

static int srpp_client_send_ping_action(struct srpp_client_context* ctx)
{
    int ret = -1;
    LOG("TRACE srpp_client_send_ping_action()\n");
    ret = srpp_send_ping(ctx);
    if (ret != 0)
    {
        ctx->next_ping_time = os_get_timestamp() + SRPP_PING_TRY_INTERVAL;
        srpp_client_update_timer(ctx);
        return ret;
    }

    ctx->next_ping_time = os_get_timestamp() + SRPP_PING_INTERVAL;
    srpp_client_update_timer(ctx);
    return ret;
}

static int srpp_client_mark_alive_action(struct srpp_client_context* ctx)
{
    LOG("TRACE srpp_client_mark_alive_action()\n");
    ctx->check_alive_time = os_get_timestamp() + SRPP_ALIVE_INTERVAL;
    srpp_client_update_timer(ctx);
    return 0;
}

static int srpp_client_teardown_action(struct srpp_client_context* ctx, int errcode)
{
    int ret = srpp_client_send_teardown(ctx);
    LOG("TRACE srpp_client_teardown_action()\n");
    if (ret != 0)
    {
        LOG("WARNING srpp_client_teardown_action(): send teardown failed! ret(%d)\n", ret);
    }

    return ret;
}

static int srpp_client_close_action(struct srpp_client_context* ctx, int errcode)
{
    ctx->next_try_time = 0;
    ctx->next_ping_time = 0;
    srpp_client_update_timer(ctx);
    os_net_close();
    ctx->status = SCS_CLOSE;
    return 0;
}


