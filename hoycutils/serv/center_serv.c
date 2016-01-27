#include <stdlib.h>

#include <common/log.h>

#include "cli_mgr.h"
#include "node_mgr.h"

typedef struct _center_serv {
    cli_mgr_t *climgr;
    node_mgr_t *nodemgr;
} center_serv_t;

static center_serv_t center_serv;


int center_serv_init(void) 
{
    logi("center server start.\n");
    center_serv_t *cs = &center_serv;

    cs->nodemgr = node_mgr_init();
    cs->climgr = cli_mgr_init(cs->nodemgr);

    return 0;
}

