#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "client.h"

#define DEFAULT_IP 		"127.0.0.1"
#define CFG_MAXARGS 	(64)

#define ARRAY_SIZE(s) 	(sizeof(s)/sizeof((s)[0]))

const char *fifo_name = "/tmp/sample_fifo";

struct cmd_ops {
    char *cmd;
    int (*func)(int argc, char **argv);
};


int parse_cmds(char *line, char *argv[])
{
    int nargs = 0;

    while(nargs < CFG_MAXARGS)
    {
        while((*line ==' ') || (*line == '\t'))
        {
            ++line;
        }

        if(*line == '\0')
        {
            argv[nargs] = NULL;
            return nargs;
        }

        argv[nargs++] = line;

        while(*line && (*line != ' ') && (*line != '\t'))
            ++line;

        if(*line == '\0')
        {
            argv[nargs] = NULL;
            return nargs;
        }
        *line++ = '\0';
    }

    return nargs;
}

static void run_netplay(void)
{
    int ret;
    int pid, w;
    int status;
    int pipe_fd = -1; 
    const int open_mode = O_WRONLY;
    struct cli_context_state state;

    client_state_save(&state);

    pid = fork();
    if(pid == 0) {
        char *newargv[] = { NULL, NULL };
        char *newenviron[] = { "PATH=$PATH", "LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./lib", NULL };

        printf("startup sample netplay.\n");

        execve("./sample_netplay", newargv, newenviron);
        perror("execve");
    } else {

        if(access(fifo_name, F_OK) == -1) {  
            ret = mkfifo(fifo_name, 0777);  
            if(ret != 0) {  
                printf("Could not create fifo %s\n", fifo_name);  
                exit(EXIT_FAILURE);  
            }  
        }  

        pipe_fd = open(fifo_name, open_mode);
        ret = write(pipe_fd, &state, sizeof(state));
        close(pipe_fd);

        do {
            w = waitpid(pid, &status, WUNTRACED | WCONTINUED);
            if (w == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }

            if (WIFEXITED(status)) {
                printf("exited, status=%d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("killed by signal %d\n", WTERMSIG(status));
            } else if (WIFSTOPPED(status)) {
                printf("stopped by signal %d\n", WSTOPSIG(status));
            }
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

static int create_group(int argc, char **argv) 
{
    int ret;
    int open = 1;
    char *name;
    char *passwd;

    if(argc > 1)
        name = argv[1];
    else
        name = "testroom";

    if(argc > 2)
        passwd = argv[2];
    else
        passwd = "";

    if(argc > 3)
        open = atoi(argv[3]);
    else
        open = 1;

    printf("create room, %d, name:%s, passwd:%s\n", 
            open, name, passwd);
    ret = client_create_group(open, name, passwd);
    if(ret) {
        printf("create room fail.\n");
        return -1;
    }

    printf("wait other player.\n");
    run_netplay();

    return 0;
}

static int delete_group(int argc, char **argv)
{
    client_delete_group();
    return 0;
}

static int list_group(int argc, char **argv)
{
    int res = 0;
    int i;
    struct group_description group[50];

    client_list_group(0, 20, group, &res);
    printf("room count:%d, infomation:\n", res);

    for(i=0; i<res; i++) {
        printf("room id:%d, room name:%s, flags:%d.\n", 
                group[i].groupid, group[i].name, group[i].flags);
    }
    return 0;
}

static int join_group(int argc, char **argv)
{
    char *passwd;
    struct group_description group;
    if(argc < 2)
        return -1;

    group.groupid = atoi(argv[1]);

    if(argc > 2)
        passwd = argv[2];
    else
        passwd = "";

    client_join_group(&group, passwd);

    printf("start play.\n");
    run_netplay();
    return 0;
}

static int leave_group(int argc, char **argv)
{
    client_leave_group();
    return 0;
}


static struct cmd_ops cmds[] = {
    { "create", create_group },
    { "delete", delete_group },
    { "join", join_group },
    { "leave", leave_group },
    { "list", list_group },
};



int cli_callback(int event, void *arg1, void *arg2)
{
    printf("receive event(%d)\n", event);
    return 0;
}


int main(int argc, char **argv)
{
    int ret;
    char buf[1024];
    char host[32] = {0};
    char* cmd_argv[CFG_MAXARGS];

    if(argc < 2) {
        sprintf(host, "%s", DEFAULT_IP);
    } else {
        sprintf(host, "%s", argv[1]);
    }

    client_init(host, CLI_MODE_CONTROL_ONLY, cli_callback);

    ret = client_login();
    if(ret) {
        printf("login fail. exit.\n");
        exit(-1);
    }

    {
        int res = 0;
        int i;
        struct group_description group[50];

        client_list_group(0, 20, group, &res);
        printf("room infomation:\n");

        for(i=0; i<res; i++) {
            printf("room id:%d, room name:%s, flags:%d.\n", 
                    group[i].groupid, group[i].name, group[i].flags);
        }
    }

    while(fgets(buf, sizeof(buf), stdin)) {
        int i;

        if(!strcmp(buf, "quit")) {
            break;
        }

        ret = parse_cmds(buf, cmd_argv);

        if(ret <= 0)
            continue;

        for(i=0; i<ARRAY_SIZE(cmds); i++) {
            if(!strncmp(cmds[i].cmd, cmd_argv[0], strlen(cmds[i].cmd))) {
                printf("exec %s.\n", cmds[i].cmd);
                cmds[i].func(ret, cmd_argv);
                break;
            }
        }
    }

    client_logout();
    printf("exit.\n");
    return 0;
}

