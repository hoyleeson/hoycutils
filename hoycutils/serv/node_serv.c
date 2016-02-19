#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <common/ioasync.h>
#include <common/list.h>
#include <common/wait.h>
#include <common/sockets.h>
#include <common/log.h>
#include <common/utils.h>
#include <common/hash.h>
#include <common/packet.h>
#include <common/ethtools.h>
#include <common/pack_head.h>

#include "task.h"
#include "serv.h"

#define WORKER_MAX_TASK_COUNT 	(512)

#define HASH_WORKER_SHIFT 	    (5)
#define HASH_WORKER_CAPACITY 	(1 << HASH_WORKER_SHIFT)
#define HASH_WORKER_MASK 		(HASH_WORKER_CAPACITY - 1)

struct _node_serv;
typedef struct _node_serv  node_serv_t;


/* ns: node server */

struct _node_serv {
    iohandler_t *mgr_hand;
    int worker_count;
    struct list_head worker_list;
    int nextseq;

    int task_count;
    task_worker_t *suit_worker;
    pthread_mutex_t lock;
};

struct _task_worker {
    ioasync_t *ioasync;
    iohandler_t *hand;
    int nextseq;
    struct sockaddr addr;

    int task_count;
    struct hlist_head tasks_map[HASH_WORKER_CAPACITY]; 	/*key: task id*/
    pthread_mutex_t lock;

    node_serv_t *owner;
    struct list_head entry;
};


static node_serv_t node_serv;


task_t *create_task(int priv_size)
{
    task_t *task;

    task = malloc(sizeof(*task) + priv_size);
    return task;
}

void release_task(task_t *task) 
{
    free(task);
}

static task_t *worker_get_task_by_id(task_worker_t *worker, uint32_t taskid);

static task_t *find_node_serv_task(node_serv_t *ns, int taskid)
{
    task_t *task;
    task_worker_t *worker;

    pthread_mutex_lock(&ns->lock);
    list_for_each_entry(worker, &ns->worker_list, entry) {
        task = worker_get_task_by_id(worker, taskid);
        if(task) {
            pthread_mutex_unlock(&ns->lock);
            return task;
        }
    }

    pthread_mutex_unlock(&ns->lock);
    return NULL;
}


void *task_worker_pkt_alloc(task_t *task)
{
    task_worker_t *worker = task->worker;
    pack_buf_t *pkb;

    pkb = iohandler_pack_buf_alloc(worker->hand);

    return pkb->data + pack_head_len();
}

void *task_worker_pkt_get(task_t *task, void *data)
{
    pack_buf_t *pkb;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_pack_buf(head);
    pack_buf_get(pkb);

    return data;
}

void task_worker_pkt_free(task_t *task, void *data)
{
    pack_buf_t *pkb;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_pack_buf(head);
    pack_buf_free(pkb);
}


void task_worker_pkt_sendto(task_t *task, int type, 
        void *data, int len, struct sockaddr *to)
{
    pack_buf_t *pkb;
    pack_head_t *head;
    task_worker_t *worker = task->worker;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_pack_buf(head);

    init_pack(head, type, len);
    head->seqnum = worker->nextseq++;

    pkb->len = len + pack_head_len();
//    pkb->addr = *to;

    dump_data("task worker send data", data, len);

    iohandler_pkt_sendto(worker->hand, pkb, to);
}

#if 0
void task_worker_pkt_multicast(task_t *task, int type, 
        void *data, int len, struct sockaddr *dst_ptr, int count)
{
    pack_buf_t *pkb;
    pack_head_t *head;
    task_worker_t *worker = task->worker;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_packet(head);

    init_pack(head, type, len);
    head->seqnum = worker->nextseq++;

    pkb->len = len + pack_head_len();

    dump_data("task worker multicast data", data, len);

    ioasync_pkt_multicast(worker->hand, pkb, dst_ptr, count);
}
#endif


/*XXX*/
static int task_req_handle(task_worker_t *worker, struct pack_task_req *pack, void *from)
{
    task_t *task;
    struct task_operations *ops;

    ops = find_task_protos_by_type(pack->type);
    if(!ops)
        return -EINVAL;

    task = worker_get_task_by_id(worker, pack->taskid);
    if(!task) {
        loge("not found task by taskid:%d.\n", pack->taskid);
        return -EINVAL;
    }

    return ops->task_handle(task, pack, from);
}


static void task_worker_handle(void *opaque, uint8_t *data, int len, void *from)
{
    int ret = 0;
    task_worker_t *worker = (task_worker_t *)opaque;
    pack_head_t *head;
    void *payload;

    logd("task worker receive pack. len:%d\n", len);

    if(data == NULL || len < sizeof(*head))
        return;

    dump_data("task worker receive data", data, len);

    head = (pack_head_t *)data;
    payload = head + 1; 

    logd("pack: type:%d, seq:%d, datalen:%d\n", head->type, head->seqnum, head->datalen);

    if(head->magic != PROTOS_MAGIC ||
            head->version != PROTOS_VERSION)
        return;

    switch(head->type) {
        case MSG_TASK_REQ:
        {
            struct pack_task_req *pack = (struct pack_task_req *)payload;
            ret = task_req_handle(worker, pack, from);
            break;
        }
        default:
            logw("unknown packet(%d).\n", head->type);
            break;
    }

    if(ret) {
        logw("task worker handle fail.(%d:%d)\n", head->type, ret);
    }
}


