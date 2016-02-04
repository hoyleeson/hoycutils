#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <common/utils.h>
#include <common/poller.h>
#include <common/queue.h>
#include <common/ioasync.h>
#include <common/workqueue.h>


struct iopacket {
    struct packet packet;
    struct sockaddr addr;
};


struct ioasync {
    struct poller poller;
    bool initialized;

    mempool_t *pkt_pool;
    pack_buf_pool_t *buf_pool;

    /* list of active iohandler objects */
    struct list_head active_list;

    /* list of closing struct iohandler objects.
     * these are waiting to push their
     * queued packets to the fd before
     * freeing themselves.
     */
    struct list_head closing_list;

    pthread_mutex_t lock;
};


struct handle_ops {
    void (*post)(void *priv, struct iopacket *pkt);
    void (*accept)(void *priv, int acceptfd);
    void (*handle)(void *priv, uint8_t *data, int len);
    void (*handlefrom)(void *priv, uint8_t *data, int len, void *from);
    void (*close)(void *priv);
};

enum iohandler_type {
    HANDLER_TYPE_NORMAL,
    HANDLER_TYPE_TCP_ACCEPT,
    HANDLER_TYPE_UDP,
};


struct iohandler {
    int fd;
    int type;
    int flags;
    int closing;

    struct handle_ops h_ops;
    void *priv_data;
    struct queue *q_in;
    struct queue *q_out;

    struct list_head node;

    pthread_mutex_t lock;
    struct ioasync* owner;
};

static struct iopacket *iohandler_pack_alloc(iohandler_t *ioh, int allocbuf)
{
    struct iopacket *pkt;
    ioasync_t *aio = ioh->owner;

    pkt = (struct iopacket *)mempool_alloc(aio->pkt_pool);
    if(allocbuf) {
        pkt->packet.buf = (pack_buf_t *)alloc_pack_buf(aio->buf_pool);
    }

    return pkt;
}

static void iohandler_pack_free(iohandler_t *ioh, struct iopacket *pkt, int freebuf)
{
    ioasync_t *aio = ioh->owner;

    if(freebuf) {
        free_pack_buf(pkt->packet.buf);
    }
    mempool_free(aio->pkt_pool, pkt);
}


static pack_buf_t *iohandler_pack_buf_alloc(iohandler_t *ioh)
{
    pack_buf_t *buf;
    ioasync_t *aio = ioh->owner;

    buf = (pack_buf_t *)alloc_pack_buf(aio->buf_pool);
    return buf;
}

static void iohandler_pack_buf_free(pack_buf_t *buf)
{
    free_pack_buf(buf);
}

void iohandler_pkt_send(iohandler_t *ioh, pack_buf_t *buf)
{
    struct iopacket *pack;

    pack = iohandler_pack_alloc(ioh, 0);
    pack->packet.buf = buf;
    queue_in(ioh->q_in, (struct packet *)pack);
}

void iohandler_pkt_sendto(iohandler_t *ioh, pack_buf_t *buf, struct sockaddr *to)
{
    struct iopacket *pack;

    pack = iohandler_pack_alloc(ioh, 0);
    pack->packet.buf = buf;
    pack->addr = *to;
    queue_in(ioh->q_in, (struct packet *)pack);
}

void iohandler_send(iohandler_t *ioh, const uint8_t *data, int len)
{
    pack_buf_t *buf;

    buf = iohandler_pack_buf_alloc(ioh);
    memcpy(buf->data, data, len);
    buf->len = len;

    iohandler_pkt_send(ioh, buf);
}

void iohandler_sendto(iohandler_t *ioh, const uint8_t *data, int len, struct sockaddr *to)
{
    pack_buf_t *buf;

    buf = iohandler_pack_buf_alloc(ioh);
    memcpy(buf->data, data, len);
    buf->len = len;

    iohandler_pkt_sendto(ioh, buf, to);
}


static void iohandler_packet_handle(struct work_struct *work)
{

}

static void iohandler_pack_queue(iohandler_t *ioh, struct iopacket *pack)
{
    ioasync_t *aio = ioh->owner;

    //   queue_work();
}

