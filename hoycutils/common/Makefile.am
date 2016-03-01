
AM_CFLAGS = @GLOBAL_CFLAGS@ -fPIC -I$(top_srcdir)/include 

noinst_LIBRARIES = libcommon.a
libcommon_a_SOURCES = common.c log.c notifier.c timer.c utils.c wait.c workqueue.c completion.c \
					  parser.c configs.c mempool.c commands.c queue.c fifo.c bsearch.c rbtree.c \
					  bitmap.c find_bit.c hweight.c idr.c dump_stack.c poller.c ioasync.c \
					  init.c hbeat.c data_frag.c packet.c pack_head.c iowait.c \
					  netsock.c sock_stream.c sock_dgram.c ethtools.c sockets.c \
					  parser.h keywords.h 
