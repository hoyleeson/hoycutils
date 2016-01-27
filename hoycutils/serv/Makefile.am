
AM_CFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = serv
serv_SOURCES = center_serv.c  cli_mgr.c  cli_mgr.h  main.c  node_mgr.c  node_mgr.h \
			   node_serv.c  protos_internal.h  serv.h  task.h  turn.c  turn.h  \
			   task_protos.c

serv_LDADD = $(top_srcdir)/common/libcommon.a $(LIBS_common) $(LIBS_serv) $(LIBS_serv_extra) $(LIBPTHREAD) 

