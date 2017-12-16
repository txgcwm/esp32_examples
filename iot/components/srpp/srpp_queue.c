#include "srpp_queue.h"


int srpp_queue_insert(struct srpp_queue* queue, const char* payload, int len, unsigned long next_try_time)
{
    int ret = -1;
    return ret;
}

int srpp_queue_remove(struct srpp_queue* queue, int index)
{
    return -1;
}

int srpp_queue_clear(struct srpp_queue* queue)
{
    return -1;
}

int srpp_queue_find_min(struct srpp_queue* queue)
{
    return -1;
}

int srpp_queue_find_by_request_id(struct srpp_queue* queue, unsigned char request_id)
{
    return -1;
}

int srpp_queue_get_payload(struct srpp_queue* queue, const char** payload_ptr, int* len_ptr)
{
    return -1;
}

unsigned long srpp_queue_get_try_time(struct srpp_queue* queue, int index)
{
    return 0;
}

int srpp_queue_set_try_time(struct srpp_queue* queue, unsigned long newtime)
{
    return -1;
}


