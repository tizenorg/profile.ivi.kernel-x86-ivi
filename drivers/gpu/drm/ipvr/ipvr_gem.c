/**************************************************************************
 * ipvr_gem.c: IPVR hook file for gem ioctls
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

#include "ipvr_gem.h"
#include "ipvr_bo.h"
#include "ipvr_fence.h"
#include "ipvr_exec.h"
#include "ipvr_trace.h"
//#include <drm_gem.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/pci.h>
#include <linux/dma-buf.h>

#define VLV_IPVR_DEV_ID (0xf31)

int
ipvr_context_create_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv)
{
	struct drm_ipvr_context_create *args = data;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	struct drm_ipvr_file_private *fpriv = file_priv->driver_priv;
	struct ipvr_context *ipvr_ctx  = NULL;
	unsigned long irq_flags;
	int ctx_id, ret = 0;

	IPVR_DEBUG_ENTRY("enter\n");
	/*
	 * todo: only one tiling region is supported now,
	 * maybe we need create additional tiling region for rotation case,
	 * which has different tiling stride
	 */
	if (!(args->tiling_scheme == 0 && args->tiling_stride <= 3) &&
		!(args->tiling_scheme == 1 && args->tiling_stride <= 2)) {
		IPVR_DEBUG_WARN("unsupported tiling scheme %d and stide %d.\n",
			args->tiling_scheme, args->tiling_stride);
		return -EINVAL;
	}
	/* add video decode context */
	ipvr_ctx = kzalloc(sizeof(struct ipvr_context), GFP_KERNEL);
	if (ipvr_ctx  == NULL)
		return -ENOMEM;

	spin_lock_irqsave(&dev_priv->ipvr_ctx_lock, irq_flags);
	ctx_id = idr_alloc(&dev_priv->ipvr_ctx_idr, ipvr_ctx ,
			   IPVR_MIN_CONTEXT_ID, IPVR_MAX_CONTEXT_ID,
			   GFP_NOWAIT);
	if (ctx_id < 0) {
		IPVR_ERROR("idr_alloc got %d, return ENOMEM\n", ctx_id);
		spin_unlock_irqrestore(&dev_priv->ipvr_ctx_lock, irq_flags);
		return -ENOMEM;
	}
	ipvr_ctx->ctx_id = ctx_id;

	INIT_LIST_HEAD(&ipvr_ctx->head);
	ipvr_ctx->ctx_type = args->ctx_type;
	ipvr_ctx->ipvr_fpriv = file_priv->driver_priv;
	list_add(&ipvr_ctx ->head, &fpriv->ctx_list);
	spin_unlock_irqrestore(&dev_priv->ipvr_ctx_lock, irq_flags);
	args->ctx_id = ctx_id;
	IPVR_DEBUG_INIT("add ctx type 0x%x, ctx_id is %d.\n",
			ipvr_ctx->ctx_type, ctx_id);

	ipvr_ctx->tiling_scheme = args->tiling_scheme;
	ipvr_ctx->tiling_stride = args->tiling_stride;

	return ret;
}

int
ipvr_context_destroy_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv)
{
	struct drm_ipvr_context_destroy *args = data;
	struct ved_private *ved_priv;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	struct ipvr_context *ipvr_ctx  = NULL;
	unsigned long irq_flags;

	IPVR_DEBUG_ENTRY("enter\n");
	spin_lock_irqsave(&dev_priv->ipvr_ctx_lock, irq_flags);
	ved_priv = dev_priv->ved_private;
	if (ved_priv && (!list_empty(&ved_priv->ved_queue)
			|| (atomic_read(&dev_priv->pending_events) > 0))) {
		IPVR_DEBUG_WARN("Destroying the context while pending cmds exist!\n");
	}
	ipvr_ctx = (struct ipvr_context *)
			idr_find(&dev_priv->ipvr_ctx_idr, args->ctx_id);
	if (!ipvr_ctx) {
		IPVR_ERROR("can not find given context %u\n", args->ctx_id);
		spin_unlock_irqrestore(&dev_priv->ipvr_ctx_lock, irq_flags);
		return -EINVAL;
	}

	if (ipvr_ctx->ipvr_fpriv != file_priv->driver_priv) {
		IPVR_ERROR("given contex %u doesn't belong to the file\n", args->ctx_id);
		spin_unlock_irqrestore(&dev_priv->ipvr_ctx_lock, irq_flags);
		return -ENOENT;
	}

	IPVR_DEBUG_GENERAL("Video:remove context %d type 0x%x\n",
		ipvr_ctx->ctx_id, ipvr_ctx->ctx_type);
	list_del(&ipvr_ctx->head);
	idr_remove(&dev_priv->ipvr_ctx_idr, ipvr_ctx->ctx_id);
	kfree(ipvr_ctx);
	spin_unlock_irqrestore(&dev_priv->ipvr_ctx_lock, irq_flags);
	return 0;
}

