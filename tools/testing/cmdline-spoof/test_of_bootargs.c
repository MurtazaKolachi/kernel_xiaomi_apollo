// SPDX-License-Identifier: GPL-2.0
/*
 * Host-side test for the device-tree half of CONFIG_CMDLINE_SPOOF_LOCK_STATE.
 *
 * The real drivers/of/kobj.c is compiled into this translation unit against the
 * stubs in include/, so the callbacks under test are the ones that ship:
 * of_node_property_read(), of_bootargs_property(), of_bootargs_read() and the
 * size that __of_add_property_sysfs() advertises.  Registration itself is inert;
 * what matters here is the read contract and the guards.
 *
 * Built twice, with and without the option.  With it off, every property must
 * come back as raw bytes exactly as upstream serves them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/of/kobj.c"

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
#include "lib/cmdline_spoof.c"
#endif

/* Definitions for the externs drivers/of/kobj.c and the stubs expect. */
struct device_node *of_chosen;
struct kset *of_kset;
unsigned int test_alloc_fail_countdown;
bool test_reader_is_global_init;

#define ORANGE "androidboot.verifiedbootstate=orange"
#define GREEN "androidboot.verifiedbootstate=green"

#define UNLOCKED ORANGE " androidboot.flash.locked=0"
#define LOCKED GREEN " androidboot.flash.locked=1"

/*
 * What the device-tree path actually serves: the same rewrite, padded back to
 * the width of the input.  green is one byte shorter than orange, so a single
 * space follows it and widens the separator; 1 and 0 are the same width, so
 * androidboot.flash.locked needs none.  strlen(LOCKED_PADDED) == strlen(UNLOCKED).
 */
#define LOCKED_PADDED GREEN "  androidboot.flash.locked=1"

static size_t failures;

static void report(int ok, const char *name)
{
	printf("%-4s %s\n", ok ? "ok" : "FAIL", name);
	if (!ok)
		failures++;
}

static struct kset test_kset;
static struct device_node chosen_node;
static struct device_node other_node;
static struct property test_prop;

/*
 * Build a chosen node carrying one property.  The value is a heap block of
 * exactly @length bytes so a sanitizer build has precise bounds and would trap
 * on any read past the property, terminated or not.
 */
static void setup_prop(const char *name, const void *value, size_t length)
{
	free(test_prop.value);
	/*
	 * __of_add_property_sysfs() takes a safe_name() copy that only the
	 * removal path frees, and the test never registers for real.
	 */
	free((void *)test_prop.attr.attr.name);
	memset(&test_prop, 0, sizeof(test_prop));

	test_prop.name = (char *)name;
	test_prop.length = (int)length;
	test_prop.value = malloc(length ? length : 1);
	if (!test_prop.value)
		abort();
	memcpy(test_prop.value, value, length);
	test_prop.next = NULL;

	memset(&chosen_node, 0, sizeof(chosen_node));
	chosen_node.name = "chosen";
	chosen_node.full_name = "chosen";
	chosen_node.properties = &test_prop;
	chosen_node.kobj.name = "chosen";
	chosen_node.kobj.state_in_sysfs = 1;
	chosen_node.kobj.state_initialized = 1;

	memset(&other_node, 0, sizeof(other_node));
	other_node.name = "firmware";
	other_node.full_name = "firmware";
	other_node.kobj.name = "firmware";
	other_node.kobj.state_in_sysfs = 1;
	other_node.kobj.state_initialized = 1;

	of_kset = &test_kset;
	of_chosen = &chosen_node;
}

/* Convenience for the common case: a NUL terminated string property. */
static void setup_string_prop(const char *name, const char *value)
{
	setup_prop(name, value, strlen(value) + 1);
}

static ssize_t prop_read(struct kobject *kobj, char *buf, loff_t off,
			 size_t count)
{
	return of_node_property_read(NULL, kobj, &test_prop.attr, buf, off,
				     count);
}

/* Read the whole property through the real callback in one go. */
static ssize_t read_all(struct kobject *kobj, char *buf, size_t buf_size)
{
	return prop_read(kobj, buf, 0, buf_size);
}

/*
 * What the property should present: the rewritten form when the option is on
 * and the property is chosen/bootargs, otherwise the stored bytes.
 */
static void check_presented(const char *what, struct kobject *kobj,
			    const char *want, size_t want_len)
{
	char buf[512];
	ssize_t got = read_all(kobj, buf, sizeof(buf));

	report(got == (ssize_t)want_len && !memcmp(buf, want, want_len), what);
}

static void check_raw(const char *what, struct kobject *kobj)
{
	check_presented(what, kobj, test_prop.value, (size_t)test_prop.length);
}

/*
 * sysfs_kf_bin_read() clamps every read to i_size, so the advertised size must
 * stay the stored length: an exempt reader has to receive the real value in
 * full.  The rewritten view is padded back to the same width, so the size is
 * exact for both readers rather than merely an upper bound.
 */
