/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_LINUX_OF_H
#define _TEST_LINUX_OF_H

/*
 * Just enough of the kobject, sysfs and device-tree surface for
 * drivers/of/kobj.c to compile and run on the host.  Registration is inert;
 * what the test drives is of_node_property_read() and the size that
 * __of_add_property_sysfs() advertises.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/* apollo_defconfig sets CONFIG_SYSFS=y, so the sysfs paths are live here. */
#define IS_ENABLED(opt) 1

#define pr_warn(fmt, ...) do { } while (0)
#define BUG_ON(cond) do { if (cond) abort(); } while (0)

/*
 * The format carries %pOF, which the host printf cannot render, so report the
 * condition instead of the message.  Still yields the value, as WARN() does.
 */
#define WARN(cond, fmt, ...)						\
	({								\
		int __cond = !!(cond);					\
		if (__cond)						\
			fputs("WARN: " #cond "\n", stderr);		\
		__cond;							\
	})

struct file;
struct kernfs_node;
struct kset;
struct list_head { struct list_head *next, *prev; };
struct mutex;

struct kobject {
	const char *name;
	struct kset *kset;
	struct kernfs_node *sd;
	struct kobject *parent;
	unsigned int state_initialized:1;
	unsigned int state_in_sysfs:1;
};

struct kobj_type {
	void (*release)(struct kobject *kobj);
};

struct kset {
	struct kobject kobj;
};

struct attribute {
	const char *name;
	unsigned short mode;
};

struct bin_attribute {
	struct attribute attr;
	size_t size;
	ssize_t (*read)(struct file *filp, struct kobject *kobj,
			struct bin_attribute *battr, char *buf,
			loff_t off, size_t count);
};

#define sysfs_bin_attr_init(battr) do { } while (0)

static inline const char *kobject_name(const struct kobject *kobj)
{
	return kobj->name;
}

static inline int sysfs_create_bin_file(struct kobject *kobj,
					const struct bin_attribute *battr)
{
	return 0;
}

static inline void sysfs_remove_bin_file(struct kobject *kobj,
					 const struct bin_attribute *battr)
{
}

/* No name collisions are constructed, so safe_name() takes its first path. */
static inline struct kernfs_node *sysfs_get_dirent(struct kernfs_node *parent,
						   const char *name)
{
	return NULL;
}

static inline void sysfs_put(struct kernfs_node *kn)
{
}

static inline int kobject_add(struct kobject *kobj, struct kobject *parent,
			      const char *fmt, ...)
{
	return 0;
}

static inline void kobject_del(struct kobject *kobj)
{
}

static inline const char *kbasename(const char *path)
{
	const char *tail = strrchr(path, '/');

	return tail ? tail + 1 : path;
}

struct property {
	char *name;
	int length;
	void *value;
	struct property *next;
	struct bin_attribute attr;
};

struct device_node {
	const char *name;
	const char *full_name;
	struct property *properties;
	struct device_node *parent;
	struct kobject kobj;
};

#define for_each_property_of_node(dn, pp) \
	for (pp = dn->properties; pp != NULL; pp = pp->next)

static inline struct device_node *of_node_get(struct device_node *node)
{
	return node;
}

static inline void of_node_put(struct device_node *node)
{
}

extern struct device_node *of_chosen;

#endif /* _TEST_LINUX_OF_H */
