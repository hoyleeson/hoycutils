/*
 * client/client.c
 * 
 * 2016-01-08  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#define LOG_TAG     "client"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <protos.h>
#include <common/ioasync.h>
#include <common/workqueue.h>
#include <common/iowait.h>
#include <common/packet.h>
#include <common/pack_head.h>
#include <common/sockets.h>
#include <common/log.h>
#include <common/hbeat.h>
#include <common/timer.h>
#include <common/init.h>
#include <common/data_frag.h>

#include "client.h"


#define CLI_FRAGMENT_MAX_LEN 	(512)
#define CLI_DATA_MAX_LEN        (4*1024*1024)


struct pack_cli_msg {
    struct pack_task_req base;

    uint8_t type;

    /*struct fragment {*/
    uint16_t seq;
    uint8_t frag:1;
    uint8_t mf:1;
    uint8_t _reserved:6;
    uint32_t frag_ofs:22;    /* max data len: 4MB */
    uint32_t datalen:10;     /* packet len */
    uint8_t data[0];
    /*}; */
};


struct client_peer {
    uint32_t taskid;
    iohandler_t *hand;
    uint16_t nextseq;
    struct sockaddr_in serv_addr;
};

#if 0
struct client_task {
    uint32_t taskid;
    ioasync_t *hand;
    uint16_t nextseq;
    struct sockaddr_in serv_addr;
};
#endif

enum client_conn_mode {
    MODE_SERV_TURN,
    MODE_CLI_P2P,
};

struct client {	
    uint32_t userid;
    uint32_t groupid;
    int mode;
    event_cb callback;
    iowait_t waits;
    int running;
    data_frags_t *frags;
    struct timer_list hbeat_timer;

    struct client_peer control; 	/* connect with center serv, taskid is invaild */
    struct client_peer task;
    pthread_mutex_t lock;
};

static struct client _client;


static void *client_pkt_alloc(struct client_peer *peer)
{
    pack_buf_t *pkb;

    pkb = iohandler_pack_buf_alloc(peer->hand);

    return pkb->data + pack_head_len();
}

static int get_pkt_seq(struct client_peer *peer)
{
    int seq;
    struct client *cli = &_client;

    pthread_mutex_lock(&cli->lock);
    seq = peer->nextseq++;
    pthread_mutex_unlock(&cli->lock);

    return seq;
}

static void client_pkt_send(struct client_peer *peer, int type, void *data, int len)
{
    pack_buf_t *pkb;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)data - pack_head_len());
    pkb = data_to_pack_buf(head);

    /* init header */
    init_pack(head, type, len);
    head->seqnum = get_pkt_seq(peer);

    pkb->len = len + pack_head_len();

    logv("client send packet, dist addr:%s, port:%d. type:%d, len:%d\n",
            inet_ntoa(peer->serv_addr.sin_addr), 
            ntohs(peer->serv_addr.sin_port), type, len);

    iohandler_pkt_sendto(peer->hand, pkb, (struct sockaddr*)&peer->serv_addr);
}

static void cli_hbeat_start(struct client *cli)
{
    mod_timer(&cli->hbeat_timer, curr_time_ms() + HBEAD_DEAD_LINE);
}

static void cli_hbeat_stop(struct client *cli)
{
    del_timer(&cli->hbeat_timer);
}

int client_login(void)
{
    int ret;
    int userid;
    void *data;
    iowait_watcher_t watcher;
    struct client *cli = &_client;

    if(!cli->running)
        return -EINVAL;

    data = client_pkt_alloc(&cli->control);

    iowait_watcher_init(&watcher, MSG_LOGIN_RESPONSE, 0, &userid, sizeof(int));
    iowait_register_watcher(&cli->waits, &watcher);

    client_pkt_send(&cli->control, MSG_CLI_LOGIN, data, 0);

    ret = wait_for_response(&cli->waits, &watcher);
    if(ret)
        return -EINVAL;

    cli->userid = userid;
    logd("client login success, userid:%u.\n", userid);

    cli_hbeat_start(cli);
    return 0;
}

