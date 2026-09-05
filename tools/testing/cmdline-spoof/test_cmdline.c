// SPDX-License-Identifier: GPL-2.0
/*
 * Host-side test for CONFIG_CMDLINE_SPOOF_LOCK_STATE.
 *
 * The real lib/cmdline_spoof.c and fs/proc/cmdline.c are compiled into this
 * translation unit against the minimal kernel stubs in include/, so the code
 * exercised here is the code that ships.  include/linux/cmdline_spoof.h is the
 * genuine kernel header, picked up from the source tree, so a signature change
 * breaks this build rather than going unnoticed.
 *
 * The Makefile builds two binaries from this one source, with and without the
 * option.  Both run the same table and assert the exact bytes emitted; with the
 * option off, every command line must come back verbatim.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *saved_command_line;
bool test_reader_is_global_init;

#include "fs/proc/cmdline.c"

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
#include "lib/cmdline_spoof.c"
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define ORANGE "androidboot.verifiedbootstate=orange"
#define GREEN "androidboot.verifiedbootstate=green"

struct testcase {
	const char *name;
	const char *cmdline;
	/* Expected body when the option is enabled; ->cmdline when disabled. */
	const char *spoofed;
};

static const struct testcase cases[] = {
	/* Degenerate inputs. */
	{ "empty command line", "", "" },
	{ "whitespace only", "   ", "   " },

	/* Token position within the line. */
	{ "token alone", ORANGE, GREEN },
	{ "token first", ORANGE " console=ttyMSM0,115200n8",
	  GREEN " console=ttyMSM0,115200n8" },
	{ "token last", "console=ttyMSM0,115200n8 " ORANGE,
	  "console=ttyMSM0,115200n8 " GREEN },
	{ "token in the middle", "rw " ORANGE " loop.max_part=7",
	  "rw " GREEN " loop.max_part=7" },

	/* Repeated occurrences. */
	{ "two occurrences", "a=1 " ORANGE " b=2 " ORANGE " c=3",
	  "a=1 " GREEN " b=2 " GREEN " c=3" },
	{ "adjacent occurrences", ORANGE " " ORANGE, GREEN " " GREEN },

	/* Other values of the same key must pass through. */
	{ "green untouched", "androidboot.verifiedbootstate=green",
	  "androidboot.verifiedbootstate=green" },
	{ "yellow untouched", "androidboot.verifiedbootstate=yellow",
	  "androidboot.verifiedbootstate=yellow" },
	{ "red untouched", "androidboot.verifiedbootstate=red",
	  "androidboot.verifiedbootstate=red" },
	{ "value case is significant", "androidboot.verifiedbootstate=ORANGE",
	  "androidboot.verifiedbootstate=ORANGE" },

	/* Token absent entirely. */
	{ "no bootstate argument", "rw console=ttyMSM0,115200n8 loglevel=0",
	  "rw console=ttyMSM0,115200n8 loglevel=0" },

	/* Only whole keys and whole values match. */
	{ "longer value not matched", "androidboot.verifiedbootstate=orangeade",
	  "androidboot.verifiedbootstate=orangeade" },
	{ "prefixed key not matched", "xandroidboot.verifiedbootstate=orange",
	  "xandroidboot.verifiedbootstate=orange" },
	{ "dotted key not matched", "foo.androidboot.verifiedbootstate=orange",
	  "foo.androidboot.verifiedbootstate=orange" },
	{ "value of another key not matched", "extra=" ORANGE,
	  "extra=" ORANGE },
	{ "trailing punctuation not matched", ORANGE ".", ORANGE "." },

	/* Whitespace runs, tabs and edge padding are reproduced exactly. */
	{ "space run preserved", "a=1   " ORANGE "   b=2",
	  "a=1   " GREEN "   b=2" },
	{ "tab separated", "a=1\t" ORANGE "\tb=2", "a=1\t" GREEN "\tb=2" },
	{ "leading whitespace preserved", "  " ORANGE " b=2",
	  "  " GREEN " b=2" },
	{ "trailing whitespace preserved", ORANGE "  ", GREEN "  " },
	{ "mixed whitespace class", "\t" ORANGE " \n b=2",
	  "\t" GREEN " \n b=2" },

	/*
	 * Quoting.  Argument boundaries follow next_arg() in lib/cmdline.c, so
	 * a quoted value is one argument and its contents are never rewritten.
	 * The first case is the reported corruption bug.
	 */
	{ "quoted value containing the token is preserved",
	  "extra=\"before " ORANGE " after\"",
	  "extra=\"before " ORANGE " after\"" },
	{ "quoted value preserved while a real token is rewritten",
	  "extra=\"a " ORANGE " b\" " ORANGE,
	  "extra=\"a " ORANGE " b\" " GREEN },
	{ "quoted value with no spaces is preserved",
	  "extra=\"" ORANGE "\"", "extra=\"" ORANGE "\"" },
	{ "empty quoted value", "extra=\"\"", "extra=\"\"" },
	{ "two quoted arguments around a real token",
	  "a=\"x " ORANGE "\" " ORANGE " b=\"y " ORANGE "\"",
	  "a=\"x " ORANGE "\" " GREEN " b=\"y " ORANGE "\"" },
	{ "quoted target value keeps its quotes",
	  "androidboot.verifiedbootstate=\"orange\"",
	  "androidboot.verifiedbootstate=\"green\"" },
	{ "whole quoted argument keeps its quotes", "\"" ORANGE "\"",
	  "\"" GREEN "\"" },
	{ "unterminated quote swallows the rest untouched",
	  "extra=\"oops " ORANGE, "extra=\"oops " ORANGE },
	{ "stray trailing quote not matched", ORANGE "\"", ORANGE "\"" },
	{ "lone quote argument", "\"", "\"" },

	/* Sibling lock-state indicators. */
	{ "flash.locked rewritten", "androidboot.flash.locked=0",
	  "androidboot.flash.locked=1" },
	{ "flash.locked already locked", "androidboot.flash.locked=1",
	  "androidboot.flash.locked=1" },
	{ "flash.locked near-miss key", "androidboot.flash.lockedx=0",
	  "androidboot.flash.lockedx=0" },
	{ "vbmeta.device_state rewritten",
	  "androidboot.vbmeta.device_state=unlocked",
	  "androidboot.vbmeta.device_state=locked" },
	{ "vbmeta.device_state already locked",
	  "androidboot.vbmeta.device_state=locked",
	  "androidboot.vbmeta.device_state=locked" },
	{ "all three indicators together",
	  ORANGE " androidboot.flash.locked=0"
	  " androidboot.vbmeta.device_state=unlocked",
	  GREEN " androidboot.flash.locked=1"
	  " androidboot.vbmeta.device_state=locked" },
	{ "veritymode is not a target",
	  "androidboot.veritymode=enforcing androidboot.veritymode=logging",
	  "androidboot.veritymode=enforcing androidboot.veritymode=logging" },

	/* Representative Android command line. */
	{ "representative android command line",
	  "console=ttyMSM0,115200n8 androidboot.hardware=qcom "
	  "androidboot.veritymode=enforcing " ORANGE
	  " androidboot.flash.locked=0 androidboot.slot_suffix=_a "
	  "androidboot.vbmeta.device_state=unlocked loop.max_part=7 rw",
	  "console=ttyMSM0,115200n8 androidboot.hardware=qcom "
	  "androidboot.veritymode=enforcing " GREEN
	  " androidboot.flash.locked=1 androidboot.slot_suffix=_a "
	  "androidboot.vbmeta.device_state=locked loop.max_part=7 rw" },
};

