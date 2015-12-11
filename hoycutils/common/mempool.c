#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <common/common.h>
#include <common/bsearch.h>
#include <common/mempool.h>

struct block
{
	struct list_head free;
};

#define block_data(b) ((void *)(b))

#define block_entry(buf)  ((struct block *)(buf))

mempool_t *mempool_create(int block_size, int init_count, int limited)
{
	mempool_t *pool = (mempool_t *)malloc(sizeof(mempool_t));

	INIT_LIST_HEAD(&pool->free_list);
	pthread_mutex_init(&pool->lock, NULL);
	pool->bsize = block_size;
	pool->count = init_count;
	pool->limited = limited;

	if(init_count > 0) {
		pool->buf = calloc(init_count, block_size);
		if(!pool->buf)
			fatal("alloc memory fail.\n");

		struct block *b;
		int i;

		for(i=0 ; i<init_count ; i++) {
			b = (struct block *) (pool->buf + i * block_size);
			list_add_tail(&b->free, &pool->free_list);
		}
	}

	return pool;
}

void mempool_release(mempool_t *pool)
{
	free(pool->buf);
	free(pool);
}

void *mempool_alloc(mempool_t *pool)
{
	struct block *b;

	pthread_mutex_lock(&pool->lock);

	if (list_empty(&pool->free_list)) {
		b = NULL;
	} else {
		struct list_head *l;

		l = pool->free_list.next;
		b = list_entry(l, struct block, free);
		list_del(l);
	}

	pthread_mutex_unlock(&pool->lock);

	if( !b ) {
		if(pool->limited)
			return NULL;
		else {
			int c;
			b = (struct block *) malloc(pool->bsize);
			c = ++ pool->count;
			if( (c & ((1<<9)-1)) == 0 ) {
				logw("hitting %d blocks\n", c);
			}
		}
	}

	return block_data(b);
}

void mempool_free(mempool_t *pool, void *buf)
{
	struct block *b = block_entry(buf);

	pthread_mutex_lock(&pool->lock);

	list_add(&b->free, &pool->free_list);

	pthread_mutex_unlock(&pool->lock);
}

struct cache_sizes {
	size_t    	cs_size;
	int 		cs_count;
	mempool_t 	*cs_cachep;
};

static struct cache_sizes cachesizes[] = {
#define CACHE(x, n)  { .cs_size = (x), .cs_count = (n) },
#include "memsizes.h"
	CACHE(ULONG_MAX, 0)
#undef CACHE
};

int mem_cache_init(void)
{
	struct cache_sizes *sizes = cachesizes;

	while(sizes->cs_size != ULONG_MAX) {
		int size = sizeof(struct mem_head) + sizes->cs_size;
		sizes->cs_cachep = mempool_create(size, sizes->cs_count, IS_LIMIT);
		if(!sizes->cs_cachep) {
			goto mem_fail;
		}
		sizes++;
	}
	return 0;

mem_fail:
	do {
		sizes--;
		mempool_release(sizes->cs_cachep);
	} while(sizes != cachesizes);
	return -ENOMEM;
}

int size_cmp(const void *key, const void *elt) 
{
	const struct cache_sizes *a = key;
	const struct cache_sizes *b = elt;
	
	return a->cs_size - b->cs_size; 
}

int size_to_index(int size) {
	struct cache_sizes *node;
	struct cache_sizes key;

	key.cs_size = size;
	node = bsearch_edge(&key, cachesizes, ARRAY_SIZE(cachesizes),
		   	sizeof(struct cache_sizes), BSEARCH_MATCH_UP, size_cmp);
	if(node)
		return node->cs_size;
	return -EINVAL;
}

void *__mm_alloc(int size, int node) 
{
	void *ret;
	mempool_t *pool;
	struct mem_item *item;
	
	if(node < 0) {
		node = size_to_index(size);
		if(node < 0 || node >= ARRAY_SIZE(cachesizes))
			return NULL;
	}

	pool = cachesizes[node].cs_cachep;
	item = (struct mem_item*)mempool_alloc(pool);
	item->head.size = size;
	return item->data;
}

void __mm_free(void *ptr, int node) 
{
	int i = 0;
	int size;
	struct mem_item *item;
	mempool_t *pool;

	item = mem_entry(ptr);
	size = item->head.size;

	if (!size)
		return;

	if(node < 0) {
		node = size_to_index(size);
		if(node < 0 || node >= ARRAY_SIZE(cachesizes))
			return;
	}

found:
	pool = cachesizes[node].cs_cachep;
	mempool_free(pool, item);
}

