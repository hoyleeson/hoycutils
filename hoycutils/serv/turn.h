/*
 * serv/turn.h
 * 
 * 2016-01-15  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#ifndef _SERV_TURN_H_
#define _SERV_TURN_H_

#include <sys/socket.h>
#include "cli_mgr.h"
#include "node_mgr.h"

enum turn_control_type {
    TURN_TYPE_USER_JOIN,
    TURN_TYPE_USER_LEAVE,
};

struct turn_info
{
    uint32_t taskid;
    struct sockaddr_in addr;
};

unsigned long turn_task_assign(node_mgr_t *mgr, group_info_t *group);
int turn_task_reclaim(node_mgr_t *mgr, unsigned long handle);
int turn_task_control(node_mgr_t *mgr, unsigned long handle, int opt, user_info_t *user);

int get_turn_info(node_mgr_t *mgr, unsigned long handle, struct turn_info *info);

static inline int turn_task_user_join(node_mgr_t *mgr, unsigned long handle, user_info_t *user)
{
    return turn_task_control(mgr, handle, TURN_TYPE_USER_JOIN, user);
}

static inline int turn_task_user_leave(node_mgr_t *mgr, unsigned long handle, user_info_t *user)
{
    return turn_task_control(mgr, handle, TURN_TYPE_USER_LEAVE, user);
}

int turn_init(void);
int turn_release(void);

#endif

