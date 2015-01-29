/**************************************************************************
 * ved_cmd.h: VED header file to support command buffer handling
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

#ifndef _VED_CMD_H_
#define _VED_CMD_H_

#include "ipvr_drv.h"
#include "ipvr_drm.h"
#include "ipvr_gem.h"
#include "ipvr_fence.h"
#include "ipvr_exec.h"
#include "ved_reg.h"
#include "ved_pm.h"

struct ved_cmd_queue {
	struct list_head head;
	void *cmd;
	u32 cmd_size;
	u16 cmd_seq;
	u32 fence_flag;
	u8 tiling_scheme;
	u8 tiling_stride;
	struct ipvr_context *ipvr_ctx;
};

int ved_irq_handler(struct ved_private *ved_priv);

int ved_mtx_send(struct ved_private *ved_priv, const void *msg);

int ved_check_idle(struct ved_private *ved_priv);

void ved_check_reset_fw(struct ved_private *ved_priv);

void ved_flush_cmd_queue(struct ved_private *ved_priv);

int ved_cmdbuf_video(struct ved_private *ved_priv,
			struct drm_ipvr_gem_object *cmd_buffer,
			u32 cmdbuf_size, struct ipvr_context *ipvr_ctx);

int ved_submit_video_cmdbuf(struct ved_private *ved_priv,
			struct drm_ipvr_gem_object *cmd_buffer, u32 cmd_size,
			struct ipvr_context *ipvr_ctx, u32 fence_flag);

int ved_cmd_dequeue_send(struct ved_private *ved_priv);

#endif