int
ipvr_get_info_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	struct drm_ipvr_get_info *args = data;
	int ret = 0;

	IPVR_DEBUG_ENTRY("enter\n");
	if (!dev_priv) {
		IPVR_DEBUG_WARN("called with no initialization.\n");
		return -ENODEV;
	}
	switch (args->key) {
	case IPVR_DEVICE_INFO: {
		/* only vlv supported now
		 */
		args->value = VLV_IPVR_DEV_ID << 16;
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

int ipvr_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	int ret;
	struct drm_ipvr_gem_create *args = data;
	struct drm_ipvr_gem_object *obj;
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	if (args->cache_level >= IPVR_CACHE_MAX)
		return -EINVAL;
	if (args->size == 0)
		return -EINVAL;
	args->rounded_size = roundup(args->size, PAGE_SIZE);
	obj = ipvr_gem_create(dev_priv, args->rounded_size, args->tiling,
			      args->cache_level);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto out;
	}
	args->mmu_offset = ipvr_gem_object_mmu_offset(obj);
	/* create handle */
	ret = drm_gem_handle_create(file_priv, &obj->base, &args->handle);
	if (ret) {
		IPVR_ERROR("could not allocate mmap offset: %d\n", ret);
		goto out_free;
	}
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&obj->base);
	/* create map offset */
	ret = drm_gem_create_mmap_offset(&obj->base);
	if (ret) {
		IPVR_ERROR("could not allocate mmap offset: %d\n", ret);
		goto out_free;
	}
	args->map_offset = drm_vma_node_offset_addr(&obj->base.vma_node);
	IPVR_DEBUG_GENERAL("bo create done, handle: %u, vpu offset: 0x%llx.\n",
		args->handle, args->mmu_offset);
	return 0;
out_free:
	ipvr_gem_free_object(&obj->base);
out:
	return ret;
}

int ipvr_gem_busy_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_ipvr_gem_busy *args = data;
	struct drm_ipvr_gem_object *obj;
	int ret = 0;

	obj = to_ipvr_bo(drm_gem_object_lookup(dev, file_priv, args->handle));
	if (!obj || &obj->base == NULL) {
		return -ENOENT;
	}
	IPVR_DEBUG_GENERAL("Checking bo %p (fence %p seq %u) busy status\n",
        obj, obj->fence, ((obj->fence)? obj->fence->seq: 0));

	ret = ipvr_bo_reserve(obj, true, false);
	if (unlikely(ret != 0))
		goto out;
	ret = ipvr_fence_wait(obj->fence, true, true);
	ipvr_bo_unreserve(obj);

    args->busy = ret? 1: 0;
out:
	drm_gem_object_unreference_unlocked(&obj->base);
	return ret;
}

/**
 * ipvr_gem_wait_ioctl - implements DRM_IOCTL_IPVR_GEM_WAIT
 * @DRM_IOCTL_ARGS: standard ioctl arguments
 *
 * Returns 0 if successful, else an error is returned with the remaining time in
 * the timeout parameter.
 *  -ETIME: object is still busy after timeout
 *  -ERESTARTSYS: signal interrupted the wait
 *  -ENONENT: object doesn't exist
 * Also possible, but rare:
 *  -EAGAIN: VPU wedged
 *  -ENOMEM: damn
 *  -ENODEV: Internal IRQ fail
 *  -E?: The add request failed
 *
 * The wait ioctl with a timeout of 0 reimplements the busy ioctl. With any
 * non-zero timeout parameter the wait ioctl will wait for the given number of
 * nanoseconds on an object becoming unbusy. Since the wait itself does so
 * without holding struct_mutex the object may become re-busied before this
 * function completes. A similar but shorter * race condition exists in the busy
 * ioctl
 */
int ipvr_gem_wait_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv)
{
	struct drm_ipvr_gem_wait *args = data;
	struct drm_ipvr_gem_object *obj;
	int ret = 0;

	IPVR_DEBUG_ENTRY("wait %d buffer to finish execution.\n", args->handle);
	obj = to_ipvr_bo(drm_gem_object_lookup(dev, file_priv, args->handle));
	if (&obj->base == NULL) {
		return -ENOENT;
	}

	ret = ipvr_bo_reserve(obj, true, false);
	if (unlikely(ret != 0))
		goto out;

	ret = ipvr_fence_wait(obj->fence, true, false);

	ipvr_bo_unreserve(obj);

out:
	drm_gem_object_unreference_unlocked(&obj->base);
	return ret;
}

int ipvr_gem_mmap_offset_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv)
{
	int ret = 0;
	struct drm_ipvr_gem_mmap_offset *args = data;
	struct drm_ipvr_gem_object *obj;

	IPVR_DEBUG_ENTRY("getting mmap offset for BO %u.\n", args->handle);
	obj = to_ipvr_bo(drm_gem_object_lookup(dev, file_priv, args->handle));

	/* create map offset */
	ret = drm_gem_create_mmap_offset(&obj->base);
	if (ret) {
		IPVR_ERROR("could not allocate mmap offset: %d\n", ret);
		goto out;
	}
	args->offset = drm_vma_node_offset_addr(&obj->base.vma_node);
out:
	drm_gem_object_unreference_unlocked(&obj->base);
	return ret;
}
