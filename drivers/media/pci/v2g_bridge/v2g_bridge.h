
/* -*- v2g_bridge-h -*-
*-----------------------------------------------------------------------------
* Filename: v2g_bridge.h - Video in to Intel graphics output bridge driver
* $Revision: 1.1.10.2 $
*-----------------------------------------------------------------------------
* Created by Wind River Systems, Inc. for
* Copyright (c) 2002-2011, Intel Corporation.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
*-----------------------------------------------------------------------------
* Description:
*
* Header file for the Intel Video input-to-Graphics output Bridge driver,
* containing definitions shared between the kernel driver and the User
* Space code.
*-----------------------------------------------------------------------------
*/

/*
 *  Created by Wind River Systems, Inc. for
 *  Copyright (c) 2011 Intel, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 */

#ifndef __V2GBRIDGE_H
#define __V2GBRIDGE_H

#include <linux/types.h>

#define V2G_MAX_FRAME_BUFS	0x5	/* Maximum number of DMA buffers */

/* Describes a single IOH input DMA buffer mapped to graphics memory */
typedef struct _v2g_buffer_info {
	unsigned int index;     /* index number of this video buffer */
	unsigned long size;      /* size (in bytes) of this video buffer */
	unsigned long paddr;     /* physical address of this video buffer */
	unsigned long vaddr;     /* virtual address of this video buffer */
} v2g_buffer_info_t;

typedef void * igd_display_h;

/* Parameters associated with a video-to-graphics bridge */
typedef struct _v2g_bridge {
	//igd_dd_context_t        context;      /* Same as EMGDHmiVideoContext */
	igd_display_h           handle;       /* Primary or Secondary handle */
	int                     num_buffers;  /* # of valid IOH DMA buffers */
	v2g_buffer_info_t       fb[V2G_MAX_FRAME_BUFS];
} v2g_bridge_t;

/* ioctl parameters used when displaying a new frame */
typedef struct _v2g_new_frame {
	int buf_num;            /* index of DMA input buffer to use */
	unsigned int address;   /* phys addr of this DMA input buffer */
} v2g_new_frame_t;

#define V2G_IOC_MAGIC		'v'
#define V2G_ENABLE_BRIDGE	_IOWR(V2G_IOC_MAGIC, 0, v2g_bridge_t *)
#define V2G_DISABLE_BRIDGE	_IOWR(V2G_IOC_MAGIC, 1, int *)
#define V2G_DISPLAY_FRAME	_IOWR(V2G_IOC_MAGIC, 2, v2g_new_frame_t *)
#define V2G_TERMINATE_CAMERVIDEO	_IO(V2G_IOC_MAGIC, 3)
#endif /* __V2GBRIDGE_H */
