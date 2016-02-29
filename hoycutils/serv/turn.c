#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <pthread.h>
#include <errno.h>

#include <common/log.h>
#include <common/packet.h>
#include <common/pack_head.h>
#include <common/iowait.h>
#include <arpa/inet.h>

#include <protos.h>
#include "turn.h"
#include "task.h"
#include "protos_internal.h"
#include "node_mgr.h"
#include "cli_mgr.h"

struct turn_assign_data {
    task_baseinfo_t base;
    group_info_t *group;
};

struct turn_control_data {
    task_baseinfo_t base;
    user_info_t *user;
};


unsigned long turn_task_assign(node_mgr_t *mgr, group_info_t *group)
{
    task_handle_t *task;
    struct turn_assign_data data;

    init_taskbase_info(&data.base);
    data.group = group;

    task = nodemgr_task_assign(mgr, TASK_TURN, TASK_PRIORITY_NORMAL, &data.base);

    return (unsigned long)task;
}

int turn_task_reclaim(node_mgr_t *mgr, unsigned long handle)
{
    return nodemgr_task_reclaim(mgr, (task_handle_t *)handle, NULL);
}

int turn_task_control(node_mgr_t *mgr, unsigned long handle, int opt, user_info_t *user)
{
    struct turn_control_data data;

    data.user = user;
    return nodemgr_task_control(mgr, (task_handle_t *)handle, opt, &data.base);
}

int get_turn_info(node_mgr_t *mgr, unsigned long handle, struct turn_info *info)
{
    task_handle_t *task = (task_handle_t *)handle;

    info->taskid = task->taskid;
    info->addr = task->addr;

    return 0;
}

static int init_turn_task_assign(task_baseinfo_t *base, 
        struct pack_task_assign *pkt)
{
    int i = 0;
    int len;
    user_info_t *user;
    struct pack_turn_assign *ta;
    struct turn_assign_data *turn = (struct turn_assign_data *)base;
    group_info_t *group = turn->group;

    if(!group)
        return -EINVAL;

    ta = (struct pack_turn_assign *)pkt;
    len = sizeof(*ta) + sizeof(client_tuple_t)*group->users;

    ta->groupid = group->groupid;
    ta->cli_count = group->users;

    /* Usually, Only creater. */
    list_for_each_entry(user, &group->userlist, entry) {
        if(i >= ta->cli_count)
            fatal("group count bug.\n");

        ta->tuple[i].userid = user->userid;
        ta->tuple[i].addr = user->addr;
        i++;
    }

    return len;	
}


static int init_turn_task_reclaim(task_baseinfo_t *base, 
        struct pack_task_reclaim *pkt)
{
    int len;

    len = sizeof(struct pack_turn_reclaim);
    return len;	
}


static int init_turn_task_control(task_baseinfo_t *base,
        struct pack_task_control *pkt)
{
    int len;
    user_info_t *user;
    struct pack_turn_control *tc;
    struct turn_control_data *data = (struct turn_control_data *)base;

    user = data->user;

    tc = (struct pack_turn_control *)pkt;
    len = sizeof(*tc);

    tc->tuple.userid = user->userid;
    tc->tuple.addr = user->addr;

    return len;
}


/*************************************************************/

enum cli_state {
    STATE_INVAILD,
    STATE_PENDING,
    STATE_RUNNING,
};

struct cli {
    uint32_t userid;
    struct sockaddr_in addr;
    uint8_t state;
};

struct turn_task {
    uint32_t groupid;
    int cli_count;
    struct cli cli[GROUP_MAX_USER];
};

static task_t *turn_task_assign_handle(struct pack_task_assign *pkt)
{
    int i;
    struct pack_turn_assign *ta;
    task_t *task;
    struct turn_task *ttask;

    ta = (struct pack_turn_assign *)pkt;

    task = create_task(sizeof(*ttask));
    ttask = (struct turn_task *)&task->priv_data;
    ttask->groupid = ta->groupid;
    ttask->cli_count = ta->cli_count;

    for(i=0; i<ta->cli_count; i++) {
        ttask->cli[i].userid = ta->tuple[i].userid;
        ttask->cli[i].addr = *((struct sockaddr_in *)&ta->tuple[i].addr);
        ttask->cli[i].state = STATE_PENDING;
    }

    return task;
}


