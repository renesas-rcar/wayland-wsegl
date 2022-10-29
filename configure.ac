# @File           configure.ac
# @Copyright      Copyright (C) 2021 Renesas Electronics Corporation. All rights reserved.
# @License        MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([wayland-wsegl], [3.1.1],
        [http://www.renesas.com/], [wayland-wsegl])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2 subdir-objects])
#AM_MAINTAINER_MODE

# Initialize libtool
AC_PROG_LIBTOOL

# Add libtool library version information
AC_SUBST([PVRWAYLAND_WSEGL_SO_VERSION], [3:1:1])

# Check headers
AC_CHECK_HEADERS([wayland-egl-backend.h])

# Obtain compiler/linker options for dependencies
PKG_CHECK_MODULES([WAYLAND_SERVER], [wayland-server])
PKG_CHECK_MODULES([WAYLAND_CLIENT], [wayland-client])
PKG_CHECK_MODULES([WAYLAND_KMS], [wayland-kms >= 1.5.0])
PKG_CHECK_MODULES([LIBGBM], [gbm >= 10.3.0])
PKG_CHECK_MODULES([LIBDRM], [libdrm >= 2.4.60])
PKG_CHECK_MODULES([LIBKMS], [libkms])
PKG_CHECK_MODULES([LIBEGL], [egl])

PKG_CHECK_MODULES([WAYLAND_EGL], [wayland-egl >= 18.1.0],
		  [AC_DEFINE([HAVE_WAYLAND_EGL_18_1_0], 1, [Define to 1 if wayland-egl version is 18.1.0 or later])],
		  [AC_MSG_NOTICE([wayland-egl < 18.1.0])])

PKG_CHECK_MODULES(WAYLAND_PROTOCOLS, [wayland-protocols],
		  [ac_wayland_protocols_pkgdatadir=`$PKG_CONFIG --variable=pkgdatadir wayland-protocols`])
PKG_CHECK_MODULES(WAYLAND_PROTOCOLS, [wayland-protocols >= 1.11.0],
		  [AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, $ac_wayland_protocols_pkgdatadir)],
		  [AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, $PKG_CONFIG_SYSROOT_DIR$ac_wayland_protocols_pkgdatadir)])

# Check for wayland-scanner
AC_CHECK_PROG([WAYLAND_SCANNER], [wayland-scanner], [wayland-scanner], [no])
if test x"${WAYLAND_SCANNER}" == x"no" ; then
	AC_MSG_ERROR([Please install wayland-scanner.])
fi

# Compile option
AC_ARG_ENABLE(debug,
	      [AS_HELP_STRING([--enable-debug],
			      [enable debugging, default: no])],
	      [case "${enableval}" in
		yes) debug=true ;;
		no)  debug=false ;;
		*) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
	       esac], [debug=false])
AM_CONDITIONAL([DEBUG], [test x$debug = xtrue])

AC_ARG_ENABLE(gcov,
	      [AS_HELP_STRING([--enable-gcov],
			      [Enable optional gcov coverage analysis, default: no])],
	      [case "${enableval}" in
	        yes) gcov=true ;;
		no)  gcov=false ;;
		*) AC_MSG_ERROR([bad value ${enableval} for --enable-gcov]) ;;
	       esac], [gcov=false])
AM_CONDITIONAL([HAVE_GCOV], [test x$gcov = xtrue])

AC_ARG_ENABLE(native_fence_sync,
	      [AS_HELP_STRING([--enable-native-fence-sync],
			      [enable native fence sync, default: yes])],
	      [case "${enableval}" in
	        yes) native_fence_sync=true ;;
		no)  native_fence_sync=false ;;
		*) AC_MSG_ERROR([bad value ${enableval} for --enable-native-fence-sync]) ;;
	       esac], [native_fence_sync=true])
AM_CONDITIONAL([NATIVE_FENCE_SYNC], [test x$native_fence_sync = xtrue])

AC_ARG_ENABLE(standalone_build,
	      [AS_HELP_STRING([--enable-standalone-build],
			      [enable standalone build, default: yes])],
	      [case "${enableval}" in
	        yes) standalone_build=true ;;
		no)  standalone_build=false ;;
		*) AC_MSG_ERROR([bad value ${enableval} for --enable-standalone-build]) ;;
	       esac], [standalone_build=true])
AM_CONDITIONAL([STANDALONE_BUILD], [test x$standalone_build = xtrue])

# Default WSEGL drivers
WSEGL_DEFAULT="r8a7795"

AC_ARG_WITH([wsegl],
	    [AS_HELP_STRING([--with-wsegl@<:@=NAME@:>@],
		            [WSEGL target to build ("r8a7795"(H3), "r8a7796"(M3)) @<:@default=r8a7795@:>@])],
	    [with_wsegl="$withval"],
	    [with_wsegl="$WSEGL_DEFAULT"])

case "$with_wsegl" in
	yes) with_wsegl="$WSEGL_DEFAULT" ;;
        no) with_wsegl='' ;;
esac

if test "x$with_wsegl" = x; then
	AC_MSG_ERROR([WSEGL target to build must be specified.])
else
	case "x$with_wsegl" in
	xr8a7795 | xr8a7796)
		;;
	*)
		AC_MSG_ERROR([Unknown WSEGL target: $with_wsegl])
		;;
	esac
fi

AC_SUBST([POWERVR_CFLAGS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
