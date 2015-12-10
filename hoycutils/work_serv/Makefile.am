AUTOMAKE_OPTIONS = foreign

AM_CFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = work_serv
work_serv_SOURCES = work_serv.c 
work_serv_LDADD = $(top_srcdir)/common/libcommon.a
work_serv_CFLAGS = @GLOBAL_CFLAGS@

DEFS+=-D_GNU_SOURCE