static int iohandler_read(iohandler_t* ioh)
{
    struct iopacket *pack;
    pack_buf_t*  buf;

    pack = iohandler_pack_alloc(ioh, 1);
    buf = pack->packet.buf;

    switch(ioh->type) {
        case HANDLER_TYPE_NORMAL:
            buf->len = fd_read(ioh->fd, buf->data, PACKET_MAX_PAYLOAD);
            break;
        case HANDLER_TYPE_UDP:
            {
                socklen_t addrlen = sizeof(struct sockaddr_in);
                bzero(&pack->addr, sizeof(pack->addr));
                buf->len = recvfrom(ioh->fd, &buf->data, PACKET_MAX_PAYLOAD,
                        0, &pack->addr, &addrlen);
                break;
            }
        case HANDLER_TYPE_TCP_ACCEPT:
            {
                int channel;
                channel = fd_accept(ioh->fd);
                memcpy(buf->data, &channel, sizeof(int));
                buf->len = sizeof(int);
                break;
            }
        default:
            buf->len = -1;
            break;
    }

    if(buf->len < 0)
        goto fail;

    iohandler_pack_queue(ioh, pack);
    return 0;

fail:
    iohandler_pack_free(ioh, pack, 1);
    return -EINVAL;
}

static int iohandler_write_packet(iohandler_t *ioh, struct iopacket *pkt)
{
    int len;

    switch(ioh->type) {
        case HANDLER_TYPE_NORMAL:
            {
                int out_pos = 0;
                int avail = 0;
                pack_buf_t *buf = pkt->packet.buf;

                while(out_pos < buf->len) {
                    avail = buf->len - out_pos;

                    len = fd_write(ioh->fd, (&buf->data) + out_pos, avail);
                    if(len < 0) 
                        goto fail;
                    out_pos += len;
                }
                break;
            }
        case HANDLER_TYPE_UDP:
            {
                pack_buf_t *buf = pkt->packet.buf;
                len = sendto(ioh->fd, &buf->data, buf->len, 0, 
                        &pkt->addr, sizeof(struct sockaddr));
                if(len < 0)
                    goto fail;
                break;
            }
        case HANDLER_TYPE_TCP_ACCEPT:
            BUG();
        default:
            goto fail;
    }

    return 0;

fail:
    loge("send data fail, ret=%d, droped.\n", len);
    return -EINVAL;
}


static int iohandler_write(iohandler_t *ioh) 
{
    int ret;
    struct iopacket *pack;
    ioasync_t *aio = ioh->owner;

    if(queue_count(ioh->q_out) == 0)
        return 0;

    pack = (struct iopacket *)queue_out(ioh->q_out);
    if(!pack)
        return 0;

    if(queue_count(ioh->q_out) == 0)
        poller_event_disable(&aio->poller, ioh->fd, EPOLLOUT);

    ret = iohandler_write_packet(ioh, pack);

    iohandler_pack_free(ioh, pack, 1);

    return ret;
}


static void iohandler_close(iohandler_t *ioh)
{
    ioasync_t *aio = ioh->owner;

    if(ioh->h_ops.close)
        ioh->h_ops.close(ioh->priv_data);

    pthread_mutex_lock(&aio->lock);
    list_del(&ioh->node);
    pthread_mutex_unlock(&aio->lock);

    queue_release(ioh->q_in);
    queue_release(ioh->q_out);

    if(ioh->fd > 0) {
        poller_event_del(&aio->poller, ioh->fd);
    }

    free(ioh);
}

void iohandler_shutdown(iohandler_t *ioh)
{
    int q_empty;
    ioasync_t *aio = ioh->owner;

    ioh->h_ops.close = NULL;

    q_empty = !!queue_count(ioh->q_out);

    if(q_empty) {
        iohandler_close(ioh);
    } else if(!q_empty && !ioh->closing) {
        ioh->closing = 1;

        pthread_mutex_lock(&aio->lock);
        list_del(&ioh->node);
        list_add(&ioh->node, &aio->closing_list);
        pthread_mutex_unlock(&aio->lock);

        return;
    }
}

/* iohandler file descriptor event callback for read/write ops */
static void iohandler_event(void *data, int events)
{
    iohandler_t *ioh = (iohandler_t *)data;

    /* in certain cases, it's possible to have both EPOLLIN and
     * EPOLLHUP at the same time. This indicates that there is incoming
     * data to read, but that the connection was nonetheless closed
     * by the sender. Be sure to read the data before closing
     * the receiver to avoid packet loss.
     */
    if(events & EV_READ) {
        iohandler_read(ioh);
    }

    if(events & EV_WRITE) {
        iohandler_write(ioh);
    }

    if(events & (EV_HUP|EV_ERROR)) {
        /* disconnection */
        loge("iohandler disconnect on fd %d", ioh->fd);
        iohandler_close(ioh);
        return;
    }
}

