/**************************************************************************
 * ipvr_drv.c: IPVR driver common file for initialization/de-initialization
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

#include "ipvr_drv.h"
#include "ipvr_gem.h"
#include "ipvr_mmu.h"
#include "ipvr_exec.h"
#include "ipvr_bo.h"
#include "ipvr_debug.h"
#include "ipvr_trace.h"
#include "ved_fw.h"
#include "ved_pm.h"
#include "ved_reg.h"
#include "ved_cmd.h"
#include <linux/device.h>
#include <linux/version.h>
#include <uapi/drm/drm.h>
#include <linux/pm_runtime.h>
#include <linux/console.h>
#include <linux/module.h>
#include <asm/uaccess.h>

int drm_ipvr_debug = 0x80;
int drm_ipvr_freq = 320;

module_param_named(debug, drm_ipvr_debug, int, 0600);
module_param_named(freq, drm_ipvr_freq, int, 0600);

MODULE_PARM_DESC(debug,
		"control debug info output"
		"default: 0"
		"0x01:IPVR_D_GENERAL, 0x02:IPVR_D_INIT, 0x04:IPVR_D_IRQ, 0x08:IPVR_D_ENTRY"
		"0x10:IPVR_D_PM, 0x20:IPVR_D_REG, 0x40:IPVR_D_VED, 0x80:IPVR_D_WARN");
MODULE_PARM_DESC(freq,
		"prefered VED frequency"
		"default: 320 MHz");

static struct drm_ioctl_desc ipvr_gem_ioctls[] = {
	DRM_IOCTL_DEF_DRV(IPVR_CONTEXT_CREATE,
			ipvr_context_create_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_CONTEXT_DESTROY,
			ipvr_context_destroy_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_GET_INFO,
			ipvr_get_info_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_GEM_EXECBUFFER,
			ipvr_gem_execbuffer_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_GEM_BUSY,
			ipvr_gem_busy_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_GEM_CREATE,
			ipvr_gem_create_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_GEM_WAIT,
			ipvr_gem_wait_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(IPVR_GEM_MMAP_OFFSET,
			ipvr_gem_mmap_offset_ioctl, DRM_AUTH|DRM_UNLOCKED),
};

static void ipvr_gem_init(struct drm_device *dev)
{
	struct drm_ipvr_private *dev_priv = dev->dev_private;

	dev_priv->ipvr_bo_slab = kmem_cache_create("ipvr_gem_object",
				  sizeof(struct drm_ipvr_gem_object), 0,
				  SLAB_HWCACHE_ALIGN, NULL);

	spin_lock_init(&dev_priv->ipvr_stat.object_stat_lock);
	dev_priv->ipvr_stat.interruptible = true;
}

static void ipvr_gem_setup_mmu(struct drm_device *dev,
				       unsigned long linear_start,
				       unsigned long linear_end,
				       unsigned long tiling_start,
				       unsigned long tiling_end)
{
	/* Let GEM Manage all of the aperture.
	 */
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	struct ipvr_address_space *addr_space = &dev_priv->addr_space;

	addr_space->dev = dev_priv->dev;

	/* Subtract the guard page ... */
	drm_mm_init(&addr_space->linear_mm, linear_start,
		    linear_end - linear_start - PAGE_SIZE);
	dev_priv->addr_space.linear_start = linear_start;
	dev_priv->addr_space.linear_total = linear_end - linear_start;

	drm_mm_init(&addr_space->tiling_mm, tiling_start,
		    tiling_end - tiling_start - PAGE_SIZE);
	dev_priv->addr_space.tiling_start = tiling_start;
	dev_priv->addr_space.tiling_total = tiling_end - tiling_start;
}

