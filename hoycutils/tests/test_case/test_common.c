#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <common/list.h>
#include <common/log.h>
#include <common/configs.h>
#include <common/workqueue.h>


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

struct test_wq
{
    struct work_struct work;
    int val;
};

static void handle_work(struct work_struct *work)
{
    struct test_wq *twq; 

    twq = container_of(work, struct test_wq, work); 
    printf("val:%d\n", twq->val);
}

int test_workqueue(int argc, char **argv)
{
    struct workqueue_struct *wq;
    struct test_wq twq; 

    init_workqueues();
    wq = create_workqueue();

    INIT_WORK(&twq.work, handle_work);
    twq.val = 35;

    queue_work(wq, &twq.work);
    sleep(1);
    return 0;
}
