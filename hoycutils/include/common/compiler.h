#ifndef _COMMON_COMPILER_H_
#define _COMMON_COMPILER_H_

#ifdef __GNUC__
#include <common/compiler-gcc.h>
#endif


#ifndef __always_inline
#define __always_inline inline
#endif

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)



#endif

