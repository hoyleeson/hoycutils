#ifndef _COMMON_HWEIGHT_H_
#define _COMMON_HWEIGHT_H_

#include <stdint.h>

/*
 * Compile time versions of __arch_hweightN()
 */
#define __const_hweight8(w)		\
	((unsigned int)			\
	 ((!!((w) & (1ULL << 0))) +	\
	  (!!((w) & (1ULL << 1))) +	\
	  (!!((w) & (1ULL << 2))) +	\
	  (!!((w) & (1ULL << 3))) +	\
	  (!!((w) & (1ULL << 4))) +	\
	  (!!((w) & (1ULL << 5))) +	\
	  (!!((w) & (1ULL << 6))) +	\
	  (!!((w) & (1ULL << 7)))))

#define __const_hweight16(w) (__const_hweight8(w)  + __const_hweight8((w)  >> 8 ))
#define __const_hweight32(w) (__const_hweight16(w) + __const_hweight16((w) >> 16))
#define __const_hweight64(w) (__const_hweight32(w) + __const_hweight32((w) >> 32))

unsigned int __hweight32(unsigned int w);
unsigned int __hweight16(unsigned int w);
unsigned int __hweight8(unsigned int w);
unsigned long __hweight64(uint64_t w);

/*
 * Generic interface.
 */
#define hweight8(w)  (__builtin_constant_p(w) ? __const_hweight8(w)  : __hweight8(w))
#define hweight16(w) (__builtin_constant_p(w) ? __const_hweight16(w) : __hweight16(w))
#define hweight32(w) (__builtin_constant_p(w) ? __const_hweight32(w) : __hweight32(w))
#define hweight64(w) (__builtin_constant_p(w) ? __const_hweight64(w) : __hweight64(w))

/*
 * Interface for known constant arguments
 */
#define HWEIGHT8(w)  (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight8(w))
#define HWEIGHT16(w) (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight16(w))
#define HWEIGHT32(w) (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight32(w))
#define HWEIGHT64(w) (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight64(w))

/*
 * Type invariant interface to the compile time constant hweight functions.
 */
#define HWEIGHT(w)   HWEIGHT64((u64)w)

#endif /* _ASM_GENERIC_BITOPS_CONST_HWEIGHT_H_ */