int ipvr_runtime_pm_get(struct drm_ipvr_private *dev_priv)
{
	int ret = 0;
	int pending;
	unsigned long irq_flags;
	struct platform_device *platdev = dev_priv->dev->platformdev;
	BUG_ON(!platdev);
	BUG_ON(atomic_read(&dev_priv->pending_events) < 0);
	spin_lock_irqsave(&dev_priv->power_usage_lock, irq_flags);
	if ((pending = atomic_inc_return(&dev_priv->pending_events)) == 1) {
		do {
			ret = pm_runtime_get_sync(&platdev->dev);
			if (ret == -EAGAIN) {
				IPVR_DEBUG_WARN("pm_runtime_get_sync returns EAGAIN\n");
			}
			else if (ret < 0) {
				IPVR_ERROR("pm_runtime_get_sync returns %d\n", ret);
				pending = atomic_dec_return(&dev_priv->pending_events);
			}
		} while (ret == -EAGAIN);
	}
	trace_ipvr_get_power(atomic_read(&platdev->dev.power.usage_count),
		pending);
	spin_unlock_irqrestore(&dev_priv->power_usage_lock, irq_flags);
	return ret;
}

int ipvr_runtime_pm_put(struct drm_ipvr_private *dev_priv, bool async)
{
	int ret = 0;
	int pending;
	unsigned long irq_flags;
	struct platform_device *platdev = dev_priv->dev->platformdev;
	BUG_ON(!platdev);
	BUG_ON(atomic_read(&dev_priv->pending_events) <= 0);
	spin_lock_irqsave(&dev_priv->power_usage_lock, irq_flags);
	if ((pending = atomic_dec_return(&dev_priv->pending_events)) == 0) {
		do {
			if (async)
				ret = pm_runtime_put(&platdev->dev);
			else
				ret = pm_runtime_put_sync(&platdev->dev);
			if (ret == -EAGAIN)
				IPVR_DEBUG_WARN("pm_runtime_put returns EAGAIN\n");
			else if (ret < 0)
				IPVR_ERROR("pm_runtime_put returns %d\n", ret);
		} while (ret == -EAGAIN);
	}
	trace_ipvr_put_power(atomic_read(&platdev->dev.power.usage_count),
		pending);
	spin_unlock_irqrestore(&dev_priv->power_usage_lock, irq_flags);
	return ret;
}

int ipvr_runtime_pm_put_all(struct drm_ipvr_private *dev_priv, bool async)
{
	int ret = 0;
	unsigned long irq_flags;
	struct platform_device *platdev = dev_priv->dev->platformdev;
	BUG_ON(!platdev);
	spin_lock_irqsave(&dev_priv->power_usage_lock, irq_flags);
	if (atomic_read(&dev_priv->pending_events) > 0) {
		atomic_set(&dev_priv->pending_events, 0);
		do {
			if (async)
				ret = pm_runtime_put(&platdev->dev);
			else
				ret = pm_runtime_put_sync(&platdev->dev);
			if (ret == -EAGAIN)
				IPVR_DEBUG_WARN("pm_runtime_put returns EAGAIN\n");
			else if (ret < 0)
				IPVR_ERROR("pm_runtime_put returns %d\n", ret);
		} while (ret == -EAGAIN);
	}
	trace_ipvr_put_power(atomic_read(&platdev->dev.power.usage_count),
		0);
	spin_unlock_irqrestore(&dev_priv->power_usage_lock, irq_flags);
	return ret;
}

static int ipvr_drm_unload(struct drm_device *dev)
{
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	IPVR_DEBUG_ENTRY("entered.");
	BUG_ON(!dev->platformdev);

	if (dev_priv) {
		if (dev_priv->ipvr_bo_slab)
			kmem_cache_destroy(dev_priv->ipvr_bo_slab);
		ipvr_fence_driver_fini(dev_priv);

		if (WARN_ON(ipvr_runtime_pm_get(dev_priv) < 0))
			IPVR_DEBUG_WARN("Error getting ipvr power\n");
		else {
			ved_core_deinit(dev_priv);
			if (WARN_ON(ipvr_runtime_pm_put_all(dev_priv, false) < 0))
				IPVR_DEBUG_WARN("Error getting ipvr power\n");
		}
		if (dev_priv->validate_ctx.buffers)
			vfree(dev_priv->validate_ctx.buffers);

		if (dev_priv->mmu) {
			ipvr_mmu_driver_takedown(dev_priv->mmu);
			dev_priv->mmu = NULL;
		}

		if (dev_priv->reg_base) {
			iounmap(dev_priv->reg_base);
			dev_priv->reg_base = NULL;
		}

		list_del(&dev_priv->default_ctx.head);
		idr_remove(&dev_priv->ipvr_ctx_idr, dev_priv->default_ctx.ctx_id);
		kfree(dev_priv);

	}
	pm_runtime_disable(&dev->platformdev->dev);

	return 0;
}

