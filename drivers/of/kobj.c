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
 * symlink onto it.  Present the same rewrite there as /proc/cmdline does, so
 * the two cannot contradict each other.  The property itself is never
 * modified, so of_property_read_string() and friends still see the real line.
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

static ssize_t of_bootargs_read(struct property *pp, char *buf, loff_t offset,
				size_t count)
{
	size_t len = cmdline_spoof_len(pp->value);
	ssize_t ret;
	char *rendered;

	/*
	 * Render the whole value, then serve the requested window out of it, so
	 * partial reads and seeks behave exactly as they do for any other
	 * property.  The trailing NUL is included, matching the length a
	 * device-tree string property reports.
	 */
	rendered = kmalloc(len + 1, GFP_KERNEL);
	if (!rendered)
		return -ENOMEM;

	cmdline_spoof_copy(rendered, len + 1, pp->value);
	ret = memory_read_from_buffer(buf, count, &offset, rendered, len + 1);
	kfree(rendered);
	return ret;
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
	 * a non-exempt task across a partial read could mix the two views.  No
	 * boot path does that, and both views are individually well formed.
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
	 * the stored length on purpose.  sysfs_kf_bin_read() clamps every read
	 * to i_size, and an exempt reader must receive the real value in full,
	 * which is never shorter than the rewritten one.  A rewritten value is
	 * simply shorter than the advertised size, and of_bootargs_read()
	 * reports end of file at its own length.
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