static void task_worker_close(void *opaque)
{

}

#define DEFAULT_BUF_SIZE        (128*1024*1024)
static task_worker_t *create_task_worker(node_serv_t *ns)
{
    int i;
    int sock;
    struct sockaddr_in addr;
    socklen_t addrlen;
    task_worker_t *tworker;
    int bufsize = DEFAULT_BUF_SIZE;
    char host[20] = {0};

    /* XXX FIXME */
    get_ipaddr(NULL, host);
    sock = socket_inaddr_any_server(0, SOCK_DGRAM);

    /* get the actual port number assigned by the system */
    addrlen = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getsockname(sock, (struct sockaddr*)&addr, &addrlen) < 0) {
        close(sock);
        return NULL;
    }
    logd("assign task worker address: %s, port: %d.\n", 
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    /* XXX */
    addr.sin_addr.s_addr = inet_addr(host);

    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

    tworker = malloc(sizeof(*tworker));
    if(!tworker)
        goto out;

    tworker->addr = *((struct sockaddr *)&addr);
    tworker->task_count = 0;
    tworker->owner = ns;

    tworker->ioasync = ioasync_init();
    tworker->hand = iohandler_udp_create(tworker->ioasync, sock,
            task_worker_handle, task_worker_close, tworker);

    for (i = 0; i < HASH_WORKER_CAPACITY; i++)
        INIT_HLIST_HEAD(&tworker->tasks_map[i]);

    tworker->nextseq = 0;
    pthread_mutex_init(&tworker->lock, NULL);

    list_add_tail(&tworker->entry, &ns->worker_list);
    ns->worker_count++;
out:
    return tworker;
}

static void free_task_worker(task_worker_t *worker) 
{
    node_serv_t *ns = worker->owner;

    list_del(&worker->entry);
    ns->worker_count--;

    if(ns->suit_worker == worker)
        ns->suit_worker = NULL;

    iohandler_shutdown(worker->hand);
    ioasync_release(worker->ioasync);
    free(worker);
}

static void *node_serv_pkt_alloc(node_serv_t *ns)
{
    pack_buf_t *pkb;

    pkb = iohandler_pack_buf_alloc(ns->mgr_hand);

    return pkb->data + pack_head_len();

}

static void node_serv_pkt_send(node_serv_t *ns, int type, void *data, int len)
{
    pack_buf_t *pkb;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_pack_buf(head);

    init_pack(head, type, len);
    head->seqnum = ns->nextseq++;

    pkb->len = len + pack_head_len();

    iohandler_pkt_send(ns->mgr_hand, pkb);
}


static task_t *node_serv_task_assign(struct pack_task_assign *pt)
{
    task_t *task;
    struct task_operations *ops;

    ops = find_task_protos_by_type(pt->type);
    if(!ops)
        return NULL;

    task = ops->assign_handle(pt);

    task->taskid = pt->taskid;
    task->type = pt->type;
    task->priority = pt->priority;

    task->ops = ops;

    return task;
}

static inline int node_serv_task_reclaim(task_t *task, struct pack_task_reclaim *pt)
{
    struct task_operations *ops = task->ops;

    if(ops->reclaim_handle)
        return ops->reclaim_handle(task, pt);

    return -EINVAL;
}

static inline int node_serv_task_control(task_t *task, int opt, struct pack_task_control *pt)
{
    struct task_operations *ops = task->ops;

    if(ops->control_handle)
        return ops->control_handle(task, opt, pt);

    return -EINVAL;
}


static inline int init_task_assign_response_pkt(task_t *task,
        struct pack_task_assign_response *pkt)
{
    struct task_operations *ops = task->ops;

    if(ops->init_assign_response_pkt)
        return ops->init_assign_response_pkt(task, pkt);

    return default_init_assign_response_pkt(task, pkt);
}


static int task_assign_response(node_serv_t *ns, task_t *task)
{
    int len;
    struct pack_task_assign_response *pkt;
    task_worker_t *worker = task->worker;

    pkt = (struct pack_task_assign_response *)node_serv_pkt_alloc(ns);

    len = init_task_assign_response_pkt(task, pkt);

    pkt->taskid = task->taskid;
    pkt->type = task->type;
    pkt->addr = worker->addr;

    node_serv_pkt_send(ns, MSG_TASK_ASSIGN_RESPONSE, pkt, len);
    return 0;
}

