
AM_CFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = serv
serv_SOURCES = main.c task_protos.c center_serv.c cli_mgr.c node_mgr.c node_serv.c \
			   turn.c \
			   cli_mgr.h node_mgr.h protos_internal.h serv.h task.h turn.h 

serv_LDADD = $(top_srcdir)/common/libcommon.a $(LIBS_common) $(LIBS_serv) $(LIBS_serv_extra) $(LIBPTHREAD) 

