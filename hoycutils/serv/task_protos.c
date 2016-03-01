/*
 * serv/task_protos.c
 * 
 * 2016-01-05  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#include <pthread.h>

#include <common/list.h>
#include "task.h"

static struct list_head task_protos_list;
static pthread_mutex_t task_protos_lock;

void task_protos_register(struct task_operations *ops) 
{
    pthread_mutex_lock(&task_protos_lock);
    list_add_tail(&ops->entry, &task_protos_list);
    pthread_mutex_unlock(&task_protos_lock);
}

void task_protos_unregister(struct task_operations *ops)
{
    pthread_mutex_lock(&task_protos_lock);
    list_del(&ops->entry);
    pthread_mutex_unlock(&task_protos_lock);
}

struct task_operations *find_task_protos_by_type(int type) 
{
    struct task_operations *ops;

    pthread_mutex_lock(&task_protos_lock);
    list_for_each_entry(ops, &task_protos_list, entry) {
        if(ops->type == type) {
            pthread_mutex_unlock(&task_protos_lock);
            return ops;
        }
    }
    pthread_mutex_unlock(&task_protos_lock);

    return NULL;
}

void task_protos_init(void)
{
    INIT_LIST_HEAD(&task_protos_list);
    pthread_mutex_init(&task_protos_lock, NULL);
}

