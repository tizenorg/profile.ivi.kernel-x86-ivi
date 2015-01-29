/**************************************************************************
 * ipvr_fence.c: IPVR fence handling to track command exectuion status
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

#include "ipvr_fence.h"
#include "ipvr_exec.h"
#include "ipvr_bo.h"
#include "ipvr_trace.h"
#include "ved_reg.h"
#include "ved_fw.h"
#include "ved_cmd.h"
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>

/**
 * ipvr_fence_create - create and init a fence
 *
 * @dev_priv: drm_ipvr_private pointer
 * @fence: ipvr fence object
 * @fence_fd: file descriptor for exporting fence
 *
 * Create a fence, actually the fence is written to ipvr through msg.
 * exporting a new file descriptor to userspace.
 * Returns pointer on success, ERR_PTR otherwise.
 */
struct ipvr_fence* __must_check
ipvr_fence_create(struct drm_ipvr_private *dev_priv)
{
	struct ipvr_fence *fence;
	unsigned long irq_flags;
	u16 old_seq;
	struct ved_private *ved_priv;

	ved_priv = dev_priv->ved_private;

	fence = kzalloc(sizeof(struct ipvr_fence), GFP_KERNEL);
	if (!fence) {
		fence = ERR_PTR(-ENOMEM);
		goto out;
	}

	kref_init(&fence->kref);
	fence->dev_priv = dev_priv;

	spin_lock_irqsave(&dev_priv->fence_drv.fence_lock, irq_flags);
	/* cmds in one batch use different fence value */
	old_seq = dev_priv->fence_drv.sync_seq;
	dev_priv->fence_drv.sync_seq = dev_priv->last_seq++;
	dev_priv->fence_drv.sync_seq <<= 4;
	fence->seq = dev_priv->fence_drv.sync_seq;

	spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock, irq_flags);

	kref_get(&fence->kref);
	IPVR_DEBUG_GENERAL("fence is created and its seq is %u (0x%04x).\n",
		fence->seq, fence->seq);
out:
	return fence;
}

/**
 * ipvr_fence_destroy - destroy a fence
 *
 * @kref: fence kref
 *
 * Frees the fence object (all asics).
 */
static void ipvr_fence_destroy(struct kref *kref)
{
	struct ipvr_fence *fence;

	fence = container_of(kref, struct ipvr_fence, kref);
	kfree(fence);
}

/**
 * ipvr_fence_process - process a fence
 *
 * @dev_priv: drm_ipvr_private pointer
 * @seq: indicate the fence seq has been signaled
 * @err: indicate if err happened, for future use
 *
 * Checks the current fence value and wakes the fence queue
 * if the sequence number has increased (all asics).
 */
void ipvr_fence_process(struct drm_ipvr_private *dev_priv, u16 seq, u8 err)
{
	int signaled_seq_int;
	u16 signaled_seq;
	u16 last_emitted;

	signaled_seq_int = atomic_read(&dev_priv->fence_drv.signaled_seq);
	signaled_seq = (u16)signaled_seq_int;
	last_emitted = dev_priv->fence_drv.sync_seq;

	if (ipvr_seq_after(seq, last_emitted)) {
		IPVR_DEBUG_WARN("seq error, seq is %u, signaled_seq is %u, "
				"last_emitted is %u.\n",
				seq, signaled_seq, last_emitted);
		return;
	}
	if (ipvr_seq_after(seq, signaled_seq)) {
		atomic_xchg(&dev_priv->fence_drv.signaled_seq, seq);
		dev_priv->fence_drv.last_activity = jiffies;
		IPVR_DEBUG_GENERAL("last emitted seq %u is updated.\n", seq);
		wake_up_all(&dev_priv->fence_queue);
	}
}

/**
 * ipvr_fence_signaled - check if a fence sequeuce number has signaled
 *
 * @dev_priv: ipvr device pointer
 * @seq: sequence number
 *
 * Check if the last singled fence sequnce number is >= the requested
 * sequence number (all asics).
 * Returns true if the fence has signaled (current fence value
 * is >= requested value) or false if it has not (current fence
 * value is < the requested value.
 */
