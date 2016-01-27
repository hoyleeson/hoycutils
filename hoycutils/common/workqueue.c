#include <stdlib.h>
#include <pthread.h>

#include <common/timer.h>
#include <common/workqueue.h>

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only for
 *    everyone else.
 *
 * P: Preemption protected.  Disabling preemption is enough and should
 *    only be modified and accessed from the local cpu.
 *
 * L: gwq->lock protected.  Access with gwq->lock held.
 *
 * X: During normal operation, modification requires gwq->lock and
 *    should be done only from local cpu.  Either disabling preemption
 *    on local cpu or grabbing gwq->lock is enough for read access.
 *    If GWQ_DISASSOCIATED is set, it's identical to L.
 *
 * F: wq->flush_mutex protected.
 *
 * W: workqueue_lock protected.
 */

struct global_wq;

/*
 * The poor guys doing the actual heavy lifting.  All on-duty workers
 * are either serving the manager role, on idle list or on busy hash.
 */
struct worker {
	struct list_head	entry;

	struct work_struct	*current_work;	/* L: work being processed */
	struct workqueue_struct *current_wq; /* L: current_work's wq */
	struct list_head	scheduled;	/* L: scheduled works XXX*/
	pthread_t 			task;		/* I: worker task */
	struct global_wq	*gwq;		/* I: the associated gwq */
	/* 64 bytes boundary on 64bit, 32 on 32bit */
	unsigned long		last_active;	/* L: last active timestamp */
	unsigned int		flags;		/* X: flags */
	int			id;		/* I: worker id */
};

/*
 * Global workqueue.  There's one and only one for
 * and all works are queued and processed here regardless of their
 * target workqueues.
 */
struct global_wq {
	pthread_mutex_t		lock;		/* the gwq lock */
	struct list_head	worklist;	/* L: list of pending works */
	unsigned int		flags;		/* L: GWQ_* flags */

	int			nr_workers;	/* L: total number of workers */
	int			nr_idle;	/* L: currently idle ones */

	/* workers are chained either in the idle_list or busy_hash */
	struct list_head	idle_list;	/* X: list of idle workers */
	struct list_head	busy_list; /* L: list of busy workers */

	struct timer_list	idle_timer;	/* L: worker idle timeout */

	int		worker_ids;	/* L: for worker IDs */

	struct worker		*first_idle;	/* L: first idle worker */

	struct list_head 	workqueues;
};

/*
 * The per-CPU workqueue.  The lower WORK_STRUCT_FLAG_BITS of
 * work_struct->data are used for flags and thus wqs need to be
 * aligned at two's power of the number of flag bits.
 */
struct workqueue_struct {
	struct global_wq	*gwq;		/* I: the associated gwq */
	unsigned int		flags;		/* I: WQ_* flags */
	struct list_head	list;		/* W: list of all workqueues */

	int			nr_active;	/* L: nr of active works */
	int			max_active;	/* L: max active works */
	struct list_head	delayed_works;	/* L: delayed works */
};

static struct global_wq _global_wq;
static struct LIST_HEAD(workqueues);
static pthread_mutex_t workqueue_lock = PTHREAD_MUTEX_INITIALIZER;

static inline struct global_wq *get_global_wq(void)
{
	return &_global_wq;
}

static struct workqueue_struct *get_work_wq(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	if (data & WORK_STRUCT_WQ)
		return (void *)(data & WORK_STRUCT_WQ_DATA_MASK);
	else
		return NULL;
}


/*
 * Policy functions.  These define the policies on how the global
 * worker pool is managed.  Unless noted otherwise, these functions
 * assume that they're being called with gwq->lock held.
 */

static bool __need_more_worker(struct global_wq *gwq)
{
	return !atomic_read(get_gwq_nr_running(gwq->cpu)) ||
		gwq->flags & GWQ_HIGHPRI_PENDING;
}

/*
 * Need to wake up a worker?  Called from anything but currently
 * running workers.
 */
static bool need_more_worker(struct global_wq *gwq)
{
	return !list_empty(&gwq->worklist) && __need_more_worker(gwq);
}