static void check_advertised_size(void)
{
	int rc;

	setup_string_prop("bootargs", UNLOCKED);
	rc = __of_add_property_sysfs(&chosen_node, &test_prop);

	report(rc == 0 && test_prop.attr.size == strlen(UNLOCKED) + 1,
	       "the advertised size stays the stored length");
	report(test_prop.attr.attr.mode == 0444,
	       "bootargs keeps its 0444 mode");

	/* A property that is not a target keeps the stored length. */
	setup_string_prop("stdout-path", UNLOCKED);
	__of_add_property_sysfs(&chosen_node, &test_prop);
	report(test_prop.attr.size == strlen(UNLOCKED) + 1,
	       "a non-target property advertises its stored length");
}

static void check_whole_read(void)
{
	setup_string_prop("bootargs", UNLOCKED);

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	check_presented("chosen/bootargs presents the rewritten value",
			&chosen_node.kobj, LOCKED_PADDED, strlen(LOCKED_PADDED) + 1);
#else
	check_raw("chosen/bootargs presents the stored value",
		  &chosen_node.kobj);
#endif
}

/* Offsets, short counts and end of buffer, through the real callback. */
static void check_partial_reads(void)
{
	const char *want;
	size_t want_len;
	char out[512];
	char chunk[7];
	size_t total = 0;
	loff_t off = 0;
	ssize_t got;
	int ok;

	setup_string_prop("bootargs", UNLOCKED);

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	want = LOCKED_PADDED;
#else
	want = UNLOCKED;
#endif
	want_len = strlen(want) + 1;

	while ((got = prop_read(&chosen_node.kobj, chunk, off,
				sizeof(chunk))) > 0) {
		memcpy(out + total, chunk, (size_t)got);
		total += (size_t)got;
		off += got;
	}

	ok = got == 0 && total == want_len && !memcmp(out, want, want_len);
	report(ok, "chunked reads reassemble the property including its NUL");

	got = prop_read(&chosen_node.kobj, chunk, (loff_t)want_len,
			sizeof(chunk));
	report(got == 0, "a read at the end of the property returns EOF");

	got = prop_read(&chosen_node.kobj, chunk, (loff_t)want_len + 32,
			sizeof(chunk));
	report(got == 0, "a read past the end of the property returns EOF");

	got = prop_read(&chosen_node.kobj, chunk, -1, sizeof(chunk));
	report(got == -EINVAL, "a negative offset is rejected");

	got = prop_read(&chosen_node.kobj, chunk, 0, 0);
	report(got == 0, "a zero-length read returns 0");

	/* A read starting mid-value must return the tail from that offset. */
	got = prop_read(&chosen_node.kobj, out, 5, sizeof(out));
	ok = got == (ssize_t)(want_len - 5) && !memcmp(out, want + 5, want_len - 5);
	report(ok, "a read at an offset returns the tail from there");
}

/*
 * Malformed properties must never be rewritten: the guard requires exactly one
 * NUL, at the very end.  Anything else is served as raw bytes.
 */
static void check_malformed(void)
{
	static const char embedded[] = "abc\0" ORANGE;
	char unterminated[64];

	memcpy(unterminated, UNLOCKED, strlen(UNLOCKED));
	setup_prop("bootargs", unterminated, strlen(UNLOCKED));
	check_raw("an unterminated bootargs value is served raw",
		  &chosen_node.kobj);

	setup_prop("bootargs", embedded, sizeof(embedded));
	check_raw("a bootargs value with an embedded NUL is served raw",
		  &chosen_node.kobj);

	setup_prop("bootargs", "", 0);
	check_presented("a zero-length bootargs property is served raw",
			&chosen_node.kobj, "", 0);
}

/* Only chosen/bootargs is a target. */
static void check_scope(void)
{
	setup_string_prop("stdout-path", UNLOCKED);
	check_raw("another property on chosen is served raw",
		  &chosen_node.kobj);

	setup_string_prop("bootargs", UNLOCKED);
	check_raw("bootargs on a node that is not chosen is served raw",
		  &other_node.kobj);

	setup_string_prop("bootargs", UNLOCKED);
	of_chosen = NULL;
	check_raw("bootargs is served raw when of_chosen is unset",
		  &chosen_node.kobj);
	of_chosen = &chosen_node;
}

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
/*
 * First-stage init resolves verifiedbootstate through this property too, so pid
 * 1 must read the stored bytes here exactly as it does from /proc/cmdline.
 */
static void check_init_exemption(void)
{
	char buf[512];
	ssize_t got;
	size_t stored;
	int ok;

	setup_string_prop("bootargs", UNLOCKED);
	stored = (size_t)test_prop.length;

	test_reader_is_global_init = true;
	check_raw("pid 1 reads the real bootargs", &chosen_node.kobj);

	/* Chunked, because reads are decided one at a time on this path. */
	{
		char chunk[7];
		size_t total = 0;
		loff_t off = 0;

		while ((got = prop_read(&chosen_node.kobj, chunk, off,
					sizeof(chunk))) > 0) {
			memcpy(buf + total, chunk, (size_t)got);
			total += (size_t)got;
			off += got;
		}
		ok = got == 0 && total == stored &&
		     !memcmp(buf, test_prop.value, stored);
		report(ok, "pid 1 reassembles the real bootargs in chunks");
	}

	test_reader_is_global_init = false;
	got = prop_read(&chosen_node.kobj, buf, 0, sizeof(buf));
	ok = got == (ssize_t)(strlen(LOCKED_PADDED) + 1) &&
	     !memcmp(buf, LOCKED_PADDED, strlen(LOCKED_PADDED) + 1);
	report(ok, "an ordinary reader still gets the rewritten bootargs");
}

