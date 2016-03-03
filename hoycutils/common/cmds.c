/*
 * common/cmds.c
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
#include <unistd.h>
#include <stdlib.h>

#include <config.h>
#include <common/log.h>
#include <common/cmds.h>

static int do_help(int argc, char** argv)
{
    cmd_tbl_t *cmdp = get_cmd_tbl_list();

    for(; cmdp->name != 0; cmdp++)
    {
        printf("[%s]\n\tusage:\n", cmdp->name);
        printf("\t%s\n", cmdp->usage);
    }
    return 0;
}

static int do_version(int argc, char** argv)
{
    printf("program:  %s\n", PACKAGE_NAME);
    printf("version:  V%s\n", VERSION);
    printf("compilation date: %s,time: %s\n", __DATE__, __TIME__);
    return 0;
}

static int do_exit(int argc, char** argv)
{
    char ch;
    int opt;

    while((opt = getopt(argc, argv,"f")) != -1) {
        switch(opt) {
            case 'f':
                exit(0);
                break;
            default:
                printf("please use help for more infomation.\n");
                break;
        }
    }

    printf("exit program?(y|n)");
    ch = getchar();
    if(ch == 'y') {
        exit(0);
    }
    return 0;
}

static int do_quit(int argc, char** argv)
{
    return do_exit(argc, argv);
}


int do_loglevel(int argc, char **argv)
{
    return 0;
}



#define CONSOLE_CMD_END() \
    { 0, 0, 0 }

static cmd_tbl_t cmd_tbl_list[] = {
    CONSOLE_CMD(help,       do_help,        "Show help info."),
    CONSOLE_CMD(version,    do_version,     "Show version info."),
    CONSOLE_CMD(exit,       do_exit,        "Exit program.\n\t-f:exit program force."),
    CONSOLE_CMD(quit,       do_quit,        "Exit program.\n\t-f:exit program force."),
    CONSOLE_CMD(loglevel,   do_loglevel,    "Setting log print level."),
    CONSOLE_CMD_END(),
};

cmd_tbl_t* get_cmd_tbl_list(void)
{
    return cmd_tbl_list;
}