static int ipvr_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_ipvr_private *dev_priv;
	u32 ctx_id;
	int ret = 0;
	struct resource *res_mmio;
	void __iomem* mmio_start;

	if (!dev->platformdev)
		return -ENODEV;

	platform_set_drvdata(dev->platformdev, dev);

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = dev_priv;
	dev_priv->dev = dev;

	INIT_LIST_HEAD(&dev_priv->validate_ctx.validate_list);

	dev_priv->pci_root = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0));
	if (!dev_priv->pci_root) {
		kfree(dev_priv);
		return -ENODEV;
	}

	res_mmio = platform_get_resource(dev->platformdev, IORESOURCE_MEM, 0);
	if (!res_mmio) {
		kfree(dev_priv);
		return -ENXIO;
	}

	mmio_start = ioremap_nocache(res_mmio->start,
					res_mmio->end - res_mmio->start);
	if (!mmio_start) {
		kfree(dev_priv);
		return -EACCES;
	}

	dev_priv->reg_base = mmio_start;
	IPVR_DEBUG_VED("reg_base is %p - 0x%p.\n",
		dev_priv->reg_base,
		dev_priv->reg_base + (res_mmio->end - res_mmio->start));

	atomic_set(&dev_priv->pending_events, 0);
	spin_lock_init(&dev_priv->power_usage_lock);
	pm_runtime_enable(&dev->platformdev->dev);
	if (WARN_ON(ipvr_runtime_pm_get(dev_priv) < 0)) {
		IPVR_ERROR("Error getting ipvr power\n");
		ret = -EBUSY;
		goto out_err;
	}

	IPVR_DEBUG_INIT("MSVDX_CORE_REV_OFFSET by readl is 0x%x.\n",
		readl(dev_priv->reg_base + 0x640));
	IPVR_DEBUG_INIT("MSVDX_CORE_REV_OFFSET by VED_REG_READ32 is 0x%x.\n",
		IPVR_REG_READ32(MSVDX_CORE_REV_OFFSET));

	/* mmu init */
	dev_priv->mmu = ipvr_mmu_driver_init(NULL, 0, dev_priv);
	if (!dev_priv->mmu) {
		ret = -EBUSY;
		goto out_err;
	}

	ipvr_mmu_set_pd_context(ipvr_mmu_get_default_pd(dev_priv->mmu), 0);

	/*
	 * Initialize sequence numbers for the different command
	 * submission mechanisms.
	 */
	dev_priv->last_seq = 1;

	ipvr_gem_init(dev);

	ipvr_gem_setup_mmu(dev,
		IPVR_MEM_MMU_LINEAR_START,
		IPVR_MEM_MMU_LINEAR_END,
		IPVR_MEM_MMU_TILING_START,
		IPVR_MEM_MMU_TILING_END);

	ved_core_init(dev_priv);

	if (WARN_ON(ipvr_runtime_pm_put(dev_priv, false) < 0))
		IPVR_DEBUG_WARN("Error putting ipvr power\n");

	dev_priv->ved_private->ved_needs_reset = 1;

	ipvr_fence_driver_init(dev_priv);

	dev_priv->validate_ctx.buffers =
		vmalloc(IPVR_NUM_VALIDATE_BUFFERS *
			sizeof(struct ipvr_validate_buffer));
	if (!dev_priv->validate_ctx.buffers) {
		ret = -ENOMEM;
		goto out_err;
	}

	/* ipvr context initialization */
	spin_lock_init(&dev_priv->ipvr_ctx_lock);
	idr_init(&dev_priv->ipvr_ctx_idr);
	/* default ipvr context is used for scaling, rotation case */
	ctx_id = idr_alloc(&dev_priv->ipvr_ctx_idr, &dev_priv->default_ctx,
			   IPVR_MIN_CONTEXT_ID, IPVR_MAX_CONTEXT_ID,
			   GFP_NOWAIT);
	if (ctx_id < 0) {
		return -ENOMEM;
		goto out_err;
	}
	dev_priv->default_ctx.ctx_id = ctx_id;
	INIT_LIST_HEAD(&dev_priv->default_ctx.head);
	dev_priv->default_ctx.ctx_type = 0;
	dev_priv->default_ctx.ipvr_fpriv = NULL;

	/* don't need protect with spinlock during module load stage */
	dev_priv->default_ctx.tiling_scheme = 0;
	dev_priv->default_ctx.tiling_stride = 0;

	return 0;
