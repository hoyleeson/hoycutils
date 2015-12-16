#ifndef _COMMON_TIMER_H_
#define _COMMON_TIMER_H_

#include <stdint.h>
#include <sys/timerfd.h>

#include <common/types.h>
#include <common/rbtree.h>
#include <common/list.h>
#include <common/types.h>

#define MSEC_PER_SEC            (1000LL)
#define NSEC_PER_MSEC           (1000000LL)
#define NSEC_PER_SEC 			(1000000000LL)

struct timer_list {
	struct rb_node entry;
	/* list need be used when several timers have the same expires*/
	struct list_head list;
	uint64_t expires;
	struct timer_base *base;

	void (*function)(unsigned long);
	unsigned long data;

	int state;
};


extern struct timer_base _timers;

#define TIMER_INITIALIZER(_name, _function, _expires, _data) {\
	.entry = RB_NODE_INITIALIZER(_name.entry),	\
	.list = LIST_HEAD_INIT(_name.list),	\
	.expires = (_expires),				\
	.base = &_timers, 					\
	.function = (_function),			\
	.data = (_data),				\
	.state = -1,					\
}

#define DEFINE_TIMER(_name, _function, _expires, _data)\
	struct timer_list _name =\
		TIMER_INITIALIZER(_name, _function, _expires, _data)

#define time_after(a,b)		\
	(typecheck(uint64_t, a) && \
	 typecheck(uint64_t, b) && \
	 ((int64_t)(b) - (int64_t)(a) < 0))
#define time_before(a,b)	time_after(b,a)

#define time_after_eq(a,b)\
	(typecheck(uint64_t, a) && \
	 typecheck(uint64_t, b) && \
	 ((int64_t)(a) - (int64_t)(b) >= 0))

#define time_before_eq(a,b)	time_after_eq(b,a)

/*register timer notifier to tick, get beat the clock. */
int init_timers(void);

static inline void setup_timer(struct timer_list * timer,
				void (*function)(unsigned long), unsigned long data)
{
	timer->function = function;
	timer->data = data;
}

void init_timer(struct timer_list* timer);
int add_timer(struct timer_list *timer);
int del_timer(struct timer_list *timer);
int mod_timer(struct timer_list* timer, unsigned long expires);


/* current time in milliseconds */
static inline uint64_t curr_time_ms(void)
{
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC, &tm);
	return tm.tv_sec * MSEC_PER_SEC + (tm.tv_nsec / NSEC_PER_MSEC);
}



#endif

