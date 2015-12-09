#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])

AC_INIT(turn, 1.0.00, hoyleeson@163.com)

AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_PROG_RANLIB
AC_PROG_LIBTOOL

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# Substitutions for .in files

AC_DEFINE(CONFIG_POOL_THREAD_COUNT, 16, "Max number of threads in the thread pool.")
AC_DEFINE(CONFIG_CONFIGS_FILE_NAME, "configs.conf", "System configs file name.")

GLOBAL_CFLAGS=-DVDEBUG

AC_SUBST(GLOBAL_CFLAGS)

AC_CONFIG_FILES([Makefile
				 client/Makefile
				 common/Makefile
				 include/Makefile include/common/Makefile 
				 configs/Makefile
				 mgr_serv/Makefile
				 work_serv/Makefile
				 tests/Makefile tests/test_case/Makefile
				 docs/Makefile])
AC_OUTPUT