/**
 * gwq_determine_ins_pos - find insertion position
 * @gwq: gwq of interest
 * @wq: wq a work is being queued for
 *
 * A work for @wq is about to be queued on @gwq, determine insertion
 * position for the work.  If @wq is for HIGHPRI wq, the work is
 * queued at the head of the queue but in FIFO order with respect to
 * other HIGHPRI works; otherwise, at the end of the queue.  This
 * function also sets GWQ_HIGHPRI_PENDING flag to hint @gwq that
 * there are HIGHPRI works pending.
 *
 * CONTEXT:
 *
 * RETURNS:
 * Pointer to inserstion position.
 */
static inline struct list_head *gwq_determine_ins_pos(struct global_wq *gwq,
					       struct workqueue_struct *wq)
{
	struct work_struct *twork;

	if (likely(!(wq->flags & WQ_HIGHPRI)))
		return &gwq->worklist;

	list_for_each_entry(twork, &gwq->worklist, entry) {
		struct workqueue_struct *twq = get_work_wq(twork);

		if (!(twq->wq->flags & WQ_HIGHPRI))
			break;
	}

	gwq->flags |= GWQ_HIGHPRI_PENDING;
	return &twork->entry;
}

/**
 * insert_work - insert a work into gwq
 * @wq: wq @work belongs to
 * @work: work to insert
 * @head: insertion point
 * @extra_flags: extra WORK_STRUCT_* flags to set
 *
 * Insert @work which belongs to @wq into @gwq after @head.
 * @extra_flags is or'd to work_struct flags.
 *
 * CONTEXT:
 */
static void insert_work(struct workqueue_struct *wq,
			struct work_struct *work, struct list_head *head,
			unsigned int extra_flags)
{
	struct global_wq *gwq = wq->gwq;

	/* we own @work, set data and link */
	set_work_wq(work, wq, extra_flags);

	list_add_tail(&work->entry, head);

	if (__need_more_worker(gwq))
		wake_up_worker(gwq);
}



/**
 * queue_work - queue work on a workqueue
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 *
 * We queue the work to the CPU on which it was submitted, but if the CPU dies
 * it can be processed by another CPU.
 */
int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	int ret;
	struct global_wq *gwq = get_global_wq();
	pthread_mutex_lock(&gwq->lock);

	BUG_ON(!list_empty(&work->entry));

	if (likely(wq->nr_active < wq->max_active)) {
		wq->nr_active++;
		worklist = gwq_determine_ins_pos(gwq, wq);
	} else {
		work_flags |= WORK_STRUCT_DELAYED;
		worklist = &wq->delayed_works;
	}

	insert_work(wq, work, worklist, work_flags);

	pthread_mutex_unlock(&gwq->lock);

	return ret;
}

static void delayed_work_timer_fn(unsigned long __data)
{
	struct delayed_work *dwork = (struct delayed_work *)__data;
	struct workqueue_struct *wq = get_work_wq(&dwork->work);

	queue_work(wq, &dwork->work);
}


/**
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 */
int queue_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay)
{
	int ret = 0;
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;
	int now;

	if (delay == 0)
		return queue_work(wq, &dwork->work);

	now = curr_time_ms();

	timer->expires = now + delay;
	timer->data = (unsigned long)dwork;
	timer->function = delayed_work_timer_fn;

	add_timer(timer);
	return 0;
}


/**
 * work_busy - test whether a work is currently pending or running
 * @work: the work to be tested
 *
 * Test whether @work is currently pending or running.  There is no
 * synchronization around this function and the test result is
 * unreliable and only useful as advisory hints or for debugging.
 * Especially for reentrant wqs, the pending state might hide the
 * running state.
 *
 * RETURNS:
 * OR'd bitmask of WORK_BUSY_* bits.
 */
unsigned int work_busy(struct work_struct *work)
{
	struct global_wq *gwq = get_work_gwq(work);
	unsigned long flags;
	unsigned int ret = 0;

	if (!gwq)
		return false;

	pthread_mutex_lock(&gwq->lock);

	if (work_pending(work))
		ret |= WORK_BUSY_PENDING;
	if (find_worker_executing_work(gwq, work))
		ret |= WORK_BUSY_RUNNING;

	pthread_mutex_unlock(&gwq->lock);

	return ret;
}


/**
 * wake_up_worker - wake up an idle worker
 * @gwq: gwq to wake worker for
 *
 * Wake up the first idle worker of @gwq.
 *
 * CONTEXT:
 */
static void wake_up_worker(struct global_wq *gwq)
{
	struct worker *worker = first_worker(gwq);

	if (likely(worker))
		wake_up_process(worker->task);
}


