/**************************************************************************
 * ved_fw.h: VED firmware support header file
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


#ifndef _VED_FW_H_
#define _VED_FW_H_

#include "ipvr_drv.h"

#define FIRMWAREID		0x014d42ab

/*  Non-Optimal Invalidation is not default */
#define MSVDX_DEVICE_NODE_FLAGS_MMU_NONOPT_INV	2

#define FW_VA_RENDER_HOST_INT		0x00004000
#define MSVDX_DEVICE_NODE_FLAGS_MMU_HW_INVALIDATION	0x00000020
#define FW_DEVA_ERROR_DETECTED 0x08000000

/* There is no work currently underway on the hardware */
#define MSVDX_FW_STATUS_HW_IDLE	0x00000001
#define MSVDX_DEVICE_NODE_FLAG_BRN23154_BLOCK_ON_FE	0x00000200
#define MSVDX_DEVICE_NODE_FLAGS_DEFAULT_D0				\
	(MSVDX_DEVICE_NODE_FLAGS_MMU_NONOPT_INV |			\
		MSVDX_DEVICE_NODE_FLAGS_MMU_HW_INVALIDATION |		\
		MSVDX_DEVICE_NODE_FLAG_BRN23154_BLOCK_ON_FE)

#define MSVDX_DEVICE_NODE_FLAGS_DEFAULT_D1				\
	(MSVDX_DEVICE_NODE_FLAGS_MMU_HW_INVALIDATION |			\
		MSVDX_DEVICE_NODE_FLAG_BRN23154_BLOCK_ON_FE)

#define MTX_CODE_BASE		(0x80900000)
#define MTX_DATA_BASE		(0x82880000)
#define PC_START_ADDRESS	(0x80900000)

#define MTX_CORE_CODE_MEM	(0x10)
#define MTX_CORE_DATA_MEM	(0x18)

#define RENDEC_A_SIZE	(4 * 1024 * 1024)
#define RENDEC_B_SIZE	(1024 * 1024)

#define TERMINATION_SIZE	48

#define MSVDX_RESET_NEEDS_REUPLOAD_FW		(0x2)
#define MSVDX_RESET_NEEDS_INIT_FW		(0x1)

/* init/deinit all ved_private related */
int __must_check ved_core_init(struct drm_ipvr_private *dev_priv);
int ved_core_deinit(struct drm_ipvr_private *dev_priv);

/* used for resetting VED after power saving */
int ved_setup_fw(struct ved_private *ved_priv);
int ved_core_reset(struct ved_private *ved_priv);
int ved_wait_for_register(struct ved_private *ved_priv,
			u32 offset, u32 value, u32 enable,
			u32 poll_cnt, u32 timeout);

#endif
