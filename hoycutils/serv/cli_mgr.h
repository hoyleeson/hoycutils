#ifndef _SERV_CLI_MGR_H_
#define _SERV_CLI_MGR_H_

#include <stdint.h>
#include <pthread.h>
#include <common/list.h>
#include <common/iohandler.h>
#include <common/hashmap.h>
#include <common/hbeat.h>

#include <protos.h>

#include "node_mgr.h"

#define GROUP_MAX_USER 		(8)

typedef struct _user_info user_info_t;
typedef struct _group_info group_info_t;
typedef struct _cli_mgr cli_mgr_t; 


enum user_state {
    USER_STATE_FREE,
    USER_STATE_LOGIN,
};


struct _user_info {
    uint32_t userid; 	/* session id */
    int state;
    struct sockaddr addr;
    group_info_t *group;
    hbeat_node_t hbeat;

    struct listnode node;
    cli_mgr_t *mgr;
};


struct _group_info {
    uint32_t groupid;
    uint16_t flags;
    char name[GROUP_NAME_MAX];
    char passwd[GROUP_PASSWD_MAX];

    int users;
    struct listnode userlist;
    unsigned long turn_handle;
};

struct _cli_mgr {
    uint32_t uid_pool; 	/* user id pool */
    uint32_t gid_pool; 	/* group id pool */
    ioasync_t *hand;

    Hashmap *user_map;
    Hashmap *group_map;
    int user_count;
    int group_count;

    node_mgr_t *node_mgr;
    uint16_t nextseq;
    struct listnode group_list;
    hbeat_god_t hbeat_god;
    pthread_mutex_t lock;
};

cli_mgr_t *cli_mgr_init(node_mgr_t *nodemgr);

#endif

