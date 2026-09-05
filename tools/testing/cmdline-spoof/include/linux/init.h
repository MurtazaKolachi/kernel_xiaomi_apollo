/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_INIT_H
#define _TEST_LINUX_INIT_H

#define __init

/*
 * cmdline.c registers exactly one initcall.  Bind it to a named object so the
 * function stays referenced and the host compiler does not report it unused.
 */
#define fs_initcall(fn) int (*const test_fs_initcall)(void) = (fn)

extern char *saved_command_line;

#endif /* _TEST_LINUX_INIT_H */
