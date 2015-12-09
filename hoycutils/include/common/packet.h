#ifndef _COMMON_PACKET_H_
#define _COMMON_PACKET_H_

#include <stdint.h>
#include <common/list.h>

struct packet 
{
	struct list_head node;

	uint32_t len;
	uint8_t data[0];	
};



#endif

