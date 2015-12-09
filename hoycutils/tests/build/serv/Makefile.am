AUTOMAKE_OPTIONS = foreign

INCLUDES= -I$(top_srcdir)/include -I$(top_srcdir)/common/include

bin_PROGRAMS = serv
serv_SOURCES = serv.c serv_lib.c
serv_LDADD = -L$(top_srcdir)/libs -lhello_lib $(top_srcdir)/common/libcommon.a

DEFS+=-D_GNU_SOURCE

