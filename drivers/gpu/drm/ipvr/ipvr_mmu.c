/**************************************************************************
 * ipvr_mmu.c: IPVR MMU handling to support VED, VEC, VSP buffer access
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
 *    Fei Jiang <fei.jiang@intel.com>
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/

#include "ipvr_mmu.h"
#include "ipvr_debug.h"

/*
 * Code for the VED MMU
 * Assumes system page size is same with VED (4KiB).
 * Doesn't work for the case of page size mismatch.
 */

/*
 * clflush on one processor only:
 * clflush should apparently flush the cache line on all processors in an
 * SMP system.
 */

/*
 * kmap atomic:
 * Usage of the slots must be completely encapsulated within a spinlock, and
 * no other functions that may be using the locks for other purposed may be
 * called from within the locked region.
 * Since the slots are per processor, this will guarantee that we are the only
 * user.
 */

/*
 *PTE's and PDE's
 */
#define IPVR_PDE_MASK		0x003FFFFF
#define IPVR_PDE_SHIFT		22
#define IPVR_PTE_SHIFT		12
#define IPVR_PTE_VALID		0x0001	/* PTE / PDE valid */
#define IPVR_PTE_WO			0x0002	/* Write only */
#define IPVR_PTE_RO			0x0004	/* Read only */
#define IPVR_PTE_CACHED		0x0008	/* CPU cache coherent */

struct ipvr_mmu_pt {
	struct ipvr_mmu_pd *pd;
	u32 index;
	u32 count;
	struct page *p;
	u32 *v;
};

struct ipvr_mmu_pd {
	struct ipvr_mmu_driver *driver;
	u32 hw_context;
	struct ipvr_mmu_pt **tables;
	struct page *p;
	struct page *dummy_pt;
	struct page *dummy_page;
	u32 pd_mask;
	u32 invalid_pde;
	u32 invalid_pte;
};

static inline u32 ipvr_mmu_pt_index(u32 offset)
{
	return (offset >> IPVR_PTE_SHIFT) & 0x3FF;
}

static inline u32 ipvr_mmu_pd_index(u32 offset)
{
	return offset >> IPVR_PDE_SHIFT;
}

#if defined(CONFIG_X86)
static inline void ipvr_clflush(void *addr)
{
	__asm__ __volatile__("clflush (%0)\n" : : "r"(addr) : "memory");
}

static inline void
ipvr_mmu_clflush(struct ipvr_mmu_driver *driver, void *addr)
{
	if (!driver->has_clflush)
		return;

	mb();
	ipvr_clflush(addr);
	mb();
}

static void
ipvr_mmu_page_clflush(struct ipvr_mmu_driver *driver, struct page* page)
{
	u32 clflush_add = driver->clflush_add >> PAGE_SHIFT;
	u32 clflush_count = PAGE_SIZE / clflush_add;
	int i;
	u8 *clf;

	clf = kmap_atomic(page);

	mb();
	for (i = 0; i < clflush_count; ++i) {
		ipvr_clflush(clf);
		clf += clflush_add;
	}
	mb();

	kunmap_atomic(clf);
}

static void ipvr_mmu_pages_clflush(struct ipvr_mmu_driver *driver,
				struct page *page[], int num_pages)
{
	int i;

	if (!driver->has_clflush)
		return ;

	for (i = 0; i < num_pages; i++)
		ipvr_mmu_page_clflush(driver, *page++);
}
#else

static inline void
ipvr_mmu_clflush(struct ipvr_mmu_driver *driver, void *addr)
{
	;
}

static void ipvr_mmu_pages_clflush(struct ipvr_mmu_driver *driver,
				struct page *page[], int num_pages)
{
	IPVR_DEBUG_GENERAL("Dumy ipvr_mmu_pages_clflush\n");
}

#endif

static void
ipvr_mmu_flush_pd_locked(struct ipvr_mmu_driver *driver, bool force)
{
	if (atomic_read(&driver->needs_tlbflush) || force) {
		if (!driver->dev_priv)
			goto out;

		atomic_set(&driver->dev_priv->ipvr_mmu_invaldc, 1);
	}
out:
	atomic_set(&driver->needs_tlbflush, 0);
}

