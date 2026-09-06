/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_CTYPE_H
#define _TEST_LINUX_CTYPE_H

#include <ctype.h>

/*
 * The kernel's isspace() applies __ismask((int)(unsigned char)(x)) and is
 * false for the NUL terminator; mirror the same cast here so the host build
 * classifies bytes identically.  The isspace inside the replacement list is
 * not re-expanded, so it resolves to the host function.
 */
#undef isspace
#define isspace(c) isspace((unsigned char)(c))

#endif /* _TEST_LINUX_CTYPE_H */
