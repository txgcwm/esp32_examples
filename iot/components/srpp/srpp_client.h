#ifndef __SRPP_CLIENT_H__
#define __SRPP_CLIENT_H__


typedef void (*srpp_on_message)(const char* payload, int len);
typedef void (*srpp_on_response)(unsigned char request_id, const char* payload, int len);
typedef void (*srpp_on_request)(unsigned char request_id, const char* payload, int len);


int srpp_client_init(srpp_on_message, srpp_on_response, srpp_on_request);
int srpp_client_connect(const char* host, unsigned short port, const char* payload, int len);
int srpp_client_colse();
int srpp_client_send_message(const char* payload, int len);
int srpp_client_send_request(const char* payload, int len);
int srpp_client_send_response(unsigned char request_id, const char* payload, int len);
int srpp_client_on_receive(const char* buf, int len);
int srpp_client_on_timeout();



#endif // __SRPP_CLIENT_H__