static bool ipvr_fence_signaled(struct drm_ipvr_private *dev_priv, u16 seq)
{
	u16 curr_seq, signaled_seq;
	unsigned long irq_flags;
	spin_lock_irqsave(&dev_priv->fence_drv.fence_lock, irq_flags);
	curr_seq = dev_priv->ved_private->ved_cur_seq;
	signaled_seq = atomic_read(&dev_priv->fence_drv.signaled_seq);

	if (ipvr_seq_after(seq, signaled_seq)) {
		/* poll new last sequence at least once */
		ipvr_fence_process(dev_priv, curr_seq, IPVR_CMD_SUCCESS);
		signaled_seq = atomic_read(&dev_priv->fence_drv.signaled_seq);
		if (ipvr_seq_after(seq, signaled_seq)) {
			spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock,
						irq_flags);
			return false;
		}
	}
	spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock, irq_flags);
	return true;
}

/**
 * ipvr_fence_lockup - ipvr lockup is detected
 *
 * @dev_priv: ipvr device pointer
 * @fence: lockup detected when wait the specific fence
 *
 * During the calling of ipvr_fence_wait, if wait to timeout,
 * indicate lockup happened, need flush cmd queue and reset ved
 * If ipvr_fence_wait_empty_locked encounter lockup, fence is NULL
 */
static void
ipvr_fence_lockup(struct drm_ipvr_private *dev_priv, struct ipvr_fence *fence)
{
	unsigned long irq_flags;
	struct ved_private *ved_priv = dev_priv->ved_private;

	IPVR_DEBUG_WARN("timeout detected, flush queued cmd, maybe lockup.\n");
	IPVR_DEBUG_WARN("MSVDX_COMMS_FW_STATUS reg is 0x%x.\n",
			IPVR_REG_READ32(MSVDX_COMMS_FW_STATUS));

	if (fence) {
		spin_lock_irqsave(&dev_priv->fence_drv.fence_lock, irq_flags);
		ipvr_fence_process(dev_priv, fence->seq, IPVR_CMD_LOCKUP);
		spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock, irq_flags);
	}

	/* should behave according to ctx type in the future */
	ved_flush_cmd_queue(dev_priv->ved_private);
	ipvr_runtime_pm_put_all(dev_priv, false);

	ved_priv->ved_needs_reset = 1;
}

/**
 * ipvr_fence_wait_seq - wait for a specific sequence number
 *
 * @dev_priv: ipvr device pointer
 * @target_seq: sequence number we want to wait for
 * @intr: use interruptable sleep
 *
 * Wait for the requested sequence number to be written.
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the sequence number.
 * Returns 0 if the sequence number has passed, error for all other cases.
 * -EDEADLK is returned when a VPU lockup has been detected.
 */
