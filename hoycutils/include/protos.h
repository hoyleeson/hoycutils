#ifndef _TURN_PROTOS_H_
#define _TURN_PROTOS_H_

#include <sys/socket.h>
#include <stdint.h>

#define SERV_MAGIC 			(0x2016)	
#define SERV_VERSION 		(1)

#define CLIENT_LOGIN_PORT 	(8123) 	 /* listen by center server. */

#define CLIENT_TASK_PORT 	(8124) 	 /* XXX: listen by client */


#define INVAILD_USERID 		(~0L)
#define INVAILD_GROUPID 	(~0L)
#define INVAILD_TASKID 		(~0L)

/*client  ---->  center server */
enum cli_center_msg_type {
    MSG_CLI_ACK,
    MSG_CLI_HBEAT,
    MSG_CLI_LOGIN,
    MSG_CLI_LOGOUT,

    MSG_CLI_CREATE_GROUP,
    MSG_CLI_DELETE_GROUP,
    MSG_CLI_LIST_GROUP,
    MSG_CLI_JOIN_GROUP,
    MSG_CLI_LEAVE_GROUP,
};

/* center server ----> client */
enum center_cli_msg_type {
    MSG_CENTER_ACK,
    MSG_LOGIN_RESPONSE,
    MSG_CREATE_GROUP_RESPONSE,
    MSG_LIST_GROUP_RESPONSE,
    MSG_JOIN_GROUP_RESPONSE,
    MSG_GROUP_DELETE,
    MSG_HANDLE_ERR,
};

enum {
    MSG_TASK_REQ,
    MSG_TURN_PACK,
    MSG_P2P_PACK,
};

/* client A <----------> client B */
enum client_msg_type {
    PACK_CHECKIN = 1,
    PACK_COMMAND,
    PACK_STATE_IMG,
};

enum task_type {
    TASK_TURN = 1,
};


#define GROUP_NAME_MAX 		(32)
#define GROUP_PASSWD_MAX 	(32)

#define GROUP_TYPE_NEED_PASSWD 	(1 << 0)
#define GROUP_TYPE_OPENED 		(1 << 1)

struct pack_creat_group {
    uint32_t userid;
    uint16_t flags;
    uint8_t name[GROUP_NAME_MAX];
    uint8_t passwd[GROUP_PASSWD_MAX];
};

struct pack_del_group {
    uint32_t userid;
    uint32_t groupid;
};

struct pack_list_group {
    uint32_t userid;
    uint32_t pos;
    uint32_t count;
};

struct pack_join_group {
    uint32_t userid;
    uint32_t groupid;

    uint8_t passwd[GROUP_PASSWD_MAX];
};

struct pack_leave_group {
    uint32_t userid;
    uint32_t groupid;
};

/* also used for join group result. */
struct pack_creat_group_result {
    uint32_t groupid;
    uint32_t taskid;
    struct sockaddr addr;
};


/* send to node server */
struct pack_task_req {
    uint32_t taskid;
    uint32_t userid;
    uint8_t type;
    uint32_t datalen;
    uint8_t data[0];
};

typedef struct _user_description {
    uint32_t userid;
} user_desc_t;


typedef struct _group_description {
    uint32_t groupid;
    uint16_t flags;
    uint32_t namelen;
    char name[0];
} group_desc_t;


#endif


