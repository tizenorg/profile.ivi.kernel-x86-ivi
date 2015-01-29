/**************************************************************************
 * ipvr_bo.h: IPVR buffer creation/destory, import/export, map etc
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
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/


#ifndef _IPVR_BO_H_
#define _IPVR_BO_H_

#include "ipvr_drv.h"
#include "ipvr_drm.h"
#include "ipvr_fence.h"
#include <drmP.h>
//#include <drm_gem.h>

struct ipvr_fence;

struct drm_ipvr_gem_object {
	struct drm_gem_object base;

	/* used to disinguish between i915 and ipvr */
	char *drv_name;

	/** MM related */
	struct drm_mm_node mm_node;

	bool tiling;

	enum ipvr_cache_level cache_level;

	bool dirty;

	struct sg_table *sg_table;
	struct page **pages;
	int pages_pin_count;

	struct ipvr_fence *fence;
	atomic_t reserved;
	wait_queue_head_t event_queue;
};

struct drm_ipvr_gem_object* ipvr_gem_create(struct drm_ipvr_private *dev_priv,
			size_t size, u32 tiling, u32 cache_level);
void ipvr_gem_free_object(struct drm_gem_object *obj);
void *ipvr_gem_object_vmap(struct drm_ipvr_gem_object *obj);
int ipvr_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int ipvr_gem_object_apply_reloc(struct drm_ipvr_gem_object *obj,
			u64 offset_in_bo, u32 value);
struct sg_table *ipvr_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *ipvr_gem_prime_import_sg_table(struct drm_device *dev,
			struct dma_buf_attachment *attach, struct sg_table *sg);
int ipvr_gem_prime_pin(struct drm_gem_object *obj);
void ipvr_gem_prime_unpin(struct drm_gem_object *obj);

static inline unsigned long
ipvr_gem_object_mmu_offset(struct drm_ipvr_gem_object *obj)
{
	return obj->mm_node.start;
}

#endif
