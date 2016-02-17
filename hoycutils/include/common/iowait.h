#ifndef _COMMON_IOWAIT_H_
#define _COMMON_IOWAIT_H_

#include <string.h>
#include <pthread.h>

#include <common/list.h>
#include <common/completion.h>

#define MAX_RESPONSE_CAPACITY   (256)
#define WAIT_RES_DEAD_LINE      (5 * 1000)

struct res_info {
    int type;
    int seq;
    void *res;
    int count;
    struct hlist_node hentry;
    struct completion done;
};

typedef struct _iowait {
    struct hlist_head *slots;
    pthread_mutex_t lock;
} iowait_t;

int iowait_init(iowait_t *wait);
int wait_for_response_data(iowait_t *wait, int type, int seq, void *result, int *count);
int post_response_data(iowait_t *wait, int type, int seq, void *result, int count);

int wait_for_response(iowait_t *wait, int type, int seq, void *result);
int post_response(iowait_t *wait, int type, int seq, void *result,
        void (*fn)(void *dst, void *src));


static inline void default_assign(void * dst, void *src, int size)
{
    memcpy(dst, src, size);
}

#define response_post(_wait, _type, _seq, _resp) ({  \
        int ret = 0; 		\
        do  { 				\
            typeof(*(_resp)) dst;  \
            ret = post_response_data(_wait, _type, _seq, _resp, sizeof(dst));    \
        } while(0); 		\
        ret; 				\
})

#define WAIT_TIMEOUT 	ETIMEDOUT

#endif

