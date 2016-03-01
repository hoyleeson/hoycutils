/*
 * include/common/compiler-gcc.h
 * 
 * 2016-01-01  written by Hoyleeson <hoyleeson@gmail.com>
 *	Copyright (C) 2015-2016 by Hoyleeson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.
 *
 */

#ifndef _COMMON_COMPILER_GCC_H_
#define _COMMON_COMPILER_GCC_H_

#if 0
/* defined in sys/cdefs.h */
#define __always_inline     inline __attribute__((always_inline))
#endif

#define barrier() __asm__ __volatile__("": : :"memory")

#endif

