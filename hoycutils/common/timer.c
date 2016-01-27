#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include <common/timer.h>
#include <common/ioasync.h>
#include <common/log.h>


struct timer_base {
	int clockid;
	unsigned long event;
	struct rb_root timer_tree;

	pthread_mutex_t lock;
	uint64_t next_expires;
};

struct timer_base _timers;


static void timer_set_interval(struct timer_list *timer, uint64_t expires)
{
	struct timer_base *base = timer->base;
	struct itimerspec itval;
	int i_sec = expires / MSEC_PER_SEC;
	int i_msec = expires % MSEC_PER_SEC;

	itval.it_interval.tv_sec = 0;
	itval.it_interval.tv_nsec = 0;

	itval.it_value.tv_sec = i_sec;
	itval.it_value.tv_nsec = i_msec * NSEC_PER_MSEC;

	logi("timer set interval:sec:%d, msec:%d", i_sec, i_msec);
	if (timerfd_settime(base->clockid, 0, &itval, NULL) == -1)
		loge("timer_set_interval: timerfd_settime failed, %d.%d\n", i_sec, i_msec);
}

static void timer_set_expires(struct timer_list *timer, uint64_t expires) {
	uint64_t now = curr_time_ms();
	struct timer_base *base = timer->base;

	base->next_expires = expires;
	timer_set_interval(timer, expires - now);
}

static void update_timer_recent_expires(struct timer_base *base) 
{
	uint64_t now = curr_time_ms();
	struct timer_list *recent;
	struct rb_root *root = &base->timer_tree;

	recent = rb_entry(root->rb_node, struct timer_list, entry);

	if(time_after(base->next_expires, recent->expires) ||
			time_before_eq(now, base->next_expires)) {
		timer_set_expires(recent, recent->expires);
	}
}

static void timer_insert_tree(struct timer_list *timer) 
{
	struct timer_list *t;
	struct timer_base* base = timer->base;
	struct rb_node ** p = &base->timer_tree.rb_node;
	struct rb_node * parent = NULL;

	while (*p) {
		parent = *p;
		t = rb_entry(parent, struct timer_list, entry);

		if (timer->expires < t->expires)
			p = &(*p)->rb_left;
		else if (timer->expires > t->expires)
			p = &(*p)->rb_right;
		else {
			list_add_tail(&timer->list, &t->list);
			return;
		}
	}

	INIT_LIST_HEAD(&timer->list);

	/* Add new node and rebalance tree. */
	rb_link_node(&timer->entry, parent, p);
	rb_insert_color(&timer->entry, &base->timer_tree);
}

static void timer_erase_tree(struct timer_list *timer)
{
	if(!RB_EMPTY_NODE(&timer->entry)) {
		struct timer_base* base = timer->base;

		rb_erase_init(&timer->entry, &base->timer_tree);
		if(!list_empty(&timer->list)) {
			struct timer_list *entry = NULL;
			entry = list_entry(timer->list.next, struct timer_list, list);
			timer_insert_tree(entry);
		}
	}

	list_del_init(&timer->list);
}


/**
 * timer_pending - is a timer pending?
 * @timer: the timer in question
 *
 * timer_pending will tell whether a given timer is currently pending,
 * or not. Callers must ensure serialization wrt. other operations done
 * to this timer, eg. interrupt contexts, or other CPUs on SMP.
 *
 * return value: 1 if the timer is pending, 0 if not.
 */
static inline int timer_pending(const struct timer_list * timer)
{
	return RB_EMPTY_NODE(&timer->entry) || !list_empty(&timer->list);
}

/*detach timer from timer list.*/
static inline void detach_timer(struct timer_list *timer)
{
	timer_erase_tree(timer);
}

/*really add timer to timer list.*/
static int internal_add_timer(struct timer_list* timer)
{
	struct timer_base* base = timer->base;
	int64_t expires = timer->expires;
	uint64_t now = curr_time_ms();

	if(time_before(now, timer->expires))
		return -EINVAL;

	timer_insert_tree(timer);

	update_timer_recent_expires(base);
	return 0;
}


/**
 * mod_timer - modify a timer's timeout
 * @timer: the timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer() is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * The function returns whether it has modified a pending timer or not.
 * (ie. mod_timer() of an inactive timer returns 0, mod_timer() of an
 * active timer returns 1.)
 */
int mod_timer(struct timer_list* timer, unsigned long expires)
{
	int ret = 0;
	struct timer_base* base = timer->base;

	pthread_mutex_lock(&base->lock);
	if(timer_pending(timer)) {
		if(timer->expires == expires) {
			goto out_unlock;
		}
		detach_timer(timer);
	}

	timer->expires = expires;
	ret = internal_add_timer(timer);

out_unlock:
	pthread_mutex_unlock(&base->lock);
	return ret;
}

/*
 * call this function add your timer to list.
 * */
int add_timer(struct timer_list *timer)
{
	int ret;
	if(timer_pending(timer)) {
		loge("timer was exist! add timer fail.\n");
		return -EINVAL;
	}
	ret = mod_timer(timer, timer->expires);
	if(ret != 0)
		return -EINVAL;

	return 0;
}

/*
 * call this function delete your timer from list.
 * */
int del_timer(struct timer_list *timer)
{
	int ret  = 0;
	struct timer_base* base = timer->base;

	pthread_mutex_lock(&base->lock);
	if(!timer_pending(timer)) {
		detach_timer(timer);
		update_timer_recent_expires(base);
		ret = 1;	
	}
	pthread_mutex_unlock(&base->lock);

	return ret;
}

static void run_timers(struct timer_base* base)
{
	struct timer_list *timer, *tmp;
	uint64_t now = curr_time_ms();
	struct rb_root *root = &base->timer_tree;
	struct rb_node *node;

	pthread_mutex_lock(&base->lock);

next:
	node = root->rb_node;
	timer = rb_entry(node, struct timer_list, entry);

	if(time_after_eq(now, timer->expires)) {
		void (*fn)(unsigned long);
		unsigned long data;
		struct list_head *l;

		fn = timer->function;
		data = timer->data;
		l = &timer->list;

		rb_erase_init(&timer->entry, &base->timer_tree);

		pthread_mutex_unlock(&base->lock);

		call_timer_fn(timer, fn, data);

		list_for_each_entry_safe(timer, tmp, l, list) {
			fn = timer->function;
			data = timer->data;
			list_del_init(&timer->list);

			call_timer_fn(timer, fn, data);
		}

		pthread_mutex_lock(&base->lock);

		goto next;
	}

	update_timer_recent_expires(base);
	pthread_mutex_unlock(&base->lock);
}


void init_timer(struct timer_list* timer)
{
	memset(timer, 0, sizeof(struct timer_list));
	rb_init_node(&timer->entry);
	INIT_LIST_HEAD(&timer->list);
	timer->state = 0;
}

static void timer_handler(struct timer_base* base, uint8_t *data, int len)
{
	run_timers(base);
}

static void timer_close(struct timer_base* base)
{
	base->event = 0;
}


int init_timers(void)
{
	struct timer_base* base;
	base = &_timers;

	base->clockid = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

	pthread_mutex_init(&base->lock, NULL);

	base->event = iohandler_create(base->clockid, 
			(handle_func)timer_handler, (close_func)timer_close, base);	
	return 0;
}


