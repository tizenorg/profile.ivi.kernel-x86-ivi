/**************************************************************************
 * ipvr_exec.h: IPVR header file for command buffer execution
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

#ifndef _IPVR_EXEC_H_
#define _IPVR_EXEC_H_

#include "ipvr_drv.h"
#include "ipvr_drm.h"
#include "ipvr_gem.h"
#include "ipvr_fence.h"

struct drm_ipvr_private;

#define IPVR_NUM_VALIDATE_BUFFERS 2048
#define IPVR_MAX_RELOC_PAGES 1024

struct ipvr_validate_buffer {
	struct drm_ipvr_gem_exec_object val_req;
	struct list_head head;
	struct drm_ipvr_gem_object *ipvr_gem_bo;
	struct ipvr_fence *old_fence;
};

int ipvr_bo_reserve(struct drm_ipvr_gem_object *obj,
			bool interruptible, bool no_wait);

void ipvr_bo_unreserve(struct drm_ipvr_gem_object *obj);

struct ipvr_context*
ipvr_find_ctx_with_fence(struct drm_ipvr_private *dev_priv, u16 fence);

void ipvr_set_tile(struct drm_ipvr_private *dev_priv,
			u8 tiling_scheme, u8 tiling_stride);

#endif