static int turn_task_reclaim_handle(task_t *task, struct pack_task_reclaim *pkt)
{
    /*	struct pack_turn_reclaim *tr;

        tr = (struct pack_turn_reclaim *)pkt;
        */
    release_task(task);
    return 0;
}

static int turn_task_control_handle(task_t *task, int opt, struct pack_task_control *pkt)
{
    int i;
    struct pack_turn_control *tc;
    struct turn_task *ttask;

    tc = (struct pack_turn_control *)pkt;
    ttask = (struct turn_task *)&task->priv_data;

    switch(opt) {
        case TURN_TYPE_USER_JOIN:
            if(ttask->cli_count >= GROUP_MAX_USER)
                return -EINVAL;

            ttask->cli[ttask->cli_count].userid = tc->tuple.userid;
            ttask->cli[ttask->cli_count].addr = *((struct sockaddr_in *)&tc->tuple.addr);
            ttask->cli[ttask->cli_count].state = STATE_PENDING;
            ttask->cli_count++;
            break;
        case TURN_TYPE_USER_LEAVE:
            for(i=0; i<ttask->cli_count; i++) {
                if(ttask->cli[i].userid == tc->tuple.userid) {
                    ttask->cli[i] = ttask->cli[--ttask->cli_count];
                    break;
                }
            }
            break;
        default:
            break;
    }

    return 0;
}

/*
   int init_turn_task_assign_response(task_t *task, struct pack_task_base *pkt)
   {
   struct pack_turn_assign_response *response;
   int len;

   len = sizeof(*ta) + sizeof(client_tuple_t)*turn->cli_count;
 *pkt = (struct pack_task_base *)malloc(len);

 response = *pkt;

 }
 */


static int turn_task_handle(task_t *task, struct pack_task_req *pack, void *from)
{
    int i;
    void *data;
    struct turn_task *ttask;

    ttask = (struct turn_task *)&task->priv_data;

    /* FIXME:XXX */
    data = task_worker_pkt_alloc(task); 
    memcpy(data, pack->data, pack->datalen);

    for(i=0; i<ttask->cli_count; i++) {
//        struct sockaddr_in *addr;

        if(pack->userid == ttask->cli[i].userid) {
            if(ttask->cli[i].state == STATE_PENDING) {
                struct sockaddr_in *address = (struct sockaddr_in *)from;

                ttask->cli[i].state = STATE_RUNNING;
                ttask->cli[i].addr = *address;
                logi("client %d turn state change to running. addr:%s, port:%d\n", 
                        pack->userid, inet_ntoa(address->sin_addr), ntohs(address->sin_port));
            }
            continue;
        }

        if(ttask->cli[i].state != STATE_RUNNING)
            continue;

       task_worker_pkt_get(task, data);
       task_worker_pkt_sendto(task, MSG_TURN_PACK, 
            data, pack->datalen, (struct sockaddr *)&ttask->cli[i].addr);

/*
 *     logd("[%d] turn pack to user:%d, addr:%s, port:%d\n", i, ttask->cli[i].userid, 
 *               inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
 */
    }
    task_worker_pkt_free(task, data);

    return 0;
}

struct task_operations turn_ops = {
    .type = TASK_TURN,

    /* used by node manager only */
    .init_assign_pkt = init_turn_task_assign,
    .init_reclaim_pkt = init_turn_task_reclaim,
    .init_control_pkt = init_turn_task_control,

    /* used by node server only */
    .assign_handle = turn_task_assign_handle,
    .reclaim_handle = turn_task_reclaim_handle,
    .control_handle = turn_task_control_handle,

    //	.init_assign_response_pkt = init_turn_task_assign_response,

    .task_handle = turn_task_handle,
};

int turn_init(void) 
{
    task_protos_register(&turn_ops);
    return 0;
}

int turn_release(void) 
{
    task_protos_unregister(&turn_ops);
    return 0;
}


