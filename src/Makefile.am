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

AM_CFLAGS = @XORG_CFLAGS@ @CWARNFLAGS@

omapfb_drv_la_LTLIBRARIES = omapfb_drv.la
omapfb_drv_la_LDFLAGS = -module -avoid-version
omapfb_drv_ladir = @moduledir@/drivers

omapfb_drv_la_SOURCES = \
         omapfb-driver.c \
         omapfb-utils.c \
         omapfb-crtc.c \
         omapfb-output.c \
         omapfb-output-dss.c \
         omapfb-overlay-pool.c \
         omapfb-xv.c \
         omapfb-xv-generic.c \
         omapfb-xv-blizzard.c \
         image-format-conversions.c \
         sw-exa.c
