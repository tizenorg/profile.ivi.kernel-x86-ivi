/**************************************************************************
 * ipvr_bo.c: IPVR buffer creation/destory, import/export, map etc
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

#include "ipvr_bo.h"
#include "ipvr_trace.h"
#include "ipvr_debug.h"
#include <drmP.h>
#include <linux/dma-buf.h>

static inline bool cpu_cache_is_coherent(enum ipvr_cache_level level)
{
	/* on valleyview no cache snooping */
	return (level != IPVR_CACHE_WRITEBACK);
}

static inline bool clflush_object(struct drm_ipvr_gem_object *obj, bool force)
{
	if (obj->sg_table == NULL)
		return false;

	/* no need to flush if cache is coherent */
	if (!force && cpu_cache_is_coherent(obj->cache_level))
		return false;

	drm_clflush_sg(obj->sg_table);

	return true;
}

static void
ipvr_object_free(struct drm_ipvr_gem_object *obj)
{
	struct drm_ipvr_private *dev_priv = obj->base.dev->dev_private;
	kmem_cache_free(dev_priv->ipvr_bo_slab, obj);
}

static struct drm_ipvr_gem_object *
ipvr_object_alloc(struct drm_ipvr_private *dev_priv, size_t size)
{
	struct drm_ipvr_gem_object *obj;

	obj = kmem_cache_alloc(dev_priv->ipvr_bo_slab, GFP_KERNEL | __GFP_ZERO);
	if (obj == NULL)
		return NULL;
	memset(obj, 0, sizeof(*obj));

	return obj;
}

static int ipvr_gem_mmu_bind_object(struct drm_ipvr_gem_object *obj)
{
	struct drm_ipvr_private *dev_priv = obj->base.dev->dev_private;
	u32 mask = 0;
	const unsigned long entry = ipvr_gem_object_mmu_offset(obj);

	if (IPVR_IS_ERR(entry)) {
		return IPVR_OFFSET_ERR(entry);
	}

	IPVR_DEBUG_GENERAL("entry is 0x%lx, size is %zu, nents is %d.\n",
			entry, obj->base.size, obj->sg_table->nents);

	/**
	 * from vxd spec we should be able to set:
	 *   mask |= IPVR_MMU_CACHED_MEMORY
	 * for those object marked as IPVR_CACHE_NOACCESS
	 * but test failed.
	 * so we force all pages flaged as non-cached by vxd now
	 */
	switch (obj->cache_level) {
	case IPVR_CACHE_NOACCESS:
		mask |= IPVR_MMU_CACHED_MEMORY;
		break;
	default:
		break;
	}
	return ipvr_mmu_insert_pages(dev_priv->mmu->default_pd,
		obj->pages, entry, obj->base.size >> PAGE_SHIFT,
		0, 0, mask);
}

static void ipvr_gem_mmu_unbind_object(struct drm_ipvr_gem_object *obj)
{
	struct drm_ipvr_private *dev_priv = obj->base.dev->dev_private;
	const unsigned long entry = ipvr_gem_object_mmu_offset(obj);
	IPVR_DEBUG_GENERAL("entry is 0x%lx, size is %zu.\n",
			entry, obj->base.size);
	ipvr_mmu_remove_pages(dev_priv->mmu->default_pd,
		entry, obj->base.size >> PAGE_SHIFT, 0, 0);
}

static void ipvr_gem_object_pin_pages(struct drm_ipvr_gem_object *obj)
{
	BUG_ON(obj->sg_table == NULL);
	obj->pages_pin_count++;
}

static void ipvr_gem_object_unpin_pages(struct drm_ipvr_gem_object *obj)
{
	BUG_ON(obj->pages_pin_count == 0);
	obj->pages_pin_count--;
}

static int ipvr_gem_bind_to_drm_mm(struct drm_ipvr_gem_object* obj,
			struct ipvr_address_space *vm)
{
	int ret = 0;
	struct drm_mm *mm;
	unsigned long start, end;
	/* bind to VPU address space */
	if (obj->tiling) {
		mm = &vm->tiling_mm;
		start = vm->tiling_start;
		end = vm->tiling_start + vm->tiling_total;
	} else {
		mm = &vm->linear_mm;
		start = vm->linear_start;
		end = vm->linear_start + vm->linear_total;
	}
	IPVR_DEBUG_GENERAL("call drm_mm_insert_node_in_range_generic.\n");
	ret = mutex_lock_interruptible(&obj->base.dev->struct_mutex);
	if (ret)
		return ret;
	ret = drm_mm_insert_node_in_range_generic(mm, &obj->mm_node, obj->base.size,
						PAGE_SIZE, obj->cache_level,
						start, end,
						DRM_MM_SEARCH_DEFAULT);
	if (ret) {
		/* no shrinker implemented yet so simply return error */
		IPVR_ERROR("failed on drm_mm_insert_node_in_range_generic: %d\n", ret);
		goto out;
	}

