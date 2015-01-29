/**************************************************************************
 * ved_pm.h: VED power management header file
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

#ifndef _VED_PM_H_
#define _VED_PM_H_

#include "ipvr_drv.h"

bool is_ved_on(struct drm_ipvr_private *dev_priv);

bool __must_check ved_power_on(struct drm_ipvr_private *dev_priv);

bool ved_power_off(struct drm_ipvr_private *dev_priv);

#endif
