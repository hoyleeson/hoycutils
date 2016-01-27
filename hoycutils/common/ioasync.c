#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <common/poller.h>
#include <common/queue.h>
#include <common/ioasync.h>


struct ioasync {
	bool 	 			initialized;
    /* the looper that manages the fds */
    struct poller*      poller;

    /* list of active iohandler objects */
	struct list_head 	active_list;

    /* list of closing struct iohandler objects.
     * these are waiting to push their
     * queued packets to the fd before
     * freeing themselves.
     */
	struct list_head 	closing_list;

	pthread_mutex_t 	lock;
	pthread_cond_t 		cond;
};


struct handle_ops {
	void (*post_func) (void* priv, struct packet *p);
	void (*handle_func) (void* priv, uint8_t *data, int len);
	void (*accept_func) (void* priv, int acceptfd);
	void (*close_func)(void* priv);
};

/* IO HANDLERS
 *
 * these are smart listeners that send incoming packets to a receiver
 * and can queue one or more outgoing packets and send them when
 * possible to the FD.
 *
 * note that we support clean shutdown of file descriptors,
 * i.e. we try to send all outgoing packets before destroying
 * the struct iohandler.
 */
struct iohandler {
    int             	fd;
    struct handle_ops 	h_ops;
	void 				*priv_data;
	struct queue 		*q_in;
	struct queue 		*q_out;

	int 				flags;
    uint8_t 			closing;
	struct ioasync* 	owner;
	struct list_head 	node;
};


static struct ioasync _ioasync;


/* close a struct iohandler (and free it). Note that this will not
 * perform a graceful shutdown, i.e. all packets in the
 * outgoing queue will be immediately free.
 *
 * this *will* notify the receiver that the file descriptor
 * was closed.
 *
 * you should call iohandler_shutdown() if you want to
 * notify the struct iohandler that its packet source is closed.
 */
void iohandler_close(struct iohandler* ioh)
{
	logd("%s: closing fd %d", __func__, ioh->fd);
	struct ioasync *ioa = ioh->owner;

	/* notify receiver */
	if(ioh->h_ops.closefn)
		ioh->h_ops.closefn(ioh->priv_data);

	/* remove the handler from its list */
	list_del(&ioh->node);

	/* get rid of outgoing packet queue */
	if (ioh->out_first != NULL) {
		Packet*  p;
		while ((p = ioh->out_first) != NULL) {
			ioh->out_first = p->next;
			packet_free(&p);
		}
	}

	/* get rid of file descriptor */
	if (ioh->fd >= 0) {
		poller_del(ioa->poller, ioh->fd);
		close(ioh->fd);
		ioh->fd = -1;
	}

	ioh->owner = NULL;
	xfree(ioh);
}

/* Ask the struct iohandler to cleanly shutdown the connection,
 * i.e. send any pending outgoing packets then auto-free
 * itself.
 */
void iohandler_shutdown(struct iohandler*  ioh)
{
	struct ioasync *ioa = ioh->owner;

	logd("%s: shoutdown.", __func__);
	/* prevent later iohandler_close() to
	 * call the receiver's close.
	 */
	ioh->h_ops.closefn = NULL;

	if (ioh->out_first != NULL && !ioh->closing)
	{
		/* move the handler to the 'closing' list */
		ioh->closing = 1;
		list_del(&ioh->node);
		list_add_tail(&ioh->node, &ioa->closing_list);
		return;
	}

	iohandler_close(ioh);
}

/* Enqueue a new packet that the struct iohandler will
 * send through its file descriptor.
 */
static void iohandler_enqueue(struct iohandler* ioh, struct packet* p)
{
	Packet*  first = ioh->out_first;

	p->next         = NULL;
	ioh->out_ptail[0] = p;
	ioh->out_ptail    = &p->next;

	if (first == NULL) {
		ioh->out_pos = 0;
		poller_enable(ioh->list->poller, ioh->fd, EPOLLOUT);
	}
}

void iohandler_send(struct iohandler *ioh, const uint8_t *data, int len) 
{
	Packet*   p;

	p = packet_alloc();
	memcpy(p->data, data, len);
	p->len = len;

	iohandler_enqueue(ioh, p);
}

