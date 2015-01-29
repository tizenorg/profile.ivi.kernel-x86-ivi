/**************************************************************************
 * ipvr_exec.c: IPVR command buffer execution
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

#include "ipvr_exec.h"
#include "ipvr_gem.h"
#include "ipvr_mmu.h"
#include "ipvr_bo.h"
#include "ipvr_fence.h"
#include "ipvr_trace.h"
#include "ved_fw.h"
#include "ved_msg.h"
#include "ved_reg.h"
#include "ved_pm.h"
#include "ved_cmd.h"
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

static inline bool ipvr_bo_is_reserved(struct drm_ipvr_gem_object *obj)
{
	return atomic_read(&obj->reserved);
}

static int
ipvr_bo_wait_unreserved(struct drm_ipvr_gem_object *obj, bool interruptible)
{
	if (interruptible) {
		return wait_event_interruptible(obj->event_queue,
					       !ipvr_bo_is_reserved(obj));
	} else {
		wait_event(obj->event_queue, !ipvr_bo_is_reserved(obj));
		return 0;
	}
}

/**
 * ipvr_bo_reserve - reserve the given bo
 *
 * @obj:     The buffer object to reserve.
 * @interruptible:     whether the waiting is interruptible or not.
 * @no_wait:    flag to indicate returning immediately
 *
 * Returns: 0 if successful, error code otherwise
 */
int ipvr_bo_reserve(struct drm_ipvr_gem_object *obj,
			bool interruptible, bool no_wait)
{
	int ret;

	while (unlikely(atomic_xchg(&obj->reserved, 1) != 0)) {
		if (no_wait)
			return -EBUSY;
		IPVR_DEBUG_GENERAL("wait bo unreserved, add to wait queue.\n");
		ret = ipvr_bo_wait_unreserved(obj, interruptible);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

/**
 * ipvr_bo_unreserve - unreserve the given bo
 *
 * @obj:     The buffer object to reserve.
 *
 * No return value.
 */
void ipvr_bo_unreserve(struct drm_ipvr_gem_object *obj)
{
	atomic_set(&obj->reserved, 0);
	wake_up_all(&obj->event_queue);
}

static void ipvr_backoff_reservation(struct list_head *list)
{
	struct ipvr_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct drm_ipvr_gem_object *obj = entry->ipvr_gem_bo;
		if (!atomic_read(&obj->reserved))
			continue;
		atomic_set(&obj->reserved, 0);
		wake_up_all(&obj->event_queue);
	}
}

/*
 * ipvr_reserve_buffers - Reserve buffers for validation.
 *
 * @list:     points to a bo list to be backoffed
 *
 * If a buffer in the list is marked for CPU access, we back off and
 * wait for that buffer to become free for VPU access.
 *
 * If a buffer is reserved for another validation, the validator with
 * the highest validation sequence backs off and waits for that buffer
 * to become unreserved. This prevents deadlocks when validating multiple
 * buffers in different orders.
 *
 * Returns:
 * 0 on success, error code on failure.
 */
int ipvr_reserve_buffers(struct list_head *list)
{
	struct ipvr_validate_buffer *entry;
	int ret;

	if (list_empty(list))
		return 0;

	list_for_each_entry(entry, list, head) {
		struct drm_ipvr_gem_object *bo = entry->ipvr_gem_bo;

		ret = ipvr_bo_reserve(bo, true, true);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
			ret = ipvr_bo_reserve(bo, true, false);
			if (!ret)
				break;
			else
				goto err;
		default:
			goto err;
		}
	}

	return 0;
err:
	ipvr_backoff_reservation(list);
	return ret;
}

/**
 * ipvr_set_tile - global setting of tiling info
 *
 * @dev:     the ipvr drm device
 * @tiling_scheme:     see ipvr_drm.h for details
 * @tiling_stride:     see ipvr_drm.h for details
 *
 * vxd392 hardware supports only one tile region so this configuration
 * is global.
 */
