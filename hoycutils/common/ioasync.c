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
#include <common/ioasync.h>

static struct ioasync _ioasync;

/* handle a packet to a receiver. Note that this transfers
 * ownership of the packet to the receiver.
 */
static inline void receiver_post(struct receiver*  r, Packet*  p)
{
	if (r->postfn)
		r->postfn(r, p);
	else
		packet_free(&p);
}

/* tell a receiver the packet source was closed.
 * this will also prevent further handleing to the
 * receiver.
 */
static inline void receiver_close(struct receiver*  r)
{
	if (r->closefn) {
		r->closefn(r->user);
		r->closefn = NULL;
	}
	r->postfn = NULL;
	r->handlefn = NULL;
}


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

	/* notify receiver */
	receiver_close(ioh->receiver);

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
		poller_del(ioh->list->poller, ioh->fd);
		close(ioh->fd);
		ioh->fd = -1;
	}

	ioh->list = NULL;
	xfree(ioh);
}

/* Ask the struct iohandler to cleanly shutdown the connection,
 * i.e. send any pending outgoing packets then auto-free
 * itself.
 */
void iohandler_shutdown(struct iohandler*  ioh)
{
	struct ioasync *ioa = ioh->owner;

	logd("%s: shoutdown", __func__);
	/* prevent later iohandler_close() to
	 * call the receiver's close.
	 */
	ioh->receiver->close = NULL;

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
static void iohandler_enqueue(struct iohandler* ioh, Packet*  p)
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
	int  len;

	/* in certain cases, it's possible to have both EPOLLIN and
	 * EPOLLHUP at the same time. This indicates that there is incoming
	 * data to read, but that the connection was nonetheless closed
	 * by the sender. Be sure to read the data before closing
	 * the receiver to avoid packet loss.
	 */
	if (events & EPOLLIN) {
		Packet*  p = packet_alloc();
		int      len;

		if ((len = fd_read(ioh->fd, p->data, MAX_PAYLOAD)) < 0) {
			loge("%s: can't recv: %s", __func__, strerror(errno));
			packet_free(&p);
		} else if (len > 0) {
			p->len     = len;
			p->channel = -101;  /* special debug value, not used */
			receiver_post(ioh->receiver, p);
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


/* Create a new struct iohandler that monitors read/writes */
static struct iohandler* iohandler_new(int fd,
	   	struct ioasync* list, struct receiver* receiver)
{
	struct iohandler*  ioh = xzalloc(sizeof(*ioh));

	ioh->fd       = fd;
	ioh->list     = list;
	ioh->receiver = *receiver;

	/* XXX fifo
	   ioh->out_first   = NULL;
	   ioh->out_ptail   = &ioh->out_first;
	   ioh->out_pos     = 0;
	   */

	list_add_tail(&ioh->node, &list->active_list);

	poller_add(list->poller, fd, (event_fn)iohandler_event, ioh);
	poller_enable(list->poller, fd, EPOLLIN);

	return ioh;
}


static void common_post_func(struct receiver *r, Packet *p)
{
	if(r->handle)
		r->handle(r->user, p->data, p->len);

	packet_free(&p);
}

struct iohandler* iohandler_create(int fd, 
		handle_func hand_fn, close_func close_fn, void *data)
{
	struct receiver  recv;
	struct ioasync *ioa = &_ioasync;

	recv.data = data;
	recv.handlefn = hand_fn;
	recv.postfn = (post_func)common_post_func;
	recv.closefn = close_fn;

	return iohandler_new(ioa, fd, &recv);
}


/* event callback function to monitor accepts() on server sockets.
 * the convention used here is that the receiver will receive a
 * dummy packet with the new client socket in p->channel
 */
static void iohandler_accept_event(struct iohandler* ioh, int  events)
{
	if (events & EPOLLIN) {
		/* this is an accept - send a dummy packet to the receiver */
		Packet*  p = packet_alloc();

		logd("%s: accepting on fd %d", __func__, ioh->fd);
		p->data[0] = 1;
		p->len     = 1;
		p->channel = fd_accept(ioh->fd);
		if (p->channel < 0) {
			loge("%s: accept failed ?: %s", __func__, strerror(errno));
			packet_free(&p);
			return;
		}
		receiver_post(ioh->receiver, p);
	}

	if (events & (EPOLLHUP|EPOLLERR)) {
		/* disconnecting !! */
		loge("%s: closing accept fd %d", __func__, ioh->fd);
		iohandler_close(ioh);
		return;
	}
}


#define LIST_MAX_COUNT 	(50)

/* Create a new struct iohandler used to monitor new connections on a
 * server socket. The receiver must expect the new connection
 * fd in the 'channel' field of a dummy packet.
 */
static struct iohandler* iohandler_new_accept(struct ioasync* ioa,
		int fd, struct receiver* receiver)
{
	struct iohandler *ioh = xzalloc(sizeof(*ioh));

	ioh->fd    	= fd;
	ioh->owner 	= ioa;
	ioh->receiver = *receiver;

	list_add_tail(&ioh->node, &list->active_list);

	poller_add(ioa->poller, fd, (event_fn)iohandler_accept_event, ioh);
	poller_enable(ioa->poller, fd, EPOLLIN);
	listen(fd, LIST_MAX_COUNT);

	return ioh;
}

static void accept_post_func(struct receiver *r, Packet *p)
{
	if(r->acceptfn)
		r->acceptfn(r->data, p->channel);
	/*XXX*/
	packet_free(&p);
}

struct iohandler* iohandler_accept_create(int fd, 
		accept_func accept_fn, close_func close_fn, void *data) 
{
	struct receiver  recv;
	struct ioasync *ioa = &_ioasync;

	if(!ioa->initialized)
		return NULL;

	recv.data = data;
	recv.postfn = (post_func)accept_post_func;
	recv.acceptfn = accept_fn;
	recv.closefn = close_fn;

	return iohandler_new_accept(ioa, fd, &recv);
}


/* initialize a ioasync */
void ioasync_init(struct poller *l)
{
	struct ioasync *ioa = &_ioasync;

	ioa->initialized = 1;
	ioa->poller = l;

	INIT_LIST_HEAD(&ioa->active_list);
	INIT_LIST_HEAD(&ioa->closing_list);
}


