AUTOMAKE_OPTIONS = foreign

SUBDIRS = common libs serv client

INCLUDES= -I$(top_srcdir)/include -I$(top_srcdir)/common/include -I$(top_srcdir)/libs
export INCLUDES

bin_PROGRAMS = hello 
hello_SOURCES = hello.c libs/hello_lib.c include/hello_utils.h

hello_LDADD = $(top_srcdir)/common/libcommon.a

