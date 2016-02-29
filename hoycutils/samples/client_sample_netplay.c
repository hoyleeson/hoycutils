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

const char *fifo_name = "/tmp/sample_fifo";

int fileseq = 0;

#define SAVE_FILE_PATH          "save"
#define SAVE_FILE_PREFIX        "test"
#define SAVE_FILE_SUFFIX        ".bmp"

int running = 0;
int pid;

int cli_callback(int event, void *arg1, void *arg2)
{
    //printf("receive event(%d)\n", event);

    running = 1; /*XXX*/

    switch(event) {
        case EVENT_CHECKIN:
            running = 1;
            break;
        case EVENT_COMMAND:
        {
            //printf("event cmd: %s, len:%d\n", (char *)arg1, (int)arg2);
            printf("^ ");
            fflush(stdout);
            break;
        }
        case EVENT_STATE_IMG:
        {
            char file[512] = {0};
            int ret;
            FILE *fp;
            char *data = (char *)arg1;
            int len = (int)((long)arg2);
            int seq = (fileseq++) % 200;

            printf("* ");
            fflush(stdout);

            sprintf(file, "%s/%d/%s%d%s", SAVE_FILE_PATH, pid,
                    SAVE_FILE_PREFIX, seq, SAVE_FILE_SUFFIX);

            fp = fopen(file, "w+");
            ret = fwrite(data, len, 1, fp);
            if(ret < 0){
                printf("warning: write file err.\n");
            }
            fclose(fp);
            break;
        }
        default:
            break;
    }
    return 0;
}

#define IMG_FILE_NAME       "test.bmp"
//#define IMG_FILE_NAME       "readme.txt"
#define DATA_MAX_LEN        (4*1024*1024)

int main(int argc, char **argv)
{
    int ret;
    int pipe_fd = -1;  
    int open_mode = O_RDONLY;
    struct cli_context_state state;
    char buf[512] = {0};
    int seq = 0;
    FILE *fp;
    char imgbuf[DATA_MAX_LEN] = {0};
    int len;
    char path[32] = {0};

    printf("sample netplay enter..\n");

    pid = getpid();

    pipe_fd = open(fifo_name, open_mode);  
    ret = read(pipe_fd, &state, sizeof(state));
    if(ret < 0)
        return 0;
    close(pipe_fd);
    client_state_dump(&state);

    client_init(DEFAULT_IP, CLI_MODE_TASK_ONLY, cli_callback);
    client_state_load(&state);
    client_task_start();

    /* remove path */
    remove(SAVE_FILE_PATH);
    mkdir(SAVE_FILE_PATH, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    sprintf(path, "%s/%d", SAVE_FILE_PATH, pid);
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    while(1) {
        if(running == 0) {
            printf(".");
            fflush(stdout);
            sleep(1);
            continue;
        }
        len = sprintf(buf, "test hello world.%d.", seq++);
        buf[len] = 0;
        client_send_command(buf, len + 1);

        fp = fopen(IMG_FILE_NAME, "r");
        if(!fp) {
            printf("file not found.\n");
            return 0;
        }

        len = fread(imgbuf, 1, DATA_MAX_LEN, fp);
        client_send_state_img(imgbuf, len);
        fclose(fp);
        sleep(1);
    }

    return 0;
}