out_err:
	ipvr_drm_unload(dev);
	return ret;
}

/*
 * The .open() method is called every time the device is opened by an
 * application. Drivers can allocate per-file private data in this method and
 * store them in the struct drm_file::driver_priv field. Note that the .open()
 * method is called before .firstopen().
 */
static int
ipvr_drm_open(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_ipvr_file_private *ipvr_fp;
	IPVR_DEBUG_ENTRY("enter\n");

	ipvr_fp = kzalloc(sizeof(*ipvr_fp), GFP_KERNEL);
	if (!ipvr_fp)
		return -ENOMEM;

	file_priv->driver_priv = ipvr_fp;
	INIT_LIST_HEAD(&ipvr_fp->ctx_list);
	return 0;
}

/*
 * The close operation is split into .preclose() and .postclose() methods.
 * Since .postclose() is deprecated, all resource destruction related to file
 * handle are now done in .preclose() method.
 */
static void
ipvr_drm_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
	/* force close all contexts not explicitly closed by user */
	struct drm_ipvr_private *dev_priv;
	struct drm_ipvr_file_private *ipvr_fpriv;
	struct ved_private *ved_priv;
	struct ipvr_context *pos = NULL, *n = NULL;
	unsigned long irq_flags;

	IPVR_DEBUG_ENTRY("enter\n");
	dev_priv = dev->dev_private;
	ipvr_fpriv = file_priv->driver_priv;
	ved_priv = dev_priv->ved_private;

	spin_lock_irqsave(&dev_priv->ipvr_ctx_lock, irq_flags);
	if (ved_priv && (!list_empty(&ved_priv->ved_queue)
			|| (atomic_read(&dev_priv->pending_events) > 0))) {
		IPVR_DEBUG_WARN("Closing the FD while pending cmds exist!\n");
	}
	list_for_each_entry_safe(pos, n, &ipvr_fpriv->ctx_list, head) {
		IPVR_DEBUG_GENERAL("Video:remove context %d type 0x%x\n",
			pos->ctx_id, pos->ctx_type);
		list_del(&pos->head);
		idr_remove(&dev_priv->ipvr_ctx_idr, pos->ctx_id);
		kfree(pos);
	}

	spin_unlock_irqrestore(&dev_priv->ipvr_ctx_lock, irq_flags);
	kfree(ipvr_fpriv);
}

static irqreturn_t ipvr_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	WARN_ON(ved_irq_handler(dev_priv->ved_private));
	return IRQ_HANDLED;
}

static const struct file_operations ipvr_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_ioctl,
#endif
	.mmap = drm_gem_mmap,
};

static int ipvr_drm_freeze(struct drm_device *dev)
{
	int ret;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	IPVR_DEBUG_ENTRY("enter\n");

	ret = ved_check_idle(dev_priv->ved_private);
	if (ret) {
		IPVR_DEBUG_PM("VED check idle fail: %d, skip freezing\n", ret);
		/**
		 * fixme: better to schedule a delayed task?
		 */
		return 0;
	}

	if (dev->irq_enabled) {
		ret = drm_irq_uninstall(dev);
		if (ret) {
			IPVR_ERROR("Failed to uninstall drm irq handler: %d\n", ret);
		}
	}

	if (is_ved_on(dev_priv)) {
		if (!ved_power_off(dev_priv)) {
			IPVR_ERROR("Failed to power off VED\n");
			return -EFAULT;
		}
		IPVR_DEBUG_PM("Successfully powered off\n");
	} else {
		IPVR_DEBUG_PM("Skiped power-off since already powered off\n");
	}

	return 0;
}