void ipvr_set_tile(struct drm_ipvr_private *dev_priv,
		u8 tiling_scheme, u8 tiling_stride)
{
	u32 cmd;
	u32 start = IPVR_MEM_MMU_TILING_START;
	u32 end = IPVR_MEM_MMU_TILING_END;

	/* Enable memory tiling */
	cmd = ((start >> 20) + (((end >> 20) - 1) << 12) +
				((0x8 | tiling_stride) << 24));
	IPVR_DEBUG_GENERAL("VED: MMU Tiling register0 %08x.\n", cmd);
	IPVR_DEBUG_GENERAL("Region 0x%08x-0x%08x.\n", start, end);
	IPVR_REG_WRITE32(cmd, MSVDX_MMU_TILE_BASE0_OFFSET);

	/* we need set tile format as 512x8 on Baytrail, which is shceme 1 */
	IPVR_REG_WRITE32(tiling_scheme << 3, MSVDX_MMU_CONTROL2_OFFSET);
}

/**
 * ipvr_find_ctx_with_fence - lookup the context with given fence seqno
 *
 * @dev_priv:     the ipvr drm device
 * @fence:     fence seqno generated by the context
 *
 * Returns:
 * context pointer if found.
 * NULL if not found.
 */
struct ipvr_context*
ipvr_find_ctx_with_fence(struct drm_ipvr_private *dev_priv, u16 fence)
{
	struct ipvr_context *pos;
	int id = 0;

	spin_lock(&dev_priv->ipvr_ctx_lock);
	idr_for_each_entry(&dev_priv->ipvr_ctx_idr, pos, id) {
		if (pos->cur_seq == fence) {
			spin_unlock(&dev_priv->ipvr_ctx_lock);
			return pos;
		}
	}
	spin_unlock(&dev_priv->ipvr_ctx_lock);

	return NULL;
}

static void ipvr_unreference_buffers(struct ipvr_validate_context *context)
{
	struct ipvr_validate_buffer *entry, *next;
	struct drm_ipvr_gem_object *obj;
	struct list_head *list = &context->validate_list;

	list_for_each_entry_safe(entry, next, list, head) {
		obj = entry->ipvr_gem_bo;
		list_del(&entry->head);
		drm_gem_object_unreference_unlocked(&obj->base);
		context->used_buffers--;
	}
}

static int ipvr_update_buffers(struct drm_file *file_priv,
					struct ipvr_validate_context *context,
					u64 buffer_list,
					int count)
{
	struct ipvr_validate_buffer *entry;
	struct drm_ipvr_gem_exec_object __user *val_arg
		= (struct drm_ipvr_gem_exec_object __user *)(uintptr_t)buffer_list;

	if (list_empty(&context->validate_list))
		return 0;

	list_for_each_entry(entry, &context->validate_list, head) {
		if (!val_arg) {
			IPVR_DEBUG_WARN("unexpected end of val_arg list!!!\n");
			return -EINVAL;
		}
		if (unlikely(copy_to_user(val_arg, &entry->val_req,
					    sizeof(entry->val_req)))) {
			IPVR_ERROR("copy_to_user fault.\n");
			return -EFAULT;
		}
		val_arg ++;
	}
	return 0;
}

static int ipvr_reference_buffers(struct drm_file *file_priv,
					struct ipvr_validate_context *context,
					u64 buffer_list,
					int count)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_ipvr_gem_exec_object __user *val_arg
		= (struct drm_ipvr_gem_exec_object __user *)(uintptr_t)buffer_list;
	struct ipvr_validate_buffer *item;
	struct drm_ipvr_gem_object *obj;
	int ret = 0;
	int i = 0;

	for (i = 0; i < count; ++i) {
		if (unlikely(context->used_buffers >= IPVR_NUM_VALIDATE_BUFFERS)) {
			IPVR_ERROR("Too many buffers on validate list.\n");
			ret = -EINVAL;
			goto out_err;
		}
		item = &context->buffers[context->used_buffers];
		if (unlikely(copy_from_user(&item->val_req, val_arg,
					    sizeof(item->val_req)) != 0)) {
			IPVR_ERROR("copy_from_user fault.\n");
			ret = -EFAULT;
			goto out_err;
		}
		INIT_LIST_HEAD(&item->head);
		obj = to_ipvr_bo(drm_gem_object_lookup(dev, file_priv,
						item->val_req.handle));
		if (&obj->base == NULL) {
			IPVR_ERROR("cannot find obj for handle %u at position %d.\n",
				item->val_req.handle, i);
			ret = -ENOENT;
			goto out_err;
		}
		item->ipvr_gem_bo = obj;

		list_add_tail(&item->head, &context->validate_list);
		context->used_buffers++;

		val_arg++;
	}

	return 0;

