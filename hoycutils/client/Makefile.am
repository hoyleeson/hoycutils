
AM_CFLAGS = -I$(top_srcdir)/include

lib_LTLIBRARIES = libclient.la
libclient_la_SOURCES = client.c
libclient_la_LIBADD = $(top_srcdir)/common/libcommon.a $(LIBS_common) $(LIBS_serv) $(LIBS_serv_extra)

clientincludedir=$(includedir)
clientinclude_HEADERS=client.h

