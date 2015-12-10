#ifndef _TEST_CASE_TEST_CASE_H_
#define _TEST_CASE_TEST_CASE_H_

struct test_case
{
	char *name;
	char *desc;
	int (*func)(int argc, char **argv);
};

extern int test_list(int argc, char **argv);
extern int test_configs(int argc, char **argv);
extern int test_fifo(int argc, char **argv);

extern struct test_case cases[];
extern int test_case_size;

#endif