void client_logout(void)
{
    uint32_t *userid;
    struct client *cli = &_client;

    if(!cli->running)
        return;

    userid = (uint32_t *)client_pkt_alloc(&cli->control);

    *userid = cli->userid;

    client_pkt_send(&cli->control, MSG_CLI_LOGOUT, userid, sizeof(uint32_t));
    cli->userid = INVAILD_USERID;
    cli_hbeat_stop(cli);
}


int client_create_group(int open, const char *name, const char *passwd)
{
    int ret;
    struct client *cli = &_client;
    struct pack_creat_group *p;
    struct pack_creat_group_result result;
    iowait_watcher_t watcher;

    if(!cli->running)
        return -EINVAL;

    p = (struct pack_creat_group *)client_pkt_alloc(&cli->control);

    p->userid = cli->userid;
    p->flags = 0;
    if(open)
        p->flags |= GROUP_TYPE_OPENED;

    if(name)
        strncpy((char *)p->name, name, GROUP_NAME_MAX);

    if(passwd) {
        p->flags |= GROUP_TYPE_NEED_PASSWD;
        strncpy((char *)p->passwd, passwd, GROUP_PASSWD_MAX);
    }

    iowait_watcher_init(&watcher, MSG_CREATE_GROUP_RESPONSE, 0, &result, sizeof(result));
    iowait_register_watcher(&cli->waits, &watcher);

    client_pkt_send(&cli->control, MSG_CLI_CREATE_GROUP, p, sizeof(*p));

    ret = wait_for_response(&cli->waits, &watcher); /* XXX seq */
    if(ret)
        return -EINVAL;

    cli->groupid = result.groupid;
    cli->task.taskid = result.taskid;
    cli->task.serv_addr = *((struct sockaddr_in*)&result.addr);
    logi("receive create group sucess. gtoupid:%d, taskid:%d\n",
            cli->groupid, cli->task.taskid);

    if(cli->mode == CLI_MODE_FULL_FUNCTION) {
        client_task_start();
    }

    return 0;
}

void client_delete_group(void)
{
    struct pack_del_group *p;
    struct client *cli = &_client;

    if(!cli->running)
        return;

    p = (struct pack_del_group *)client_pkt_alloc(&cli->control);

    p->userid = cli->userid;

    client_pkt_send(&cli->control, MSG_CLI_DELETE_GROUP, p, sizeof(*p));
}

int client_list_group(int pos, int count, struct group_description *gres, int *rescount)
{
#define RESULT_MAX_LEN 	 	(4000)
    int ret;
    char result[RESULT_MAX_LEN];
    struct pack_list_group *p;
    group_desc_t *gdesc;
    iowait_watcher_t watcher;
    int retlen = 0;
    int ofs = 0;
    struct group_description *gp = gres;
    struct client *cli = &_client;

    if(!cli->running)
        return -EINVAL;

    p = (struct pack_list_group *)client_pkt_alloc(&cli->control);

    p->userid = cli->userid;
    p->pos = pos;
    p->count = count;

    iowait_watcher_init(&watcher, MSG_LIST_GROUP_RESPONSE, 0, result, sizeof(result));
    iowait_register_watcher(&cli->waits, &watcher);

    client_pkt_send(&cli->control, MSG_CLI_LIST_GROUP, p, sizeof(*p));

    ret = wait_for_response_data(&cli->waits, &watcher, &retlen); /* XXX */
    if(ret)
        return -EINVAL;

    logd("%s result:%d\n", __func__, retlen);

    *rescount = 0;

    /* XXX: current version: group_desc_t equals struct group_description */
    while(ofs < retlen) {
        gdesc = (group_desc_t *)(result + ofs);

        gp->groupid = gdesc->groupid;
        gp->flags = gdesc->flags;
        memcpy(gp->name, gdesc->name, gdesc->namelen);
        gp->name[gdesc->namelen] = '\0';

        //	logd("%s gp->groupid:%d, gp->name:%s\n", __func__, gp->groupid, gp->name);
        gp++;
        ofs += sizeof(group_desc_t) + gdesc->namelen;
        (*rescount)++;
    }

    return 0;
}