static int ipvr_drm_thaw(struct drm_device *dev)
{
	int ret;
	int locked;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	IPVR_DEBUG_ENTRY("enter\n");
	if (!is_ved_on(dev_priv)) {
		if (!ved_power_on(dev_priv)) {
			IPVR_ERROR("Failed to power on VED\n");
			return -EFAULT;
		}
		IPVR_DEBUG_PM("Successfully powered on\n");
	} else {
		IPVR_DEBUG_PM("Skiped power-on since already powered on\n");
	}

	locked = 0;
	if(mutex_is_locked(&dev->struct_mutex)){
		locked = 1;
		mutex_unlock(&dev->struct_mutex);
	}

	if (!dev->irq_enabled) {
		ret = drm_irq_install(dev);   
		if (ret) {
			IPVR_ERROR("Failed to install drm irq handler: %d\n", ret);
		}
	}
	if (locked == 1)
		mutex_lock(&dev->struct_mutex);

	return 0;
}

static int ipvr_pm_suspend(struct device *dev)
{
	struct platform_device *platformdev = to_platform_device(dev);
	struct drm_device *drm_dev = platform_get_drvdata(platformdev);
	IPVR_DEBUG_PM("PM suspend called\n");
	return drm_dev? ipvr_drm_freeze(drm_dev): 0;
}
static int ipvr_pm_resume(struct device *dev)
{
	struct platform_device *platformdev = to_platform_device(dev);
	struct drm_device *drm_dev = platform_get_drvdata(platformdev);
	IPVR_DEBUG_PM("PM resume called\n");
	return drm_dev? ipvr_drm_thaw(drm_dev): 0;
}

static const struct vm_operations_struct ipvr_gem_vm_ops = {
	.fault = ipvr_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver ipvr_drm_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_PRIME,
	.load = ipvr_drm_load,
	.unload = ipvr_drm_unload,
	.open = ipvr_drm_open,
	.preclose = ipvr_drm_preclose,
	.irq_handler = ipvr_irq_handler,
	.gem_free_object = ipvr_gem_free_object,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_get_sg_table = ipvr_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = ipvr_gem_prime_import_sg_table,
	.gem_prime_pin		= ipvr_gem_prime_pin,
	.gem_prime_unpin	= ipvr_gem_prime_unpin,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = ipvr_debugfs_init,
	.debugfs_cleanup = ipvr_debugfs_cleanup,
#endif
	.gem_vm_ops = &ipvr_gem_vm_ops,
	.ioctls = ipvr_gem_ioctls,
	.num_ioctls = ARRAY_SIZE(ipvr_gem_ioctls),
	.fops = &ipvr_fops,
	.name = IPVR_DRIVER_NAME,
	.desc = IPVR_DRIVER_DESC,
	.date = IPVR_DRIVER_DATE,
	.major = IPVR_DRIVER_MAJOR,
	.minor = IPVR_DRIVER_MINOR,
	.patchlevel = IPVR_DRIVER_PATCHLEVEL,
};

static int ipvr_plat_probe(struct platform_device *device)
{	
	return drm_platform_init(&ipvr_drm_driver, device);
}

static int ipvr_plat_remove(struct platform_device *device)
{
	struct drm_device *drm_dev = platform_get_drvdata(device);
	if (drm_dev) {
		drm_put_dev(drm_dev);
		platform_set_drvdata(device, NULL);
	}
	return 0;
}

static struct dev_pm_ops ipvr_pm_ops = {
	.suspend = ipvr_pm_suspend,
	.resume = ipvr_pm_resume,
	.freeze = ipvr_pm_suspend,
	.thaw = ipvr_pm_resume,
	.poweroff = ipvr_pm_suspend,
	.restore = ipvr_pm_resume,
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = ipvr_pm_suspend,
	.runtime_resume = ipvr_pm_resume,
#endif
};

static struct platform_driver ipvr_vlv_plat_driver = {
	.driver = {
		.name = "ipvr-ved-vlv",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &ipvr_pm_ops,
#endif
	},
	.probe = ipvr_plat_probe,
	.remove = ipvr_plat_remove,
};

module_platform_driver(ipvr_vlv_plat_driver);
MODULE_LICENSE("GPL");
