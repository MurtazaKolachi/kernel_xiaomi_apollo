/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_STRING_H
#define _TEST_LINUX_STRING_H

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include <linux/types.h>

/*
 * Transcribed from lib/string.c so the sysfs read contract - offsets, short
 * counts and end of buffer - behaves exactly as it does in the kernel.
 */
static inline ssize_t memory_read_from_buffer(void *to, size_t count,
					      loff_t *ppos, const void *from,
					      size_t available)
{
	loff_t pos = *ppos;

	if (pos < 0)
		return -EINVAL;
	if (pos >= (loff_t)available || !count)
		return 0;
	if (count > available - (size_t)pos)
		count = available - (size_t)pos;

	memcpy(to, (const char *)from + pos, count);
	*ppos = pos + count;
	return count;
}

#endif /* _TEST_LINUX_STRING_H */
