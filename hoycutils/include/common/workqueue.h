#ifndef _COMMON_WORK_QUEUE_H_
#define _COMMON_WORK_QUEUE_H_

#include <common/list.h>
#include <common/timer.h>

struct workqueue_struct;

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

/*
 * The first word is the work queue pointer and the flags rolled into
 * one
 */
#define work_data_bits(work) ((unsigned long *)(&(work)->data))


enum {
	WORK_STRUCT_PENDING_BIT = 0,    /* work item is pending execution */
	WORK_STRUCT_DELAYED_BIT = 1,    /* work item is delayed */
};

struct work_struct {
	struct list_head entry;
	work_func_t func;
	unsigned long data;
};

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;
};

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

#define __WORK_INITIALIZER(n, f, d) {              \
	.data = (d),            \
	.entry  = { &(n).entry, &(n).entry },           \
	.func = (f),                        \
}

#define __DELAYED_WORK_INITIALIZER(n, f, d) {          \
	.work = __WORK_INITIALIZER((n).work, (f), (d)),      \
	.timer = TIMER_INITIALIZER((n).timer, NULL, 0, 0),         \
}


#define DECLARE_WORK(n, f, d)                  \
	struct work_struct n = __WORK_INITIALIZER(n, f, d)

#define DECLARE_DELAYED_WORK(n, f, d)              \
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f)


#define create_workqueue(max_active)					\
	alloc_workqueue((max_active), 0)


extern struct workqueue_struct *global_wq;

extern int queue_work(struct workqueue_struct *wq, struct work_struct *work);
extern int queue_delayed_work(struct workqueue_struct *wq,
		            struct delayed_work *work, unsigned long delay);


extern void flush_workqueue(struct workqueue_struct *wq);
extern void drain_workqueue(struct workqueue_struct *wq);
extern void flush_scheduled_work(void);

extern int schedule_work(struct work_struct *work);
extern int schedule_delayed_work(struct delayed_work *work, unsigned long delay);


extern bool flush_work(struct work_struct *work);
extern bool flush_work_sync(struct work_struct *work);
extern bool cancel_work_sync(struct work_struct *work);

extern bool flush_delayed_work(struct delayed_work *dwork);
extern bool flush_delayed_work_sync(struct delayed_work *work);
extern bool cancel_delayed_work_sync(struct delayed_work *dwork);

extern void workqueue_set_max_active(struct workqueue_struct *wq,
		                     int max_active);
extern bool workqueue_congested(unsigned int cpu, struct workqueue_struct *wq);
extern unsigned int work_busy(struct work_struct *work);

#endif
