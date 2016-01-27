#ifndef _COMMON_IOASYNC_H_
#define _COMMON_IOASYNC_H_

#include <common/types.h>
#include <common/packet.h>
#include <common/poller.h>



typedef void (*handle_func) (void* priv, uint8_t *data, int len);
typedef void (*accept_func) (void* priv, int acceptfd);
typedef void (*close_func) (void*  priv);


unsigned long iohandler_create(int fd, handle_func hand_fn, close_func close_fn,
	   	void *data);
unsigned long iohandler_accept_create(int fd, accept_func accept_fn, close_func close_fn,
	   	void *data);

void iohandler_send(unsigned long handle, const uint8_t *data, int len);
void iohandler_shutdown(unsigned long handle);

void ioasync_init(struct poller* l);
void ioasync_release(void);

#endif