/*
 * The read path must not allocate at all.  The stub allocator decrements
 * test_alloc_fail_countdown on every request, so an unchanged counter after a
 * read proves no allocation was even attempted - stronger than merely
 * surviving failures.  Arm it to fail anyway, so a regression that starts
 * allocating breaks the read rather than passing quietly.
 */
static void check_read_does_not_allocate(void)
{
	const unsigned int armed = 1000;
	char buf[512];
	unsigned int attempts;
	ssize_t got;

	setup_string_prop("bootargs", UNLOCKED);

	test_alloc_fail_countdown = armed;
	got = prop_read(&chosen_node.kobj, buf, 0, sizeof(buf));
	attempts = armed - test_alloc_fail_countdown;
	test_alloc_fail_countdown = 0;

	report(attempts == 0, "a read attempts no allocation at all");
	report(got == (ssize_t)(strlen(LOCKED_PADDED) + 1) &&
	       !memcmp(buf, LOCKED_PADDED, strlen(LOCKED_PADDED) + 1),
	       "a read succeeds with every allocation failing");
}

/*
 * The advertised size must equal the bytes actually readable, for BOTH views.
 * That is the inconsistency this design exists to remove.
 */
static void check_size_matches_readable(void)
{
	char buf[512];
	size_t advertised;
	ssize_t got;
	int pass;

	setup_string_prop("bootargs", UNLOCKED);
	__of_add_property_sysfs(&chosen_node, &test_prop);
	advertised = test_prop.attr.size;

	for (pass = 0; pass < 2; pass++) {
		test_reader_is_global_init = pass;
		got = prop_read(&chosen_node.kobj, buf, 0, sizeof(buf));
		report(got == (ssize_t)advertised,
		       pass ? "pid 1 reads exactly the advertised size"
			    : "an ordinary reader reads exactly the advertised size");
	}
	test_reader_is_global_init = false;
}

/*
 * Every byte boundary, exercised one byte at a time.  This walks across both
 * rewritten tokens, the padding, and the final NUL, and checks each single-byte
 * read against the expected rendering.
 */
static void check_every_boundary(void)
{
	char whole[512];
	char one[1];
	size_t total;
	size_t i;
	int ok = 1;
	int pass;

	setup_string_prop("bootargs", UNLOCKED);

	for (pass = 0; pass < 2; pass++) {
		const char *want;

		test_reader_is_global_init = pass;
		want = pass ? UNLOCKED : LOCKED_PADDED;
		total = strlen(want) + 1;

		if (prop_read(&chosen_node.kobj, whole, 0, sizeof(whole)) !=
		    (ssize_t)total)
			ok = 0;

		for (i = 0; i < total; i++) {
			if (prop_read(&chosen_node.kobj, one, (loff_t)i, 1) != 1 ||
			    one[0] != want[i])
				ok = 0;
		}

		/* One past the last byte, including the NUL, is EOF. */
		if (prop_read(&chosen_node.kobj, one, (loff_t)total, 1) != 0)
			ok = 0;
	}
	test_reader_is_global_init = false;

	report(ok, "single-byte reads match at every offset, both views");
}
#endif /* CONFIG_CMDLINE_SPOOF_LOCK_STATE */

/* Reading must never disturb the stored property, in either configuration. */
static void check_source_bytes_preserved(void)
{
	char buf[512];
	size_t len = strlen(UNLOCKED) + 1;

	setup_string_prop("bootargs", UNLOCKED);

	prop_read(&chosen_node.kobj, buf, 0, sizeof(buf));
	prop_read(&chosen_node.kobj, buf, 7, 11);
	test_reader_is_global_init = true;
	prop_read(&chosen_node.kobj, buf, 0, sizeof(buf));
	test_reader_is_global_init = false;

	report(!memcmp(test_prop.value, UNLOCKED, len),
	       "the stored property is unchanged after reads");
}

int main(void)
{
#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	puts("CONFIG_CMDLINE_SPOOF_LOCK_STATE=y (device-tree path)");
#else
	puts("CONFIG_CMDLINE_SPOOF_LOCK_STATE is not set (device-tree path)");
#endif

	check_advertised_size();
	check_whole_read();
	check_partial_reads();
	check_malformed();
	check_scope();
	check_source_bytes_preserved();
#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	check_init_exemption();
	check_read_does_not_allocate();
	check_size_matches_readable();
	check_every_boundary();
#endif

	free(test_prop.value);
	free((void *)test_prop.attr.attr.name);

	if (failures)
		printf("%zu check(s) FAILED\n", failures);
	else
		puts("all checks passed");
	return failures ? 1 : 0;
}
