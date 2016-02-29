#ifndef _SERV_CLI_MGR_H_
#define _SERV_CLI_MGR_H_

#include <stdint.h>
#include <pthread.h>
#include <common/list.h>
#include <common/ioasync.h>
#include <common/hbeat.h>

#include <protos.h>

#include "node_mgr.h"

#define GROUP_MAX_USER 		(8)

#define HASH_USER_SHIFT 	    (9)
#define HASH_USER_CAPACITY 	    (1 << HASH_USER_SHIFT)
#define HASH_USER_MASK 			(HASH_USER_CAPACITY - 1)

#define HASH_GROUP_SHIFT        (8)
#define HASH_GROUP_CAPACITY     (1 << HASH_GROUP_SHIFT)
#define HASH_GROUP_MASK         (HASH_GROUP_CAPACITY - 1)

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

    struct list_head entry;
    struct hlist_node hentry;
    cli_mgr_t *mgr;
};


struct _group_info {
    uint32_t groupid;
    uint16_t flags;
    char name[GROUP_NAME_MAX];
    char passwd[GROUP_PASSWD_MAX];

    int users;
    struct list_head userlist;
    struct hlist_node hentry;
    unsigned long turn_handle;
};

struct _cli_mgr {
    uint32_t uid_pool; 	/* user id pool */
    uint32_t gid_pool; 	/* group id pool */
    iohandler_t *hand;

    struct hlist_head user_map[HASH_USER_CAPACITY];
    struct hlist_head group_map[HASH_GROUP_CAPACITY];
    int user_count;
    int group_count;

    node_mgr_t *node_mgr;
    uint16_t nextseq;
    struct list_head group_list;
    hbeat_god_t hbeat_god;
    pthread_mutex_t lock;
};

cli_mgr_t *cli_mgr_init(node_mgr_t *nodemgr);

#endif

