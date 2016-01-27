#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <pthread.h>
#include <errno.h>

#include <common/log.h>
#include <common/pack.h>
#include <common/wait.h>
#include <common/sockets.h>
#include <common/iohandler.h>
#include <arpa/inet.h>

#include "node_mgr.h"
#include "turn.h"
#include "cli_mgr.h"

static int create_cli_mgr_channel(void)
{
    return socket_inaddr_any_server(CLIENT_LOGIN_PORT, SOCK_DGRAM);
}


static void *client_pkt_alloc(cli_mgr_t *cm)
{
    packet_t *packet;

    packet = ioasync_pkt_alloc(cm->hand);

    return packet->data + pack_head_len();

}

static void client_pkt_sendto(cli_mgr_t *cm, int type, 
        void *data, int len, struct sockaddr *to)
{
    packet_t *packet;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    packet = data_to_packet(head);

    /* init header */
    init_pack(head, type, len);
    head->seqnum = cm->nextseq++;

    packet->len = len + pack_head_len();
//    packet->addr = *to;

    dump_data("client mgr send data", packet->data, packet->len);
    ioasync_pkt_sendto(cm->hand, packet, to);
}


static void cli_mgr_send_ack(cli_mgr_t *cm, pack_head_t *head)
{

}

static inline int cli_mgr_alloc_uid(cli_mgr_t *cm)
{
    return cm->uid_pool++;
}

static inline int cli_mgr_alloc_gid(cli_mgr_t *cm)
{
    return cm->gid_pool++;
}

static int cli_ack_handle(cli_mgr_t *cm, uint16_t seqnum)
{
    return 0;
}

static void cli_mgr_add_user(cli_mgr_t *cm, user_info_t *user) 
{
    pthread_mutex_lock(&cm->lock);
    hashmapPut(cm->user_map, (void *)user->userid, user);
    cm->user_count++;
    pthread_mutex_unlock(&cm->lock);
}

static user_info_t *cli_mgr_del_user(cli_mgr_t *cm, uint32_t userid)
{
    user_info_t *user;

    pthread_mutex_lock(&cm->lock);

    user = hashmapRemove(cm->user_map, (void *)userid);
    if(!user) {
        pthread_mutex_unlock(&cm->lock);
        return NULL;
    }

    cm->user_count--;

    pthread_mutex_unlock(&cm->lock);
    return user;
}

static user_info_t *cli_mgr_get_user(cli_mgr_t *cm, uint32_t userid)
{
    user_info_t *user;

    pthread_mutex_lock(&cm->lock);
    user = hashmapGet(cm->user_map, (void*)userid);
    pthread_mutex_unlock(&cm->lock);
    return user;
}


static void cli_mgr_add_group(cli_mgr_t *cm, group_info_t *group) 
{
    pthread_mutex_lock(&cm->lock);
    hashmapPut(cm->group_map, (void *)group->groupid, group);
    cm->group_count++;
    pthread_mutex_unlock(&cm->lock);
}

static group_info_t *cli_mgr_del_group(cli_mgr_t *cm, uint32_t groupid)
{
    group_info_t *group;

    pthread_mutex_lock(&cm->lock);
    group = hashmapRemove(cm->group_map, (void *)groupid);
    if(!group) {
        pthread_mutex_unlock(&cm->lock);
        return NULL;
    }

    cm->group_count--;
    pthread_mutex_unlock(&cm->lock);

    return group;
}

#if 0
static user_info_t *cli_mgr_get_group(uint32_t groupid)
{
    user_info_t *user;

    pthread_mutex_lock(&cm->lock);
    user = hashmapGet(cm->user_map, (void*)userid);
    pthread_mutex_unlock(&cm->lock);
    return user;
}
#endif

static void login_result_response(cli_mgr_t *cm, 
        user_info_t *uinfo, struct sockaddr *to)
{
    uint32_t *userid;

    userid = (uint32_t *)client_pkt_alloc(cm);
    *userid = uinfo->userid;

    client_pkt_sendto(cm, MSG_LOGIN_RESPONSE, userid, sizeof(uint32_t), to);
}

