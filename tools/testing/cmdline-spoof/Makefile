# SPDX-License-Identifier: GPL-2.0
#
# Host-side test for CONFIG_CMDLINE_SPOOF_LOCK_STATE.
# Run with: make -C tools/testing/cmdline-spoof check

KDIR ?= ../../..

CFLAGS ?= -O2 -std=gnu89 -Wall -Wextra -Wno-unused-parameter \
	  -Wdeclaration-after-statement

# -Iinclude comes first so the stubs shadow the kernel headers we cannot use on
# the host; $(KDIR)/include then supplies the real linux/cmdline_spoof.h, so a
# change to that interface breaks this build instead of going unnoticed.
CPPFLAGS = -I$(KDIR) -Iinclude -I$(KDIR)/include

SPOOF = $(KDIR)/lib/cmdline_spoof.c $(KDIR)/include/linux/cmdline_spoof.h
PROC_SOURCES = test_cmdline.c $(KDIR)/fs/proc/cmdline.c $(SPOOF)
OF_SOURCES = test_of_bootargs.c $(KDIR)/drivers/of/kobj.c $(SPOOF)

BINARIES = test-enabled test-disabled test-of-enabled test-of-disabled

all: $(BINARIES)

test-enabled: $(PROC_SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DCONFIG_CMDLINE_SPOOF_LOCK_STATE -o $@ $<

test-disabled: $(PROC_SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $<

# CONFIG_OF_KOBJ is set in apollo_defconfig; without it of_private.h supplies
# stub definitions that collide with the real ones in kobj.c.
test-of-enabled: $(OF_SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DCONFIG_OF_KOBJ \
		-DCONFIG_CMDLINE_SPOOF_LOCK_STATE -o $@ $<

test-of-disabled: $(OF_SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DCONFIG_OF_KOBJ -o $@ $<

check: all
	./test-enabled
	./test-disabled
	./test-of-enabled
	./test-of-disabled

clean:
	rm -f $(BINARIES)

.PHONY: all check clean
