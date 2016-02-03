#ifndef _COMMON_PACKET_H_
#define _COMMON_PACKET_H_

#include <stdint.h>
#include <common/list.h>
#include <common/fake_atomic.h>
#include <common/mempool.h>

#define PACKET_MAX_PAYLOAD      (2000)

typedef struct _pack_buf pack_buf_t;

struct packet {
	struct list_head node;
    pack_buf_t *buf;
};


typedef struct _pack_buf_pool {
    mempool_t *pool;
} pack_buf_pool_t;

struct _pack_buf {
    pack_buf_pool_t *owner;
    fake_atomic_t refcount;

    int len;
    uint8_t data[0];
};


pack_buf_pool_t *create_pack_buf_pool(int esize, int ecount);
void free_pack_buf_pool(pack_buf_pool_t *pool);

pack_buf_t *alloc_pack_buf(pack_buf_pool_t *pool);
pack_buf_t *pack_buf_get(pack_buf_t *buf);
void free_pack_buf(pack_buf_t *buf);


#endif