out_err:
	ipvr_unreference_buffers(context);
	return ret;
}

static int ipvr_fixup_reloc_entries(struct drm_device *dev,
					struct drm_file *filp,
					struct ipvr_validate_buffer *val_obj)
{
	int i, ret;
	u64 mmu_offset;
	struct drm_ipvr_gem_object *obj, *target_obj;
	struct drm_ipvr_gem_exec_object *exec_obj = &val_obj->val_req;
	struct drm_ipvr_gem_relocation_entry __user *reloc_entries
		= (struct drm_ipvr_gem_relocation_entry __user *)(uintptr_t)exec_obj->relocs_ptr;
	struct drm_ipvr_gem_relocation_entry local_reloc_entry;

	obj = val_obj->ipvr_gem_bo;
	if (!obj)
		return -ENOENT;

	/* todo: check write access */

	/* overwrite user content and update relocation entries */
	mmu_offset = ipvr_gem_object_mmu_offset(obj);
	if (mmu_offset != exec_obj->offset) {
		exec_obj->offset = mmu_offset;
		IPVR_DEBUG_GENERAL("Fixup BO %u offset to 0x%llx\n",
			exec_obj->handle, exec_obj->offset);
	}
	for (i = 0; i < exec_obj->relocation_count; ++i) {
		if (unlikely(copy_from_user(&local_reloc_entry, &reloc_entries[i],
					    sizeof(local_reloc_entry)) != 0)) {
			IPVR_ERROR("copy_from_user fault.\n");
			return -EFAULT;
		}
		target_obj = to_ipvr_bo(drm_gem_object_lookup(dev, filp,
						local_reloc_entry.target_handle));
		if (&target_obj->base == NULL) {
			IPVR_ERROR("cannot find obj for handle %u at position %d.\n",
				local_reloc_entry.target_handle, i);
			return -ENOENT;
		}
		ret = ipvr_gem_object_apply_reloc(obj,
				local_reloc_entry.offset,
				local_reloc_entry.delta + ipvr_gem_object_mmu_offset(target_obj));
		if (ret) {
			IPVR_ERROR("Failed applying reloc: %d\n", ret);
			drm_gem_object_unreference_unlocked(&target_obj->base);
			return ret;
		}
		if (unlikely(copy_to_user(&reloc_entries[i], &local_reloc_entry,
						sizeof(local_reloc_entry)) != 0)) {
			IPVR_DEBUG_WARN("copy_to_user fault.\n");
		}
		IPVR_DEBUG_GENERAL("Fixup offset %llx in BO %u to 0x%lx\n",
			local_reloc_entry.offset, exec_obj->handle,
			local_reloc_entry.delta + ipvr_gem_object_mmu_offset(target_obj));
		drm_gem_object_unreference_unlocked(&target_obj->base);
	}
	return 0;
}

static int ipvr_fixup_relocs(struct drm_device *dev,
					struct drm_file *filp,
					struct ipvr_validate_context *context)
{
	int ret;
	struct ipvr_validate_buffer *entry;

	if (list_empty(&context->validate_list)) {
		IPVR_DEBUG_WARN("No relocs required in validate contex, skip\n");
		return 0;
	}

	list_for_each_entry(entry, &context->validate_list, head) {
		IPVR_DEBUG_GENERAL("Fixing up reloc for BO handle %u\n",
			entry->val_req.handle);
		ret = ipvr_fixup_reloc_entries(dev, filp, entry);
		if (ret) {
			IPVR_ERROR("Failed to fixup reloc for BO handle %u\n",
				entry->val_req.handle);
			return ret;
		}
	}
	return 0;
}

