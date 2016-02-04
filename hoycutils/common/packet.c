#include <stdlib.h>
#include <pthread.h>

#include <common/packet.h>
#include <protos.h>
#include <common/log.h>


pack_buf_pool_t *create_pack_buf_pool(int esize, int ecount)
{
    pack_buf_pool_t *pool;

    pool = malloc(sizeof(*pool));

    /*Create unlimited memory pools. */
    pool->pool = mempool_create(esize + sizeof(pack_buf_t), ecount, 0);

    return pool;
}

void free_pack_buf_pool(pack_buf_pool_t *pool)
{
    mempool_release(pool->pool);
    free(pool);
}


pack_buf_t *alloc_pack_buf(pack_buf_pool_t *pool)
{
    pack_buf_t *buf;
    buf = mempool_alloc(pool->pool);

    buf->owner = pool;
    fake_atomic_init(&buf->refcount, 1); 

    return buf;
}

pack_buf_t *pack_buf_get(pack_buf_t *buf)
{
    fake_atomic_inc(&buf->refcount); 
}

void free_pack_buf(pack_buf_t *buf)
{
    if(fake_atomic_dec_and_test(&buf->refcount)) {
        pack_buf_pool_t *pool = buf->owner;
        mempool_free(pool->pool, buf); 
    }
}



