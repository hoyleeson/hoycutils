#ifndef _TURN_CLIENT_H_
#define _TURN_CLIENT_H_

#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

enum client_mode {
    CLI_MODE_CONTROL_ONLY,
    CLI_MODE_TASK_ONLY,
    CLI_MODE_FULL_FUNCTION,
};

#define NAME_MAX_LEN 	(128)
struct group_description {
    uint32_t groupid;
    uint16_t flags;
    char name[NAME_MAX_LEN];
};

struct cli_context_state {
    uint32_t userid;
    uint32_t groupid;

    uint32_t taskid;
    struct sockaddr_in addr;
};

/* event callback routine.
 * event: EVENT_*
 * arg1, arg2: event dependent args.
 * return: 0 success, other fail */
typedef int (*event_cb)(int event, void *arg1, void *arg2);

int client_login(void);
void client_logout(void);
int client_create_group(int open, const char *name, const char *passwd);
void client_delete_group(void);

int client_list_group(int pos, int count, struct group_description *gres, int *rescount);
int client_join_group(struct group_description *group, const char *passwd);
void client_leave_group(void);

void client_send_command(void *data, int len);
void client_send_state_img(void *data, int len);

int client_init(const char *host, int mode, event_cb callback);
int client_task_start(void);
void client_release(void);

/* state: out argument. */
int client_state_save(struct cli_context_state *state);

/* state: in argument. */
int client_state_load(struct cli_context_state *state);

/* used for debug. */
void client_state_dump(struct cli_context_state *state);
void client_dump(void);

enum {
    EVENT_NONE,

    /* arg1: void *, receive data.
     * arg2: int, data length. */
    EVENT_COMMAND,

    /* arg1: void *, receive state image.
     * arg2: int, image length. */
    EVENT_STATE_IMG,

    /* arg1: NULL
     * arg2: NULL */
    EVENT_CHECKIN,

    /* arg1: NULL
     * arg2: NULL */
    EVENT_GROUP_DELETE,
};


#ifdef __cplusplus
}
#endif

#endif
