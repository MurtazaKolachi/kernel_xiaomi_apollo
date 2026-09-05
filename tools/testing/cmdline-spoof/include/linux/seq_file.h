/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_SEQ_FILE_H
#define _TEST_LINUX_SEQ_FILE_H

#include <stdlib.h>
#include <string.h>

/*
 * Stand-in for the kernel's seq_file: an unbounded growable sink, so the test
 * observes exactly the byte sequence cmdline_proc_show() emits, without the
 * buffer-full retry logic getting in the way.
 */
struct seq_file {
	char *buf;
	size_t len;
	size_t size;
};

static inline int seq_write(struct seq_file *m, const void *data, size_t len)
{
	if (m->len + len + 1 > m->size) {
		size_t size = m->size ? m->size : 64;

		while (size < m->len + len + 1)
			size *= 2;
		m->buf = realloc(m->buf, size);
		if (!m->buf)
			abort();
		m->size = size;
	}

	memcpy(m->buf + m->len, data, len);
	m->len += len;
	m->buf[m->len] = '\0';
	return 0;
}

static inline void seq_puts(struct seq_file *m, const char *s)
{
	seq_write(m, s, strlen(s));
}

static inline void seq_putc(struct seq_file *m, char c)
{
	seq_write(m, &c, 1);
}

#endif /* _TEST_LINUX_SEQ_FILE_H */
