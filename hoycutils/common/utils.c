#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <common/log.h>

#define DEFAULT_NET_DEV     "eth0"

/** UTILITIES
 **/

void* xalloc(size_t sz)
{
    void*  p;

    if (sz == 0)
        return NULL;

    p = malloc(sz);
    if (p == NULL)
        fatal("not enough memory");

    return p;
}

void* xzalloc(size_t  sz)
{
    void*  p = xalloc(sz);
    memset(p, 0, sz);
    return p;
}

void* xrealloc(void*  block, size_t  size)
{
    void*  p = realloc(block, size);

    if (p == NULL && size > 0)
        fatal("not enough memory");

    return p;
}

int hexdigit( int  c )
{
    unsigned  d;

    d = (unsigned)(c - '0');
    if (d < 10) return d;

    d = (unsigned)(c - 'a');
    if (d < 6) return d+10;

    d = (unsigned)(c - 'A');
    if (d < 6) return d+10;

    return -1;
}

int hex2int(const uint8_t*  data, int  len)
{
    int  result = 0;
    while (len > 0) {
        int       c = *data++;
        unsigned  d;

        result <<= 4;
        do {
            d = (unsigned)(c - '0');
            if (d < 10)
                break;

            d = (unsigned)(c - 'a');
            if (d < 6) {
                d += 10;
                break;
            }

            d = (unsigned)(c - 'A');
            if (d < 6) {
                d += 10;
                break;
            }

            return -1;
        }
        while (0);

        result |= d;
        len    -= 1;
    }
    return  result;
}

void int2hex(int  value, uint8_t*  to, int  width)
{
    int  nn = 0;
    static const char hexchars[16] = "0123456789abcdef";

    for (--width; width >= 0; width--, nn++) {
        to[nn] = hexchars[(value >> (width*4)) & 15];
    }
}

int fd_read(int  fd, void*  to, int  len)
{
    int  ret;

    do {
        ret = read(fd, to, len);
    } while (ret < 0 && errno == EINTR);

    return ret;
}

int fd_write(int  fd, const void*  from, int  len)
{
    int  ret;

    do {
        ret = write(fd, from, len);
    } while (ret < 0 && errno == EINTR);

    return ret;
}

void fd_setnonblock(int  fd)
{
    int  ret, flags;

    do {
        flags = fcntl(fd, F_GETFD);
    } while (flags < 0 && errno == EINTR);

    if (flags < 0) {
        fatal("%s: could not get flags for fd %d: %s",
                __FUNCTION__, fd, strerror(errno));
    }

    do {
        ret = fcntl(fd, F_SETFD, flags | O_NONBLOCK);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        fatal("%s: could not set fd %d to non-blocking: %s",
                __FUNCTION__, fd, strerror(errno));
    }
}


int fd_accept(int  fd)
{
    struct sockaddr  from;
    socklen_t        fromlen = sizeof(from);
    int              ret;

    do {
        ret = accept(fd, &from, &fromlen);
    } while (ret < 0 && errno == EINTR);

    return ret;
}




/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
time_t gettime(void)
{
#if 0
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        loge("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return 0;
    }

    return ts.tv_sec;
#else
    return time(NULL);
#endif
}


/* reads a file, making sure it is terminated with \n \0 */
void *read_file(const char *fname, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;
    struct stat sb;

    data = 0;
    fd = open(fname, O_RDONLY);
    if(fd < 0)
        return 0;

    // for security reasons, disallow world-writable
    // or group-writable files
    if (fstat(fd, &sb) < 0) {
        loge("fstat failed for '%s'\n", fname);
        goto oops;
    }
    if ((sb.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        loge("skipping insecure file '%s'\n", fname);
        goto oops;
    }

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0)
        goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) 
        goto oops;

    data = (char*) malloc(sz + 2);
    if(data == 0)
        goto oops;

    if(read(fd, data, sz) != sz) 
        goto oops;

    close(fd);
    data[sz] = '\n';
    data[sz+1] = 0;
    if(_sz)
        *_sz = sz;

    return data;

oops:
    close(fd);
    if(data != 0)
        free(data);
    return 0;
}

int get_ipaddr(const char* eth, char* ipaddr)
{
    int i = 0;
    int sockfd;
    struct ifconf ifconf;
    char buf[512];
    struct ifreq *ifreq;
    char *dev = (char *)eth;

    if(!dev) {
        dev = DEFAULT_NET_DEV;
    }

    ifconf.ifc_len = 512;
    ifconf.ifc_buf = buf;

    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0))<0) {
        perror("socket");
        exit(1);
    }

    ioctl(sockfd, SIOCGIFCONF, &ifconf);
    ifreq = (struct ifreq*)buf;

    for(i=(ifconf.ifc_len/sizeof(struct ifreq)); i>0; i--) {
        if(strcmp(ifreq->ifr_name, dev)==0) {
            strcpy(ipaddr, inet_ntoa(((struct sockaddr_in*)&(ifreq->ifr_addr))->sin_addr));
            return 0;
        }

        ifreq++;
    }
    return -EINVAL;
}