int client_join_group(struct group_description *group, const char *passwd)
{
    int ret;
    struct pack_join_group *p;
    struct client *cli = &_client;
    struct pack_creat_group_result result;
    iowait_watcher_t watcher;

    if(!cli->running)
        return -EINVAL;

    p = (struct pack_join_group *)client_pkt_alloc(&cli->control);

    p->userid = cli->userid;
    p->groupid = group->groupid;

    iowait_watcher_init(&watcher, MSG_JOIN_GROUP_RESPONSE, 0, &result, sizeof(result));
    iowait_register_watcher(&cli->waits, &watcher);

    client_pkt_send(&cli->control, MSG_CLI_JOIN_GROUP, p, sizeof(*p));

    ret = wait_for_response(&cli->waits, &watcher); /* XXX */
    if(ret)
        return -EINVAL;

    cli->groupid = result.groupid;
    cli->task.taskid = result.taskid;
    cli->task.serv_addr = *((struct sockaddr_in*)&result.addr);

    if(cli->mode == CLI_MODE_FULL_FUNCTION) {
        client_task_start();
    }

    return 0;
}


void client_leave_group(void)
{
    struct pack_join_group *p;
    struct client *cli = &_client;

    if(!cli->running)
        return;

    p = (struct pack_join_group *)client_pkt_alloc(&cli->control);

    p->userid = cli->userid;

    client_pkt_send(&cli->control, MSG_CLI_LEAVE_GROUP, p, sizeof(*p));
}


static void *create_task_req_pack(struct client *cli, int type)
{
    struct pack_task_req *p;

    p = (struct pack_task_req *)client_pkt_alloc(&cli->task);

    p->taskid = cli->task.taskid;
    p->userid = cli->userid;
    p->type = type;

    return &p->data;
}

static void task_req_pack_send(struct client *cli, void *data, int size)
{
    struct pack_task_req *p;
    p = (struct pack_task_req *)((uint8_t *)data - sizeof(struct pack_task_req));
    p->datalen = size;

    client_pkt_send(&cli->task, MSG_TASK_REQ, p, sizeof(*p) + size);
}


void client_checkin(void)
{
    struct pack_cli_msg *p;
    struct client *cli = &_client;

    if(cli->task.taskid == INVAILD_TASKID) {
        loge("checkin request failed, task id invaild.\n");
        return;	
    }

    p = create_task_req_pack(cli, TASK_TURN);

    p->type = PACK_CHECKIN;
    p->datalen = 0;
    task_req_pack_send(cli, p, sizeof(*p));
}

void client_send_command(void *data, int len)
{
    struct pack_cli_msg *p;
    struct client *cli = &_client;

    if(cli->task.taskid == INVAILD_TASKID) {
        loge("send command failed, task id invaild.\n");
        return;	
    }

    p = create_task_req_pack(cli, TASK_TURN);

    p->type = PACK_COMMAND;
    p->frag = 0;
    p->datalen = len;
    memcpy(p->data, data, len);

    task_req_pack_send(cli, p, sizeof(*p) + len);
}

static void cli_frag_output(void *opaque, data_vec_t *v)
{
    struct pack_cli_msg *p;
    struct client *cli = (struct client *)opaque;

    p = create_task_req_pack(cli, TASK_TURN);
    p->type = PACK_STATE_IMG;
    p->seq = v->seq;
    p->frag = 1;
    p->mf = v->mf;
    p->frag_ofs = v->ofs;
    p->datalen = v->len;
    memcpy(p->data, v->data, v->len);

    logv("pack state img output: seq: %d, mf:%d, offset:%d, datalen:%d\n", 
            p->seq, p->mf, p->frag_ofs, p->datalen);

    task_req_pack_send(cli, p, sizeof(*p) + v->len);
    usleep(100); /* XXX */
}

