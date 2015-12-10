#ifndef _COMMON_MEMPOOL_H_
#define _COMMON_MEMPOOL_H_

#include <common/list.h>
#include <pthread.h>

typedef struct mempool_s {
	struct list_head free_list;
	pthread_mutex_t lock;
	int bsize; /* block size */
	int count; /* total count for blocks ever alloced */
	int limited; /* is resource limited to initial count? */
} mempool_t;

mempool_t *mempool_create(int block_size, int init_count, int limited);
void *mempool_alloc(mempool_t *pool);
void mempool_free(mempool_t *pool, void *buf);

#endif

