#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "srpp_serialize.h"



short srpp_parse_command(const char* buf, int len)
{
    return (len >= 1) ? ((buf[0] >> 5) & 0x7) : -1;
}

short srpp_parse_param_a(const char* buf, int len)
{
    return (len >= 2) ? buf[1] : -1;
}

long srpp_parse_param_bc(const char* buf, int len)
{
    if (len < 4)
    {
        return -1;
    }

    long hi = (unsigned char)buf[2];
    long lo = (unsigned char)buf[3];
    return (hi << 8) + lo;
}

int srpp_parse_data_offset(const char* buf, int len)
{
    if (len < 1)
    {
        return -1;
    }

    int param_count = (buf[0] & 1) ? 3 : 1;
    return 1 + param_count;
}

int srpp_serialize(unsigned char cmd, unsigned char pa, const char* payload, int payload_len, char* buf, int buf_len)
{
    int all_bytes = 0;
    int param_count = 3;

    if (cmd == SRPP_CMD_PING || cmd == SRPP_CMD_TEARDOWN)
    {
        param_count = 0;
    }

    all_bytes = 1 + param_count + payload_len;
    if (all_bytes > buf_len)
    {
        return -EINVAL;
    }

    buf[0] = cmd << 5;
    if (param_count == 3)
    {
        buf[0] |= 1;
        buf[1] = pa;
        buf[2] = (char)((payload_len >> 8) & 0xFF);
        buf[3] = (char)(payload_len & 0xFF);
        memcpy(&buf[4], payload, payload_len);
        //printf("buf[4]: %s\n", &buf[4]);
    }

    return all_bytes;
}

int srpp_serialize_connack(unsigned char errcode, unsigned short port, char* buf, int buf_len)
{
    unsigned char cmd = SRPP_CMD_CONNACK;
    if (buf_len < 4)
    {
        return -EINVAL;
    }

    buf[0] = cmd << 5;
    buf[1] = errcode;
    buf[2] = (char)((port >> 8) & 0xFF);
    buf[3] = (char)(port & 0xFF);

    return 4;
}

