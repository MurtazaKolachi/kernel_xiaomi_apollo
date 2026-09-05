/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_SLAB_H
#define _TEST_LINUX_SLAB_H

#include <linux/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFP_KERNEL 0

/*
 * Set to N to make the next N allocations return NULL, so the failure paths in
 * the code under test can be reached deliberately.
 */
extern unsigned int test_alloc_fail_countdown;

static inline bool test_alloc_should_fail(void)
{
	if (!test_alloc_fail_countdown)
		return false;

	test_alloc_fail_countdown--;
	return true;
}

static inline void *kmalloc(size_t size, int flags)
{
	if (test_alloc_should_fail())
		return NULL;
	return malloc(size);
}

static inline void kfree(const void *p)
{
	free((void *)p);
}

static inline char *kstrdup(const char *s, int flags)
{
	if (test_alloc_should_fail())
		return NULL;
	return strdup(s);
}

static inline char *kasprintf(int flags, const char *fmt, ...)
{
	va_list ap;
	char *out;
	int len;

	if (test_alloc_should_fail())
		return NULL;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (len < 0)
		return NULL;

	out = malloc((size_t)len + 1);
	if (!out)
		return NULL;

	va_start(ap, fmt);
	vsnprintf(out, (size_t)len + 1, fmt, ap);
	va_end(ap);
	return out;
}

#endif /* _TEST_LINUX_SLAB_H */
