/*
 * serv/center_serv.c
 * 
 * 2016-01-05  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#include <stdlib.h>

#include <common/log.h>

#include "cli_mgr.h"
#include "node_mgr.h"

typedef struct _center_serv {
    cli_mgr_t *climgr;
    node_mgr_t *nodemgr;
} center_serv_t;

static center_serv_t center_serv;


int center_serv_init(void) 
{
    logi("center server start.\n");
    center_serv_t *cs = &center_serv;

    cs->nodemgr = node_mgr_init();
    cs->climgr = cli_mgr_init(cs->nodemgr);

    return 0;
}

