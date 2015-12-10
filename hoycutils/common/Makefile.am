AUTOMAKE_OPTIONS = foreign

AM_CFLAGS = -I$(top_srcdir)/include 

noinst_LIBRARIES = libcommon.a
libcommon_a_SOURCES = common.c log.c notifier.c timer.c utils.c wait.c workqueue.c \
					  completion.c parser.c configs.c mempool.c commands.c poller.c \
					  fifo.c \
					  parser.h keywords.h

#libcommon_a_LIBADD = librt.a

DEFS+=-D_GNU_SOURCE
libcommon_a_CFLAGS = @GLOBAL_CFLAGS@