void client_send_state_img(void *data, int len)
{
    struct client *cli = &_client;

    if(cli->task.taskid == INVAILD_TASKID) {
        return;	
    }

    logv("client send state img, len:%d\n", len);

    data_frag(cli->frags, data, len);
}


static void client_hbeat(void)
{
    uint32_t *userid;
    struct client *cli = &_client;

    userid = (uint32_t *)client_pkt_alloc(&cli->control);

    *userid = cli->userid;

    client_pkt_send(&cli->control, MSG_CLI_HBEAT, userid, sizeof(uint32_t));
}

static void cli_group_delete_handle(void)
{
    int ret;
    struct client *cli = &_client;

    ret = cli->callback(EVENT_GROUP_DELETE, NULL, NULL);
    if(ret) {
        loge("client EVENT_GROUP_DELETE handle fail.\n");	
    }

    logd("receive group delete nodify.\n");
    cli->groupid = INVAILD_GROUPID;
    cli->task.taskid = INVAILD_TASKID;
}

static void cli_msg_handle(void* user, uint8_t *data, int len, void *from)
{
    struct client *cli = user;
    pack_head_t *head;
    void *payload;

    logd("client receive pack. len:%d\n", len);

    if(data == NULL || len < sizeof(*head))
        return;

    dump_data("client receive data", data, len);

    head = (pack_head_t *)data;
    payload = head + 1; 

    logd("pack: type:%d, seq:%d, datalen:%d\n", head->type, head->seqnum, head->datalen);

    if(head->magic != PROTOS_MAGIC ||
            head->version != PROTOS_VERSION)
        return;

    if(head->type == MSG_CENTER_ACK) {
        //cli_ack_handle(cm, head->seqnum);
    }

    //cli_mgr_send_ack(cm, head);

    switch(head->type) {
        case MSG_LOGIN_RESPONSE:
        {
            uint32_t userid = *(uint32_t *)payload;
            response_post(&cli->waits, head->type, 0, &userid);
            break;
        }
        case MSG_CREATE_GROUP_RESPONSE:
        case MSG_JOIN_GROUP_RESPONSE:
        {
            struct pack_creat_group_result *gres;
            gres = (struct pack_creat_group_result *)payload;

            response_post(&cli->waits, head->type, 0, gres);
            break;
        }
        case MSG_LIST_GROUP_RESPONSE:
        {
            post_response_data(&cli->waits, head->type, 0, payload, head->datalen);
            break;
        }
        case MSG_GROUP_DELETE:
            cli_group_delete_handle();
            break;
        case MSG_HANDLE_ERR:
            break;
        default:
            break;
    }

    return;
}

static void cli_msg_close(void *user)
{
}

static void pack_checkin_handle(struct pack_cli_msg *msg) 
{
    int ret;
    struct client *cli = &_client;
    unsigned long len = msg->datalen;

    ret = cli->callback(EVENT_CHECKIN, (void *)msg->data, (void *)len);
    if(ret) {
        loge("client EVENT_CHECKIN handle fail.\n");	
    }
}

static void pack_command_handle(struct pack_cli_msg *msg) 
{
    int ret;
    struct client *cli = &_client;
    unsigned long len = msg->datalen;

    ret = cli->callback(EVENT_COMMAND, (void *)msg->data, (void *)len);
    if(ret) {
        loge("client EVENT_COMMAND handle fail.\n");	
    }
}

static void cli_frag_input(void *opaque, void *data, int datalen)
{
    int ret;
    struct client *cli = (struct client *)opaque;
    unsigned long len = datalen;

    ret = cli->callback(EVENT_STATE_IMG, (void *)data, (void *)len);
    if(ret) {
        loge("client EVENT_STATE_IMG handle fail.\n");	
    }
}

