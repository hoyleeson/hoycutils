#ifndef _COMMON_TYPES_H_
#define _COMMON_TYPES_H_

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;

#ifndef bool
typedef int bool;
#define  TRUE 	(1)
#define  FALSE 	(0)
#endif

#endif
