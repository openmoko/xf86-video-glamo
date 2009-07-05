#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

AC_PREREQ(2.57)
AC_INIT([xf86-video-glamo],
        0.4.0,
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        xf86-video-glamo)

AC_CONFIG_SRCDIR([Makefile.am])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

# Checks for programs.
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CC

AH_TOP([#include "xorg-server.h"])

AC_ARG_WITH(xorg-module-dir,
            AC_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])

AC_ARG_ENABLE(jbt6k74-set-state, AS_HELP_STRING([--disable-jbt6k74-set-state],
                                 [Whether the driver should set the jbt6k74
                                 state when changing resolution  (default:
                                 enabled)]),
             [JBT6K74_SET_STATE=$enableval], [JBT6K74_SET_STATE=yes])

AC_ARG_WITH(jbt6k74-state-path, AS_HELP_STRING([--with-jbt6k74-state-path=PATH],
                                               [Path to the jbt6k74 sysfs state file [[default=/sys/bus/spi/devices/spi2.0/state]]]),
           [JBT6K74_STATE_PATH="$withval"],
           [JBT6K74_STATE_PATH="/sys/bus/spi/devices/spi2.0/state"])

AC_ARG_ENABLE(engine-ioctls, AS_HELP_STRING([--enable-engine-ioctls],
                                 [Whether the driver should use framebuffer
                                 ioctls to enable/disable engines. This requires
                                 kernel support. (default: disabled)]),
             [HAVE_ENGINE_IOCTLS=$enableval], [HAVE_ENGINE_IOCTLS=no])
# Checks for extensions
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(XV, videoproto)

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG, [xorg-server >= 1.0.99.901 xproto fontsproto $REQUIRED_MODULES])
sdkdir=$(pkg-config --variable=sdkdir xorg-server)

if test "x$JBT6K74_SET_STATE" = xyes; then
    AC_DEFINE(JBT6K74_SET_STATE, 1, [Set jbt6k74 state when changing resolution])
    AC_DEFINE_UNQUOTED(JBT6K74_STATE_PATH, "$JBT6K74_STATE_PATH", [Path to the jbt6k74 sysfs state path])
fi

if test "x$HAVE_ENGINE_IOCTLS" = xyes; then
    AC_DEFINE(HAVE_ENGINE_IOCTLS, 1, [Use ioctls to enable/disable engines])
fi

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

AC_SUBST([XORG_CFLAGS])
AC_SUBST([moduledir])

DRIVER_NAME=glamo
AC_SUBST([DRIVER_NAME])

XORG_MANPAGE_SECTIONS
XORG_RELEASE_VERSION

AC_OUTPUT([
	Makefile
	src/Makefile
	man/Makefile
])