static int cmd_login_handle(cli_mgr_t *cm, struct sockaddr *from)
{
    user_info_t *uinfo;
    struct sockaddr_in *addr = (struct sockaddr_in *)from;

    uinfo = malloc(sizeof(*uinfo));
    if(!uinfo)
        return -EINVAL;

    uinfo->userid = cli_mgr_alloc_uid(cm);
    uinfo->addr = *from;
    uinfo->group = NULL;
    uinfo->state = 0;
    uinfo->mgr = cm;
    hbeat_add_to_god(&cm->hbeat_god, &uinfo->hbeat);

    cli_mgr_add_user(cm, uinfo);

    logi("user login. from %s, %d, alloc userid:%u.\n", 
            inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), uinfo->userid);

    login_result_response(cm, uinfo, from);

    return 0;
}

static int cmd_logout_handle(cli_mgr_t *cm, uint32_t uid)
{
    user_info_t *uinfo;

    uinfo = cli_mgr_del_user(cm, uid);
    if(uinfo->group != NULL) {
        //exit_from_group(); /* XXX */
    }

    hbeat_rm_from_god(&cm->hbeat_god, &uinfo->hbeat);
    free(uinfo);
    return 0;
}

void client_user_dead(hbeat_node_t *hbeat)
{
    user_info_t *user;

    user = node_to_item(hbeat, user_info_t, hbeat);
    logi("user %d dead.\n", user->userid);

    cmd_logout_handle(user->mgr, user->userid); /* XXX */
}


static void create_group_response(cli_mgr_t *cm, group_info_t *ginfo, struct sockaddr *to)
{
    struct turn_info info;
    struct pack_creat_group_result *result;

    result = (struct pack_creat_group_result *)client_pkt_alloc(cm);

    get_turn_info(cm->node_mgr, ginfo->turn_handle, &info);

    result->groupid = ginfo->groupid;
    result->taskid = info.taskid;
    result->addr = *((struct sockaddr *)&info.addr);

    client_pkt_sendto(cm, MSG_CREATE_GROUP_RESPONSE, result, sizeof(*result), to);
}

static int cmd_create_group_handle(cli_mgr_t *cm, struct pack_creat_group *pr)
{
    group_info_t *ginfo;
    user_info_t *creater;

    logd("create group request from user:%u.\n", pr->userid);

    creater = cli_mgr_get_user(cm, pr->userid);
    if(!creater) {
        loge("user %u not found.\n", pr->userid);
        return -EINVAL;
    }

    ginfo = malloc(sizeof(*ginfo));
    if(!ginfo) {
        return -ENOMEM;
    }

    ginfo->groupid = cli_mgr_alloc_gid(cm);
    ginfo->flags = pr->flags;
    list_init(&ginfo->userlist);

    strncpy(ginfo->name, (char *)pr->name, GROUP_NAME_MAX);

    if(ginfo->flags & GROUP_TYPE_NEED_PASSWD)
        strncpy(ginfo->passwd, (char *)pr->passwd, GROUP_PASSWD_MAX);

    creater->group = ginfo;
    list_add_tail(&ginfo->userlist, &creater->node);
    ginfo->users++;

    ginfo->turn_handle = turn_task_assign(cm->node_mgr, ginfo);
    if(ginfo->turn_handle == 0) {
        loge("assign turn task failed.\n");
        goto fail;
    }

    cli_mgr_add_group(cm, ginfo);

    create_group_response(cm, ginfo, &creater->addr);
    return 0;

fail:
    free(ginfo);
    return -EINVAL;
}


static void group_delete_notify(cli_mgr_t *cm, user_info_t *user)
{
    uint32_t *groupid;
    group_info_t *ginfo = user->group;

    groupid = (uint32_t *)client_pkt_alloc(cm);
    *groupid = ginfo->groupid;

    client_pkt_sendto(cm, MSG_GROUP_DELETE, groupid, sizeof(uint32_t), &user->addr);
}

static int cmd_delete_group_handle(cli_mgr_t *cm, struct pack_del_group *pr)
{
    int ret;
    user_info_t *creater, *user;
    group_info_t *ginfo;
#if 0
    /* Permission check */
    creater = node_to_item(ginfo->userlist.next, user_info_t, node);
    if(creater->userid != pr->userid) {
        return -EINVAL;
    }
#endif

    ginfo = cli_mgr_del_group(cm, pr->groupid);

    list_for_each_entry(user, &ginfo->userlist, node) {
        if(pr->userid == user->userid)
            continue;

        group_delete_notify(cm, user);
        user->group = NULL;
    }

    ret = turn_task_reclaim(cm->node_mgr, ginfo->turn_handle);
    if(ret)
        logw("turn task reclaim fail.\n");

    free(ginfo);
    return 0;
}