static void worker_add_task(task_worker_t *worker, task_t *task)
{
    uint32_t key;

    key = hash_32(task->taskid, HASH_WORKER_SHIFT);

    pthread_mutex_lock(&worker->lock);
    worker->task_count++;
    task->worker = worker;

    logd("worker add task. taskid:%d\n", task->taskid);
    hlist_add_head(&task->hentry, &worker->tasks_map[key]);

    pthread_mutex_unlock(&worker->lock);
}

static task_t *worker_get_task_by_id(task_worker_t *worker, uint32_t taskid)
{
    task_t *task;
    uint32_t key;
    struct hlist_node *tmp;

    key = hash_32(taskid, HASH_WORKER_SHIFT);

    pthread_mutex_lock(&worker->lock);

    hlist_for_each_entry(task, tmp, &worker->tasks_map[key], hentry) {
        if(task->taskid == taskid) {
            pthread_mutex_unlock(&worker->lock);
            return task;
        }
    }

    pthread_mutex_unlock(&worker->lock);
    return NULL;
}


static void worker_remove_task(task_worker_t *worker, task_t *task)
{
    pthread_mutex_lock(&worker->lock);

    hlist_del_init(&task->hentry);
    worker->task_count--;

    /*XXX*/
    if(worker->task_count == 0)
        free_task_worker(worker);

    pthread_mutex_unlock(&worker->lock);
}

static int node_serv_task_register(node_serv_t *ns, task_t *task)
{
    int ret = 0;
    task_worker_t *pos, *worker = NULL;
    int count = WORKER_MAX_TASK_COUNT;

    pthread_mutex_lock(&ns->lock);
    worker = ns->suit_worker;
    if(worker && worker->task_count < WORKER_MAX_TASK_COUNT)
        goto found;

    /* slow path */
    list_for_each_entry(pos, &ns->worker_list, entry) {
        if(count < pos->task_count) {
            count = pos->task_count;
            worker = pos;
        }
    }

    if(count != WORKER_MAX_TASK_COUNT) {
        goto found;
    } else {
        worker = create_task_worker(ns);
        if(!worker) {
            ret = -EINVAL;
            goto out;
        }
    }

found:
    ns->task_count++;
    worker_add_task(worker, task);

    ns->suit_worker = worker;	
out:
    pthread_mutex_unlock(&ns->lock);
    return ret;
}

static void node_serv_task_unregister(node_serv_t *ns, task_t *task)
{
    task_worker_t *worker = task->worker;

    pthread_mutex_lock(&ns->lock);
    worker_remove_task(worker, task);
    ns->task_count--;
    task->worker = NULL;

    pthread_mutex_unlock(&ns->lock);
}


static void node_serv_handle(void *opaque, uint8_t *data, int len)
{
    int ret = 0;
    node_serv_t *ns = (node_serv_t *)opaque;
    pack_head_t *head;
    task_t *task;
    void *payload;

    logd("node server receive pack. len:%d\n", len);

    if(data == NULL || len < sizeof(*head))
        return;

    dump_data("node server receive data", data, len);

    head = (pack_head_t *)data;
    payload = head + 1; 

    logd("pack: type:%d, seq:%d, datalen:%d\n", head->type, head->seqnum, head->datalen);

    if(head->magic != PROTOS_MAGIC ||
            head->version != PROTOS_VERSION)
        return;

    switch(head->type) {
        case MSG_TASK_ASSIGN:
        {
            struct pack_task_assign *pt = (struct pack_task_assign *)payload;
            task = node_serv_task_assign(pt);
            node_serv_task_register(ns, task);

            ret = task_assign_response(ns, task);
            break;
        }
        case MSG_TASK_RECLAIM:
        {
            struct pack_task_reclaim *pt = (struct pack_task_reclaim *)payload;
            task = find_node_serv_task(ns, pt->taskid);

            node_serv_task_unregister(ns, task);
            ret = node_serv_task_reclaim(task, pt);
            break;
        }
        case MSG_TASK_CONTROL:
        {
            struct pack_task_control *pt = (struct pack_task_control *)payload;
            task = find_node_serv_task(ns, pt->taskid);

            ret = node_serv_task_control(task, pt->opt, pt);
            break;
        }
        default:
            logw("unknown packet(%d).\n", head->type);
            break;
    }
    if(ret) {
        logw("node server handle fail.(%d:%d)\n", head->type, ret);
    }
}


static void node_serv_close(void *opaque)
{

}


int node_serv_init(const char *host)
{
    int socket;
    node_serv_t *ns = &node_serv;

    logi("node server start. host:%s\n", host);
    socket = socket_network_client(host, NODE_SERV_LOGIN_PORT, SOCK_STREAM);
    if(socket < 0) {
        loge("connect to server fail.\n");
        return -EINVAL;
    }

    ns->mgr_hand = iohandler_create(get_global_ioasync(), socket,
            node_serv_handle, node_serv_close, ns);

    ns->task_count = 0;
    ns->nextseq = 0;
    ns->worker_count = 0;
    INIT_LIST_HEAD(&ns->worker_list);
    ns->suit_worker = NULL;
    pthread_mutex_init(&ns->lock, NULL);

    return 0;
}

