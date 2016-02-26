#ifndef _COMMON_IOWAIT_H_
#define _COMMON_IOWAIT_H_

#include <string.h>
#include <pthread.h>

#include <common/list.h>
#include <common/completion.h>

#define MAX_RESPONSE_CAPACITY   (256)
#define WAIT_RES_DEAD_LINE      (5 * 1000)


typedef struct iowait_watcher {
	/* in param */
    int type;
    int seq;
    int count;
    void *res;

    struct completion done;
    struct hlist_node hentry;
} iowait_watcher_t;


typedef struct _iowait {
    struct hlist_head *slots;
    pthread_mutex_t lock;
} iowait_t;


#define __IOWAIT_WATCHER_INITIALIZER(name, _type, _seq, _res, _count) { 	\
	.type = _type, 		\
	.seq = _seq, 		\
	.res = _res, 		\
	.count = _count, 	\
	.done = COMPLETION_INITIALIZER((name).done) 	\
}

#define DECLARE_IOWAIT_WATCHER(name, _type, _seq, _res, _count) 	\
	iowait_watcher_t name = __IOWAIT_WATCHER_INITIALIZER(name)


int iowait_init(iowait_t *wait);
void iowait_watcher_init(iowait_watcher_t *watcher, 
		int type, int seq, void *result, int count);
int iowait_register_watcher(iowait_t *wait, iowait_watcher_t *watcher);

int wait_for_response_data(iowait_t *wait, iowait_watcher_t *watcher, int *res);
int wait_for_response(iowait_t *wait, iowait_watcher_t *watcher);

int post_response_data(iowait_t *wait, int type, int seq, void *result, int count);
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

