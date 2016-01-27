#ifndef _COMMON_QUEUE_H_
#define _COMMON_QUEUE_H_

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#include <common/list.h>
#include <common/packet.h>

#define QUEUE_F_BLOCK 		(1 << 0)

#define QUEUE_NONBLOCK 		(0)
#define QUEUE_BLOCK 		(1)

struct queue {
	struct list_head 	list;
	pthread_mutex_t 	lock;
	pthread_cond_t 		cond;
	size_t 	count;
	int 	flags;
};

struct queue *queue_init();
void queue_release(struct queue *q);

void queue_in(struct queue *q, struct packet *p);
struct packet *queue_out(struct queue *q);
struct packet *queue_peek(struct queue *q);

size_t queue_count(struct queue *q);
void queue_clear(struct queue *q, void(*reclaim)(struct packet *p));

#endif
