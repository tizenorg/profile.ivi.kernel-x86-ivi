/**************************************************************************
 * ipvr_gem.h: IPVR header file for GEM ioctls
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

#ifndef _IPVR_GEM_H_
#define _IPVR_GEM_H_

#include "ipvr_drv.h"

int ipvr_context_create_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int ipvr_context_destroy_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int ipvr_get_info_ioctl(struct drm_device *dev,
			void *data,	struct drm_file *file_priv);
int ipvr_gem_execbuffer_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int ipvr_gem_busy_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int ipvr_gem_create_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int ipvr_gem_wait_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int ipvr_gem_mmap_offset_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);

#endif
