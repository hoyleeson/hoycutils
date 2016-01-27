#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>

#include <common/poller.h>


/* return the struct event_hook corresponding to a given
 * monitored file descriptor, or NULL if not found
 */
static struct event_hook* poller_find(struct poller*  l, int  fd)
{
    struct event_hook*  hook = l->hooks;
    struct event_hook*  end  = hook + l->num_fds;

    for (; hook < end; hook++) {
        if (hook->fd == fd)
            return hook;
    }
    return NULL;
}

/* grow the arrays in the poller object */
static void poller_grow(struct poller*  l)
{
    int  old_max = l->max_fds;
    int  new_max = old_max + (old_max >> 1) + 4;
    int  n;

    xrenew(l->events, new_max);
    xrenew(l->hooks,  new_max);
    l->max_fds = new_max;

    /* now change the handles to all events */
    for (n = 0; n < l->num_fds; n++) {
        struct epoll_event ev;
        struct event_hook*          hook = l->hooks + n;

        ev.events   = hook->wanted;
        ev.data.ptr = hook;
        epoll_ctl(l->epoll_fd, EPOLL_CTL_MOD, hook->fd, &ev);
    }
}

/* register a file descriptor and its event handler.
 * no event mask will be enabled
 */
void poller_add(struct poller* l, int fd, event_func  func, void*  data)
{
    struct epoll_event  ev;
    struct event_hook*           hook;

    if (l->num_fds >= l->max_fds)
        poller_grow(l);

    hook = l->hooks + l->num_fds;

    hook->fd      = fd;
    hook->data = data;
    hook->func = func;
    hook->state   = 0;
    hook->wanted  = 0;
    hook->events  = 0;

    fd_setnonblock(fd);

    ev.events   = 0;
    ev.data.ptr = hook;
    epoll_ctl(l->epoll_fd, EPOLL_CTL_ADD, fd, &ev);

    l->num_fds += 1;
}

/* unregister a file descriptor and its event handler
 */
void poller_del(struct poller*  l, int  fd)
{
    struct event_hook*  hook = poller_find(l, fd);

    if (!hook) {
        loge("%s: invalid fd: %d", __func__, fd);
        return;
    }
    /* don't remove the hook yet */
    hook->state |= HOOK_CLOSING;

    epoll_ctl(l->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

/* enable monitoring of certain events for a file
 * descriptor. This adds 'events' to the current
 * event mask
 */
void poller_enable(struct poller*  l, int  fd, int  events)
{
    struct event_hook*  hook = poller_find(l, fd);

    if (!hook) {
        loge("%s: invalid fd: %d", __func__, fd);
        return;
    }

    if (events & ~hook->wanted) {
        struct epoll_event  ev;

        hook->wanted |= events;
        ev.events   = hook->wanted;
        ev.data.ptr = hook;

        epoll_ctl(l->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }
}

/* disable monitoring of certain events for a file
 * descriptor. This ignores events that are not
 * currently enabled.
 */
void poller_disable(struct poller*  l, int  fd, int  events)
{
    struct event_hook*  hook = poller_find(l, fd);

    if (!hook) {
        loge("%s: invalid fd: %d", __func__, fd);
        return;
    }

    if (events & hook->wanted) {
        struct epoll_event  ev;

        hook->wanted &= ~events;
        ev.events   = hook->wanted;
        ev.data.ptr = hook;

        epoll_ctl(l->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }
}

static int poller_exec(struct poller* l) {
	int  n, count;

	do {
		count = epoll_wait(l->epoll_fd, l->events, l->num_fds, -1);
	} while (count < 0 && errno == EINTR);

	if (count < 0) {
		loge("%s: error: %s", __func__, strerror(errno));
		return -EINVAL;
	}

	if (count == 0) {
		loge("%s: huh ? epoll returned count=0", __func__);
		return 0;
	}

	/* mark all pending hooks */
	for (n = 0; n < count; n++) {
		struct event_hook*  hook = l->events[n].data.ptr;
		hook->state  = HOOK_PENDING;
		hook->events = l->events[n].events;
	}

	/* execute hook callbacks. this may change the 'hooks'
	 * and 'events' array, as well as l->num_fds, so be careful */
	for (n = 0; n < l->num_fds; n++) {
		struct event_hook*  hook = l->hooks + n;
		if (hook->state & HOOK_PENDING) {
			hook->state &= ~HOOK_PENDING;
			hook->func(hook->data, hook->events);
		}
	}

	/* now remove all the hooks that were closed by
	 * the callbacks */
	for (n = 0; n < l->num_fds;) {
		struct epoll_event ev;
		struct event_hook*  hook = l->hooks + n;

		if (!(hook->state & HOOK_CLOSING)) {
			n++;
			continue;
		}

		hook[0]     = l->hooks[l->num_fds-1];
		l->num_fds -= 1;
		ev.events   = hook->wanted;
		ev.data.ptr = hook;
		epoll_ctl(l->epoll_fd, EPOLL_CTL_MOD, hook->fd, &ev);
	}
	return 0;
}

/* wait until an event occurs on one of the registered file
 * descriptors. Only returns in case of error !!
 */
void poller_loop(struct poller*  l)
{
	int ret;
    for (;;) {
		ret = poller_exec(l);
		if(ret)
			break;
    }
}


/* initialize a poller object */
struct poller *poller_create(void) 
{
	struct poller* l;

	xnew(l);

    l->epoll_fd = epoll_create(4);
    l->num_fds  = 0;
    l->max_fds  = 0;
    l->events   = NULL;
    l->hooks    = NULL;
}

/* finalize a poller object */
void poller_release(struct poller*  l)
{
    xfree(l->events);
    xfree(l->hooks);
    l->max_fds = 0;
    l->num_fds = 0;

    close(l->epoll_fd);
    l->epoll_fd  = -1;

	xfree(l);
}

