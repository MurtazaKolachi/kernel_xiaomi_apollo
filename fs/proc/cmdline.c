// SPDX-License-Identifier: GPL-2.0
#include <linux/cmdline_spoof.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
static void cmdline_seq_emit(void *ctx, const char *data, size_t len)
{
	seq_write(ctx, data, len);
}
#endif

static int cmdline_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	/*
	 * Boot-critical readers get the real line; see
	 * cmdline_spoof_exempt_reader().  For everyone else, rendering straight
	 * into the seq_file keeps this allocation free and safe to repeat:
	 * seq_file discards a short buffer and calls us again, and the rewrite
	 * only reads saved_command_line, so every pass emits the same bytes.
	 *
	 * The decision is taken per invocation, not pinned per open.  Each call
	 * builds one complete line, so no single generation mixes the two
	 * views, and the buffer growth retries stay in the same task.
	 *
	 * It is not, however, one view per reader, and that is an explicit
	 * non-goal.  seq_read() serves whatever is still buffered in m->count
	 * without calling show() again, so on a descriptor shared between tasks
	 * of differing identity a continuation read can hand out bytes
	 * generated for the previous reader.  Enforcing a per-reader view would
	 * mean invalidating another task's buffered output from inside a read,
	 * and the result would still splice two renderings at an offset derived
	 * from the wrong one, so it buys consistency nowhere.
	 *
	 * What is guaranteed: a reader that opens its own descriptor and reads
	 * from offset zero gets its own view, because *ppos == 0 clears
	 * m->count and forces regeneration.  That covers init on the boot path,
	 * which opens /proc/cmdline for itself.  Sharing this descriptor across
	 * a privilege boundary is out of scope; nothing on the boot path does.
	 */
	if (cmdline_spoof_exempt_reader())
		seq_puts(m, saved_command_line);
	else
		cmdline_spoof_render(saved_command_line, cmdline_seq_emit, m);
#else
	seq_puts(m, saved_command_line);
#endif
	seq_putc(m, '\n');
	return 0;
}

static int __init proc_cmdline_init(void)
{
	proc_create_single("cmdline", 0, NULL, cmdline_proc_show);
	return 0;
}
fs_initcall(proc_cmdline_init);
