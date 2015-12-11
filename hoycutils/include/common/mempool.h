#ifndef _COMMON_MEMPOOL_H_
#define _COMMON_MEMPOOL_H_

#include <stdint.h>
#include <pthread.h>
#include <common/list.h>

typedef struct mempool_s {
	struct list_head free_list;
	struct list_head dynamic_free_list;
	pthread_mutex_t lock;
	uint8_t *buf;

	int bsize; /* block size */
	int init_count; /* initiailized count for blocks ever alloced */
	int count; /* total count for blocks ever alloced */
	int used; 	/* used block count */
	int dynamic_used; 	/* used block count */
	int limited; /* is resource limited to initial count? */
} mempool_t;

mempool_t *mempool_create(int block_size, int init_count, int limited);
void *mempool_alloc(mempool_t *pool);
void mempool_free(mempool_t *pool, void *buf);


struct mem_head {
	int access;
	int size;
};

struct mem_item {
	struct mem_head head;
	uint8_t data[0];
};

#define IS_LIMIT 	(0)

#define mem_entry(b) container_of(b, struct mem_item, data)

void *__mm_alloc(int size, int node);
void __mm_free(void *ptr, int node);

static __always_inline void *mm_alloc(int size) 
{
	int i = 0;
	if (__builtin_constant_p(size)) {

		if (!size)
			return NULL;

#define CACHE(x, n) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include "memsizes.h"
#undef CACHE

		return malloc(size);
	} else {
		i = -1;
	}

found:
	return __mm_alloc(size, i);
}

static inline void mm_free(void *ptr) 
{
	return __mm_free(ptr, -1);
}


#endif

