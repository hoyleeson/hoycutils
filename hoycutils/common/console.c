/*
 * common/console.c
 * 
 * 2016-01-01  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <common/log.h>
#include <common/cmds.h>
#include <common/console.h>

#define CFG_MAXARGS     (16)
#define CMD_MAX_LEN     (1024)


static int read_cmds(char* cmd)
{
    char buf[CMD_MAX_LEN], *p;
    int len = 0;

    printf("hoy>");

    if(fgets(buf, sizeof(buf), stdin)) {
        p = strchr(buf, '\n');
        len = p - buf;
        len = CMD_MAX_LEN > len? len : CMD_MAX_LEN;

        strncpy(cmd, buf, len);
    }

    return len;
}

static int parse_cmds(char *line, char **argv)
{
    int nargs = 0;
    logv("parse line: \"%s\"\n", line);

    while(nargs < CFG_MAXARGS) {
        while((*line ==' ') || (*line == '\t')) {
            ++line;
        }

        if(*line == '\0') {
            argv[nargs] = NULL;
            return nargs;
        }

        argv[nargs++] = line;

        while(*line && (*line != ' ') && (*line != '\t'))
            ++line;

        if(*line == '\0') {
            argv[nargs] = NULL;
            return nargs;
        }
        *line++ = '\0';
    }

    printf("**too many args (max. %d)**\n", CFG_MAXARGS);
    return nargs;
}

static cmd_tbl_t *find_cmd(cmd_tbl_t* cmd_list, const char *cmd)
{
    int n_found = 0;
    unsigned int len;
    cmd_tbl_t *cmdp, *cmdp_temp = NULL;

    if(!cmd)
        return NULL;

    len = strlen(cmd);
    for(cmdp = cmd_list; cmdp->name != 0; cmdp++) {
        if(strncmp(cmdp->name, cmd, len) == 0) {
            if(len == strlen(cmdp->name))
                return cmdp;

            cmdp_temp = cmdp;
            n_found++;
        }
    }

    if(n_found) {
        return cmdp_temp;
    }
    return NULL;
}


static int execute_cmds(cmd_tbl_t* cmd_list, int argc, char** argv)
{
    int ret = -EINVAL;
    cmd_tbl_t* cmdtp;

    cmdtp = find_cmd(cmd_list, argv[0]);
    if(!cmdtp)
        return ret;

    ret = cmdtp->cmd(argc, argv);
    printf("\n");

    return ret;
}

void console_loop(void)
{
    int ret;
    int len;
    char cmd[CMD_MAX_LEN];
    char* cmd_argv[CFG_MAXARGS];
    cmd_tbl_t *cmd_list;

    cmd_list = get_cmd_tbl_list();

    for( ;; ) {
        memset(cmd, 0, CMD_MAX_LEN);

        len = read_cmds(cmd);
        if(len <= 0)
            continue;

        ret = parse_cmds(cmd, cmd_argv);
        if(ret <= 0) {
            logv("parse cmd failed.\n");
            continue;
        }

        ret = execute_cmds(cmd_list, ret, cmd_argv);
        if(ret) {
            logv("execute cmd failed.\n");
            continue;
        }
    }
}

