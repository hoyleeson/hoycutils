#include <stdio.h>
#include <stdlib.h>

#include <common/log.h>
#include <common/utils.h>
#include <common/timer.h>
#include <common/ioasync.h>
#include <common/workqueue.h>



int common_init(void)
{
    init_workqueues();
    global_ioasync_init();
    init_timers();

    return 0;
}