static void ipvr_mmu_flush(struct ipvr_mmu_driver *driver, bool rc_prot)
{
	if (rc_prot)
		down_write(&driver->sem);

	if (!driver->dev_priv)
		goto out;

	atomic_set(&driver->dev_priv->ipvr_mmu_invaldc, 1);

out:
	if (rc_prot)
		up_write(&driver->sem);
}

void ipvr_mmu_set_pd_context(struct ipvr_mmu_pd *pd, u32 hw_context)
{
	ipvr_mmu_pages_clflush(pd->driver, &pd->p, 1);
	down_write(&pd->driver->sem);
	wmb();
	ipvr_mmu_flush_pd_locked(pd->driver, 1);
	pd->hw_context = hw_context;
	up_write(&pd->driver->sem);
}

static inline unsigned long
ipvr_pd_addr_end(unsigned long addr, unsigned long end)
{

	addr = (addr + IPVR_PDE_MASK + 1) & ~IPVR_PDE_MASK;
	return (addr < end) ? addr : end;
}

static inline u32 ipvr_mmu_mask_pte(u32 pfn, u32 type)
{
	u32 mask = IPVR_PTE_VALID;

	if (type & IPVR_MMU_CACHED_MEMORY)
		mask |= IPVR_PTE_CACHED;
	if (type & IPVR_MMU_RO_MEMORY)
		mask |= IPVR_PTE_RO;
	if (type & IPVR_MMU_WO_MEMORY)
		mask |= IPVR_PTE_WO;

	return (pfn << PAGE_SHIFT) | mask;
}

static struct ipvr_mmu_pd* __must_check
ipvr_mmu_alloc_pd(struct ipvr_mmu_driver *driver, u32 invalid_type)
{
	struct ipvr_mmu_pd *pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	u32 *v;
	int i;

	if (!pd)
		return NULL;

	pd->p = alloc_page(GFP_DMA32);
	if (!pd->p)
		goto out_err1;
	pd->dummy_pt = alloc_page(GFP_DMA32);
	if (!pd->dummy_pt)
		goto out_err2;
	pd->dummy_page = alloc_page(GFP_DMA32);
	if (!pd->dummy_page)
		goto out_err3;

	pd->invalid_pde =
		ipvr_mmu_mask_pte(page_to_pfn(pd->dummy_pt), invalid_type);
	pd->invalid_pte =
		ipvr_mmu_mask_pte(page_to_pfn(pd->dummy_page), invalid_type);

	v = kmap(pd->dummy_pt);
	if (!v)
		goto out_err4;
	for (i = 0; i < (PAGE_SIZE / sizeof(u32)); ++i)
		v[i] = pd->invalid_pte;

	kunmap(pd->dummy_pt);

	v = kmap(pd->p);
	if (!v)
		goto out_err4;
	for (i = 0; i < (PAGE_SIZE / sizeof(u32)); ++i)
		v[i] = pd->invalid_pde;

	kunmap(pd->p);

	v = kmap(pd->dummy_page);
	if (!v)
		goto out_err4;
	clear_page(v);
	kunmap(pd->dummy_page);

	pd->tables = vmalloc_user(sizeof(struct ipvr_mmu_pt *) * 1024);
	if (!pd->tables)
		goto out_err4;

	pd->hw_context = -1;
	pd->pd_mask = IPVR_PTE_VALID;
	pd->driver = driver;

	return pd;

out_err4:
	__free_page(pd->dummy_page);
out_err3:
	__free_page(pd->dummy_pt);
out_err2:
	__free_page(pd->p);
out_err1:
	kfree(pd);
	return NULL;
}

static void ipvr_mmu_free_pt(struct ipvr_mmu_pt *pt)
{
	__free_page(pt->p);
	kfree(pt);
}

static void ipvr_mmu_free_pagedir(struct ipvr_mmu_pd *pd)
{
	struct ipvr_mmu_driver *driver = pd->driver;
	struct ipvr_mmu_pt *pt;
	int i;

	down_write(&driver->sem);
	if (pd->hw_context != -1)
		ipvr_mmu_flush_pd_locked(driver, 1);

	/* Should take the spinlock here, but we don't need to do that
	   since we have the semaphore in write mode. */

	for (i = 0; i < 1024; ++i) {
		pt = pd->tables[i];
		if (pt)
			ipvr_mmu_free_pt(pt);
	}

	vfree(pd->tables);
	__free_page(pd->dummy_page);
	__free_page(pd->dummy_pt);
	__free_page(pd->p);
	kfree(pd);
	up_write(&driver->sem);
}

