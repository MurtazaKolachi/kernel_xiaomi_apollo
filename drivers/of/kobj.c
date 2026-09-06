// SPDX-License-Identifier: GPL-2.0
#include <linux/cmdline_spoof.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "of_private.h"

/* true when node is initialized */
static int of_node_is_initialized(struct device_node *node)
{
	return node && node->kobj.state_initialized;
}

/* true when node is attached (i.e. present on sysfs) */
int of_node_is_attached(struct device_node *node)
{
	return node && node->kobj.state_in_sysfs;
}


#ifndef CONFIG_OF_DYNAMIC
static void of_node_release(struct kobject *kobj)
{
	/* Without CONFIG_OF_DYNAMIC, no nodes gets freed */
}
#endif /* CONFIG_OF_DYNAMIC */

struct kobj_type of_node_ktype = {
	.release = of_node_release,
};

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
/*
 * The kernel command line reaches userspace a second time as the chosen node's
 * bootargs property, readable by anyone through
 * /sys/firmware/devicetree/base/chosen/bootargs and the /proc/device-tree
 * symlink onto it.  Apply the same rewrite there that /proc/cmdline gets.
 *
 * That makes the policy consistent, not the output identical: this property and
 * saved_command_line need not hold the same text to begin with, since the
 * kernel may prepend or append CONFIG_CMDLINE and a boot image header line can
 * be merged in.  What holds per interface is that no rewrite target survives
 * for a non-exempt reader.  The property itself is never modified, so
 * of_property_read_string() and friends still see the real line.
 */
static bool of_bootargs_property(struct kobject *kobj, struct property *pp)
{
	if (!of_chosen || kobj != &of_chosen->kobj)
		return false;
	if (strcmp(pp->name, "bootargs"))
		return false;

	/*
	 * Only touch a well formed string property: exactly one NUL, sitting
	 * at the very end.  Anything else is passed through as raw bytes.
	 */
	return pp->length > 0 &&
	       strnlen(pp->value, pp->length) == (size_t)pp->length - 1;
}

/*
 * Serve the rewritten value without allocating, and without changing the length
 * userspace sees.
 *
 * sysfs has a single attr.size for every reader, so it cannot advertise one
 * length to an exempt reader and another to the rest.  The rendering used here
 * pads a shortened value back to its original width, which keeps the property
 * exactly pp->length bytes for both views: attr.size stays the stored length,
 * an exempt reader still gets every original byte, and stat() agrees with what
 * a full read returns.  cmdline_spoof_copy_range() produces only the requested
 * window, so nothing is rendered into a temporary buffer first.
 *
 * pp->length counts the terminating NUL, which sits at index pp->length - 1 and
 * is emitted here rather than by the renderer.
 */
static ssize_t of_bootargs_read(struct property *pp, char *buf, loff_t offset,
				size_t count)
{
	size_t total = pp->length;
	size_t body = total - 1;
	size_t copied = 0;

	if (offset < 0)
		return -EINVAL;
	if (!count || offset >= (loff_t)total)
		return 0;

	if (count > total - (size_t)offset)
		count = total - (size_t)offset;

	if ((size_t)offset < body) {
		size_t want = body - (size_t)offset;

		if (want > count)
			want = count;

		copied = cmdline_spoof_copy_range(buf, want, (size_t)offset,
						  pp->value);
	}

	if (copied < count)
		buf[copied++] = '\0';

	return copied;
}
#endif /* CONFIG_CMDLINE_SPOOF_LOCK_STATE */

static ssize_t of_node_property_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t offset, size_t count)
{
	struct property *pp = container_of(bin_attr, struct property, attr);

#ifdef CONFIG_CMDLINE_SPOOF_LOCK_STATE
	/*
	 * Boot-critical readers are served the stored bytes; only other
	 * readers get the rewrite.  Unlike /proc/cmdline this decision is taken
	 * per read() rather than per open, so an fd shared between an exempt and
	 * a non-exempt task across a partial read could mix the two views.  Both
	 * views are individually well formed; whether any userspace shares this
	 * descriptor that way has not been established either way.
	 */
	if (of_bootargs_property(kobj, pp) && !cmdline_spoof_exempt_reader())
		return of_bootargs_read(pp, buf, offset, count);
#endif
	return memory_read_from_buffer(buf, count, &offset, pp->value, pp->length);
}

