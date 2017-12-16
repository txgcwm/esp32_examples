#ifndef __SRPP_SERIALIZE_H__
#define __SRPP_SERIALIZE_H__

#ifdef __cplusplus
extern "C" {
#endif

enum SrppCommand {
    SRPP_CMD_CONNECT    = 1,
    SRPP_CMD_CONNACK    = 1,
    SRPP_CMD_REQUEST    = 2,
    SRPP_CMD_RESPONSE   = 3,
    SRPP_CMD_MESSAGE    = 4,
    SRPP_CMD_PING       = 5,
    SRPP_CMD_PONG       = 5,
    SRPP_CMD_TEARDOWN   = 6,
};

int srpp_serialize(unsigned char cmd, unsigned char pa, const char* payload, int payload_len, char* buf, int buf_len);
int srpp_serialize_connack(unsigned char errcode, unsigned short port, char* buf, int buf_len);

short srpp_parse_command(const char* buf, int len);
short srpp_parse_param_a(const char* buf, int len);
long srpp_parse_param_bc(const char* buf, int len);
int srpp_parse_data_offset(const char* buf, int len);


#ifdef __cplusplus
};
#endif

#endif // __SRPP_SERIALIZE_H__