#define RESULT_MAX_LEN 	 	(4000)
struct group_list_tmp {
    int pos;
    int count;

    int curr_pos;
    int offset;
    int rescount;
    uint8_t *data;
};

/* FIXME: tmp */
static bool hash_entry_cb(void* key, void* value, void* context)
{
    group_desc_t *group;
    group_info_t *ginfo = (group_info_t *)value;
    struct group_list_tmp *rtmp = (struct group_list_tmp *)context;

    if(rtmp->curr_pos++ < rtmp->pos) {
        return true;
    }

    if(rtmp->rescount >= rtmp->count) {
        return false; /* submit */
    }

    if(rtmp->offset + sizeof(group_desc_t) >= RESULT_MAX_LEN) {
        return false; /* submit */
    }

    group = (group_desc_t *)(rtmp->data + rtmp->offset);
    group->groupid = ginfo->groupid;
    group->flags = ginfo->flags;
    group->namelen = strlen(ginfo->name);

    if(RESULT_MAX_LEN - rtmp->offset < strlen(ginfo->name)) {
        return false; /*no more space */
    }

    strncpy(group->name, ginfo->name, GROUP_NAME_MAX);
    rtmp->offset += (sizeof(group_desc_t) + group->namelen);
    rtmp->rescount++;

    return true;
}


static int cmd_list_group_handle(cli_mgr_t *cm, struct pack_list_group *pr)
{
    user_info_t *uinfo;
    struct group_list_tmp rtmp;

    logd("list group request from user:%u.\n", pr->userid);
    uinfo = hashmapGet(cm->user_map, (void*)pr->userid);
    if(!uinfo)
        return -EINVAL;

    logd("list group. pos:%d, count:%d.\n", pr->pos, pr->count);

    rtmp.pos = pr->pos;
    rtmp.count = pr->count;

    rtmp.curr_pos = 0;
    rtmp.offset = 0;
    rtmp.rescount = 0;
    rtmp.data = client_pkt_alloc(cm);

    hashmapForEach(cm->group_map, hash_entry_cb, &rtmp);

    client_pkt_sendto(cm, MSG_LIST_GROUP_RESPONSE, rtmp.data, rtmp.offset, &uinfo->addr);
    return 0;
}

static void join_group_response(cli_mgr_t *cm, group_info_t *ginfo, struct sockaddr *to)
{
    struct turn_info info;
    struct pack_creat_group_result *result; 	/* XXX */

    result = (struct pack_creat_group_result *)client_pkt_alloc(cm);

    get_turn_info(cm->node_mgr, ginfo->turn_handle, &info);

    result->groupid = ginfo->groupid;
    result->taskid = info.taskid;
    result->addr = *((struct sockaddr *)&info.addr);

    client_pkt_sendto(cm, MSG_JOIN_GROUP_RESPONSE, result, sizeof(*result), to);
}

static int cmd_join_group_handle(cli_mgr_t *cm, struct pack_join_group *pr)
{
    user_info_t *uinfo;
    group_info_t *ginfo;

    logd("join group request from user:%u, group:%u", pr->userid, pr->groupid);

    uinfo = hashmapGet(cm->user_map, (void*)pr->userid);
    if(!uinfo)
        return -EINVAL;

    ginfo = hashmapGet(cm->group_map, (void*)pr->groupid);
    if(!ginfo)
        return -EINVAL;

    uinfo->group = ginfo;
    list_add_tail(&ginfo->userlist, &uinfo->node);
    ginfo->users++;

    turn_task_user_join(cm->node_mgr, ginfo->turn_handle, uinfo);

    join_group_response(cm, ginfo, &uinfo->addr);
    return 0;
}


