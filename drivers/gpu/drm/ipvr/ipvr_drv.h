/**************************************************************************
 * ipvr_drv.h: IPVR driver common header file
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

#ifndef _IPVR_DRV_H_
#define _IPVR_DRV_H_
#include "drmP.h"
#include "ipvr_drm.h"
#include "ipvr_mmu.h"
#include <linux/version.h>
#include <linux/io-mapping.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/backlight.h>
#include <linux/intel-iommu.h>
#include <linux/kref.h>
#include <linux/pm_qos.h>
#include <linux/mmu_notifier.h>

#define IPVR_DRIVER_AUTHOR		"Intel, Inc."
#define IPVR_DRIVER_NAME		"ipvr"
#define IPVR_DRIVER_DESC		"PowerVR video drm driver"
#define IPVR_DRIVER_DATE		"20141113"
#define IPVR_DRIVER_MAJOR		0
#define IPVR_DRIVER_MINOR		1
#define IPVR_DRIVER_PATCHLEVEL	0

/* read/write domains */
#define IPVR_GEM_DOMAIN_CPU		0x00000001
#define IPVR_GEM_DOMAIN_VPU		0x00000002

/* context ID and type */
#define IPVR_CONTEXT_INVALID_ID 0
#define IPVR_MIN_CONTEXT_ID 1
#define IPVR_MAX_CONTEXT_ID 0xff

/*
 *Debug print bits setting
 */
#define IPVR_D_GENERAL   (1 << 0)
#define IPVR_D_INIT      (1 << 1)
#define IPVR_D_IRQ       (1 << 2)
#define IPVR_D_ENTRY     (1 << 3)
#define IPVR_D_PM        (1 << 4)
#define IPVR_D_REG       (1 << 5)
#define IPVR_D_VED       (1 << 6)
#define IPVR_D_WARN      (1 << 7)