/**
 * flush_workqueue - ensure that any scheduled work has run to completion.
 * @wq: workqueue to flush
 *
 * Forces execution of the workqueue and blocks until its completion.
 * This is typically used in driver shutdown handlers.
 *
 * We sleep until all works which were queued on entry have been handled,
 * but we are not livelocked by new incoming ones.
 */
void flush_workqueue(struct workqueue_struct *wq)
{

	/*XXX*/
}

/* Do I need to keep working?  Called from currently running workers. */
static bool keep_working(struct global_wq *gwq)
{

	return !list_empty(&gwq->worklist) &&
		((gwq->nr_worker - gwq->nr_idle) <= 1 ||
		 gwq->flags & GWQ_HIGHPRI_PENDING);
}

/**
 * worker_enter_idle - enter idle state
 * @worker: worker which is entering idle state
 *
 * @worker is entering idle state.  Update stats and idle timer if
 * necessary.
 *
 * LOCKING:
 * spin_lock_irq(gwq->lock).
 */
static void worker_enter_idle(struct worker *worker)
{
	struct global_wq *gwq = worker->gwq;

	BUG_ON(worker->flags & WORKER_IDLE);
	BUG_ON(!list_empty(&worker->entry));

	/* can't use worker_set_flags(), also called from start_worker() */
	worker->flags |= WORKER_IDLE;
	gwq->nr_idle++;
	worker->last_active = get_curr_ms();

	/* idle_list is LIFO */
	list_add(&worker->entry, &gwq->idle_list);

	if (too_many_workers(gwq) && !timer_pending(&gwq->idle_timer))
		mod_timer(&gwq->idle_timer,
				jiffies + IDLE_WORKER_TIMEOUT);
}

/**
 * worker_leave_idle - leave idle state
 * @worker: worker which is leaving idle state
 *
 * @worker is leaving idle state.  Update stats.
 *
 * LOCKING:
 * spin_lock_irq(gwq->lock).
 */
static void worker_leave_idle(struct worker *worker)
{
	struct global_wq *gwq = worker->gwq;

	BUG_ON(!(worker->flags & WORKER_IDLE));
	worker->flags &= ~flags;
	gwq->nr_idle--;
	list_del_init(&worker->entry);
}



/**
 * worker_thread - the worker thread function
 * @__worker: self
 *
 * The gwq worker thread function.  There's a single dynamic pool of
 * these per each cpu.  These workers process all works regardless of
 * their specific target workqueue.  The only exception is works which
 * belong to workqueues with a rescuer which will be explained in
 * rescuer_thread().
 */
static int worker_thread(void *__worker)
{
	struct worker *worker = __worker;
	struct global_wq *gwq = worker->gwq;

woke_up:
	pthread_mutex_lock(&gwq->lock);

	worker_leave_idle(worker);
recheck:
	/* no more worker necessary? */
	if (!need_more_worker(gwq))
		goto sleep;

	/* do we need to manage? */
	if (unlikely(!may_start_working(gwq)) && manage_workers(worker))
		goto recheck;

	do {
		struct work_struct *work =
			list_first_entry(&gwq->worklist,
					struct work_struct, entry);

		work_func_t f = work->func;

		worker->current_work = work;
		list_del_init(&work->entry);

		pthread_mutex_unlock(&gwq->lock);

		f(work);

		pthread_mutex_lock(&gwq->lock);

	} while (keep_working(gwq));

sleep:
	if (unlikely(need_to_manage_workers(gwq)) && manage_workers(worker))
		goto recheck;

	/*
	 * gwq->lock is held and there's no work to process and no
	 * need to manage, sleep.  Workers are woken up only while
	 * holding gwq->lock or from local cpu, so setting the
	 * current state before releasing gwq->lock is enough to
	 * prevent losing any event.
	 */
	worker_enter_idle(worker);


	pthread_mutex_unlock(&gwq->lock);

	goto woke_up;

	return 0;
}

struct workqueue_struct *alloc_workqueue(int max_active, unsigned int flags)
{
	struct workqueue_struct *wq;

	wq = (struct workqueue_struct *)malloc(sizeof(*wq));
	if(!wq)
		return NULL;

	pthread_mutex_init(&wq->lock);
	wq->flags = flags;
	wq->max_active = max_active;
	wq->nr_active = 0;

	INIT_LIST_HEAD(&wq->delayed_works);

