// SPDX-License-Identifier: GPL-2.0
/*
 * Presentation-layer rewrite of the Android bootloader lock-state arguments.
 *
 * An unlocked bootloader advertises itself on the kernel command line.  When
 * CONFIG_CMDLINE_SPOOF_LOCK_STATE is enabled the interfaces that copy that line
 * out to userspace render it through this file first, so they present one
 * consistent answer.  Nothing here writes to the source string: the real state
 * stays in saved_command_line, boot_command_line and the device-tree property
 * for every in-kernel consumer, and this changes no verified-boot decision.
 *
 * Policy, applied per whole argument and never to a substring:
 *
 *	androidboot.verifiedbootstate=orange	-> green
 *	androidboot.flash.locked=0		-> 1
 *	androidboot.vbmeta.device_state=unlocked -> locked
 *
 * Only those exact key and value pairs are rewritten.  Arguments that are
 * absent are never invented, and values that are not listed are passed through
 * untouched.  That includes the other verified-boot states: yellow, which means
 * the boot chain verified against a user-supplied trust root rather than the
 * vendor's, and red, which means verification actually failed.  Neither
 * describes an unlocked bootloader, so neither is a rewrite target.
 */
#include <linux/cmdline_spoof.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/types.h>

/*
 * Every rule must satisfy strlen(to) <= strlen(from).  The padded rendering
 * used for sysfs relies on it: it makes up the difference with spaces so the
 * output is exactly as long as the input.  A rule that grew a value could not
 * be padded back and would reintroduce the size mismatch the device-tree
 * attribute exists to avoid.  A host test pins this for every rule.
 */
static const struct cmdline_spoof_rule {
	const char *key;
	const char *from;
	const char *to;
} cmdline_spoof_rules[] = {
	{ "androidboot.verifiedbootstate",	"orange",	"green"	 },
	{ "androidboot.flash.locked",		"0",		"1"	 },
	{ "androidboot.vbmeta.device_state",	"unlocked",	"locked" },
};

/*
 * Android's first-stage init decides whether AVB may run permissively from the
 * exported boot state: IsDeviceUnlocked() in fs_mgr/libfs_avb/util.cpp resolves
 * "verifiedbootstate" and treats "orange" as unlocked, and that answer is what
 * lets AvbHandle::Open() tolerate a verification error.  Showing it the
 * rewritten value would make it verify strictly and fail the early mount, so
 * init is served the real command line and only other readers see the rewrite.
 *
 * Identify init by task identity rather than by its name, which any process can
 * change.  is_global_init() compares the thread group id in the initial pid
 * namespace, so a container's pid 1 does not qualify.
 *
 * This does not extend to processes init forks, which are ordinary readers.  If
 * some other boot-critical consumer of the boot state exists on a given
 * userspace it has to be audited and added here deliberately.
 */
bool cmdline_spoof_exempt_reader(void)
{
	return is_global_init(current);
}

/*
 * Strip one balanced layer of double quotes from @s / @len in place, matching
 * the way next_arg() in lib/cmdline.c drops them, and report whether it did.
 */
static bool cmdline_spoof_unquote(const char **s, size_t *len)
{
	if (*len < 2 || (*s)[0] != '"' || (*s)[*len - 1] != '"')
		return false;

	*s += 1;
	*len -= 2;
	return true;
}

/* Emit @n spaces, used to keep a padded rendering the length of its input. */
static void cmdline_spoof_pad(size_t n, cmdline_spoof_emit_fn emit, void *ctx)
{
	static const char spaces[] = "        ";

	while (n) {
		size_t chunk = n < sizeof(spaces) - 1 ? n : sizeof(spaces) - 1;

		emit(ctx, spaces, chunk);
		n -= chunk;
	}
}

/*
 * Rewrite one whole argument if it matches a rule, otherwise emit it verbatim.
 * Any quoting the argument or its value carried is reproduced, so the only
 * bytes that ever change are those of a targeted value.  With @pad set, a
 * shorter replacement is followed by spaces so the argument keeps its original
 * width; see cmdline_spoof_copy_range().
 */
