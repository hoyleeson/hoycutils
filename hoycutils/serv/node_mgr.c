#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h>

#include <common/log.h>
#include <common/packet.h>
#include <common/pack_head.h>
#include <common/sockets.h>

#include "node_mgr.h"
#include "task.h"

static int node_register(node_mgr_t *mgr, node_info_t *node)
{
    logd("node server register success.\n");
    pthread_mutex_lock(&mgr->lock);
    list_add_tail(&node->entry, &mgr->nodelist);
    mgr->node_count++;
    pthread_mutex_unlock(&mgr->lock);
    return 0;
}

static void node_unregister(node_mgr_t *mgr, node_info_t *node)
{
    logd("node server unregister success.\n");
    pthread_mutex_lock(&mgr->lock);
    list_del(&node->entry);
    mgr->node_count--;
    pthread_mutex_unlock(&mgr->lock);
}

static void *nodemgr_task_pkt_alloc(node_info_t *node)
{
    pack_buf_t *pkb;
    pkb = iohandler_pack_buf_alloc(node->hand);

    return pkb->data + pack_head_len();

}

static void nodemgr_task_pkt_send(node_info_t *node, int type, void *data, int len)
{
    pack_buf_t *pkb;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_pack_buf(head);

    init_pack(head, type, len);
    head->seqnum = node->nextseq++;

    pkb->len = len + pack_head_len();

    iohandler_pkt_send(node->hand, pkb);
}

#if 0
static void copy_sockaddr(void *dst, void *src)
{
    *(struct sockaddr *)dst = *(struct sockaddr *)src;
}
#endif

