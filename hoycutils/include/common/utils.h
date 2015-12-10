#ifndef _COMMON_UTILS_H_
#define _COMMON_UTILS_H_

#include <stdint.h>
#include <time.h>

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

/*
 *  Determine whether some value is a power of two, where zero is
 * *not* considered a power of two.
 */
static inline __attribute__((const))
int is_power_of_2(unsigned long n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

static inline int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}


static inline unsigned fls_long(unsigned long l)
{
	return fls(l);
}

/*
 * round up to nearest power of two
 */
static inline __attribute__((const))
unsigned long roundup_pow_of_two(unsigned long n)
{
	return 1UL << fls_long(n - 1);
}

/*
 * round down to nearest power of two
 */
static inline __attribute__((const))
unsigned long rounddown_pow_of_two(unsigned long n)
{
	return 1UL << (fls_long(n) - 1);
}


#endif