static struct ipvr_mmu_pt *ipvr_mmu_alloc_pt(struct ipvr_mmu_pd *pd)
{
	struct ipvr_mmu_pt *pt = kmalloc(sizeof(*pt), GFP_KERNEL);
	void *v;
	u32 clflush_add = pd->driver->clflush_add >> PAGE_SHIFT;
	u32 clflush_count = PAGE_SIZE / clflush_add;
	spinlock_t *lock = &pd->driver->lock;
	u8 *clf;
	u32 *ptes;
	int i;

	if (!pt)
		return NULL;

	pt->p = alloc_page(GFP_DMA32);
	if (!pt->p) {
		kfree(pt);
		return NULL;
	}

	spin_lock(lock);

	v = kmap_atomic(pt->p);

	clf = (u8 *) v;
	ptes = (u32 *) v;
	for (i = 0; i < (PAGE_SIZE / sizeof(u32)); ++i)
		*ptes++ = pd->invalid_pte;


#if defined(CONFIG_X86)
	if (pd->driver->has_clflush && pd->hw_context != -1) {
		mb();
		for (i = 0; i < clflush_count; ++i) {
			ipvr_clflush(clf);
			clf += clflush_add;
		}
		mb();
	}
#endif
	kunmap_atomic(v);

	spin_unlock(lock);

	pt->count = 0;
	pt->pd = pd;
	pt->index = 0;

	return pt;
}

static struct ipvr_mmu_pt *
ipvr_mmu_pt_alloc_map_lock(struct ipvr_mmu_pd *pd, unsigned long addr)
{
	u32 index = ipvr_mmu_pd_index(addr);
	struct ipvr_mmu_pt *pt;
	u32 *v;
	spinlock_t *lock = &pd->driver->lock;

	spin_lock(lock);
	pt = pd->tables[index];
	while (!pt) {
		spin_unlock(lock);
		pt = ipvr_mmu_alloc_pt(pd);
		if (!pt)
			return NULL;
		spin_lock(lock);

		if (pd->tables[index]) {
			spin_unlock(lock);
			ipvr_mmu_free_pt(pt);
			spin_lock(lock);
			pt = pd->tables[index];
			continue;
		}

		v = kmap_atomic(pd->p);

		pd->tables[index] = pt;
		v[index] = (page_to_pfn(pt->p) << 12) |
			pd->pd_mask;


		pt->index = index;

		kunmap_atomic((void *) v);

		if (pd->hw_context != -1) {
			ipvr_mmu_clflush(pd->driver, (void *) &v[index]);
			atomic_set(&pd->driver->needs_tlbflush, 1);
		}
	}

	pt->v = kmap_atomic(pt->p);

	return pt;
}

static struct ipvr_mmu_pt *
ipvr_mmu_pt_map_lock(struct ipvr_mmu_pd *pd, unsigned long addr)
{
	u32 index = ipvr_mmu_pd_index(addr);
	struct ipvr_mmu_pt *pt;
	spinlock_t *lock = &pd->driver->lock;

	spin_lock(lock);
	pt = pd->tables[index];
	if (!pt) {
		spin_unlock(lock);
		return NULL;
	}

	pt->v = kmap_atomic(pt->p);

	return pt;
}

static void ipvr_mmu_pt_unmap_unlock(struct ipvr_mmu_pt *pt)
{
	struct ipvr_mmu_pd *pd = pt->pd;
	u32 *v;

	kunmap_atomic(pt->v);

	if (pt->count == 0) {
		v = kmap_atomic(pd->p);

		v[pt->index] = pd->invalid_pde;
		pd->tables[pt->index] = NULL;

		if (pd->hw_context != -1) {
			ipvr_mmu_clflush(pd->driver,
					(void *) &v[pt->index]);
			atomic_set(&pd->driver->needs_tlbflush, 1);
		}

		kunmap_atomic(pt->v);

		spin_unlock(&pd->driver->lock);
		ipvr_mmu_free_pt(pt);
		return;
	}
	spin_unlock(&pd->driver->lock);
}

static inline void
ipvr_mmu_set_pte(struct ipvr_mmu_pt *pt, unsigned long addr, u32 pte)
{
	pt->v[ipvr_mmu_pt_index(addr)] = pte;
}