static iohandler_t *ioasync_create_context(ioasync_t *aio, int fd, int type)
{
    iohandler_t *ioh;

    ioh = malloc(sizeof(*ioh));
    if(!ioh)
        return NULL;

    ioh->fd = fd;
    ioh->type = type;
    ioh->flags = 0;
    ioh->closing = 0;

    /*XXX*/
    ioh->q_in = queue_init(0);
    ioh->q_out = queue_init(0);
    ioh->owner = aio;
    pthread_mutex_init(&ioh->lock, NULL);

    /*Add to active list*/
    pthread_mutex_lock(&aio->lock);
    list_add(&ioh->node, &aio->active_list);
    pthread_mutex_unlock(&aio->lock);

    poller_event_add(&aio->poller, fd, iohandler_event, ioh);
    poller_event_enable(&aio->poller, fd, EV_READ);
    return ioh;
}


static void iohandler_normal_post(void* priv, struct iopacket *pkt)
{
    iohandler_t *ioh = (iohandler_t *)priv;
    pack_buf_t *buf = pkt->packet.buf;

    if(!buf) {
        iohandler_pack_free(ioh, pkt, 0);
        return;
    }

    if(ioh->h_ops.handle)
        ioh->h_ops.handle(ioh->priv_data, buf->data, buf->len);

    iohandler_pack_free(ioh, pkt, 1);
}

iohandler_t *iohandler_create(ioasync_t *aio, int fd,
        void (*handle)(void *, uint8_t *, int), void (*close)(void *), void *priv)
{
    iohandler_t *ioh;

    ioh = ioasync_create_context(aio, fd, HANDLER_TYPE_NORMAL);

    ioh->h_ops.post = iohandler_normal_post;
    ioh->h_ops.handle = handle;
    ioh->h_ops.close = close;

    ioh->priv_data = priv;

    return ioh;
}


static void iohandler_accept_post(void* priv, struct iopacket *pkt)
{
    iohandler_t *ioh = (iohandler_t *)priv;
    pack_buf_t *buf = pkt->packet.buf;

    if(!buf) {
        iohandler_pack_free(ioh, pkt, 0);
        return;
    }

    if(ioh->h_ops.accept) {
        int channel;
        channel = ((int *)buf->data)[0];
        ioh->h_ops.accept(ioh->priv_data, channel);
    }

    iohandler_pack_free(ioh, pkt, 1);
}

iohandler_t *iohandler_accept_create(ioasync_t *aio, int fd,
        void (*accept)(void *, int), void (*close)(void *), void *priv)
{
    iohandler_t *ioh;

    ioh = ioasync_create_context(aio, fd, HANDLER_TYPE_TCP_ACCEPT);

    ioh->h_ops.post = iohandler_accept_post;
    ioh->h_ops.accept = accept;
    ioh->h_ops.close = close;

    ioh->priv_data = priv;

    listen(fd, 50);

    return ioh;
}

static void iohandler_udp_post(void* priv, struct iopacket *pkt)
{
    iohandler_t *ioh = (iohandler_t *)priv;
    pack_buf_t *buf = pkt->packet.buf;

    if(!buf) {
        iohandler_pack_free(ioh, pkt, 0);
        return;
    }
    if(ioh->h_ops.handlefrom)
        ioh->h_ops.handlefrom(ioh->priv_data, buf->data, buf->len, &pkt->addr);

    iohandler_pack_free(ioh, pkt, 1);
}

iohandler_t *iohandler_udp_create(ioasync_t *aio, int fd,
        void (*handlefrom)(void *, uint8_t *, int, void *),
        void (*close)(void *), void *priv)
{
    iohandler_t *ioh;

    ioh = ioasync_create_context(aio, fd, HANDLER_TYPE_UDP);

    ioh->h_ops.post = iohandler_udp_post;
    ioh->h_ops.handlefrom = handlefrom;
    ioh->h_ops.close = close;

    ioh->priv_data = priv;

    return ioh;
}


ioasync_t *ioasync_init(void)
{
    ioasync_t *aio;

    aio = malloc(sizeof(*aio));
    if(!aio)
        return NULL;

    poller_init(&aio->poller);

    aio->pkt_pool = mempool_create(sizeof(struct iopacket), 128, 0);
    aio->buf_pool = create_pack_buf_pool(PACKET_MAX_PAYLOAD, 128);

    INIT_LIST_HEAD(&aio->active_list);
    INIT_LIST_HEAD(&aio->closing_list);

    pthread_mutex_init(&aio->lock, NULL);

    aio->initialized = 1;
    return aio;
}

void ioasync_loop(ioasync_t *aio)
{
    poller_loop(&aio->poller);
}

void ioasync_release(ioasync_t *aio)
{
    aio->initialized = 0;

    poller_done(&aio->poller);

    free_pack_buf_pool(aio->buf_pool);
    mempool_release(aio->pkt_pool);

    free(aio);
}


/*****************************************************/

static ioasync_t *g_ioasync;

void global_ioasync_init(void)
{
    g_ioasync = ioasync_init();
}


ioasync_t *get_global_ioasync(void)
{
    return g_ioasync;
}
