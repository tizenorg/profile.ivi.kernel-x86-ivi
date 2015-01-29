/**************************************************************************
 * ipvr_debug.c: IPVR debugfs support to assist bug triage
 *
 * Copyright (c) 2014 Intel Corporation, Hillsboro, OR, USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *    Fei Jiang <fei.jiang@intel.com>
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/

#if defined(CONFIG_DEBUG_FS)

#include "ipvr_debug.h"
#include "ipvr_drv.h"
#include "ved_reg.h"
#include <linux/seq_file.h>
#include <linux/debugfs.h>

union ipvr_debugfs_vars debugfs_vars;

static int ipvr_debug_info(struct seq_file *m, void *data)
{
	seq_printf(m, "ipvr platform\n");
	return 0;
}

/* some bookkeeping */
void
ipvr_stat_add_object(struct drm_ipvr_private *dev_priv, struct drm_ipvr_gem_object *obj)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.allocated_count++;
	dev_priv->ipvr_stat.allocated_memory += obj->base.size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void
ipvr_stat_remove_object(struct drm_ipvr_private *dev_priv, struct drm_ipvr_gem_object *obj)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.allocated_count--;
	dev_priv->ipvr_stat.allocated_memory -= obj->base.size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void
ipvr_stat_add_imported(struct drm_ipvr_private *dev_priv, struct drm_ipvr_gem_object *obj)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.imported_count++;
	dev_priv->ipvr_stat.imported_memory += obj->base.size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void
ipvr_stat_remove_imported(struct drm_ipvr_private *dev_priv, struct drm_ipvr_gem_object *obj)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.imported_count--;
	dev_priv->ipvr_stat.imported_memory -= obj->base.size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void
ipvr_stat_add_exported(struct drm_ipvr_private *dev_priv, struct drm_ipvr_gem_object *obj)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.exported_count++;
	dev_priv->ipvr_stat.exported_memory += obj->base.size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void
ipvr_stat_remove_exported(struct drm_ipvr_private *dev_priv, struct drm_ipvr_gem_object *obj)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.exported_count--;
	dev_priv->ipvr_stat.exported_memory -= obj->base.size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void ipvr_stat_add_mmu_bind(struct drm_ipvr_private *dev_priv, size_t size)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.mmu_used_size += size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

void ipvr_stat_remove_mmu_bind(struct drm_ipvr_private *dev_priv, size_t size)
{
	spin_lock(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.mmu_used_size -= size;
	spin_unlock(&dev_priv->ipvr_stat.object_stat_lock);
}

static int ipvr_debug_gem_object_info(struct seq_file *m, void* data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "total allocate %u objects, %zu bytes\n\n",
		   dev_priv->ipvr_stat.allocated_count,
		   dev_priv->ipvr_stat.allocated_memory);
	seq_printf(m, "total imported %u objects, %zu bytes\n\n",
		   dev_priv->ipvr_stat.imported_count,
		   dev_priv->ipvr_stat.imported_memory);
	seq_printf(m, "total exported %u objects, %zu bytes\n\n",
		   dev_priv->ipvr_stat.exported_count,
		   dev_priv->ipvr_stat.exported_memory);
	seq_printf(m, "total used MMU size %zu bytes\n\n",
		   dev_priv->ipvr_stat.mmu_used_size);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int ipvr_debug_gem_seqno_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_ipvr_private_t *dev_priv = dev->dev_private;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "last signaled seq is %d, last emitted seq is %d\n",
		atomic_read(&dev_priv->fence_drv.signaled_seq),
		dev_priv->fence_drv.sync_seq);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static ssize_t ipvr_debug_ved_reg_read(struct file *filp, char __user *ubuf,
					size_t max, loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	drm_ipvr_private_t *dev_priv = dev->dev_private;
	char buf[200], offset[20], operation[10], format[20], val[20];
	int len = 0, ret, no_of_tokens;
	unsigned long reg_offset, reg_to_write;

	if (debugfs_vars.reg.reg_input == 0)
		return len;

	snprintf(format, sizeof(format), "%%%zus %%%zus %%%zus",
			sizeof(operation), sizeof(offset), sizeof(val));

	no_of_tokens = sscanf(debugfs_vars.reg.reg_vars,
					format, operation, offset, val);

	if (no_of_tokens < 3)
		return len;

	len = sizeof(debugfs_vars.reg.reg_vars);

	if (strcmp(operation, IPVR_READ_TOKEN) == 0) {
		ret = kstrtoul(offset, 16, &reg_offset);
		if (ret)
			return -EINVAL;

		len = scnprintf(buf, sizeof(buf), "0x%x: 0x%x\n",
			(u32)reg_offset,
			IPVR_REG_READ32((u32)reg_offset));
	} else if (strcmp(operation, IPVR_WRITE_TOKEN) == 0) {
		ret = kstrtoul(offset, 16, &reg_offset);
		if (ret)
			return -EINVAL;

		ret = kstrtoul(val, 16, &reg_to_write);
		if (ret)
			return -EINVAL;

		IPVR_REG_WRITE32(reg_offset, reg_to_write);
		len = scnprintf(buf, sizeof(buf),
				"0x%x: 0x%x\n",
				(u32)reg_offset,
				(u32)IPVR_REG_READ32(reg_offset));
	} else {
		len = scnprintf(buf, sizeof(buf), "Operation Not Supported\n");
	}

	debugfs_vars.reg.reg_input = 0;

	simple_read_from_buffer(ubuf, max, ppos, buf, len);

	return len;
}

