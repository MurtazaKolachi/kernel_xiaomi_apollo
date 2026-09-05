/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CMDLINE_SPOOF_H
#define _LINUX_CMDLINE_SPOOF_H

#include <linux/types.h>

/*
 * Presentation-layer rewrite of the Android bootloader lock-state arguments on
 * the kernel command line; see lib/cmdline_spoof.c for the exact policy.
 *
 * Every interface that copies the command line out to userspace renders it
 * through cmdline_spoof_render() so they all agree.  The source string is only
 * ever read, so saved_command_line, boot_command_line and the device-tree
 * properties keep the real boot state for in-kernel consumers.
 */
typedef void (*cmdline_spoof_emit_fn)(void *ctx, const char *data, size_t len);

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE

/*
 * True when the calling task must be shown the real command line rather than
 * the rewritten one.  Callers that export the command line have to consult this
 * before rendering; see lib/cmdline_spoof.c for which readers are exempt and
 * why.  Only valid from process context.
 */
bool cmdline_spoof_exempt_reader(void);

/* Feed the rewritten @cmdline to @emit in order, in arbitrary sized pieces. */
void cmdline_spoof_render(const char *cmdline, cmdline_spoof_emit_fn emit,
			  void *ctx);

/* Length of the rewritten @cmdline, excluding any terminator. */
size_t cmdline_spoof_len(const char *cmdline);

/*
 * Render @cmdline into @dst, writing at most @dst_size bytes including the NUL
 * terminator.  Returns the number of bytes written, excluding that terminator.
 */
size_t cmdline_spoof_copy(char *dst, size_t dst_size, const char *cmdline);

#endif /* CONFIG_CMDLINE_SPOOF_LOCK_STATE */

#endif /* _LINUX_CMDLINE_SPOOF_H */
