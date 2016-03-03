/*
 * serv/main.c
 * 
 * 2016-01-05  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <protos.h>
#include <config.h>
#include <common/log.h>
#include <common/utils.h>
#include <common/init.h>
#include <common/console.h>

#include "serv.h"
#include "turn.h"

#define SERV_MODE_CENTER_SERV  	(1 << 0)
#define SERV_MODE_NODE_SERV 	 	(1 << 1)
#define SERV_MODE_FULL_FUNC 		(SERV_MODE_CENTER_SERV | SERV_MODE_NODE_SERV)
#define SERV_MODE_UNKNOWN  			(0)

#define LOCAL_HOST 	"127.0.0.1"

static const struct option longopts[] = {
    {"mode", required_argument, 0, 'm'},
    {"server", required_argument, 0, 's'},
    {"version", 0, 0, 'v'},
    {"help", 0, 0, 'h'},
    {0, 0, 0, 0}
};


struct {
    char *keystr;
    int mode;
} mode_maps[] = {
    { "center", SERV_MODE_CENTER_SERV },
    { "node", SERV_MODE_NODE_SERV}, 
    { "full", SERV_MODE_FULL_FUNC }, 
};

struct {
    char *desc;
    int (*init)(void);
    int (*release)(void);
} task_protos[] = {
    { "turn", turn_init, turn_release },
};


static int serv_mode_parse(const char *m) 
{
    int i;

    for(i=0; i<ARRAY_SIZE(mode_maps); i++) {
        if(!strcmp(mode_maps[i].keystr, m))	
            return mode_maps[i].mode;
    }

    return SERV_MODE_UNKNOWN;
}


static void init_task_protocals(void)
{
    int i;
    int ret = 0;

    task_protos_init();
    for(i=0; i<ARRAY_SIZE(task_protos); i++) {
        ret = task_protos[i].init();
        logi("init protos %s %s.\n", task_protos[i].desc, ret ? "fail":"success");
    }
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

static void do_help(void)
{
    int len = 0;
    char buf[1024] = { 0 };

    len += sprintf(buf + len, "Compile Date: %s,Time: %s, Version: %s\n", 
            __DATE__, __TIME__, VERSION);

    len += sprintf(buf + len, "\n"
            "usage: serv command [command options]\n" 
            "\n"
            "Command syntax:\n"
            "\tserv [-m full|node|center] [-s Host Address]\n"
            "\n"
            "Command parameters:\n"
            "\t'-m' or '--mode'    - Specify the server working mode.\n"
            "\t'-s' or '--server'  - Name of center server host address.\n"
            "\t'-v' or '--version' - show version num.\n"
            "\t'-h' or '--help'    - show this help message.\n");

    logi("%s\n", buf);
}


int main(int argc, char **argv)
{
    int opt;
    int mode = SERV_MODE_FULL_FUNC;
    char *chost = LOCAL_HOST;

    while((opt = getopt_long(argc, argv, "m:s:vh", longopts, NULL)) > 0) {
        switch(opt) {
            case 'm':
                mode = serv_mode_parse(optarg);
                break;
            case 's':
                chost = optarg;
                break;
            case 'v':
                logi("compilation date: %s,time: %s, version: %s\n", 
                        __DATE__, __TIME__, VERSION);
                return 0;
            case 'h':
            default:
                do_help();
                return 0;
        }
    }

    logi("server running. mode=%d\n", mode);

    umask(0);
    signals_init();

    common_init();
    init_task_protocals();

    if(mode & SERV_MODE_CENTER_SERV) {
        chost = LOCAL_HOST;
        center_serv_init();
    }

    if(mode & SERV_MODE_NODE_SERV)
        node_serv_init(chost);

    console_loop();

    return 0;
}