static pack_buf_t *payload_to_pack_buf(void *p)
{
    pack_buf_t *pkb;
    pack_head_t *head;

    head = (pack_head_t *)((uint8_t *)p - pack_head_len());
    pkb = data_to_pack_buf(head);

    return pkb;
}


static void cli_frag_pkt_free(void *opaque, void *frag_pkt)
{
    pack_buf_t *pkb;
    //    struct client *cli = (struct client *)opaque;

    pkb = payload_to_pack_buf(frag_pkt);

    pack_buf_free(pkb);
}

static void pack_state_img_handle(struct pack_cli_msg *msg) 
{
    /*XXX defrag. */
    struct client *cli = &_client;
    data_vec_t v;
    pack_buf_t *pkb;

    v.seq = msg->seq;
    v.mf = msg->mf;
    v.ofs = msg->frag_ofs;
    v.data = msg->data;
    v.len = msg->datalen;

    logd("pack state img info: seq: %d, mf:%d, offset:%d, datalen:%d\n", 
            msg->seq, msg->mf, msg->frag_ofs, msg->datalen);
    pkb = payload_to_pack_buf(msg);
    pack_buf_get(pkb);

    data_defrag(cli->frags, &v, msg);
}

static void cli_pack_handle(struct pack_cli_msg *msg) 
{
    logd("client packet handle. type:%d\n", msg->type);

    switch(msg->type) {
        case PACK_CHECKIN:
            pack_checkin_handle(msg);
            break;
        case PACK_COMMAND:
            pack_command_handle(msg);
            break;
        case PACK_STATE_IMG:
            pack_state_img_handle(msg);
            break;
        default:
            break;
    }
}

static void cli_task_handle(void* user, uint8_t *data, int len, void *from)
{
    //	struct client *cli = user;
    pack_head_t *head;
    void *payload;

    logd("client task pack. len:%d\n", len);

    if(data == NULL || len < sizeof(*head))
        return;

    dump_data("client receive data", data, len);

    head = (pack_head_t *)data;
    payload = head + 1; 

    logd("pack: type:%d, seq:%d, datalen:%d\n", head->type, head->seqnum, head->datalen);

    if(head->magic != PROTOS_MAGIC ||
            head->version != PROTOS_VERSION)
        return;

    if(head->type == MSG_CENTER_ACK) {
        //cli_ack_handle(cm, head->seqnum);
    }

    //cli_mgr_send_ack(cm, head);

    switch(head->type) {
        case MSG_TURN_PACK:
        case MSG_P2P_PACK:
        {
            struct pack_cli_msg *msg = (struct pack_cli_msg *)payload;
            cli_pack_handle(msg);
            break;
        }
        default:
            break;
    }

    return;
}

static void cli_task_close(void *user)
{
}

#if 0
static void *client_thread_handle(void *args)
{
    ioasync_loop(); 
    return 0;
}
#endif


int client_task_start(void)
{
    int sock;
    struct sockaddr_in addr; 	/* used for debug */
    socklen_t addrlen = sizeof(addr); 	/* used for debug */
    struct client *cli = &_client;

    sock = socket_inaddr_any_server(0, SOCK_DGRAM);
    cli->task.nextseq = 0;

    cli->task.hand = iohandler_udp_create(get_global_ioasync(), sock, 
            cli_task_handle, cli_task_close, cli);

    if (getsockname(sock, (struct sockaddr*)&addr, &addrlen) < 0) {
        close(sock);
        return -EINVAL;
    }

    logi("communicate with node server. bind to %s, %d.\n",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    /* report receive port */
    client_checkin();
    return 0;
}

static void cli_hbeat_timer_handle(unsigned long data)
{
    struct client *cli = (struct client *)data;

    client_hbeat();
    mod_timer(&cli->hbeat_timer, curr_time_ms() + HBEAD_DEAD_LINE);
}

void common_release(void)
{
    global_ioasync_release();
}

static void signal_handler(int signal)
{
    loge("caught signal %d.\n", signal);
    dump_stack();
    exit(1);
}

static void signals_init(void)
{
    signal(SIGCHLD, SIG_IGN);

    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGBUS, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGCHLD, signal_handler);
    signal(SIGPIPE, signal_handler);
    signal(SIGABRT, signal_handler);
}