	IPVR_DEBUG_GENERAL("drm_mm_insert_node_in_range_generic success: "
		"start=0x%lx, size=%lu, color=%lu.\n",
		obj->mm_node.start, obj->mm_node.size, obj->mm_node.color);

out:
	mutex_unlock(&obj->base.dev->struct_mutex);

	return ret;
}

struct drm_ipvr_gem_object* ipvr_gem_create(struct drm_ipvr_private *dev_priv,
			size_t size, u32 tiling, u32 cache_level)
{
	struct drm_ipvr_gem_object *obj;
	int ret = 0;
	int npages;
	struct address_space *mapping;
	gfp_t mask;

	BUG_ON(size & (PAGE_SIZE - 1));
	npages = size >> PAGE_SHIFT;
	IPVR_DEBUG_GENERAL("create bo size is %zu, tiling is %u, "
			"cache level is %u.\n",	size, tiling, cache_level);

	/* Allocate the new object */
	obj = ipvr_object_alloc(dev_priv, size);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	/* initialization */
	ret = drm_gem_object_init(dev_priv->dev, &obj->base, size);
	if (ret) {
		IPVR_ERROR("failed on drm_gem_object_init: %d\n", ret);
		goto err_free_obj;
	}
	init_waitqueue_head(&obj->event_queue);
	/* todo: need set correct mask */
	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;

	/* ipvr cannot relocate objects above 4GiB. */
	mask &= ~__GFP_HIGHMEM;
	mask |= __GFP_DMA32;

	mapping = file_inode(obj->base.filp)->i_mapping;
	mapping_set_gfp_mask(mapping, mask);

	obj->base.write_domain = IPVR_GEM_DOMAIN_CPU;
	obj->base.read_domains = IPVR_GEM_DOMAIN_CPU;
	obj->drv_name = "ipvr";
	obj->fence = NULL;
	obj->tiling = tiling;
	obj->cache_level = cache_level;

	/* get physical pages */
	obj->pages = drm_gem_get_pages(&obj->base,GFP_KERNEL); 
	if (IS_ERR(obj->pages)) {
		ret = PTR_ERR(obj->pages);
		IPVR_ERROR("failed on drm_gem_get_pages: %d\n", ret);
		goto err_free_obj;
	}

	obj->sg_table = drm_prime_pages_to_sg(obj->pages, obj->base.size >> PAGE_SHIFT);
	if (IS_ERR(obj->sg_table)) {
		ret = PTR_ERR(obj->sg_table);
		IPVR_ERROR("failed on drm_gem_get_pages: %d\n", ret);
		goto err_put_pages;
	}

	/* set cacheability */
	switch (obj->cache_level) {
	case IPVR_CACHE_NOACCESS:
	case IPVR_CACHE_UNCACHED:
		ret = set_pages_array_uc(obj->pages, npages);
		break;
	case IPVR_CACHE_WRITECOMBINE:
		ret = set_pages_array_wc(obj->pages, npages);
		break;
	case IPVR_CACHE_WRITEBACK:
		ret = set_pages_array_wb(obj->pages, npages);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret) {
		IPVR_DEBUG_WARN("failed to set page cache: %d.\n", ret);
		goto err_put_sg;
	}

	ipvr_gem_object_pin_pages(obj);

	/* bind to VPU address space */
	ret = ipvr_gem_bind_to_drm_mm(obj, &dev_priv->addr_space);
	if (ret) {
		IPVR_ERROR("failed to call ipvr_gem_bind_to_drm_mm: %d.\n", ret);
		goto err_put_sg;
	}

	ret = ipvr_gem_mmu_bind_object(obj);
	if (ret) {
		IPVR_ERROR("failed to call ipvr_gem_mmu_bind_object: %d.\n", ret);
		goto err_remove_node;
	}

	ipvr_stat_add_object(dev_priv, obj);
	trace_ipvr_create_object(obj, ipvr_gem_object_mmu_offset(obj));
	return obj;
err_remove_node:
	drm_mm_remove_node(&obj->mm_node);
err_put_sg:
	sg_free_table(obj->sg_table);
	kfree(obj->sg_table);
err_put_pages:
	drm_gem_put_pages(&obj->base, obj->pages, false, false);
err_free_obj:
	ipvr_object_free(obj);
	return ERR_PTR(ret);
}

