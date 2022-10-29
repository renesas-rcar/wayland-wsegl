# @File           Makefile.am
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

AM_CFLAGS = \
	-Wall -DLINUX

if DEBUG
AM_CFLAGS += -DDEBUG -O2 -g
else
AM_CFLAGS += -O3
endif

if HAVE_GCOV
AM_CFLAGS += -fprofile-arcs -ftest-coverage
endif

if NATIVE_FENCE_SYNC
AM_CFLAGS += -DSUPPORT_NATIVE_FENCE_SYNC
endif

if STANDALONE_BUILD
AM_CFLAGS += -DREL_STANDALONE_BUILD
endif

TARGET_WSEGL = libpvrWAYLAND_WSEGL.la
AM_CFLAGS += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/../../hwdefs

lib_LTLIBRARIES = $(TARGET_WSEGL)

WSEGL_CORE_SOURCES = \
	src/waylandws.c \
	src/waylandws_server.c \
	src/waylandws_client.c \
	linux-dmabuf-unstable-v1-protocol.c

WSEGL_CORE_CFLAGS = \
	$(AM_CFLAGS) \
	@POWERVR_CFLAGS@ \
	@WAYLAND_SERVER_CFLAGS@ \
	@WAYLAND_CLIENT_CFLAGS@ \
	@WAYLAND_KMS_CFLAGS@ \
	@LIBGBM_CFLAGS@ \
	@LIBKMS_CFLAGS@ \
	@LIBDRM_CFLAGS@ \
	@LIBEGL_CFLAGS@

WSEGL_CORE_LIBADD = \
	@WAYLAND_CLIENT_LIBS@ \
	@WAYLAND_SERVER_LIBS@ \
	@WAYLAND_KMS_LIBS@ \
	@LIBGBM_LIBS@ \
	@LIBKMS_LIBS@ \
	@LIBDRM_LIBS@

libpvrWAYLAND_WSEGL_la_SOURCES = \
	$(WSEGL_CORE_SOURCES) \
	src/waylandws_pvrsrv.c

libpvrWAYLAND_WSEGL_la_CFLAGS = \
	$(WSEGL_CORE_CFLAGS)

libpvrWAYLAND_WSEGL_la_LIBADD = \
	$(WSEGL_CORE_LIBADD) \
	-lsrv_um

noinst_HEADERS = \
	src/waylandws.h \
	src/waylandws_client.h \
	src/waylandws_server.h \
	src/waylandws_pvr.h \
	linux-dmabuf-unstable-v1-client-protocol.h

EXTRA_DIST = linux-dmabuf-unstable-v1.xml
CLEANFILES = linux-dmabuf-unstable-v1-protocol.c linux-dmabuf-unstable-v1-client-protocol.h

src/waylandws_client.c: linux-dmabuf-unstable-v1-client-protocol.h

.SECONDEXPANSION:

define protostability
$(if $(findstring unstable,$1),unstable,stable)
endef

define protoname
$(shell echo $1 | sed 's/\([a-z\-]\+\)-[a-z]\+-v[0-9]\+/\1/')
endef

%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(WAYLAND_SCANNER) code < $< > $@

%-server-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(WAYLAND_SCANNER) server-header < $< > $@

%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(WAYLAND_SCANNER) client-header < $< > $@
