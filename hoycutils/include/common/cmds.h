/*
 * include/common/cmds.h
 * 
 * 2016-01-01  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#ifndef _COMMON_CMDS_H_
#define _COMMON_CMDS_H_

struct cmd_tbl_s {
    const char *name;   /* command name*/
    int (*cmd)(int, char **); /* implement function*/
    const char *usage;  /* usage message(short)*/
};

typedef struct cmd_tbl_s cmd_tbl_t;

#define CONSOLE_CMD(name, cmd, usage)   \
    { #name, cmd, usage }

#define CMD(name, maxargs, cmd, usage)   \
    cmd_tbl_t _cmd_##name ={ #name, cmd, usage }


cmd_tbl_t* get_cmd_tbl_list(void);

#endif