void *ipvr_gem_object_vmap(struct drm_ipvr_gem_object *obj)
{
	pgprot_t pg = PAGE_KERNEL;
	switch (obj->cache_level) {
	case IPVR_CACHE_WRITECOMBINE:
		pg = pgprot_writecombine(pg);
		break;
	case IPVR_CACHE_NOACCESS:
	case IPVR_CACHE_UNCACHED:
		pg = pgprot_noncached(pg);
		break;
	default:
		break;
	}
	return vmap(obj->pages, obj->base.size >> PAGE_SHIFT, VM_MAP, pg);
}

/*
 * When the last reference to a GEM object is released the GEM core calls the
 * drm_driver .gem_free_object() operation. That operation is mandatory for
 * GEM-enabled drivers and must free the GEM object and all associated
 * resources.
 * called with struct_mutex locked.
 */
void ipvr_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_device *dev = gem_obj->dev;
	struct drm_ipvr_gem_object *obj = to_ipvr_bo(gem_obj);
	drm_ipvr_private_t *dev_priv = dev->dev_private;
	int ret;
	unsigned long mmu_offset;
	int npages = gem_obj->size >> PAGE_SHIFT;

	/* fixme: consider unlocked case */
	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	mmu_offset = ipvr_gem_object_mmu_offset(obj);
	ipvr_gem_mmu_unbind_object(obj);

	if (unlikely(obj->fence)) {
		ret = ipvr_fence_wait(obj->fence, true, false);
		if (ret)
			IPVR_DEBUG_WARN("Failed to wait fence signaled: %d.\n", ret);
	}

	drm_mm_remove_node(&obj->mm_node);
	ipvr_gem_object_unpin_pages(obj);

	if (WARN_ON(obj->pages_pin_count))
		obj->pages_pin_count = 0;

	BUG_ON(!obj->pages || !obj->sg_table);
	/* set back to page_wb */
	set_pages_array_wb(obj->pages, npages);
	if (obj->base.import_attach) {
		IPVR_DEBUG_GENERAL("free imported object (mmu_offset 0x%lx)\n", mmu_offset);
		drm_prime_gem_destroy(&obj->base, obj->sg_table);
		drm_free_large(obj->pages);
		ipvr_stat_remove_imported(dev_priv, obj);
	}
	else {
		IPVR_DEBUG_GENERAL("free object (mmu_offset 0x%lx)\n", mmu_offset);
		sg_free_table(obj->sg_table);
		kfree(obj->sg_table);
		drm_gem_put_pages(&obj->base, obj->pages, obj->dirty, true);
		ipvr_stat_remove_object(dev_priv, obj);
	}

	/* mmap offset is freed by drm_gem_object_release */
	drm_gem_object_release(&obj->base);

	trace_ipvr_free_object(obj);

	ipvr_object_free(obj);
}

static inline struct page *get_object_page(struct drm_ipvr_gem_object *obj, int n)
{
	struct sg_page_iter sg_iter;

	for_each_sg_page(obj->sg_table->sgl, &sg_iter, obj->sg_table->nents, n)
		return sg_page_iter_page(&sg_iter);

	return NULL;
}

int ipvr_gem_object_apply_reloc(struct drm_ipvr_gem_object *obj,
				u64 offset_in_bo, u32 value)
{
	u64 page_offset = offset_in_page(offset_in_bo);
	char *vaddr;
	struct page *target_page;

	/* set to cpu domain */
	target_page = get_object_page(obj,	offset_in_bo >> PAGE_SHIFT);
	if (!target_page)
		return -EINVAL;

	/**
	 * for efficiency we'd better record the page index,
	 * and avoid frequent map/unmap on the same page
	 */
	vaddr = kmap_atomic(target_page);
	if (!vaddr)
		return -ENOMEM;
	*(u32 *)(vaddr + page_offset) = value;

	kunmap_atomic(vaddr);

	return 0;
}

int ipvr_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct drm_device *dev = obj->dev;
	unsigned long pfn;
	pgoff_t pgoff;
	int ret;
	struct drm_ipvr_gem_object *ipvr_obj = to_ipvr_bo(obj);

	/* Make sure we don't parallel update on a fault, nor move or remove
	 * something from beneath our feet
	 */
	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		goto out;

	if (!ipvr_obj->sg_table) {
		ret = -ENODATA;
		goto out_unlock;
	}

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	pfn = page_to_pfn(ipvr_obj->pages[pgoff]);

	IPVR_DEBUG_GENERAL("Inserting %p pfn %lx, pa %lx\n", vmf->virtual_address,
			pfn, pfn << PAGE_SHIFT);

	ret = vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);