static inline void
ipvr_mmu_invalidate_pte(struct ipvr_mmu_pt *pt, unsigned long addr)
{
	pt->v[ipvr_mmu_pt_index(addr)] = pt->pd->invalid_pte;
}

struct ipvr_mmu_pd *ipvr_mmu_get_default_pd(struct ipvr_mmu_driver *driver)
{
	struct ipvr_mmu_pd *pd;

	/* down_read(&driver->sem); */
	pd = driver->default_pd;
	/* up_read(&driver->sem); */

	return pd;
}

/* Returns the physical address of the PD shared by sgx/msvdx */
u32 __must_check ipvr_get_default_pd_addr32(struct ipvr_mmu_driver *driver)
{
	struct ipvr_mmu_pd *pd;
	unsigned long pfn;
	pd = ipvr_mmu_get_default_pd(driver);
	pfn = page_to_pfn(pd->p);
	if (pfn >= 0x00100000UL)
		return 0;
	return pfn << PAGE_SHIFT;
}

void ipvr_mmu_driver_takedown(struct ipvr_mmu_driver *driver)
{
	ipvr_mmu_free_pagedir(driver->default_pd);
	kfree(driver);
}

struct ipvr_mmu_driver * __must_check
ipvr_mmu_driver_init(u8 __iomem * registers, u32 invalid_type,
			struct drm_ipvr_private *dev_priv)
{
	struct ipvr_mmu_driver *driver;

	driver = kmalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return NULL;

	driver->dev_priv = dev_priv;

	driver->default_pd =
		ipvr_mmu_alloc_pd(driver, invalid_type);
	if (!driver->default_pd)
		goto out_err1;

	spin_lock_init(&driver->lock);
	init_rwsem(&driver->sem);
	down_write(&driver->sem);
	driver->register_map = registers;
	atomic_set(&driver->needs_tlbflush, 1);

	driver->has_clflush = false;

#if defined(CONFIG_X86)
	if (cpu_has_clflush) {
		u32 tfms, misc, cap0, cap4, clflush_size;

		/*
		 * clflush size is determined at kernel setup for x86_64
		 *  but not for i386. We have to do it here.
		 */

		cpuid(0x00000001, &tfms, &misc, &cap0, &cap4);
		clflush_size = ((misc >> 8) & 0xff) * 8;
		driver->has_clflush = true;
		driver->clflush_add =
			PAGE_SIZE * clflush_size / sizeof(u32);
		driver->clflush_mask = driver->clflush_add - 1;
		driver->clflush_mask = ~driver->clflush_mask;
	}
#endif

	up_write(&driver->sem);
	return driver;

out_err1:
	kfree(driver);
	return NULL;
}

#if defined(CONFIG_X86)
static void ipvr_mmu_flush_ptes(struct ipvr_mmu_pd *pd,
			unsigned long address,
			int num_pages,
			u32 desired_tile_stride,
			u32 hw_tile_stride)
{
	struct ipvr_mmu_pt *pt;
	int rows = 1;
	int i;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long add;
	unsigned long row_add;
	unsigned long clflush_add = pd->driver->clflush_add;
	unsigned long clflush_mask = pd->driver->clflush_mask;
	IPVR_DEBUG_GENERAL("call x86 ipvr_mmu_flush_ptes, address is 0x%lx, "
			"num pages is %d.\n", address, num_pages);
	if (!pd->driver->has_clflush) {
		IPVR_DEBUG_GENERAL("call ipvr_mmu_pages_clflush.\n");
		ipvr_mmu_pages_clflush(pd->driver, &pd->p, num_pages);
		return;
	}

	if (hw_tile_stride)
		rows = num_pages / desired_tile_stride;
	else
		desired_tile_stride = num_pages;

	add = desired_tile_stride << PAGE_SHIFT;
	row_add = hw_tile_stride << PAGE_SHIFT;
	mb();
	for (i = 0; i < rows; ++i) {
		addr = address;
		end = addr + add;

		do {
			next = ipvr_pd_addr_end(addr, end);
			pt = ipvr_mmu_pt_map_lock(pd, addr);
			if (!pt)
				continue;
			do {
				ipvr_clflush(&pt->v[ipvr_mmu_pt_index(addr)]);
			} while (addr +=
					 clflush_add,
				 (addr & clflush_mask) < next);

			ipvr_mmu_pt_unmap_unlock(pt);
		} while (addr = next, next != end);
		address += row_add;
	}
	mb();
}
#else

