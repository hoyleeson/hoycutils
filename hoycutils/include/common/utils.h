#ifndef _COMMON_UTILS_H_
#define _COMMON_UTILS_H_

#include <stdint.h>

#include <common/types.h>

void* xalloc(size_t   sz);
void* xzalloc(size_t  sz);
void* xrealloc(void*  block, size_t  size);

int hexdigit( int  c );
int hex2int(const uint8_t*  data, int  len);
void int2hex(int  value, uint8_t*  to, int  width);
int fd_read(int  fd, void*  to, int  len);
int fd_write(int  fd, const void*  from, int  len);
void fd_setnonblock(int  fd);
int fd_accept(int  fd);

void *read_file(const char *fn, unsigned *_sz);
time_t gettime(void);

#define  xnew(p)   do { (p) = xalloc(sizeof(*(p))); } while(0)
#define  xznew(p)   do { (p) = xzalloc(sizeof(*(p))); } while(0)
#define  xfree(p)    do { (free((p)), (p) = NULL); } while(0)
#define  xrenew(p,count)  do { (p) = xrealloc((p),sizeof(*(p))*(count)); } while(0)

#endif

