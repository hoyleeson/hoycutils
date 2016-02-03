#ifndef _COMMON_IOASYNC_H_
#define _COMMON_IOASYNC_H_

#include <common/types.h>
#include <common/packet.h>
#include <common/poller.h>
#include <sys/socket.h>

typedef struct iohandler iohandler_t;
typedef struct ioasync ioasync_t;

typedef void (*handle_func) (void* priv, uint8_t *data, int len);
typedef void (*handlefrom_func) (void* priv, uint8_t *data, int len, void *from);
typedef void (*accept_func) (void* priv, int acceptfd);
typedef void (*close_func) (void*  priv);


void ioasync_send(iohandler_t *ioh, const uint8_t *data, int len);
void ioasync_sendto(iohandler_t *ioh, const uint8_t *data, int len, void *to);

void ioasync_pkt_send(iohandler_t *ioh, pack_buf_t *buf);
void ioasync_pkt_sendto(iohandler_t *ioh, pack_buf_t *buf, struct sockaddr *to);


iohandler_t *iohandler_create(ioasync_t *aio, int fd,
        handle_func hand_fn, close_func close_fn, void *priv);

iohandler_t *iohandler_accept_create(ioasync_t *aio, int fd,
        accept_func accept_fn, close_func close_fn, void *priv);

iohandler_t *iohandler_udp_create(ioasync_t *aio, int fd,
        handlefrom_func hand_fn, close_func close_fn, void *priv);

iohandler_t *iohandler_udp_create_exclusive(ioasync_t *aio, int fd,
        handlefrom_func handfrom_fn, close_func close_fn, void *priv);

void iohandler_close(iohandler_t* ioh);
void iohandler_shutdown(iohandler_t* ioh);

ioasync_t *ioasync_init(void);
void ioasync_loop(ioasync_t *aio);
void ioasync_done(ioasync_t *aio);


void global_ioasync_init(void);
ioasync_t *get_global_ioasync(void);

#endif

