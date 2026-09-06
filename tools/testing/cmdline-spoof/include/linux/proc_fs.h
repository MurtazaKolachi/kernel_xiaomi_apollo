/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_PROC_FS_H
#define _TEST_LINUX_PROC_FS_H

struct proc_dir_entry;
struct seq_file;

/*
 * The test drives cmdline_proc_show() directly, so registration does nothing.
 * Keeping it a typed function rather than a macro still checks that the show
 * callback matches the signature proc_create_single() expects.
 */
static inline struct proc_dir_entry *
proc_create_single(const char *name, unsigned int mode,
		   struct proc_dir_entry *parent,
		   int (*show)(struct seq_file *, void *))
{
	(void)name;
	(void)mode;
	(void)parent;
	(void)show;
	return 0;
}

#endif /* _TEST_LINUX_PROC_FS_H */
