#ifndef _COMMON_POLLER_H_
#define _COMMON_POLLER_H_


/* A struct poller object is used to monitor activity on one or more
 * file descriptors (e.g sockets).
 *
 * - call poller_add() to register a function that will be
 *   called when events happen on the file descriptor.
 *
 * - call poller_enable() or poller_disable() to enable/disable
 *   the set of monitored events for a given file descriptor.
 *
 * - call poller_del() to unregister a file descriptor.
 *   this does *not* close the file descriptor.
 *
 * Note that you can only provide a single function to handle
 * all events related to a given file descriptor.

 * You can call poller_enable/_disable/_del within a function
 * callback.
 */

/* the current implementation uses Linux's epoll facility
 * the event mask we use are simply combinations of EPOLLIN
 * EPOLLOUT, EPOLLHUP and EPOLLERR
 */

#define  MAX_CHANNELS  16
#define  MAX_EVENTS    (MAX_CHANNELS+1)  /* each channel + the serial fd */

/* the event handler function type, 'data' is a user-specific
 * opaque pointer passed to poller_add().
 */
typedef void (*event_func)(void*  data, int  events);

/* bit flags for the struct event_hook structure.
 *
 * HOOK_PENDING means that an event happened on the
 * corresponding file descriptor.
 *
 * HOOK_CLOSING is used to delay-close monitored
 * file descriptors.
 */
enum {
    HOOK_PENDING = (1 << 0),
    HOOK_CLOSING = (1 << 1),
};


#define EV_READ 	POLLIN
#define EV_WRITE 	POLLOUT		
#define EV_ERROR 	POLLERR		


/* A struct event_hook structure is used to monitor a given
 * file descriptor and record its event handler.
 */
struct event_hook {
    int        fd;
    int        wanted;  /* events we are monitoring */
    int        events;  /* events that occured */
    int        state;   /* see HOOK_XXX constants */
    void*      data; /* user-provided handler parameter */
    event_func  func; /* event handler callback */
};

/* struct poller is the main object modeling a poller object
 */
struct poller {
    int                  epoll_fd;
    int                  num_fds;
    int                  max_fds;
    struct epoll_event*  events;
    struct event_hook*   hooks;
};


void poller_add(struct poller*  l, int  fd, event_func  func, void*  data);
void poller_del(struct poller*  l, int  fd);

void poller_enable(struct poller*  l, int  fd, int  events);
void poller_disable(struct poller*  l, int  fd, int  events);

struct poller *poller_create(void);
void poller_release(struct poller*  l);

void poller_loop(struct poller*  l);

#endif