/* struct iohandler file descriptor event callback for read/write ops */
static void iohandler_event(struct iohandler* ioh, int  events)
{
	/* in certain cases, it's possible to have both EPOLLIN and
	 * EPOLLHUP at the same time. This indicates that there is incoming
	 * data to read, but that the connection was nonetheless closed
	 * by the sender. Be sure to read the data before closing
	 * the receiver to avoid packet loss.
	 */
	if (events & EPOLLIN) {
		struct packet* p = packet_alloc();

		p->len = fd_read(ioh->fd, p->data, MAX_PAYLOAD);
		if(p->len < 0) {
			loge("%s: can't recv: %s", __func__, strerror(errno));
			packet_free(p);
		} else if (len > 0) {
			p->len     = len;

			if(ioh->h_ops.post_func) {
				ioh->h_ops.post_func(ioh, p);
			}
		}
	}

	if (events & (EPOLLHUP|EPOLLERR)) {
		/* disconnection */
		loge("%s: disconnect on fd %d", __func__, ioh->fd);
		iohandler_close(ioh);
		return;
	}

	if (events & EPOLLOUT && ioh->out_first) {
		Packet*  p = ioh->out_first;
		int      avail, len;

		avail = p->len - ioh->out_pos;
		if ((len = fd_write(ioh->fd, p->data + ioh->out_pos, avail)) < 0) {
			loge("%s: can't send: %s", __func__, strerror(errno));
		} else {
			ioh->out_pos += len;
			if (ioh->out_pos >= p->len) {
				ioh->out_pos   = 0;
				ioh->out_first = p->next;
				packet_free(&p);
				if (ioh->out_first == NULL) {
					ioh->out_ptail = &ioh->out_first;
					poller_disable(ioh->list->poller, ioh->fd, EPOLLOUT);
				}
			}
		}
	}
}


static void common_post_func(void *priv, struct packet *p)
{
	struct iohandler *ioh = (struct iohandler *)priv;

	if(ioh->h_ops.handlefn)
		ioh->h_ops.handlefn(ioh->priv_data, p->data, p->len);

	packet_free(&p);
}

struct iohandler* iohandler_create(int fd, 
		handle_func hand_fn, close_func close_fn, void *data)
{
	struct ioasync *ioa = &_ioasync;
	struct iohandler*  ioh = xzalloc(sizeof(*ioh));

	ioh->fd       = fd;
	ioh->owner 	= ioa;
	ioh->priv_data = data;
	ioh->h_ops.handlefn = hand_fn;
	ioh->h_ops.postfn = common_post_func;
	ioh->h_ops.closefn = close_fn;

	list_add_tail(&ioh->node, &list->active_list);

	poller_add(list->poller, fd, (event_fn)iohandler_event, ioh);
	poller_enable(list->poller, fd, EPOLLIN);
	
	return (unsigned long)ioh;
}


/* event callback function to monitor accepts() on server sockets.
 * the convention used here is that the receiver will receive a
 * dummy packet with the new client socket in p->channel
 */
static void iohandler_accept_event(struct iohandler* ioh, int  events)
{
	if (events & EPOLLIN) {
		int newfd;
		struct packet* p;

		newfd = fd_accept(ioh->fd);
		if (newfd < 0) {
			loge("%s: accept failed: %s", __func__, strerror(errno));
			return;
		}

		logd("%s: accepting on fd %d, new fd:%d", __func__, ioh->fd, newfd);

		p = packet_alloc();
		p->len = sizeof(int);
		memcpy(p->data, newfd, p->len);

		if(ioh->h_ops.post_func) {
			ioh->h_ops.post_func(ioh, p);
		}
	}

	if (events & (EPOLLHUP|EPOLLERR)) {
		/* disconnecting */
		loge("%s: closing accept fd %d", __func__, ioh->fd);
		iohandler_close(ioh);
		return;
	}
}


#define LIST_MAX_COUNT 	(50)

static void accept_post_func(void *priv, struct packet *p)
{
	struct iohandler *ioh = (struct iohandler *)priv;
	int newfd = *(int *)p->data;

	if(ioh->h_ops.acceptfn)
		ioh->h_ops.acceptfn(ioh->priv_data, newfd);

	packet_free(p);
}

unsigned long iohandler_accept_create(int fd, 
		accept_func accept_fn, close_func close_fn, void *priv) 
{
	struct iohandler *ioh;
	struct ioasync *ioa = &_ioasync;

	if(!ioa->initialized)
		return NULL;

	ioh = xzalloc(sizeof(*ioh));

	ioh->fd    	= fd;
	ioh->owner 	= ioa;
	ioh->priv_data = data;
	ioh->h_ops.postfn = accept_post_func;
	ioh->h_ops.acceptfn = accept_fn;
	ioh->h_ops.closefn = close_fn;

	list_add_tail(&ioh->node, &list->active_list);

	poller_add(ioa->poller, fd, (event_fn)iohandler_accept_event, ioh);
	poller_enable(ioa->poller, fd, EPOLLIN);
	listen(fd, LIST_MAX_COUNT);

	return (unsigned long)ioh;
}


/* initialize a ioasync */
void ioasync_init(struct poller *l)
{
	struct ioasync *ioa = &_ioasync;

	ioa->initialized = true;
	ioa->poller = l;

	INIT_LIST_HEAD(&ioa->active_list);
	INIT_LIST_HEAD(&ioa->closing_list);
}

