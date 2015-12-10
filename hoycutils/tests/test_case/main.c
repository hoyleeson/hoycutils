#include <stdio.h>
#include <common/common.h>

#include "test_case.h"



int main(int argc, char **argv)
{
	int i;
	int ret;
	int result = 0;
	struct test_case *tcase;

	for(i=0; i<test_case_size; i++) {
		tcase = cases + i;
		if(tcase->func != NULL) {
			printf("\n\n==========================================================\n");
			printf("test case [%d]: %s\n", i, tcase->name);
			//printf("%s\n", tcase->desc);

			ret = tcase->func(argc - 1, ++argv);
			if(ret) {
				result++;
			}
		}
	}

	printf("\n\n=======================end================================\n");
	printf("result: failed count:%d\n", result);
}
