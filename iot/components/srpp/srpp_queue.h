#ifndef __SRPP_QUEUE_H__
#define __SRPP_QUEUE_H__


struct srpp_queue
{
};


int srpp_queue_insert(struct srpp_queue* queue, const char* payload, int len, unsigned long next_try_time);
int srpp_queue_remove(struct srpp_queue* queue, int index);
int srpp_queue_clear(struct srpp_queue* queue);
int srpp_queue_find_min(struct srpp_queue* queue);
int srpp_queue_find_by_request_id(struct srpp_queue* queue, unsigned char request_id);
int srpp_queue_get_payload(struct srpp_queue* queue, const char** payload_ptr, int* len_ptr);
unsigned long srpp_queue_get_try_time(struct srpp_queue* queue, int index);
int srpp_queue_set_try_time(struct srpp_queue* queue, unsigned long newtime);


#endif // __SRPP_QUEUE_H__

