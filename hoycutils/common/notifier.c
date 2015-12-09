#include <stdlib.h>
#include <errno.h>

#include <common/log.h>
#include <common/notifier.h>

/*
 *	Notifier chain core routines.  The exported routines below
 *	are layered on top of these, with appropriate locking added.
 */

static int __notifier_chain_register(struct notifier_block **nl,
		struct notifier_block *n)
{
	while ((*nl) != NULL) {
		if (n->priority > (*nl)->priority)
			break;
		nl = &((*nl)->next);
	}
	n->next = *nl;
	*nl = n;
	return 0;
}

static int __notifier_chain_cond_register(struct notifier_block **nl,
		struct notifier_block *n)
{
	while ((*nl) != NULL) {
		if ((*nl) == n)
			return 0;
		if (n->priority > (*nl)->priority)
			break;
		nl = &((*nl)->next);
	}
	n->next = *nl;
	*nl = n;
	return 0;
}

static int __notifier_chain_unregister(struct notifier_block **nl,
		struct notifier_block *n)
{
	while ((*nl) != NULL) {
		if ((*nl) == n) {
			*nl = n->next;
			return 0;
		}
		nl = &((*nl)->next);
	}
	return -ENOENT;
}

/**
 * notifier_call_chain - Informs the registered notifiers about an event.
 *	@nl:		Pointer to head of the blocking notifier chain
 *	@val:		Value passed unmodified to notifier function
 *	@v:		Pointer passed unmodified to notifier function
 *	@nr_to_call:	Number of notifier functions to be called. Don't care
 *			value of this parameter is -1.
 *	@nr_calls:	Records the number of notifications sent. Don't care
 *			value of this field is NULL.
 *	@returns:	notifier_call_chain returns the value returned by the
 *			last notifier function called.
 */
static int __notifier_call_chain(struct notifier_block **nl,
					unsigned long val, void *v,
					int nr_to_call,	int *nr_calls)
{
	int ret = NOTIFY_DONE;
	struct notifier_block *nb, *next_nb;

	nb = *nl;

	while (nb && nr_to_call) {
		next_nb = nb->next;

		ret = nb->notifier_call(nb, val, v);

		if (nr_calls)
			(*nr_calls)++;

		if ((ret & NOTIFY_STOP_MASK) == NOTIFY_STOP_MASK) {
			loge("notifier_call_chain : NOTIFY BAD %pf\n", nb->notifier_call);
			break;
		}
		nb = next_nb;
		nr_to_call--;
	}
	return ret;
}


int notifier_chain_register(struct notifier_head *nh, struct notifier_block *n)
{
	int ret;

	pthread_rwlock_wrlock(&nh->lock);
	ret = __notifier_chain_register(&nh->head, n);
	pthread_rwlock_unlock(&nh->lock);
	return ret;
}

int notifier_chain_cond_register(struct notifier_head *nh, struct notifier_block *n)
{
	int ret;

	pthread_rwlock_wrlock(&nh->lock);
	ret = __notifier_chain_cond_register(&nh->head, n);
	pthread_rwlock_unlock(&nh->lock);
	return ret;
}

int notifier_chain_unregister(struct notifier_head *nh, struct notifier_block *n)
{
	unsigned long flags;
	int ret;

	pthread_rwlock_wrlock(&nh->lock);
	ret = __notifier_chain_unregister(&nh->head, n);
	pthread_rwlock_unlock(&nh->lock);
	return ret;
}


int notifier_call_chain_nr(struct notifier_head *nh, unsigned long val, void *v,
					int nr_to_call, int *nr_calls)
{
	int ret;

	pthread_rwlock_rdlock(&nh->lock);
	ret = __notifier_call_chain(&nh->head, val, v, nr_to_call, nr_calls);
	pthread_rwlock_unlock(&nh->lock);
	return ret;
}

int notifier_call_chain(struct notifier_head *nh, unsigned long val, void *v)
{
	return notifier_call_chain_nr(nh, val, v, -1, NULL);
}


