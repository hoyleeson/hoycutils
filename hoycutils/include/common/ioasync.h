#ifndef _COMMON_IOASYNC_H_
#define _COMMON_IOASYNC_H_


/* PACKET RECEIVER
 *
 * Simple abstraction for something that can receive a packet
 * from a struct iohandler (see below) or something else.
 *
 * Send a packet to it with 'receiver_post'
 *
 * Call 'receiver_close' to indicate that the corresponding
 * packet source was closed.
 */

typedef void (*post_func) (void* user, Packet *p);
typedef void (*handle_func) (void* user, uint8_t *data, int len);
typedef void (*accept_func) (void* user, int acceptfd);
typedef void (*close_func)(void*  user);

struct receiver {
    void*      	data;

	post_func   postfn;
    close_func  closefn;
	union {
		handle_func handlefn;
		accept_func acceptfn;
	};
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
struct ioasync;

struct iohandler {
    int             fd;
    uint8_t 		closing;
    struct receiver receiver;
/* XXX
    int             out_pos;
    Packet*         out_first;
    Packet**        out_ptail;
*/
	struct list_head 	node;
	struct ioasync* 	owner;
};

struct ioasync {
	uint8_t 			initialized;
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
};

void iohandler_close(struct iohandler*  ioh);
void iohandler_shutdown(struct iohandler*  ioh);

void iohandler_send(struct iohandler *ioh, const uint8_t *data, int len);

struct iohandler* iohandler_create(int fd,
	   	handle_func hand_fn, close_func close_fn, void *data);

struct iohandler* iohandler_accept_create(int fd, 
		accept_func accept_fn, close_func close_fn, void *data);

void ioasync_init(struct poller* l);
void ioasync_release(void);

#endif