static int ipvr_fence_wait_seq(struct drm_ipvr_private *dev_priv,
					u16 target_seq, bool intr)
{
	struct ipvr_fence_driver	*fence_drv = &dev_priv->fence_drv;
	unsigned long timeout, last_activity;
	u16 signaled_seq;
	int ret;
	unsigned long irq_flags;
	bool signaled;
	spin_lock_irqsave(&dev_priv->fence_drv.fence_lock, irq_flags);

	while (ipvr_seq_after(target_seq,
			(u16)atomic_read(&fence_drv->signaled_seq))) {
		/* seems the fence_drv->last_activity is useless? */
		timeout = IPVR_FENCE_JIFFIES_TIMEOUT;
		signaled_seq = atomic_read(&fence_drv->signaled_seq);
		/* save last activity valuee, used to check for VPU lockups */
		last_activity = fence_drv->last_activity;

		spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock, irq_flags);
		if (intr) {
			ret = wait_event_interruptible_timeout(
				dev_priv->fence_queue,
				(signaled = ipvr_fence_signaled(dev_priv, target_seq)),
				timeout);
		} else {
			ret = wait_event_timeout(
				dev_priv->fence_queue,
				(signaled = ipvr_fence_signaled(dev_priv, target_seq)),
				timeout);
		}
		spin_lock_irqsave(&dev_priv->fence_drv.fence_lock, irq_flags);

		if (unlikely(!signaled)) {
			/* we were interrupted for some reason and fence
			 * isn't signaled yet, resume waiting until timeout  */
			if (unlikely(ret < 0)) {
				/* should return -ERESTARTSYS,
				 * interrupted by a signal */
				continue;
			}

			/* check if sequence value has changed since
			 * last_activity */
			if (signaled_seq !=
				atomic_read(&fence_drv->signaled_seq)) {
				continue;
			}

			if (last_activity != fence_drv->last_activity) {
				continue;
			}

			/* lockup happen, it is better have some reg to check */
			IPVR_DEBUG_WARN("VPU lockup (waiting for 0x%0x last "
					"signaled fence id 0x%x).\n",
					target_seq, signaled_seq);

			/* change last activity so nobody else
			 * think there is a lockup */
			fence_drv->last_activity = jiffies;
			spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock,
					irq_flags);
			return -EDEADLK;

		}
	}
	spin_unlock_irqrestore(&dev_priv->fence_drv.fence_lock, irq_flags);
	return 0;
}

/**
 * ipvr_fence_wait - wait for a fence to signal
 *
 * @fence: ipvr fence object
 * @intr: use interruptable sleep
 * @no_wait: not signaled, if need add into wait queue
 *
 * Wait for the requested fence to signal (all asics).
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the fence.
 * Returns 0 if the fence has passed, error for all other cases.
 */
int ipvr_fence_wait(struct ipvr_fence *fence, bool intr, bool no_wait)
{
	int ret;
	struct drm_ipvr_private *dev_priv;

	if (fence == NULL || fence->seq == IPVR_FENCE_SIGNALED_SEQ) {
		IPVR_DEBUG_GENERAL("fence is NULL or has been singaled.\n");
		return 0;
	}
	dev_priv = fence->dev_priv;

	IPVR_DEBUG_GENERAL("wait fence seq %u, last signaled seq is %d, "
			"last emitted seq is %u.\n", fence->seq,
			atomic_read(&dev_priv->fence_drv.signaled_seq),
			dev_priv->fence_drv.sync_seq);
	if (!no_wait)
		trace_ipvr_fence_wait_begin(fence,
			atomic_read(&dev_priv->fence_drv.signaled_seq),
			dev_priv->fence_drv.sync_seq);

	if (ipvr_fence_signaled(dev_priv, fence->seq)) {
		IPVR_DEBUG_GENERAL("fence has been signaled.\n");
		/*
		 * compare with ttm_bo_wait, don't need create a tmp_obj
		 * it is better we also set bo->fence = NULL
		 */
		if (!no_wait)
			trace_ipvr_fence_wait_end(fence,
				atomic_read(&dev_priv->fence_drv.signaled_seq),
				dev_priv->fence_drv.sync_seq);
		fence->seq = IPVR_FENCE_SIGNALED_SEQ;
		ipvr_fence_unref(&fence);
		return 0;
	}

	if (no_wait)
		return -EBUSY;

	ret = ipvr_fence_wait_seq(dev_priv, fence->seq, intr);
	if (ret) {
		if (ret == -EDEADLK) {
			trace_ipvr_fence_wait_lockup(fence,
					atomic_read(&dev_priv->fence_drv.signaled_seq),
					dev_priv->fence_drv.sync_seq);
			ipvr_fence_lockup(dev_priv, fence);
		}
		return ret;
	}
	trace_ipvr_fence_wait_end(fence,
			atomic_read(&dev_priv->fence_drv.signaled_seq),
			dev_priv->fence_drv.sync_seq);
	fence->seq = IPVR_FENCE_SIGNALED_SEQ;

	return 0;
}

