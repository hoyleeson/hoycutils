#include <stdlib.h>

#include <common/pack_head.h>
#include <protos.h>
#include <common/log.h>


pack_head_t *create_pack(uint8_t type, uint32_t len)
{
    pack_head_t *pack;
    pack = (pack_head_t *)malloc(sizeof(*pack) + len);	
    if(!pack)
        return NULL;

    pack->magic = PROTOS_MAGIC;
    pack->version = PROTOS_VERSION;

    pack->type = type;
    pack->datalen = len;
    return pack;
}

void init_pack(pack_head_t *pack, uint8_t type, uint32_t len)
{
    pack->magic = PROTOS_MAGIC;
    pack->version = PROTOS_VERSION;

    pack->type = type;
    pack->datalen = len;
}


void free_pack(pack_head_t *pack)
{
    free(pack);
}