static void node_hand_fn(void* opaque, uint8_t *data, int len)
{
    node_info_t *node = (node_info_t *)opaque;
    pack_head_t *head;
    void *payload;

    logd("node manager receive pack. len:%d\n", len);

    if(data == NULL || len < sizeof(*head))
        return;

    dump_data("node manager receive data", data, len);

    head = (pack_head_t *)data;
    payload = head + 1; 

    logd("pack: type:%d, seq:%d, datalen:%d\n", head->type, head->seqnum, head->datalen);

    if(head->magic != PROTOS_MAGIC ||
            head->version != PROTOS_VERSION)
        return;

    switch(head->type) {
        case MSG_TASK_ASSIGN_RESPONSE:
        {
            struct pack_task_assign_response *pt;

            pt = (struct pack_task_assign_response *)payload;
            response_post(&node->waits, MSG_TASK_ASSIGN_RESPONSE, pt->taskid, &pt->addr);
#if 1
            struct sockaddr_in *addr = (struct sockaddr_in *)&pt->addr;
            logd("response :task worker address: %s, port: %d.\n", 
                    inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
#endif
            break;
        }
        default:
            break;
    }
}

static void node_close_fn(void *user)
{
    node_info_t *node = (node_info_t *)user;
    node_unregister(node->mgr, node);
}


static inline int calc_node_priority(int old_prio, int regulate)
{
    return old_prio + regulate;	
}

static inline int calc_node_weight(node_info_t *n)
{
    if(!n)
        return 0x7fffffff;

    /* ingore priority */
    return n->task_count;
}

static int node_weight_compare(node_info_t *n1, node_info_t *n2)
{
    int n1_weight = calc_node_weight(n1);
    int n2_weight = calc_node_weight(n2);

    return n1_weight - n2_weight;
}

static node_info_t *nodemgr_choice_node(node_mgr_t *mgr, int priority)
{
    node_info_t *p, *node = NULL;

    list_for_each_entry(p, &mgr->nodelist, entry) {
        if(node_weight_compare(node, p)) {
            node = p;
        }
    }

    return node;
}


static int init_task_assign_pkt(task_handle_t *task, task_baseinfo_t *base,
        struct pack_task_assign *pkt)
{
    struct task_operations *ops = task->ops;

    if(ops->init_assign_pkt)
        return ops->init_assign_pkt(base, pkt);

    return default_init_assign_pkt(base, pkt);
}

static uint32_t alloc_taskid(node_mgr_t *mgr)
{
    uint32_t id;

    pthread_mutex_lock(&mgr->lock);
    id = ++mgr->taskids;
    pthread_mutex_unlock(&mgr->lock);

    return id;
}

static void node_register_task(node_info_t *node, task_handle_t *task)
{
    pthread_mutex_lock(&node->lock);

    node->task_count++;
    node->priority = calc_node_priority(node->priority, task->priority);
    list_add_tail(&task->entry, &node->tasklist);

    pthread_mutex_unlock(&node->lock);
}


static void node_unregister_task(node_info_t *node, task_handle_t *task)
{
    pthread_mutex_lock(&node->lock);

    node->task_count--;
    node->priority = calc_node_priority(node->priority, -task->priority);
    list_del(&task->entry);

    pthread_mutex_unlock(&node->lock);
}

task_handle_t *nodemgr_task_assign(node_mgr_t *mgr, int type, int priority,
        task_baseinfo_t *base)
{
    int len;
    node_info_t *node;
    struct pack_task_assign *pkt;
    task_handle_t *task;

    if(!mgr)
        return NULL;

    task = malloc(sizeof(*task));
    if(!task)
        return NULL;

    node = nodemgr_choice_node(mgr, priority);

    task->node = node;
    task->taskid = alloc_taskid(mgr);
    task->type = type;
    task->priority = priority;
    task->ops = find_task_protos_by_type(type);
    if(!task->ops) {
        logi("not found task protocol by type:%d.\n", type);
        goto fail;
    }

    node_register_task(node, task);

    pkt = (struct pack_task_assign *)nodemgr_task_pkt_alloc(node);

    len = init_task_assign_pkt(task, base, pkt);

    pkt->type = task->type;
    pkt->taskid = task->taskid;
    pkt->priority = task->priority;

    nodemgr_task_pkt_send(node, MSG_TASK_ASSIGN, pkt, len);

    /* get node server port by assign request. */
    wait_for_response(&node->waits, MSG_TASK_ASSIGN_RESPONSE, task->taskid, &task->addr);
    logd("task worker address: %s, port: %d.\n", 
            inet_ntoa(task->addr.sin_addr), ntohs(task->addr.sin_port));

    return task;

fail:
    free(task);
    return NULL;
}


static int init_task_reclaim_pkt(task_handle_t *task, task_baseinfo_t *base,
        struct pack_task_reclaim *pkt)
{
    struct task_operations *ops = task->ops;

    if(ops->init_reclaim_pkt)
        return ops->init_reclaim_pkt(base, pkt);

    return default_init_reclaim_pkt(base, pkt);
}


int nodemgr_task_reclaim(node_mgr_t *mgr, task_handle_t *task,
        task_baseinfo_t *base)
{
    int len;
    node_info_t *node;
    struct pack_task_reclaim *pkt;

    if(!mgr)
        return -EINVAL;

    node = task->node;

    node_unregister_task(node, task);

    pkt = (struct pack_task_reclaim *)nodemgr_task_pkt_alloc(node);

    len = init_task_reclaim_pkt(task, base, pkt);

    pkt->taskid = task->taskid;
    pkt->type = task->type;

    nodemgr_task_pkt_send(node, MSG_TASK_RECLAIM, pkt, len);
    return 0;
}


static int init_task_control_pkt(task_handle_t *task, task_baseinfo_t *base,
        struct pack_task_control *pkt)
{
    struct task_operations *ops = task->ops;

    if(ops->init_control_pkt)
        return ops->init_control_pkt(base, pkt);

    return default_init_control_pkt(base, pkt);
}


int nodemgr_task_control(node_mgr_t *mgr, task_handle_t *task,
        int opt, task_baseinfo_t *base)
{
    int len;
    node_info_t *node;
    struct pack_task_control *pkt;

    if(!task || !mgr)
        return -EINVAL;

    node = task->node;

    pkt = (struct pack_task_control *)nodemgr_task_pkt_alloc(node);

    len = init_task_control_pkt(task, base, pkt);

    pkt->taskid = task->taskid;
    pkt->type = task->type;
    pkt->opt = opt;

    nodemgr_task_pkt_send(node, MSG_TASK_CONTROL, pkt, len);
    return 0;
}

static void nodemgr_accept_fn(void* user, int acceptfd)
{
    node_info_t *node;
    socklen_t addrlen;
    node_mgr_t *mgr = (node_mgr_t *)user;

    logd("accept node server connect.\n");

    node = malloc(sizeof(*node));
    if(!node)
        return;

    node->fd = acceptfd;
    node->mgr = mgr;
    node->hand = iohandler_create(get_global_ioasync(), acceptfd,
            node_hand_fn, node_close_fn, node);

    node->nextseq = 0;
    node->task_count = 0;
    INIT_LIST_HEAD(&node->tasklist);

    addrlen = sizeof(node->addr);
    if (getsockname(acceptfd, (struct sockaddr*)&node->addr, &addrlen) < 0) {
        close(acceptfd);
        goto fail;
    }

    iowait_init(&node->waits);
    node_register(mgr, node);
    return;

fail:
    free(node);
}


static void nodemgr_close_fn(void *user)
{
}


node_mgr_t *node_mgr_init(void)
{
    int sock;
    node_mgr_t *nodemgr;

    nodemgr = malloc(sizeof(*nodemgr));
    if(!nodemgr)
        return NULL;

    sock = socket_inaddr_any_server(NODE_SERV_LOGIN_PORT, SOCK_STREAM);
    nodemgr->hand = iohandler_accept_create(get_global_ioasync(), sock,
            nodemgr_accept_fn, nodemgr_close_fn, nodemgr);

    INIT_LIST_HEAD(&nodemgr->nodelist);
    pthread_mutex_init(&nodemgr->lock, NULL);

    return nodemgr;
}

