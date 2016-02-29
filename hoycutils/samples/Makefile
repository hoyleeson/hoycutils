CC:=gcc

CFLAGS := -I. -I./include -g2 -O0 -Wall
LDFLAGS := -lpthread -lrt -L./lib -lclient

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

ifdef ARCH
	sample_rooms = sample_rooms-$(ARCH)
	sample_netplay = sample_netplay-$(ARCH)
else
	sample_rooms = sample_rooms 
	sample_netplay = sample_netplay
endif

ROOMS_OBJS=client_sample_rooms.o 
NETPLAY_OBJS=client_sample_netplay.o 

all: $(sample_rooms) $(sample_netplay)

$(sample_rooms): $(ROOMS_OBJS)
	$(CC) -o $@ $(ROOMS_OBJS) $(LDFLAGS)

$(sample_netplay): $(NETPLAY_OBJS)
	$(CC) -o $@ $(NETPLAY_OBJS) $(LDFLAGS)

.PHONY: clean

.depend: $(SRCS)
	$(CC) $(CFLAGS) -MM $(SRCS) >$@
	sinclude .depend

clean:
	rm -f *.o .depend $(sample_rooms) $(sample_netplay)

