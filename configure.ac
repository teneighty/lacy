AC_PREREQ([2.65])
AC_INIT([lacy], [0.0.1], [tmhorton@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h unistd.h])

AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_HEADER_STDBOOL
AC_CHECK_FUNCS([memset mkdir socket])

AC_CONFIG_FILES([
    Makefile
    src/Makefile
])
AC_OUTPUT

