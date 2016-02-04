#ifndef _COMMON_BSEARCH_H_
#define _COMMON_BSEARCH_H_

#include <unistd.h>
#include <stdint.h>

void *bsearch(const void *key, const void *base, size_t num, size_t size,
        int (*cmp)(const void *key, const void *elt));

#define BSEARCH_MATCH_UP 		(0)
#define BSEARCH_MATCH_DOWN 		(1)

void *bsearch_edge(const void *key, const void *base, size_t num, size_t size, int edge,
        int (*cmp)(const void *key, const void *elt));

#endif