int client_init(const char *host, int mode, event_cb callback) 
{
    int sock;
    struct hostent *hp;
#if 0
    int ret;
    pthread_t th;
#endif
    struct sockaddr_in addr; 	/* used for debug */
    socklen_t addrlen = sizeof(addr); 	/* used for debug */
    struct client *cli = &_client;

    signals_init();
    common_init();

    iowait_init(&cli->waits);

    cli->callback = callback;
    cli->mode = mode;
    pthread_mutex_init(&cli->lock, NULL);
    cli->frags = data_frag_init(CLI_FRAGMENT_MAX_LEN, cli_frag_input, 
            cli_frag_output, cli_frag_pkt_free, cli);

    /*	if(mode == CLI_MODE_CONTROL_ONLY || mode == CLI_MODE_TASK_ONLY) { */
    /* dynamic alloc port by system. */
    sock = socket_inaddr_any_server(0, SOCK_DGRAM);

    if (getsockname(sock, (struct sockaddr*)&addr, &addrlen) < 0) {
        close(sock);
        return -EINVAL;
    }

    logi("communicate with center server. bind to %s, %d.\n",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    hp = gethostbyname(host);
    if(hp == 0){
        cli->control.serv_addr.sin_addr.s_addr = inet_addr(host);
    } else 
        memcpy(&cli->control.serv_addr.sin_addr, hp->h_addr, hp->h_length);

    cli->control.serv_addr.sin_family = AF_INET;
    cli->control.serv_addr.sin_port = htons(CLIENT_LOGIN_PORT);
    cli->control.nextseq = 0;

    cli->userid = INVAILD_USERID;
    cli->groupid = INVAILD_GROUPID;

    init_timer(&cli->hbeat_timer);
    setup_timer(&cli->hbeat_timer, cli_hbeat_timer_handle, (unsigned long)cli);
    cli->control.hand = iohandler_udp_create(get_global_ioasync(), sock,
            cli_msg_handle, cli_msg_close, cli);

    cli->running = 1;
    /*	} */

#if 0
    ret = pthread_create(&th, NULL, client_thread_handle, cli);
    if(ret)
        return ret;
#endif

    return 0;
}

void client_release(void)
{
    struct client *cli = &_client;

    cli->running = 0;
    common_release();
}

int client_state_save(struct cli_context_state *state)
{
    struct client *cli = &_client;

    state->userid  = cli->userid;
    state->groupid = cli->groupid;
    state->taskid  = cli->task.taskid;
    state->addr    = cli->task.serv_addr;

    if(cli->userid != INVAILD_USERID)
        cli_hbeat_stop(cli);

    return 0;
}

int client_state_load(struct cli_context_state *state)
{
    struct client *cli = &_client;

    cli->userid 		= state->userid;
    cli->groupid 		= state->groupid;
    cli->task.taskid 	= state->taskid;
    cli->task.serv_addr = state->addr;

    if(cli->userid != INVAILD_USERID)
        cli_hbeat_start(cli);

    return 0;
}

void client_state_dump(struct cli_context_state *state)
{
    struct sockaddr_in *addr;

    logi("client state:\n");
    logi("user id: %d\n", state->userid);
    logi("group id: %d\n", state->groupid);
    logi("task id: %d\n", state->taskid);

    addr = &state->addr;
    logi("task server addr: %s, port: %d\n", 
            inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
}


void client_dump(void)
{
    struct client *cli = &_client;
    struct sockaddr_in *addr;

    logi("client state:\n");
    logi("user id: %d\n", cli->userid);
    logi("group id: %d\n", cli->groupid);
    logi("task id: %d\n", cli->task.taskid);

    addr = &cli->task.serv_addr;
    logi("task server addr: %s, port: %d\n", 
            inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
}

