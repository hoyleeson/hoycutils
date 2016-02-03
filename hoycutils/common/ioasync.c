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

#include <common/poller.h>
#include <common/queue.h>
#include <common/ioasync.h>
#include <common/workqueue.h>


struct iopacket {
    struct packet packet;
    struct sockaddr_in addr;
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
    pthread_cond_t cond;
};


struct handle_ops {
    void (*post)(void* priv, struct packet *pkt);
    void (*accept)(void* priv, int acceptfd);
    void (*handle)(void* priv, uint8_t *data, int len);
    void (*handlefrom)(void* priv, uint8_t *data, int len, void *from);
    void (*close)(void* priv);
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

static struct iopacket *iohandler_pack_alloc(iohandler_t *ioh)
{
    struct iopacket *pkt;
    ioasync_t *aio = ioh->owner;

    pkt = (struct iopacket *)mempool_alloc(&aio->pkt_pool);

    return pkt;
}

static void iohandler_pack_free(iohandler_t *ioh, struct iopacket *pkt)
{
    ioasync_t *aio = ioh->owner;

    mempool_free(&aio->pkt_pool, pkt);
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

void iohandler_send(iohandler_t *ioh, const uint8_t *data, int len)
{
    pack_buf_t *buf;

    buf = iohandler_pack_buf_alloc(ioh);
    memcpy(buf->data, data, len);
    buf->len = len;

    iohandler_pkt_send(ioh, buf);
}

void iohandler_sendto(iohandler_t *ioh, const uint8_t *data, int len, void *to)
{
    pack_buf_t *buf;

    buf = iohandler_pack_buf_alloc(ioh);
    memcpy(buf->data, data, len);
    buf->len = len;

    iohandler_pkt_sendto(ioh, buf, to);
}


void iohandler_pkt_send(iohandler_t *ioh, pack_buf_t *buf)
{
    struct iopacket *pack;
    
    pack = iohandler_pack_alloc(ioh);
    pack->packet.buf = buf;
    queue_in(ioh->q_in, (struct packet *)pack);
}

void iohandler_pkt_sendto(iohandler_t *ioh, pack_buf_t *buf, struct sockaddr *to)
{
    struct iopacket *pack;
    
    pack = iohandler_pack_alloc(ioh);
    pack->packet.buf = buf;
    pack->addr = (struct sockaddr_in *)to;
    queue_in(ioh->q_in, (struct packet *)pack);
}

static void iohandler_packet_handle(work_struct *work)
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

    pack = iohandler_pack_alloc(ioh);
    buf = iohandler_pack_buf_alloc(ioh);
    pack->buf = buf;

    switch(ioh->type) {
        case HANDLER_TYPE_NORMAL:
            buf->len = fd_read(ioh->fd, buf->data, MAX_PAYLOAD);
            break;
        case HANDLER_TYPE_UDP:
        {
            socklen_t addrlen = sizeof(struct sockaddr_in);
            bzero(&pack->addr, sizeof(pack->addr));
            buf->len = recvfrom(ioh->fd, &buf->data, MAX_PAYLOAD, 0, &pack->addr, &addrlen);
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
    iohandler_pack_buf_free(buf);
    iohandler_pack_free(pack);
    return -EINVAL;
}

static int iohandler_write_packet(iohandler_t *ioh, struct iopacket *p)
{
    int len;

    switch(ioh->type) {
        case HANDLER_TYPE_NORMAL:
        {
            int out_pos = 0;
            int avail = 0;
            pack_buf_t *buf = p->packet.buf;

            while(out_pos < p->len) {
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
            pack_buf_t *buf = p->packet.buf;
            len = sendto(ioh->fd, &buf->data, buf->len, 0, 
                    &p->addr, sizeof(struct sockaddr));
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
    struct iopacket *pack;
    int ret;

    if(queue_count(ioh->q_out) == 0)
        return 0;

    pack = (struct iopacket *)queue_out(ioh->q_out);
    if(!pack)
        return 0;

    if(queue_count(ioh->q_out) == 0)
        poller_event_disable(ioh->owner->poller, ioh->fd, EPOLLOUT);

    ret = iohandler_write_packet(ioh, pack);
    /*XXX*/
    ioasync_pkt_free(p);

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
        poller_event_del(aio->poller, ioh->fd);
    }

    free(ioh);
}

void iohandler_shutdown(iohandler_t *ioh)
{
    int q_empty;
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
        loge("%s: disconnect on fd %d", __func__, ioh->fd);
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

    ioh->q_in = queue_init(0);
    ioh->q_out = queue_init(0);
    ioh->owner = aio;
    pthread_mutex_init(&ioh->lock);

    pthread_mutex_lock(&aio->lock);
    list_add(&ioh->node, &aio->active_list);
    pthread_mutex_unlock(&aio->lock);

    poller_event_add(&aio->poller, fd, iohandler_event, ioh);
    poller_event_enable(&aio->poller, fd, EV_READ);
    return ioh;
}


static void normal_post_func(void* priv, struct iopacket *pkt)
{
    iohandler_t *ioh = (iohandler_t *)priv;
    pack_buf_t *buf = pkt->packet.buf;

    if(!buf)
        goto out;

    if(ioh->h_ops.handle)
        ioh->h_ops.handle(ioh->priv_data, buf->data, buf->len);

    iohandler_pack_buf_free(buf);
out:
    iohandler_pack_free(ioh, pkt);
}

iohandler_t *iohandler_create(ioasync_t *aio, int fd,
        handle_func hand_fn, close_func close_fn, void *priv)
{
    iohandler_t *ioh;

    ioh = ioasync_create_context(aio, fd, HANDLER_TYPE_NORMAL);

    ioh->h_ops.post = normal_post_func;
    ioh->h_ops.handle = hand_fn;
    ioh->h_ops.close = close_fn;

    ioh->priv_data = priv;

    return ioh;
}


static void accept_post_func(void* priv, struct iopacket *pkt)
{
    iohandler_t *ioh = (iohandler_t *)priv;
    pack_buf_t *buf = pkt->packet.buf;
    if(!buf)
        goto out;

    if(ioh->h_ops.accept) {
        int channel;
        channel = ((int *)buf->data)[0];
        ioh->h_ops.accept(ioh->priv_data, channel);
    }

    iohandler_pack_buf_free(buf);
out:
    iohandler_pack_free(ioh, pkt);
}

iohandler_t *iohandler_accept_create(ioasync_t *aio, int fd,
        accept_func accept_fn, close_func close_fn, void *priv)
{
    iohandler_t *ioh;

    ioh = ioasync_create_context(aio, fd, HANDLER_TYPE_TCP_ACCEPT);

    ioh->h_ops.post = accept_post_func;
    ioh->h_ops.accept = accept_fn;
    ioh->h_ops.close = close_fn;

    ioh->priv_data = priv;

    listen(fd, 50);

    return ioh;
}

static void udp_post_func(void* priv, struct iopacket *pkt)
{
    iohandler_t *ioh = (iohandler_t *)priv;
    pack_buf_t *buf = pkt->packet.buf;

    if(!buf)
        goto out;

    if(ioh->h_ops.handlefrom)
        ioh->h_ops.handlefrom(ioh->priv_data, buf->data, buf->len, &pkt->addr);

    iohandler_pack_buf_free(buf);
out:
    iohandler_pack_free(ioh, pkt);
}

iohandler_t *iohandler_udp_create(ioasync_t *aio, int fd,
        handlefrom_func hand_fn, close_func close_fn, void *priv)
{
    iohandler_t *ioh;

    ioh = ioasync_create_context(aio, fd, HANDLER_TYPE_UDP);

    ioh->h_ops.post = udp_post_func;
    ioh->h_ops.handlefrom = hand_fn;
    ioh->h_ops.close = close_fn;

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

    aio->initialized = 1;

    INIT_LIST_HEAD(&aio->active_list);
    INIT_LIST_HEAD(&aio->closing_list);

    return aio;
}

void ioasync_loop(ioasync_t *aio)
{
    poller_loop(&aio->poller);
}

void ioasync_done(ioasync_t *aio)
{
    poller_done(&aio->poller);
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
