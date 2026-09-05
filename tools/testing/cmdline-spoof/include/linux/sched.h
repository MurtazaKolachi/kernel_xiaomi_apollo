/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_SCHED_H
#define _TEST_LINUX_SCHED_H

#include <linux/types.h>

struct task_struct;

/*
 * Stands in for the reading task's identity.  Set this to model a read made by
 * the global init process, which cmdline_spoof_exempt_reader() must serve the
 * real command line.
 */
extern bool test_reader_is_global_init;

#define current ((struct task_struct *)0)

static inline int is_global_init(struct task_struct *tsk)
{
	return test_reader_is_global_init;
}

#endif /* _TEST_LINUX_SCHED_H */