/**
 * ipvr_fence_driver_init - init the fence driver
 *
 * @dev_priv: ipvr device pointer
 *
 * Init the fence driver, will not fail
 */
void ipvr_fence_driver_init(struct drm_ipvr_private *dev_priv)
{
	spin_lock_init(&dev_priv->fence_drv.fence_lock);
	init_waitqueue_head(&dev_priv->fence_queue);
	dev_priv->fence_drv.sync_seq = 0;
	atomic_set(&dev_priv->fence_drv.signaled_seq, 0);
	dev_priv->fence_drv.last_activity = jiffies;
	dev_priv->fence_drv.initialized = false;
}

/**
 * ipvr_fence_wait_empty_locked - wait for all fences to signal
 *
 * @dev_priv: ipvr device pointer
 *
 * Wait for all fences to be signalled.
 */
void ipvr_fence_wait_empty_locked(struct drm_ipvr_private *dev_priv)
{
	u16 seq;

	seq = dev_priv->fence_drv.sync_seq;

	while(1) {
		int ret;
		ret = ipvr_fence_wait_seq(dev_priv, seq, false);
		if (ret == 0) {
			return;
		} else if (ret == -EDEADLK) {
			ipvr_fence_lockup(dev_priv, NULL);
			IPVR_DEBUG_WARN("Lockup found waiting for seq %d.\n",
					seq);
			return;
		} else {
			continue;
		}
	}
}

/**
 * ipvr_fence_driver_fini - tear down the fence driver
 * for all possible rings.
 *
 * @dev_priv: ipvr device pointer
 *
 * Tear down the fence driver for all possible rings (all asics).
 */
void ipvr_fence_driver_fini(struct drm_ipvr_private *dev_priv)
{
	if (!dev_priv->fence_drv.initialized)
		return;
	ipvr_fence_wait_empty_locked(dev_priv);
	wake_up_all(&dev_priv->fence_queue);
	dev_priv->fence_drv.initialized = false;
}

/**
 * ipvr_fence_ref - take a ref on a fence
 *
 * @fence: fence object
 *
 * Take a reference on a fence (all asics).
 * Returns the fence.
 */
struct ipvr_fence *ipvr_fence_ref(struct ipvr_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

/**
 * ipvr_fence_unref - remove a ref on a fence
 *
 * @fence: ipvr fence object
 *
 * Remove a reference on a fence, if ref == 0, destory the fence.
 */
void ipvr_fence_unref(struct ipvr_fence **fence)
{
	struct ipvr_fence *tmp = *fence;

	*fence = NULL;
	if (tmp) {
		kref_put(&tmp->kref, &ipvr_fence_destroy);
	}
}

/**
 * ipvr_fence_buffer_objects - bind fence to buffer list
 *
 * @list: validation buffer list
 * @fence: ipvr fence object
 *
 * bind a fence to all obj in the validation list
 */
void
ipvr_fence_buffer_objects(struct list_head *list, struct ipvr_fence *fence)
{
	struct ipvr_validate_buffer *entry;
	struct drm_ipvr_gem_object *obj;

	if (list_empty(list))
		return;

	list_for_each_entry(entry, list, head) {
		obj = entry->ipvr_gem_bo;
		/**
		 * do not update fence if val_args specifies so
		 */
		if (entry->val_req.flags & IPVR_EXEC_OBJECT_NEED_FENCE) {
			entry->old_fence = obj->fence;
			obj->fence = ipvr_fence_ref(fence);
			if (entry->old_fence)
				ipvr_fence_unref(&entry->old_fence);
		}
		else {
			IPVR_DEBUG_GENERAL("obj 0x%lx marked as non-fence\n",
				ipvr_gem_object_mmu_offset(obj));
		}
		ipvr_bo_unreserve(obj);
	}
}
