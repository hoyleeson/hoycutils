#include <stdio.h>
#include <stdlib.h>

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
		char *buf = calloc(init_count, block_size);
		struct block *b;
		int i;

		for(i=0 ; i<init_count ; i++) {
			b = (struct block *) (buf + i * block_size);
			list_add_tail(&b->free, &pool->free_list);
		}
	}

	return pool;
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

static void *mm_alloc(int size) 
{

}

