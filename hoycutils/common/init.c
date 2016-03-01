/*
 * common/init.c
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
#include <stdlib.h>

#include <common/log.h>
#include <common/utils.h>
#include <common/mempool.h>
#include <common/timer.h>
#include <common/ioasync.h>
#include <common/workqueue.h>
#include <common/idr.h>


int common_init(void)
{
    mem_cache_init();
    init_workqueues();
    global_ioasync_init();
    init_timers();

    idr_init_cache();

    return 0;
}