static void ipvr_mmu_flush_ptes(struct ipvr_mmu_pd *pd,
					unsigned long address,
					int num_pages,
					u32 desired_tile_stride,
					u32 hw_tile_stride)
{
	IPVR_DEBUG_GENERAL("call non-x86 ipvr_mmu_flush_ptes.\n");
}
#endif

void ipvr_mmu_remove_pages(struct ipvr_mmu_pd *pd, unsigned long address,
			int num_pages, u32 desired_tile_stride,
			u32 hw_tile_stride)
{
	struct ipvr_mmu_pt *pt;
	int rows = 1;
	int i;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long add;
	unsigned long row_add;
	unsigned long f_address = address;

	if (hw_tile_stride)
		rows = num_pages / desired_tile_stride;
	else
		desired_tile_stride = num_pages;

	add = desired_tile_stride << PAGE_SHIFT;
	row_add = hw_tile_stride << PAGE_SHIFT;

	/* down_read(&pd->driver->sem); */

	/* Make sure we only need to flush this processor's cache */

	for (i = 0; i < rows; ++i) {

		addr = address;
		end = addr + add;

		do {
			next = ipvr_pd_addr_end(addr, end);
			pt = ipvr_mmu_pt_map_lock(pd, addr);
			if (!pt)
				continue;
			do {
				ipvr_mmu_invalidate_pte(pt, addr);
				--pt->count;

			} while (addr += PAGE_SIZE, addr < next);
			ipvr_mmu_pt_unmap_unlock(pt);

		} while (addr = next, next != end);
		address += row_add;
	}
	if (pd->hw_context != -1)
		ipvr_mmu_flush_ptes(pd, f_address, num_pages,
				   desired_tile_stride, hw_tile_stride);

	/* up_read(&pd->driver->sem); */

	if (pd->hw_context != -1)
		ipvr_mmu_flush(pd->driver, 0);
	ipvr_stat_remove_mmu_bind(pd->driver->dev_priv, num_pages << PAGE_SHIFT);
}

int ipvr_mmu_insert_pages(struct ipvr_mmu_pd *pd, struct page **pages,
			unsigned long address, int num_pages,
			u32 desired_tile_stride,
			u32 hw_tile_stride, u32 type)
{
	struct ipvr_mmu_pt *pt;
	int rows = 1;
	int i;
	u32 pte;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long add;
	unsigned long row_add;
	unsigned long f_address = address;
	unsigned long pfn;
	int ret = 0;

	if (hw_tile_stride) {
		if (num_pages % desired_tile_stride != 0)
			return -EINVAL;
		rows = num_pages / desired_tile_stride;
	} else {
		desired_tile_stride = num_pages;
	}

	add = desired_tile_stride << PAGE_SHIFT;
	row_add = hw_tile_stride << PAGE_SHIFT;

	down_read(&pd->driver->sem);

	for (i = 0; i < rows; ++i) {

		addr = address;
		end = addr + add;

		do {
			next = ipvr_pd_addr_end(addr, end);
			pt = ipvr_mmu_pt_alloc_map_lock(pd, addr);
			if (!pt) {
				ret = -ENOMEM;
				goto out;
			}
			do {
				pfn = page_to_pfn(*pages++);
				/* should be under 4GiB */
				if (pfn >= 0x00100000UL) {
					IPVR_ERROR("cannot support pfn 0x%lx\n", pfn);
					ret = -EINVAL;
					goto out;
				}
				pte = ipvr_mmu_mask_pte(pfn, type);
				ipvr_mmu_set_pte(pt, addr, pte);
				pt->count++;
			} while (addr += PAGE_SIZE, addr < next);
			ipvr_mmu_pt_unmap_unlock(pt);

		} while (addr = next, next != end);

		address += row_add;
	}
out:
	if (pd->hw_context != -1)
		ipvr_mmu_flush_ptes(pd, f_address, num_pages,
				   desired_tile_stride, hw_tile_stride);

	up_read(&pd->driver->sem);

	if (pd->hw_context != -1)
		ipvr_mmu_flush(pd->driver, 1);

	ipvr_stat_add_mmu_bind(pd->driver->dev_priv, num_pages << PAGE_SHIFT);
	return ret;
}