static int cmd_leave_group_handle(cli_mgr_t *cm, struct pack_leave_group *pr)
{
    user_info_t *uinfo;
    group_info_t *ginfo;

    uinfo = hashmapGet(cm->user_map, (void*)pr->userid);
    if(!uinfo)
        return -EINVAL;

    ginfo = uinfo->group;
    if(!ginfo) 
        return -EINVAL;

    ginfo->users--;
    list_remove(&uinfo->node);

    turn_task_user_leave(cm->node_mgr, ginfo->turn_handle, uinfo);

    return 0;
}

static int cmd_hbeat_handle(cli_mgr_t *cm, uint32_t userid)
{
    user_info_t *uinfo;

    uinfo = hashmapGet(cm->user_map, (void*)userid);
    if(!uinfo)
        return -EINVAL;

    user_heartbeat(&uinfo->hbeat);
    return 0;
}


static void cli_mgr_handle(void *opaque, uint8_t *data, int len, void *from)
{
    int ret = 0;
    cli_mgr_t *cm = (cli_mgr_t *)opaque;
    pack_head_t *head;
    void *payload;
    struct sockaddr *cliaddr = from;

    logd("client manager receive pack. len:%d\n", len);
    if(data == NULL || len < sizeof(*head))
        return;

    dump_data("client mgr receive data", data, len);

    head = (pack_head_t *)data;
    payload = head + 1; 

    logd("pack: type:%d, seq:%d, datalen:%d\n", head->type, head->seqnum, head->datalen);

    if(head->magic != SERV_MAGIC ||
            head->version != SERV_VERSION)
        return;

    if(head->type == MSG_CLI_ACK) {
        cli_ack_handle(cm, head->seqnum);
    }

    cli_mgr_send_ack(cm, head);

    switch(head->type) {
        case MSG_CLI_LOGIN:
            //uint32_t id = period; /*XXX*/
            ret = cmd_login_handle(cm, cliaddr);
            break;
        case MSG_CLI_LOGOUT:
        {
            uint32_t uid = *(uint32_t *)payload;
            ret = cmd_logout_handle(cm, uid);
            break;
        }
        case MSG_CLI_CREATE_GROUP:
            ret = cmd_create_group_handle(cm, (struct pack_creat_group *)payload);
            break;
        case MSG_CLI_DELETE_GROUP:
            ret = cmd_delete_group_handle(cm, (struct pack_del_group *)payload);
            break;
        case MSG_CLI_LIST_GROUP:
            ret = cmd_list_group_handle(cm, (struct pack_list_group *)payload);
            break;
        case MSG_CLI_JOIN_GROUP:
            ret = cmd_join_group_handle(cm, (struct pack_join_group *)payload);
            break;
        case MSG_CLI_LEAVE_GROUP:
            ret = cmd_leave_group_handle(cm, (struct pack_leave_group *)payload);
            break;
        case MSG_CLI_HBEAT:
        {
            uint32_t uid = *(uint32_t *)payload;
            ret = cmd_hbeat_handle(cm, uid);
            break;
        }
        default:
            logw("unknown packet. type:%d\n", head->type);
            break;
    }

    if(ret) {
        logw("packet handle fatal. type:%d\n", head->type);
    }
}


static void cli_mgr_close(void *opaque)
{

}


#define ID_FRIST_NUM 	(1)

#define HASH_USER_CAPACITY 			(1024)
#define HASH_GROUP_CAPACITY 		(256)

cli_mgr_t *cli_mgr_init(node_mgr_t *nodemgr) 
{
    int clifd;
    cli_mgr_t *cm;

    logd("client manager running.\n");
    cm = malloc(sizeof(*cm));
    if(!cm)
        return NULL;

    cm->uid_pool = ID_FRIST_NUM;
    cm->gid_pool = ID_FRIST_NUM;
    cm->user_count = 0;
    cm->group_count = 0;
    cm->nextseq = 0;
    cm->node_mgr = nodemgr;

    clifd = create_cli_mgr_channel();
    cm->hand = ioasync_udp_create(clifd, cli_mgr_handle, cli_mgr_close, cm);
    cm->user_map = hashmapCreate(HASH_USER_CAPACITY, int_hash, int_equals);
    cm->group_map = hashmapCreate(HASH_GROUP_CAPACITY, int_hash, int_equals);
    hbeat_god_init(&cm->hbeat_god, client_user_dead);

    pthread_mutex_init(&cm->lock, NULL);
    return cm;
}