static size_t failures;

static void print_escaped(const char *s, size_t len)
{
	size_t i;

	putchar('"');
	for (i = 0; i < len; i++) {
		switch (s[i]) {
		case '\t':
			fputs("\\t", stdout);
			break;
		case '\n':
			fputs("\\n", stdout);
			break;
		case '"':
			fputs("\\\"", stdout);
			break;
		default:
			putchar(s[i]);
		}
	}
	putchar('"');
}

static void report(int ok, const char *name)
{
	printf("%-4s %s\n", ok ? "ok" : "FAIL", name);
	if (!ok)
		failures++;
}

/* Drive cmdline_proc_show() and compare the exact bytes it emits. */
static void run_case(const struct testcase *tc)
{
	struct seq_file m;
	const char *want_body;
	size_t want_len;
	char *want;
	int ok;

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	want_body = tc->spoofed;
#else
	want_body = tc->cmdline;
#endif

	/* cmdline_proc_show() always terminates the line with a newline. */
	want_len = strlen(want_body) + 1;
	want = malloc(want_len + 1);
	if (!want)
		abort();
	memcpy(want, want_body, want_len - 1);
	want[want_len - 1] = '\n';
	want[want_len] = '\0';

	memset(&m, 0, sizeof(m));
	/*
	 * Hand over a heap copy, as the kernel does, so a sanitizer build has
	 * exact bounds and would trap on any read past the terminator.
	 */
	saved_command_line = strdup(tc->cmdline);
	if (!saved_command_line)
		abort();
	cmdline_proc_show(&m, NULL);

	ok = m.len == want_len && !memcmp(m.buf, want, want_len);
	report(ok, tc->name);
	if (!ok) {
		fputs("       in   ", stdout);
		print_escaped(tc->cmdline, strlen(tc->cmdline));
		fputs("\n       want ", stdout);
		print_escaped(want, want_len);
		fputs("\n       got  ", stdout);
		print_escaped(m.buf ? m.buf : "", m.len);
		putchar('\n');
	}

	free(saved_command_line);
	free(m.buf);
	free(want);
}

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE

