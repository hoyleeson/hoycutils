#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common/list.h>
#include <common/configs.h>


struct test_list_st
{
	int num;
	struct list_head node;
};

int test_list(int argc, char **argv)
{
	int i;
	struct test_list_st *tlist, *n;
	LIST_HEAD(list);

	for(i=0; i<10; i++) {
		tlist = malloc(sizeof(*tlist));
		tlist->num = i;
		list_add(&tlist->node, &list);	
	}
	list_for_each_entry_safe(tlist, n, &list, node) {
		printf("%d ", tlist->num);
		list_del(&tlist->node);
		free(tlist);
	}
	printf("\n");

	if(list_empty(&list))
		return 0;
	return -1;
}

int test_configs(int argc, char **argv)
{
	init_configs("configs/configs.conf");

	exec_commands();
	exec_deamons();
	return 0;
}