static int ipvr_validate_buffer_list(struct drm_file *file_priv,
					struct ipvr_validate_context *context,
					bool *need_fixup_relocs,
					struct drm_ipvr_gem_object **cmd_buffer)
{
	struct ipvr_validate_buffer *entry;
	struct drm_ipvr_gem_object *obj;
	struct list_head *list = &context->validate_list;
	int ret = 0;
	u64 real_mmu_offset;

	list_for_each_entry(entry, list, head) {
		obj = entry->ipvr_gem_bo;
		/**
		 * need validate bo locate in the mmu space
		 * check if presumed offset is correct
		 * with ved_check_presumed, if presume is not correct,
		 * call fixup relocs with ved_fixup_relocs.
		 * current implementation doesn't support shrink/evict,
		 * so needn't validate mmu offset.
		 * need be implemented in the future if shrink/evict
		 * is supported.
		 */
		real_mmu_offset = ipvr_gem_object_mmu_offset(obj);
		if (IPVR_IS_ERR(real_mmu_offset))
			return -ENOENT;
		if (entry->val_req.offset != real_mmu_offset) {
			IPVR_DEBUG_GENERAL("BO %u offset doesn't match MMU, need fixup reloc\n", entry->val_req.handle);
			*need_fixup_relocs = true;
		}
		if (entry->val_req.flags & IPVR_EXEC_OBJECT_SUBMIT) {
			if (*cmd_buffer != NULL) {
				IPVR_ERROR("Only one BO can be submitted in one exec ioctl\n");
				return -EINVAL;
			}
			*cmd_buffer = obj;
		}
	}

	return ret;
}

/**
 * ipvr_gem_do_execbuffer - lookup the context with given fence seqno
 *
 * @dev:     the ipvr drm device
 * @file_priv:      the ipvr drm file pointer
 * @args:      input argument passed from userland
 * @vm:      ipvr address space for all the bo to bind to
 *
 * Returns: 0 on success, error code on failure
 */
static int ipvr_gem_do_execbuffer(struct drm_device *dev,
					struct drm_file *file_priv,
					struct drm_ipvr_gem_execbuffer *args,
					struct ipvr_address_space *vm)
{
	drm_ipvr_private_t *dev_priv = dev->dev_private;
	struct ipvr_validate_context *context = &dev_priv->validate_ctx;
	struct ved_private *ved_priv = dev_priv->ved_private;
	struct drm_ipvr_gem_object *cmd_buffer = NULL;
	struct ipvr_context *ipvr_ctx  = NULL;
	int ret, ctx_id;
	bool need_fixup_relocs = false;

	/* if not pass 0, use default context instead */
	if (args->ctx_id == 0)
		ctx_id = dev_priv->default_ctx.ctx_id;
	else
		ctx_id = args->ctx_id;

	IPVR_DEBUG_GENERAL("try to find ctx according ctx_id %d.\n", ctx_id);

	/* we're already in struct_mutex lock */
	ipvr_ctx = (struct ipvr_context *)
			idr_find(&dev_priv->ipvr_ctx_idr, ctx_id);
	if (!ipvr_ctx) {
		IPVR_DEBUG_WARN("video ctx is not found.\n");
		return -ENOENT;
	}

	IPVR_DEBUG_GENERAL("reference all buffers passed through buffer_list.\n");
	ret = ipvr_reference_buffers(file_priv, context,
				args->buffers_ptr, args->buffer_count);
	if (unlikely(ret)) {
		IPVR_DEBUG_WARN("reference buffer failed: %d.\n", ret);
		return ret;
	}

	IPVR_DEBUG_GENERAL("reserve all buffers to make them not accessed "
			"by other threads.\n");
	ret = ipvr_reserve_buffers(&context->validate_list);
	if (unlikely(ret)) {
		IPVR_ERROR("reserve buffers failed.\n");
		/* -EBUSY or -ERESTARTSYS */
		goto out_unref_buf;
	}