/*
 * cmdline_spoof_len() feeds the size that __of_add_property_sysfs() advertises
 * for chosen/bootargs.  If it ever disagreed with what the render actually
 * produces, sysfs would report a length it cannot deliver.
 */
static void check_len_matches_render(void)
{
	size_t i;
	int ok = 1;

	for (i = 0; i < ARRAY_LEN(cases); i++) {
		if (cmdline_spoof_len(cases[i].cmdline) !=
		    strlen(cases[i].spoofed)) {
			printf("       length mismatch for: %s\n",
			       cases[i].name);
			ok = 0;
		}
	}
	report(ok, "cmdline_spoof_len agrees with the rendered length");
}

static void check_copy(void)
{
	const char *line = ORANGE " androidboot.flash.locked=0";
	const char *want = GREEN " androidboot.flash.locked=1";
	size_t len = strlen(want);
	char small[10];
	char one[1];
	char *exact;
	int ok;

	exact = malloc(len + 1);
	if (!exact)
		abort();

	ok = cmdline_spoof_copy(exact, len + 1, line) == len &&
	     !strcmp(exact, want);
	report(ok, "cmdline_spoof_copy fills an exact-sized buffer");

	/* A short buffer must truncate and still terminate. */
	memset(small, 'X', sizeof(small));
	ok = cmdline_spoof_copy(small, sizeof(small), line) ==
		     sizeof(small) - 1 &&
	     small[sizeof(small) - 1] == '\0' &&
	     !memcmp(small, want, sizeof(small) - 1);
	report(ok, "cmdline_spoof_copy truncates and NUL terminates");

	one[0] = 'X';
	ok = cmdline_spoof_copy(one, 1, line) == 0 && one[0] == '\0';
	report(ok, "cmdline_spoof_copy handles a one-byte buffer");

	ok = cmdline_spoof_copy(NULL, 0, line) == 0;
	report(ok, "cmdline_spoof_copy handles a zero-sized buffer");

	free(exact);
}

/* Faithful stand-in for the kernel's memory_read_from_buffer(). */
static long mem_read_from_buffer(void *to, size_t count, long long *ppos,
				 const void *from, size_t available)
{
	long long pos = *ppos;

	if (pos < 0)
		return -1;
	if ((size_t)pos >= available || !count)
		return 0;
	if (count > available - (size_t)pos)
		count = available - (size_t)pos;

	memcpy(to, (const char *)from + pos, count);
	*ppos = pos + count;
	return (long)count;
}

/*
 * Mirror of of_bootargs_read(): render once, then serve a window out of it.
 * Reading the property in small chunks must reassemble into the full rendering
 * including the trailing NUL that a device-tree string property carries.
 */
static void check_partial_reads(void)
{
	const char *line = ORANGE " androidboot.vbmeta.device_state=unlocked";
	const char *want = GREEN " androidboot.vbmeta.device_state=locked";
	size_t len = cmdline_spoof_len(line);
	size_t total = 0;
	long long pos = 0;
	char *rendered;
	char *out;
	char chunk[7];
	long got;
	int ok;

	rendered = malloc(len + 1);
	out = malloc(len + 2);
	if (!rendered || !out)
		abort();

	cmdline_spoof_copy(rendered, len + 1, line);

	/* len + 1 is the property length: the string plus its NUL. */
	while ((got = mem_read_from_buffer(chunk, sizeof(chunk), &pos, rendered,
					   len + 1)) > 0) {
		memcpy(out + total, chunk, (size_t)got);
		total += (size_t)got;
	}

	ok = got == 0 && total == len + 1 && strlen(want) == len &&
	     !memcmp(out, want, len) && out[len] == '\0';
	report(ok, "chunked reads reassemble the whole property with its NUL");

	/* A read past the end must report EOF rather than spilling. */
	pos = (long long)len + 1;
	ok = mem_read_from_buffer(chunk, sizeof(chunk), &pos, rendered,
				  len + 1) == 0;
	report(ok, "a read past the end of the property returns EOF");

	free(rendered);
	free(out);
}

