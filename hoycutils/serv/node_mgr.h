/*
 * serv/node_mgr.h
 * 
 * 2016-01-05  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#ifndef _SERV_NODE_MGR_H_
#define _SERV_NODE_MGR_H_

#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <common/list.h>
#include <common/idr.h>
#include <common/iowait.h>
#include <common/ioasync.h>

#include "task.h"

typedef struct _node_mgr node_mgr_t;
typedef struct _node_info node_info_t;

struct _node_info {
    int fd;
    iohandler_t *hand;
    iowait_t waits;
    int nextseq;
    int task_count;
    int priority;

    struct list_head entry;
    struct sockaddr_in addr;
    node_mgr_t *mgr;

    struct list_head tasklist;
    pthread_mutex_t lock;
};

struct _node_mgr {
    struct ida taskids;
    int node_count;
    iohandler_t *hand;

    struct list_head nodelist;
    pthread_mutex_t lock;
};

typedef struct _task_handle {
    int taskid;
    int type;
    int priority;

    struct sockaddr_in addr; 	/* assign response */
    struct task_operations *ops;

    node_info_t *node;
    struct list_head entry;
} task_handle_t;

node_mgr_t *node_mgr_init(void);
task_handle_t *nodemgr_task_assign(node_mgr_t *mgr, int type, int priority, task_baseinfo_t *base);
int nodemgr_task_reclaim(node_mgr_t *mgr, task_handle_t *task, task_baseinfo_t *base);
int nodemgr_task_control(node_mgr_t *mgr, task_handle_t *task, int opt, task_baseinfo_t *base);

#endif