/* always return newly allocated name, caller must free after use */
static const char *safe_name(struct kobject *kobj, const char *orig_name)
{
	const char *name = orig_name;
	struct kernfs_node *kn;
	int i = 0;

	/* don't be a hero. After 16 tries give up */
	while (i < 16 && (kn = sysfs_get_dirent(kobj->sd, name))) {
		sysfs_put(kn);
		if (name != orig_name)
			kfree(name);
		name = kasprintf(GFP_KERNEL, "%s#%i", orig_name, ++i);
	}

	if (name == orig_name) {
		name = kstrdup(orig_name, GFP_KERNEL);
	} else {
		pr_warn("Duplicate name in %s, renamed to \"%s\"\n",
			kobject_name(kobj), name);
	}
	return name;
}

int __of_add_property_sysfs(struct device_node *np, struct property *pp)
{
	int rc;

	/* Important: Don't leak passwords */
	bool secure = strncmp(pp->name, "security-", 9) == 0;

	if (!IS_ENABLED(CONFIG_SYSFS))
		return 0;

	if (!of_kset || !of_node_is_attached(np))
		return 0;

	sysfs_bin_attr_init(&pp->attr);
	pp->attr.attr.name = safe_name(&np->kobj, pp->name);
	pp->attr.attr.mode = secure ? 0400 : 0444;
	pp->attr.size = secure ? 0 : pp->length;
	pp->attr.read = of_node_property_read;

	/*
	 * Note for CONFIG_CMDLINE_SPOOF_LOCK_STATE: the advertised size stays
	 * the stored length, unmodified, and that is now exact for both views.
	 * sysfs_kf_bin_read() clamps every read to i_size, so a smaller size
	 * would truncate an exempt reader's original bytes; of_bootargs_read()
	 * instead pads the rewritten rendering back to the same width, so
	 * stat() and a full read agree whoever is asking.
	 */
	rc = sysfs_create_bin_file(&np->kobj, &pp->attr);
	WARN(rc, "error adding attribute %s to node %pOF\n", pp->name, np);
	return rc;
}

void __of_sysfs_remove_bin_file(struct device_node *np, struct property *prop)
{
	if (!IS_ENABLED(CONFIG_SYSFS))
		return;

	sysfs_remove_bin_file(&np->kobj, &prop->attr);
	kfree(prop->attr.attr.name);
}

void __of_remove_property_sysfs(struct device_node *np, struct property *prop)
{
	/* at early boot, bail here and defer setup to of_init() */
	if (of_kset && of_node_is_attached(np))
		__of_sysfs_remove_bin_file(np, prop);
}

void __of_update_property_sysfs(struct device_node *np, struct property *newprop,
		struct property *oldprop)
{
	/* At early boot, bail out and defer setup to of_init() */
	if (!of_kset)
		return;

	if (oldprop)
		__of_sysfs_remove_bin_file(np, oldprop);
	__of_add_property_sysfs(np, newprop);
}

int __of_attach_node_sysfs(struct device_node *np)
{
	const char *name;
	struct kobject *parent;
	struct property *pp;
	int rc;

	if (!IS_ENABLED(CONFIG_SYSFS) || !of_kset)
		return 0;

	np->kobj.kset = of_kset;
	if (!np->parent) {
		/* Nodes without parents are new top level trees */
		name = safe_name(&of_kset->kobj, "base");
		parent = NULL;
	} else {
		name = safe_name(&np->parent->kobj, kbasename(np->full_name));
		parent = &np->parent->kobj;
	}
	if (!name)
		return -ENOMEM;

	rc = kobject_add(&np->kobj, parent, "%s", name);
	kfree(name);
	if (rc)
		return rc;

	for_each_property_of_node(np, pp)
		__of_add_property_sysfs(np, pp);

	of_node_get(np);
	return 0;
}

void __of_detach_node_sysfs(struct device_node *np)
{
	struct property *pp;

	BUG_ON(!of_node_is_initialized(np));
	if (!of_kset)
		return;

	/* only remove properties if on sysfs */
	if (of_node_is_attached(np)) {
		for_each_property_of_node(np, pp)
			__of_sysfs_remove_bin_file(np, pp);
		kobject_del(&np->kobj);
	}

	of_node_put(np);
}