/*
 * Android's first-stage init must keep seeing the real boot state, or it will
 * verify with AVB strictly and fail the early mount.  Every case must therefore
 * come back byte for byte when the reader is pid 1.
 */
static void check_init_sees_real(void)
{
	size_t i;
	int ok = 1;

	test_reader_is_global_init = true;

	for (i = 0; i < ARRAY_LEN(cases); i++) {
		struct seq_file m;
		size_t want_len = strlen(cases[i].cmdline) + 1;

		memset(&m, 0, sizeof(m));
		saved_command_line = strdup(cases[i].cmdline);
		if (!saved_command_line)
			abort();
		cmdline_proc_show(&m, NULL);

		if (m.len != want_len ||
		    memcmp(m.buf, cases[i].cmdline, want_len - 1) ||
		    m.buf[want_len - 1] != '\n') {
			printf("       pid 1 saw a rewrite for: %s\n",
			       cases[i].name);
			ok = 0;
		}

		free(saved_command_line);
		free(m.buf);
	}

	test_reader_is_global_init = false;
	report(ok, "pid 1 reads the real command line for every case");
}

/*
 * Scope, deliberately narrow: this drives show() repeatedly with the identity
 * flipped between passes and checks that each generation is one complete line
 * of one identity, so no single generation mixes the two views.
 *
 * It does NOT model seq_read()'s buffering.  The kernel serves whatever is
 * still in m->count without calling show() again, so on a descriptor shared
 * between tasks of differing identity a continuation read can return bytes
 * generated for the previous reader.  Reproducing that would mean
 * reimplementing seq_read() here and would only test the model, so shared
 * descriptor caching is left to hardware testing.
 */
static void check_regeneration_is_whole(void)
{
	const char *line = ORANGE " androidboot.flash.locked=0";
	const char *real = ORANGE " androidboot.flash.locked=0\n";
	const char *spoofed = GREEN " androidboot.flash.locked=1\n";
	int pass;
	int ok = 1;

	for (pass = 0; pass < 4; pass++) {
		struct seq_file m;
		const char *want;

		test_reader_is_global_init = pass % 2;
		want = test_reader_is_global_init ? real : spoofed;

		memset(&m, 0, sizeof(m));
		saved_command_line = strdup(line);
		if (!saved_command_line)
			abort();
		cmdline_proc_show(&m, NULL);

		if (m.len != strlen(want) || memcmp(m.buf, want, m.len))
			ok = 0;

		free(saved_command_line);
		free(m.buf);
	}

	test_reader_is_global_init = false;
	report(ok, "each generation is one whole view of one identity");
}

/* And the exemption must not leak into ordinary readers. */
static void check_non_init_still_rewritten(void)
{
	struct seq_file m;
	const char *want = GREEN "\n";
	int ok;

	test_reader_is_global_init = false;

	memset(&m, 0, sizeof(m));
	saved_command_line = strdup(ORANGE);
	if (!saved_command_line)
		abort();
	cmdline_proc_show(&m, NULL);

	ok = m.len == strlen(want) && !memcmp(m.buf, want, m.len);
	report(ok, "an ordinary reader still gets the rewritten line");

	free(saved_command_line);
	free(m.buf);
}

/*
 * The rewrite must never grow the line: init/main.c sizes a memblock buffer
 * from cmdline_spoof_len(), and the device-tree size is advertised up front.
 */
static void check_never_grows(void)
{
	size_t i;
	int ok = 1;

	for (i = 0; i < ARRAY_LEN(cases); i++)
		if (cmdline_spoof_len(cases[i].cmdline) >
		    strlen(cases[i].cmdline))
			ok = 0;

	report(ok, "the rewrite never lengthens the command line");
}

#endif /* CONFIG_CMDLINE_SPOOF_LOCK_STATE */

int main(void)
{
	size_t i;

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	puts("CONFIG_CMDLINE_SPOOF_LOCK_STATE=y");
#else
	puts("CONFIG_CMDLINE_SPOOF_LOCK_STATE is not set");
#endif

	for (i = 0; i < ARRAY_LEN(cases); i++)
		run_case(&cases[i]);

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	check_len_matches_render();
	check_copy();
	check_partial_reads();
	check_never_grows();
	check_init_sees_real();
	check_non_init_still_rewritten();
	check_regeneration_is_whole();
#endif

	if (failures)
		printf("%zu check(s) FAILED\n", failures);
	else
		puts("all checks passed");
	return failures ? 1 : 0;
}
