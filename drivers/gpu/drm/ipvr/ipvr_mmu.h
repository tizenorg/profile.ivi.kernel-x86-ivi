/**************************************************************************
 * ipvr_mmu.h: IPVR header file for VED/VEC/VSP MMU handling
 *
 * Copyright (c) 2014 Intel Corporation, Hillsboro, OR, USA
 * Copyright (c) Imagination Technologies Limited, UK
 * Copyright (c) 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 *    Eric Anholt <eric@anholt.net>
 *    Fei Jiang <fei.jiang@intel.com>
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/

#ifndef _IPVR_MMU_H_
#define _IPVR_MMU_H_

#include "ipvr_drv.h"

static inline bool __must_check IPVR_IS_ERR(__force const unsigned long offset)
{
	return unlikely((offset) >= (unsigned long)-MAX_ERRNO);
}

static inline long __must_check IPVR_OFFSET_ERR(__force const unsigned long offset)
{
	return (long)offset;
}

static inline unsigned long __must_check IPVR_ERR_OFFSET(__force const long err)
{
	return (unsigned long)err;
}

/**
 * memory access control for VPU
 */
#define IPVR_MMU_CACHED_MEMORY	  (1 << 0)	/* Bind to MMU only */
#define IPVR_MMU_RO_MEMORY	  	  (1 << 1)	/* MMU RO memory */
#define IPVR_MMU_WO_MEMORY	      (1 << 2)	/* MMU WO memory */

/*
 * linear MMU size is 512M : 0 - 512M
 * tiling MMU size is 512M : 512M - 1024M
 */
#define IPVR_MEM_MMU_LINEAR_START	0x00000000
#define IPVR_MEM_MMU_LINEAR_END		0x20000000
#define IPVR_MEM_MMU_TILING_START	0x20000000
#define IPVR_MEM_MMU_TILING_END		0x40000000

struct ipvr_mmu_pd;
struct ipvr_mmu_pt;

struct ipvr_mmu_driver {
	/* protects driver- and pd structures. Always take in read mode
	 * before taking the page table spinlock.
	 */
	struct rw_semaphore sem;

	/* protects page tables, directory tables and pt tables.
	 * and pt structures.
	 */
	spinlock_t lock;

	atomic_t needs_tlbflush;

	u8 __iomem *register_map;
	struct ipvr_mmu_pd *default_pd;

	bool has_clflush;
	u32 clflush_add;
	unsigned long clflush_mask;

	struct drm_ipvr_private *dev_priv;
};

struct ipvr_mmu_driver *__must_check ipvr_mmu_driver_init(u8 __iomem *registers,
			u32 invalid_type, struct drm_ipvr_private *dev_priv);

void ipvr_mmu_driver_takedown(struct ipvr_mmu_driver *driver);

struct ipvr_mmu_pd *
ipvr_mmu_get_default_pd(struct ipvr_mmu_driver *driver);

void ipvr_mmu_set_pd_context(struct ipvr_mmu_pd *pd, u32 hw_context);

u32 __must_check ipvr_get_default_pd_addr32(struct ipvr_mmu_driver *driver);

int ipvr_mmu_insert_pages(struct ipvr_mmu_pd *pd, struct page **pages,
			unsigned long address, int num_pages,
			u32 desired_tile_stride, u32 hw_tile_stride, u32 type);

void ipvr_mmu_remove_pages(struct ipvr_mmu_pd *pd,
			unsigned long address, int num_pages,
			u32 desired_tile_stride, u32 hw_tile_stride);

#endif
