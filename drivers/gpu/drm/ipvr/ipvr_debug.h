/**************************************************************************
 * ipvr_debug.h: IPVR debugfs support header file
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


#ifndef _IPVR_DEBUG_H_
#define _IPVR_DEBUG_H_

#include "ipvr_bo.h"
#include "drmP.h"

/* Operations supported */
#define IPVR_MAX_BUFFER_STR_LEN		200

#define IPVR_READ_TOKEN			"READ"
#define IPVR_WRITE_TOKEN		"WRITE"

/* DebugFS Variable declaration */
struct ipvr_debugfs_reg_vars {
	char reg_vars[IPVR_MAX_BUFFER_STR_LEN];
	u32 reg_input;
};

union ipvr_debugfs_vars {
	struct ipvr_debugfs_reg_vars reg;
};

int ipvr_debugfs_init(struct drm_minor *minor);
void ipvr_debugfs_cleanup(struct drm_minor *minor);

void ipvr_stat_add_object(struct drm_ipvr_private *dev_priv,
			struct drm_ipvr_gem_object *obj);

void ipvr_stat_remove_object(struct drm_ipvr_private *dev_priv,
			struct drm_ipvr_gem_object *obj);

void ipvr_stat_add_imported(struct drm_ipvr_private *dev_priv,
			struct drm_ipvr_gem_object *obj);

void ipvr_stat_remove_imported(struct drm_ipvr_private *dev_priv,
			struct drm_ipvr_gem_object *obj);

void ipvr_stat_add_exported(struct drm_ipvr_private *dev_priv,
			struct drm_ipvr_gem_object *obj);

void ipvr_stat_remove_exported(struct drm_ipvr_private *dev_priv,
			struct drm_ipvr_gem_object *obj);

void ipvr_stat_add_mmu_bind(struct drm_ipvr_private *dev_priv,
			size_t size);

void ipvr_stat_remove_mmu_bind(struct drm_ipvr_private *dev_priv,
			size_t size);

#endif
