/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_TYPES_H
#define _TEST_LINUX_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Kernel spellings that drivers/of/of_private.h refers to. */
typedef uint64_t u64;
typedef uint32_t u32;
typedef unsigned int gfp_t;
typedef u32 phandle;

#endif /* _TEST_LINUX_TYPES_H */
