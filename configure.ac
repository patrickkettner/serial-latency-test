AC_PREREQ(2.59)
AC_INIT([serial-latency-test], [0.0.1], [jakob.flierl@gmail.com])
AC_CONFIG_SRCDIR([src/serial-latency-test.c])
AM_INIT_AUTOMAKE([-Wall foreign])

AC_PREFIX_DEFAULT(/usr)

AM_MAINTAINER_MODE([enable])

dnl Checks for programs.

dnl try to guess cross-compiler if not set
if test "x$target" != "x$host" -a -z "`echo $CC | grep -e '-gcc'`";
then
  AC_MSG_CHECKING(for cross-compiler)

  which ${program_prefix}gcc >/dev/null 2>&1 && CC=${program_prefix}gcc
  which ${target_cpu}-${target_os}-gcc >/dev/null 2>&1 \
  && CC=${target_cpu}-${target-os}-gcc
  which ${target_cpu}-${target_vendor}-${target_os}-gcc >/dev/null 2>&1 \
  && CC=${target_cpu}-${target_vendor}-${target_os}-gcc

  AC_MSG_RESULT($CC)
fi

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S

AC_HEADER_STDC
AC_CHECK_HEADERS([sched.h select.h sys/utsname.h termios.h sys/ioctl.h linux/serial.h linux/ioctl.h asm/ioctls.h])

test "x$prefix" = xNONE && prefix=$ac_default_prefix

eval dir="$datadir"
case "$dir" in
/*) ;;
*) dir="$prefix/share"
esac

AM_CONFIG_HEADER(include/config.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_HEADER_TIME

AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_FUNCS([memset])
AC_CHECK_FUNCS([select])
AC_CHECK_FUNCS([strerror])
AC_CHECK_FUNCS([strtol])
AC_CHECK_FUNCS([uname])
AC_CHECK_HEADERS([fcntl.h float.h limits.h mach/mach.h sys/time.h])

AC_PROG_RANLIB

AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

AC_CHECK_FUNCS([clock_gettime], [CLOCK_LIB=], [AC_CHECK_LIB([rt], [clock_gettime], [CLOCK_LIB=-lrt])])
AC_SUBST([CLOCK_LIB])

dnl Checks for library functions.
AC_PROG_GCC_TRADITIONAL

dnl Enable largefile support
AC_SYS_LARGEFILE

AC_OUTPUT(Makefile m4/Makefile src/Makefile include/Makefile)
