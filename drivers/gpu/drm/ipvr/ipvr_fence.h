/**************************************************************************
 * ipvr_fence.h: IPVR header file for fence handling
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

#ifndef _IPVR_FENCE_H_
#define _IPVR_FENCE_H_

#include "ipvr_drv.h"

/* seq_after(a,b) returns true if the seq a is after seq b.*/
#define ipvr_seq_after(a,b) \
    (typecheck(u16, a) && \
     typecheck(u16, b) && \
     ((s16)(a - b) > 0))

enum ipvr_cmd_status {
   IPVR_CMD_SUCCESS,
   IPVR_CMD_FAILED,
   IPVR_CMD_LOCKUP,
   IPVR_CMD_SKIP
};

#define IPVR_FENCE_JIFFIES_TIMEOUT		(HZ / 2)
/* fence seq are set to this number when signaled */
#define IPVR_FENCE_SIGNALED_SEQ		0LL

struct ipvr_fence {
	struct drm_ipvr_private *dev_priv;
	struct kref kref;
	/* protected by dev_priv->fence_drv.fence_lock */
	u16 seq;
	char name[32];
};

int ipvr_fence_wait(struct ipvr_fence *fence, bool intr, bool no_wait);

void ipvr_fence_process(struct drm_ipvr_private *dev_priv, u16 seq, u8 err);

void ipvr_fence_driver_init(struct drm_ipvr_private *dev_priv);

void ipvr_fence_driver_fini(struct drm_ipvr_private *dev_priv);

struct ipvr_fence* __must_check ipvr_fence_create(struct drm_ipvr_private *dev_priv);

void ipvr_fence_buffer_objects(struct list_head *list, struct ipvr_fence *fence);

void ipvr_fence_unref(struct ipvr_fence **fence);

void ipvr_fence_wait_empty_locked(struct drm_ipvr_private *dev_priv);

#endif
