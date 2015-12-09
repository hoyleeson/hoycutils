#ifndef _COMMON_BUG_H_
#define _COMMON_BUG_H_

#define BUILD_BUG_ON(condition)					\
	do {							\
		((void)sizeof(char[1 - 2*!!(condition)]));	\
		if (condition) __build_bug_on_failed = 1;	\
	} while(0)

/**
 * BUILD_BUG - break compile if used.
 *
 * If you have some code that you expect the compiler to eliminate at
 * build time, you should use BUILD_BUG to detect if it is
 * unexpectedly used.
 */
#define BUILD_BUG()						\
	do {							\
		extern void __build_bug_failed(void)		\
			__linktime_error("BUILD_BUG failed");	\
		__build_bug_failed();				\
	} while (0)



/* Force a compilation error if a constant expression is not a power of 2 */
#define BUILD_BUG_ON_NOT_POWER_OF_2(n)			\
	BUILD_BUG_ON((n) == 0 || (((n) & ((n) - 1)) != 0))

/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))
#define BUILD_BUG_ON_NULL(e) ((void *)sizeof(struct { int:-!!(e); }))

#endif