#define IPVR_DEBUG_GENERAL(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_GENERAL, _fmt, ##_arg)
#define IPVR_DEBUG_INIT(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_INIT, _fmt, ##_arg)
#define IPVR_DEBUG_IRQ(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_IRQ, _fmt, ##_arg)
#define IPVR_DEBUG_ENTRY(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_ENTRY, _fmt, ##_arg)
#define IPVR_DEBUG_PM(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_PM, _fmt, ##_arg)
#define IPVR_DEBUG_REG(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_REG, _fmt, ##_arg)
#define IPVR_DEBUG_VED(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_VED, _fmt, ##_arg)
#define IPVR_DEBUG_WARN(_fmt, _arg...) \
	IPVR_DEBUG(IPVR_D_WARN, _fmt, ##_arg)

#define IPVR_DEBUG(_flag, _fmt, _arg...) \
	do { \
		if (unlikely((_flag) & drm_ipvr_debug)) \
			printk(KERN_INFO \
			       "[ipvr:0x%02x:%s] " _fmt , _flag, \
			       __func__ , ##_arg); \
	} while (0)

#define IPVR_ERROR(_fmt, _arg...) \
	do { \
			printk(KERN_ERR \
			       "[ipvr:ERROR:%s] " _fmt, \
			       __func__ , ##_arg); \
	} while (0)

#define IPVR_UDELAY(usec) \
	do { \
		cpu_relax(); \
	} while (0)

#define IPVR_REG_WRITE32(_val, _offs) \
	iowrite32(_val, dev_priv->reg_base + (_offs))
#define IPVR_REG_READ32(_offs) \
	ioread32(dev_priv->reg_base + (_offs))

typedef struct ipvr_validate_buffer ipvr_validate_buffer_t;

#define to_ipvr_bo(x) container_of(x, struct drm_ipvr_gem_object, base)

extern int drm_ipvr_debug;
extern int drm_ipvr_freq;

struct ipvr_validate_context {
	ipvr_validate_buffer_t *buffers;
	int used_buffers;
	struct list_head validate_list;
};

struct ipvr_mmu_driver;
struct ipvr_mmu_pd;

struct ipvr_gem_stat {
	/**
	 * Are we in a non-interruptible section of code?
	 */
	bool interruptible;

	/* accounting, useful for userland debugging */
	spinlock_t object_stat_lock;
	size_t allocated_memory;
	int allocated_count;
	size_t imported_memory;
	int imported_count;
	size_t exported_memory;
	int exported_count;
	size_t mmu_used_size;
};

struct ipvr_address_space {
	struct drm_mm linear_mm;
	struct drm_mm tiling_mm;
	struct drm_device *dev;
	unsigned long linear_start;
	size_t linear_total;
	unsigned long tiling_start;
	size_t tiling_total;

	/* need it during clear_range */
	struct {
		dma_addr_t addr;
		struct page *page;
	} scratch;
};

struct ipvr_fence_driver {
	u16	sync_seq;
	atomic_t signaled_seq;
	unsigned long last_activity;
	bool initialized;
	spinlock_t fence_lock;
};

struct ipvr_context {
	/* used to link into ipvr_ctx_list */
	struct list_head head;
	u32 ctx_id;
	/* used to double check ctx when find with idr, may be removed */
	struct drm_ipvr_file_private *ipvr_fpriv; /* DRM device file pointer */
	u32 ctx_type;

	u16 cur_seq;

	/* for IMG DDK, only use tiling for 2k and 4k buffer stride */
	/*
	 * following tiling strides for VED are supported:
	 * stride 0: 512 for scheme 0, 1024 for scheme 1
	 * stride 1: 1024 for scheme 0, 2048 for scheme 1
	 * stride 2: 2048 for scheme 0, 4096 for scheme 1
	 * stride 3: 4096 for scheme 0
	 */
	u8 tiling_stride;
	/*
	 * scheme 0: tile is 256x16, while minimal tile stride is 512
	 * scheme 1: tile is 512x8, while minimal tile stride is 1024
	 */
	u8 tiling_scheme;
};

typedef struct drm_ipvr_private {
	struct drm_device *dev;
	struct pci_dev *pci_root;

	/* IMG video context */
	spinlock_t ipvr_ctx_lock;
	struct idr ipvr_ctx_idr;
	struct ipvr_context default_ctx;

	/* PM related */
	atomic_t pending_events;
	spinlock_t power_usage_lock;

	/* exec related */
	struct ipvr_validate_context validate_ctx;

	/* IMG MMU specific */
	struct ipvr_mmu_driver *mmu;
	atomic_t ipvr_mmu_invaldc;

	/* GEM mm related */
	struct ipvr_gem_stat ipvr_stat;
	struct kmem_cache *ipvr_bo_slab;
	struct ipvr_address_space addr_space;

	/* fence related */
	u32 last_seq;
	wait_queue_head_t fence_queue;
	struct ipvr_fence_driver fence_drv;

	/* MMIO window shared from parent device */
	u8 __iomem* reg_base;

	/*
	 * VED specific
	 */
	struct ved_private *ved_private;
}drm_ipvr_private_t;

struct drm_ipvr_gem_object;

/* VED private structure */
struct ved_private {
	struct drm_ipvr_private *dev_priv;

	/* used to record seq got from irq fw-to-host msg */
	u16 ved_cur_seq;

	/*
	 * VED Rendec Memory
	 */
	struct drm_ipvr_gem_object *ccb0;
	u32 base_addr0;
	struct drm_ipvr_gem_object *ccb1;
	u32 base_addr1;
	bool rendec_initialized;

	/* VED firmware related */
	struct drm_ipvr_gem_object  *fw_bo;
	u32 fw_offset;
	u32 mtx_mem_size;
	bool fw_loaded_to_bo;
	bool ved_fw_loaded;
	void *ved_fw_ptr;
	size_t ved_fw_size;

	/*
	 * ved command queue
	 */
	spinlock_t ved_lock;
	struct mutex ved_mutex;
	struct list_head ved_queue;
	/* busy means cmd submitted to fw, while irq hasn't been receieved */
	bool ved_busy;
	u32 ved_dash_access_ctrl;

	/* pm related */
	int ved_needs_reset;

	int default_tiling_stride;
	int default_tiling_scheme;

	struct page *mmu_recover_page;
};

struct drm_ipvr_file_private {
	/**
	 * protected by dev_priv->ipvr_ctx_lock
	 */
	struct list_head ctx_list;
};

/* helpers for runtime pm */
int ipvr_runtime_pm_get(struct drm_ipvr_private *dev_priv);
int ipvr_runtime_pm_put(struct drm_ipvr_private *dev_priv, bool async);
int ipvr_runtime_pm_put_all(struct drm_ipvr_private *dev_priv, bool async);

#endif
