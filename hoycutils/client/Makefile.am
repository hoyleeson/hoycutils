AUTOMAKE_OPTIONS = foreign

lib_LTLIBRARIES = libclient.la
libclient_la_SOURCES = client.c
libclient_la_LIBADD = $(top_srcdir)/common/libcommon.a
libclient_la_CFLAGS = @GLOBAL_CFLAGS@

clientincludedir=$(includedir)/client
clientinclude_HEADERS=$(top_srcdir)/client/client.h

