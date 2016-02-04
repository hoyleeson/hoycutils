#ifndef _SERV_PROTOS_INTERNAL_H_
#define _SERV_PROTOS_INTERNAL_H_

#include <sys/socket.h>

#define NODE_SERV_LOGIN_PORT 	(9123) 	/* listen by center server. */


/* node server ------> center server */
enum node_center_msg_type {
    /* 
     * MSG_NODE_REGISTER,
     * MSG_NODE_UNREGISTER, 
     */
    MSG_TASK_ASSIGN_RESPONSE,
};

/* center server ----> node server */
enum center_node_msg_type {
    MSG_TASK_ASSIGN,
    MSG_TASK_RECLAIM,
    MSG_TASK_CONTROL,
};

#define TASK_PRIORITY_MIN 		(0)
#define TASK_PRIORITY_MAX 		(8)
#define TASK_PRIORITY_NORMAL 	TASK_PRIORITY_MIN

struct pack_task_assign {
    uint32_t taskid;
    uint8_t type;
    uint8_t priority;
};

struct pack_task_reclaim {
    uint32_t taskid;
    uint8_t type;
};

struct pack_task_control {
    uint32_t taskid;
    uint8_t type;
    uint8_t opt;
};

struct pack_task_assign_response {
    uint32_t taskid;
    uint8_t type;

    /* task worker infomation */
    struct sockaddr addr;
};


typedef struct _client_tuple {
    uint32_t userid;
    struct sockaddr addr;
} client_tuple_t;


struct pack_turn_assign {
    struct pack_task_assign base;
    uint32_t groupid;
    int cli_count;
    client_tuple_t tuple[0];
};

struct pack_turn_reclaim {
    struct pack_task_reclaim base;
};

struct pack_turn_control {
    struct pack_task_control base;
    client_tuple_t tuple;
};


#endif
