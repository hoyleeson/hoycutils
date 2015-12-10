AUTOMAKE_OPTIONS = foreign

AM_CFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = mgr_serv
mgr_serv_SOURCES = mgr_serv.c 
mgr_serv_LDADD = $(top_srcdir)/common/libcommon.a
mgr_serv_CFLAGS = @GLOBAL_CFLAGS@

DEFS+=-D_GNU_SOURCE

