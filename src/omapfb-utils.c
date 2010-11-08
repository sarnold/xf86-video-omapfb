/* Texas Instruments OMAP framebuffer driver for X.Org
 * Copyright 2008 Kalle Vahlman, <zuh@iki.fi>
 *
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without any express
 * or implied warranty.
 *
 * THE AUTHORS AND COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "omapfb-utils.h"

int
read_sysfs_value(const char *fname, char *value, size_t len)
{
	int fd;
	int r;

	fd = open(fname, O_RDONLY, 0);
	if (fd != -1)
	{
		r = read(fd, value, len);
		close(fd);
	}
	return r;
}

int
read_dss_sysfs_value(const char *target, int index,
                     const char *entry, char *value, size_t len)
{
	char fname[512];

	snprintf(fname, 512, SYSFS_DSS_DIR "/%s%i/%s", target, index, entry);

	return read_sysfs_value(fname, value, len);
}

int
write_sysfs_value(const char *fname, const char *value)
{
	int fd;

	fd = open(fname, O_WRONLY, 0);
	if (fd != -1)
	{
		int w = write(fd, value, strlen(value));
		close(fd);
		if (w == -1)
			return errno;
	}
	return 0;
}

int
write_dss_sysfs_value(const char *target, int index,
                      const char *entry, const char *value)
{
	char fname[512];

	snprintf(fname, 512, SYSFS_DSS_DIR "/%s%i/%s", target, index, entry);

	return write_sysfs_value(fname, value);
}