static ssize_t
ipvr_debug_ved_reg_write(struct file *filp,const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	/* reset the string */
	memset(debugfs_vars.reg.reg_vars, 0, IPVR_MAX_BUFFER_STR_LEN);

	if (cnt > 0) {
		if (cnt > sizeof(debugfs_vars.reg.reg_vars) - 1)
			return -EINVAL;

		if (copy_from_user(debugfs_vars.reg.reg_vars, ubuf, cnt))
			return -EFAULT;

		debugfs_vars.reg.reg_vars[cnt] = 0;

		/* Enable Read */
		debugfs_vars.reg.reg_input = 1;
	}

	return cnt;
}

/* As the drm_debugfs_init() routines are called before dev->dev_private is
 * allocated we need to hook into the minor for release. */
static int ipvr_add_fake_info_node(struct drm_minor *minor,
					struct dentry *ent, const void *key)
{
	struct drm_info_node *node;

	node = kmalloc(sizeof(struct drm_info_node), GFP_KERNEL);
	if (node == NULL) {
		debugfs_remove(ent);
		return -ENOMEM;
	}

	node->minor = minor;
	node->dent = ent;
	node->info_ent = (void *) key;

	mutex_lock(&minor->debugfs_lock);
	list_add(&node->list, &minor->debugfs_list);
	mutex_unlock(&minor->debugfs_lock);

	return 0;
}

static int ipvr_debugfs_create(struct dentry *root,
			       struct drm_minor *minor,
			       const char *name,
			       const struct file_operations *fops)
{
	struct drm_device *dev = minor->dev;
	struct dentry *ent;

	ent = debugfs_create_file(name,
				  S_IRUGO | S_IWUSR,
				  root, dev,
				  fops);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return ipvr_add_fake_info_node(minor, ent, fops);
}

static const struct file_operations ipvr_ved_reg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ipvr_debug_ved_reg_read,
	.write = ipvr_debug_ved_reg_write,
	.llseek = default_llseek,
};

static struct drm_info_list ipvr_debugfs_list[] = {
	{"ipvr_capabilities", ipvr_debug_info, 0},
	{"ipvr_gem_objects", ipvr_debug_gem_object_info, 0},
	{"ipvr_gem_seqno", ipvr_debug_gem_seqno_info, 0},

};
#define IPVR_DEBUGFS_ENTRIES ARRAY_SIZE(ipvr_debugfs_list)

static struct ipvr_debugfs_files {
	const char *name;
	const struct file_operations *fops;
} ipvr_debugfs_files[] = {
	{"ipvr_ved_reg_api", &ipvr_ved_reg_fops},
};

int ipvr_debugfs_init(struct drm_minor *minor)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ipvr_debugfs_files); i++) {
		ret = ipvr_debugfs_create(minor->debugfs_root, minor,
				   ipvr_debugfs_files[i].name,
				   ipvr_debugfs_files[i].fops);
		if (ret)
			return ret;
	}

	return drm_debugfs_create_files(ipvr_debugfs_list,
				 IPVR_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);
}

void ipvr_debugfs_cleanup(struct drm_minor *minor)
{
	int i;

	drm_debugfs_remove_files(ipvr_debugfs_list,
			  IPVR_DEBUGFS_ENTRIES, minor);

	for (i = 0; i < ARRAY_SIZE(ipvr_debugfs_files); i++) {
		struct drm_info_list *info_list =
			(struct drm_info_list *)ipvr_debugfs_files[i].fops;

		drm_debugfs_remove_files(info_list, 1, minor);
	}
}

#endif /* CONFIG_DEBUG_FS */