out_unlock:
	mutex_unlock(&dev->struct_mutex);
out:
	switch (ret) {
	case -EAGAIN:
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

struct sg_table *ipvr_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_ipvr_gem_object *ipvr_obj = to_ipvr_bo(obj);
	struct sg_table *sgt = NULL;
	int ret;

	if (!ipvr_obj->sg_table) {
		ret = -ENOENT;
		goto out;
	}

	sgt = drm_prime_pages_to_sg(ipvr_obj->pages, obj->size >> PAGE_SHIFT);
	if (IS_ERR(sgt)) {
		goto out;
	}

	IPVR_DEBUG_GENERAL("exported sg_table for obj (mmu_offset 0x%lx)\n",
		ipvr_gem_object_mmu_offset(ipvr_obj));
out:
	return sgt;
}

struct drm_gem_object *ipvr_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg)
{
	struct drm_ipvr_gem_object *obj;
	int ret = 0;
	int i, npages;
	unsigned long pfn;
	struct drm_ipvr_private *dev_priv = dev->dev_private;

	if (!sg || !attach || (attach->dmabuf->size & (PAGE_SIZE - 1)))
		return ERR_PTR(-EINVAL);

	IPVR_DEBUG_ENTRY("enter, size=0x%zx\n", attach->dmabuf->size);

	obj = ipvr_object_alloc(dev_priv, attach->dmabuf->size);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	memset(obj, 0, sizeof(*obj));

	drm_gem_private_object_init(dev, &obj->base, attach->dmabuf->size);

	init_waitqueue_head(&obj->event_queue);

	obj->drv_name = "ipvr";
	obj->fence = NULL;
	obj->cache_level = IPVR_CACHE_NOACCESS;
	obj->tiling = 0;

	npages = attach->dmabuf->size >> PAGE_SHIFT;

	obj->sg_table = sg;
	obj->pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (!obj->pages) {
		ret = -ENOMEM;
		goto err_free_obj;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sg, obj->pages, NULL, npages);
	if (ret)
		goto err_put_pages;

	/* validate sg_table
	 * should be under 4GiB
	 */
	for (i = 0; i < npages; ++i) {
		pfn = page_to_pfn(obj->pages[i]);
		if (pfn >= 0x00100000UL) {
			IPVR_ERROR("cannot support pfn 0x%lx.\n", pfn);
			ret = -EINVAL; /* what's the better err code? */
			goto err_put_pages;
		}
	}

	ret = ipvr_gem_bind_to_drm_mm(obj, &dev_priv->addr_space);
	if (ret) {
		IPVR_ERROR("failed to call ipvr_gem_bind_to_drm_mm: %d.\n", ret);
		goto err_put_pages;
	}

	/* do we really have to set the external pages uncached?
	 * this might causes perf issue in exporter side */
	ret = set_pages_array_uc(obj->pages, npages);
	if (ret)
		IPVR_DEBUG_WARN("failed to set imported pages as uncached: %d, ignore\n", ret);

	ret = ipvr_gem_mmu_bind_object(obj);
	if (ret) {
		IPVR_ERROR("failed to call ipvr_gem_mmu_bind_object: %d.\n", ret);
		goto err_remove_node;
	}
	IPVR_DEBUG_GENERAL("imported sg_table, new bo mmu offset=0x%lx.\n",
		ipvr_gem_object_mmu_offset(obj));
	ipvr_stat_add_imported(dev_priv, obj);
	ipvr_gem_object_pin_pages(obj);
	return &obj->base;
err_remove_node:
	drm_mm_remove_node(&obj->mm_node);
err_put_pages:
	drm_free_large(obj->pages);
err_free_obj:
	ipvr_object_free(obj);
	return ERR_PTR(ret);
}

int ipvr_gem_prime_pin(struct drm_gem_object *obj)
{
	struct drm_ipvr_private *dev_priv = obj->dev->dev_private;
	IPVR_DEBUG_ENTRY("mmu offset 0x%lx\n", ipvr_gem_object_mmu_offset(to_ipvr_bo(obj)));
	ipvr_stat_add_exported(dev_priv, to_ipvr_bo(obj));
	return 0;
}

void ipvr_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct drm_ipvr_private *dev_priv = obj->dev->dev_private;
	IPVR_DEBUG_ENTRY("mmu offset 0x%lx\n", ipvr_gem_object_mmu_offset(to_ipvr_bo(obj)));
	ipvr_stat_remove_exported(dev_priv, to_ipvr_bo(obj));
}