	pthread_mutex_lock(&workqueue_lock);
	list_add(&wq->list, &workqueues);
	pthread_mutex_unlock(&workqueue_lock);

	return wq;
}

/**
 * destroy_workqueue - safely terminate a workqueue
 * @wq: target workqueue
 *
 * Safely destroy a workqueue. All work currently pending will be done first.
 */
void destroy_workqueue(struct workqueue_struct *wq)
{
	unsigned int flush_cnt = 0;
	bool drained;

	wq->flags |= WQ_DYING;

reflush:
	flush_workqueue(wq);

	pthread_mutex_lock(&wq->gwq->lock);
	drained = !wq->nr_active && list_empty(&wq->delayed_works);
	pthread_mutex_lock(&wq->gwq->lock);

	if (!drained) {
		if (++flush_cnt == 10 ||
				(flush_cnt % 100 == 0 && flush_cnt <= 1000))
			logw("workqueue %s: flush on destruction isn't complete"
					" after %u tries\n", wq->name, flush_cnt);
		goto reflush;
	}

	pthread_mutex_lock(&workqueue_lock);
	list_del(&wq->list);
	pthread_mutex_unlock(&workqueue_lock);

	BUG_ON(wq->nr_active);
	BUG_ON(!list_empty(&wq->delayed_works));

	free(wq);
}


static struct worker *alloc_worker(void)
{
	struct worker *worker;

	worker = malloc(sizeof(*worker));
	if (!worker) 
		return NULL;

	INIT_LIST_HEAD(&worker->entry);
	/* on creation a worker is in !idle && prep state */
	worker->flags = WORKER_PREP;

	return worker;
}

/**
 * create_worker - create a new workqueue worker
 * @gwq: gwq the new worker will belong to
 * @bind: whether to set affinity to @cpu or not
 *
 * Create a new worker which is bound to @gwq.  The returned worker
 * can be started by calling start_worker() or destroyed using
 * destroy_worker().
 *
 * CONTEXT:
 * Might sleep.  Does GFP_KERNEL allocations.
 *
 * RETURNS:
 * Pointer to the newly created worker.
 */
static struct worker *create_worker(struct global_wq *gwq)
{
	struct worker *worker = NULL;
	pthread_attr_t attr;

	worker = alloc_worker();
	if (!worker)
		goto fail;

	worker->gwq = gwq;
	worker->id = gwq->worker_ids++;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&worker->task, &attr, worker_thread, worker);
	if(ret)
		goto create_fail;

	return worker;

create_fail:
	free(worker);
fail:
	return NULL;
}


/**
 * start_worker - start a newly created worker
 * @worker: worker to start
 *
 * Make the gwq aware of @worker and start it.
 *
 * CONTEXT:
 */
static void start_worker(struct worker *worker)
{
	worker->flags |= WORKER_STARTED;
	worker->gwq->nr_workers++;

	worker_enter_idle(worker);
	wake_up_thread(worker->task);
}

/**
 * destroy_worker - destroy a workqueue worker
 * @worker: worker to be destroyed
 *
 * Destroy @worker and adjust @gwq stats accordingly.
 *
 * CONTEXT:
 */
static void destroy_worker(struct worker *worker)
{
	struct global_wq *gwq = worker->gwq;
	int id = worker->id;

	/* sanity check frenzy */
	if(worker->current_work)
		return;
	/*XXX*/ //BUG_ON(!list_empty(&worker->scheduled));

	if (worker->flags & WORKER_STARTED)
		gwq->nr_workers--;
	if (worker->flags & WORKER_IDLE)
		gwq->nr_idle--;

	list_del_init(&worker->entry);
	worker->flags |= WORKER_DIE;

//	kthread_stop(worker->task);
	free(worker);
}



static int __init init_workqueues(void)
{
	struct worker *worker;
	struct global_wq *gwq = get_global_wq();

	pthread_mutex_init(&gwq->lock);	
	
	INIT_LIST_HEAD(&gwq->worklist);
	gwq->flags = flags;
	gwq->nr_workers = 0;
	gwq->nr_idle = 0;
	gwq->worker_ids = 0;
	
	INIT_LIST_HEAD(&gwq->idle_list);
	INIT_LIST_HEAD(&gwq->busy_list);

	init_timer(&gwq->idle_timer);

	gwq->first_idle = NULL;

	INIT_LIST_HEAD(&gwq->workqueues);

	worker = create_worker(gwq);
	start_worker(worker);

	return 0;
}
