#ifndef _COMMON_PACK_HEAD_H_
#define _COMMON_PACK_HEAD_H_

#include <stdint.h>

#define PROTOS_MAGIC        (0x2016)	
#define PROTOS_VERSION      (1)

typedef struct _pack_header {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t seqnum;
    uint8_t chsum;
    uint8_t _reserved1;
    uint32_t datalen;
    uint8_t data[0];
}__attribute__((packed)) pack_head_t;


#define pack_head_len() 	sizeof(pack_head_t)

pack_head_t *create_pack(uint8_t type, uint32_t len);
void init_pack(pack_head_t *pack, uint8_t type, uint32_t len);
void free_pack(pack_head_t *pack);


#endif