static void cmdline_spoof_argument(const char *arg, size_t len, bool pad,
				   cmdline_spoof_emit_fn emit, void *ctx)
{
	const char *inner = arg;
	size_t inner_len = len;
	bool outer_quoted;
	unsigned int i;

	outer_quoted = cmdline_spoof_unquote(&inner, &inner_len);

	for (i = 0; i < ARRAY_SIZE(cmdline_spoof_rules); i++) {
		const struct cmdline_spoof_rule *rule = &cmdline_spoof_rules[i];
		size_t key_len = strlen(rule->key);
		size_t from_len = strlen(rule->from);
		size_t to_len = strlen(rule->to);
		const char *val;
		size_t val_len;
		bool val_quoted;

		/* Whole key, then a separator: never a longer key. */
		if (inner_len <= key_len || inner[key_len] != '=' ||
		    memcmp(inner, rule->key, key_len))
			continue;

		val = inner + key_len + 1;
		val_len = inner_len - key_len - 1;
		val_quoted = cmdline_spoof_unquote(&val, &val_len);

		/* Keys are unique, so a value mismatch ends the search. */
		if (val_len != from_len || memcmp(val, rule->from, val_len))
			break;

		if (outer_quoted)
			emit(ctx, "\"", 1);
		emit(ctx, rule->key, key_len);
		emit(ctx, "=", 1);
		if (val_quoted)
			emit(ctx, "\"", 1);
		emit(ctx, rule->to, to_len);
		if (val_quoted)
			emit(ctx, "\"", 1);
		if (outer_quoted)
			emit(ctx, "\"", 1);

		/*
		 * Make the shorter replacement up to the original width.  The
		 * spaces land immediately after a complete argument, so they
		 * are always outside any quoted region and never inside a
		 * value; they only ever widen the separator that follows, or
		 * add trailing whitespace at the end of the line.
		 */
		if (pad && from_len > to_len)
			cmdline_spoof_pad(from_len - to_len, emit, ctx);
		return;
	}

	emit(ctx, arg, len);
}

static void cmdline_spoof_do_render(const char *cmdline, bool pad,
				    cmdline_spoof_emit_fn emit, void *ctx)
{
	const char *pos = cmdline;

	while (*pos) {
		const char *start = pos;
		bool in_quote;

		if (isspace(*pos)) {
			while (isspace(*pos))
				pos++;
			emit(ctx, start, pos - start);
			continue;
		}

		/*
		 * Argument boundaries follow next_arg() in lib/cmdline.c: a
		 * leading quote opens a quoted region, every further quote
		 * toggles it, and whitespace separates only outside one.  That
		 * keeps extra="a b c" a single argument, so a rule can never
		 * fire on text that belongs to some other argument's value.
		 * An unterminated quote swallows the rest of the line, which
		 * leaves those bytes untouched rather than risking corruption.
		 */
		in_quote = *pos == '"';
		if (in_quote)
			pos++;

		while (*pos) {
			if (isspace(*pos) && !in_quote)
				break;
			if (*pos == '"')
				in_quote = !in_quote;
			pos++;
		}

		cmdline_spoof_argument(start, pos - start, pad, emit, ctx);
	}
}

void cmdline_spoof_render(const char *cmdline, cmdline_spoof_emit_fn emit,
			  void *ctx)
{
	cmdline_spoof_do_render(cmdline, false, emit, ctx);
}

static void cmdline_spoof_count(void *ctx, const char *data, size_t len)
{
	*(size_t *)ctx += len;
}

size_t cmdline_spoof_len(const char *cmdline)
{
	size_t len = 0;

	cmdline_spoof_render(cmdline, cmdline_spoof_count, &len);
	return len;
}

struct cmdline_spoof_sink {
	char *dst;
	size_t written;
	size_t room;
};

static void cmdline_spoof_store(void *ctx, const char *data, size_t len)
{
	struct cmdline_spoof_sink *sink = ctx;

	if (len > sink->room)
		len = sink->room;

	memcpy(sink->dst + sink->written, data, len);
	sink->written += len;
	sink->room -= len;
}

size_t cmdline_spoof_copy(char *dst, size_t dst_size, const char *cmdline)
{
	struct cmdline_spoof_sink sink;

	if (!dst_size)
		return 0;

	sink.dst = dst;
	sink.written = 0;
	sink.room = dst_size - 1;

	cmdline_spoof_render(cmdline, cmdline_spoof_store, &sink);
	dst[sink.written] = '\0';
	return sink.written;
}

struct cmdline_spoof_window {
	char *dst;
	size_t skip;
	size_t want;
	size_t copied;
};

static void cmdline_spoof_window_emit(void *ctx, const char *data, size_t len)
{
	struct cmdline_spoof_window *win = ctx;

	if (!win->want)
		return;

	if (win->skip >= len) {
		win->skip -= len;
		return;
	}

	data += win->skip;
	len -= win->skip;
	win->skip = 0;

	if (len > win->want)
		len = win->want;

	memcpy(win->dst + win->copied, data, len);
	win->copied += len;
	win->want -= len;
}

size_t cmdline_spoof_copy_range(char *dst, size_t count, size_t offset,
				const char *cmdline)
{
	struct cmdline_spoof_window win;

	win.dst = dst;
	win.skip = offset;
	win.want = count;
	win.copied = 0;

	cmdline_spoof_do_render(cmdline, true, cmdline_spoof_window_emit, &win);
	return win.copied;
}
