/*
 * include/common/configs.h
 * 
 * 2016-01-01  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#ifndef _COMMON_CONFIGS_H_
#define _COMMON_CONFIGS_H_

#define DEFAULT_CONFIGS_NAME 	"configs.conf"

void exec_deamons(void);
void exec_commands(void);
int init_configs(const char *fname);

#endif