	IPVR_DEBUG_GENERAL("validate buffer list, mainly check "
			"the bo mmu offset.\n");
	ret = ipvr_validate_buffer_list(file_priv, context, &need_fixup_relocs, &cmd_buffer);
	if (unlikely(ret)) {
		IPVR_ERROR("validate buffers failed: %d.\n", ret);
		goto out_backoff_reserv;
	}

	if (unlikely(cmd_buffer == NULL)) {
		IPVR_ERROR("No cmd BO found.\n");
		ret = -EINVAL;
		goto out_backoff_reserv;
	}

	if (unlikely(need_fixup_relocs)) {
		ret = ipvr_fixup_relocs(dev, file_priv, context);
		if (ret) {
			IPVR_ERROR("fixup relocs failed.\n");
			goto out_backoff_reserv;
		}
	}

#if 0
	bo = idr_find(&file_priv->object_idr, args->cmdbuf_handle);
	if (!bo) {
		IPVR_DEBUG_WARN("Invalid cmd object handle 0x%x.\n",
			args->cmdbuf_handle);
		ret = -EINVAL;
		goto out_backoff_reserv;
	}

	cmd_buffer = to_ipvr_bo(bo);
#endif
	/**
	 * check contex id and type
	 */
	/*
	 * only VED is supported currently
	 */
	if (ipvr_ctx->ctx_type == IPVR_CONTEXT_TYPE_VED)
	{
		/* fixme: should support non-zero start_offset */
		if (unlikely(args->exec_start_offset != 0)) {
			IPVR_ERROR("Unsupported exec_start_offset %u\n", args->exec_start_offset);
			ret = -EINVAL;
			goto out_backoff_reserv;
		}

		ret = mutex_lock_interruptible(&ved_priv->ved_mutex);
		if (unlikely(ret)) {
			IPVR_ERROR("Error get VED mutex: %d\n", ret);
			/* -EINTR */
			goto out_backoff_reserv;
		}

		IPVR_DEBUG_GENERAL("parse cmd buffer and send to VED.\n");
		ret = ved_cmdbuf_video(ved_priv, cmd_buffer,
				args->exec_len, ipvr_ctx );
		if (unlikely(ret)) {
			IPVR_ERROR("ved_cmdbuf_video returns %d.\n", ret);
			/* -EINVAL, -ENOMEM, -EFAULT, -EBUSY */
			mutex_unlock(&ved_priv->ved_mutex);
			goto out_backoff_reserv;
		}

		mutex_unlock(&ved_priv->ved_mutex);
	}

	/**
	 * update mmu_offsets and fence fds to user
	 */
	ret = ipvr_update_buffers(file_priv, context,
				args->buffers_ptr, args->buffer_count);
	if (unlikely(ret)) {
		IPVR_DEBUG_WARN("ipvr_update_buffers returns error %d.\n", ret);
		ret = 0;
	}

out_backoff_reserv:
	IPVR_DEBUG_GENERAL("unreserve buffer list.\n");
	ipvr_backoff_reservation(&context->validate_list);
out_unref_buf:
	IPVR_DEBUG_GENERAL("unref bufs which are refered during bo lookup.\n");
	ipvr_unreference_buffers(context);
	return ret;
}

/**
 * ipvr_gem_do_execbuffer - lookup the context with given fence seqno
 *
 * ioctl entry for DRM_IPVR_GEM_EXECBUFFER
 *
 * Returns: 0 on success, error code on failure
 */
int ipvr_gem_execbuffer_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_ipvr_private *dev_priv = dev->dev_private;
	struct drm_ipvr_gem_execbuffer *args = data;
	int ret;
	struct ipvr_validate_context *context = &dev_priv->validate_ctx;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	if (!context || !context->buffers) {
		ret = -EINVAL;
		goto out;
	}

	context->used_buffers = 0;

	if (args->buffer_count < 1 ||
		args->buffer_count >
			(UINT_MAX / sizeof(struct ipvr_validate_buffer))) {
		IPVR_ERROR("validate %d buffers.\n", args->buffer_count);
		ret = -EINVAL;
		goto out;
	}

	trace_ipvr_execbuffer(args);
	ret = ipvr_gem_do_execbuffer(dev, file_priv, args,
				    &dev_priv->addr_space);
out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}
