/**************************************************************************
 * ipvr_drm.h: IPVR header file exported to user space
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


/* this file only define structs and macro which need export to user space */
#ifndef _IPVR_DRM_H_
#define _IPVR_DRM_H_

#include <drm/drm.h>
struct drm_ipvr_context_create {
	/* passed ctx_info, including codec, profile info */
#define IPVR_CONTEXT_TYPE_VED   (0x1)
	__u32 ctx_type;
	/* returned back ctx_id */
	__u32 ctx_id;
	/*
	 * following tiling strides for VED are supported:
	 * stride 0: 512 for scheme 0, 1024 for scheme 1
	 * stride 1: 1024 for scheme 0, 2048 for scheme 1
	 * stride 2: 2048 for scheme 0, 4096 for scheme 1
	 * stride 3: 4096 for scheme 0
	 */
	__u32 tiling_stride;
	/*
	 * scheme 0: tile is 256x16, while minimal tile stride is 512
	 * scheme 1: tile is 512x8, while minimal tile stride is 1024
	 */
	__u32 tiling_scheme;
};

struct drm_ipvr_context_destroy {
	__u32 ctx_id;
	__u32 pad64;
};

/* ioctl used for querying info from driver */
enum drm_ipvr_misc_key {
	IPVR_DEVICE_INFO,
};
struct drm_ipvr_get_info {
	__u64 key;
	__u64 value;
};

struct drm_ipvr_gem_relocation_entry {
	/**
	 * Handle of the buffer being pointed to by this relocation entry.
	 *
	 * It's appealing to make this be an index into the mm_validate_entry
	 * list to refer to the buffer, but this allows the driver to create
	 * a relocation list for state buffers and not re-write it per
	 * exec using the buffer.
	 */
	__u32 target_handle;

	/**
	 * Value to be added to the offset of the target buffer to make up
	 * the relocation entry.
	 */
	__u32 delta;

	/** Offset in the buffer the relocation entry will be written into */
	__u64 offset;

	/**
	 * Offset value of the target buffer that the relocation entry was last
	 * written as.
	 *
	 * If the buffer has the same offset as last time, we can skip syncing
	 * and writing the relocation.  This value is written back out by
	 * the execbuffer ioctl when the relocation is written.
	 */
	__u64 presumed_offset;

	/**
	 * Target memory domains read by this operation.
	 */
	__u32 read_domains;

	/**
	 * Target memory domains written by this operation.
	 *
	 * Note that only one domain may be written by the whole
	 * execbuffer operation, so that where there are conflicts,
	 * the application will get -EINVAL back.
	 */
	__u32 write_domain;
};

struct drm_ipvr_gem_exec_object {
	/**
	 * User's handle for a buffer to be bound into the MMU for this
	 * operation.
	 */
	__u32 handle;

	/** Number of relocations to be performed on this buffer */
	__u32 relocation_count;
	/**
	 * Pointer to array of struct drm_i915_gem_relocation_entry containing
	 * the relocations to be performed in this buffer.
	 */
	__u64 relocs_ptr;

	/** Required alignment in graphics aperture */
	__u64 alignment;

	/**
	 * Returned value of the updated offset of the object, for future
	 * presumed_offset writes.
	 */
	__u64 offset;

#define IPVR_EXEC_OBJECT_NEED_FENCE (1 << 0)
#define IPVR_EXEC_OBJECT_SUBMIT     (1 << 1)
	__u64 flags;

	__u64 rsvd1;
	__u64 rsvd2;
};

struct drm_ipvr_gem_execbuffer {
	/**
	 * List of gem_exec_object2 structs
	 */
	__u64 buffers_ptr;
	__u32 buffer_count;

	/** Offset in the batchbuffer to start execution from. */
	__u32 exec_start_offset;
	/** Bytes used in batchbuffer from batch_start_offset */
	__u32 exec_len;

	/**
	 * ID of hardware context.
	 */
	__u32 ctx_id;

	__u64 flags;
	__u64 rsvd1;
	__u64 rsvd2;
};

enum ipvr_cache_level
{
	IPVR_CACHE_NOACCESS,
	IPVR_CACHE_UNCACHED,
	IPVR_CACHE_WRITEBACK,
	IPVR_CACHE_WRITECOMBINE,
	IPVR_CACHE_MAX,
};

struct drm_ipvr_gem_create {
	/*
	 * Requested size for the object.
	 * The (page-aligned) allocated size for the object will be returned.
	 */
	__u64 size;
	__u64 rounded_size;
	__u64 mmu_offset;
	/*
	 * Returned handle for the object.
	 * Object handles are nonzero.
	 */
	__u32 handle;
	__u32 tiling;

	__u32 cache_level;
	__u32 pad64;
	/*
	 * Handle used for user to mmap BO
	 */
	__u64 map_offset;
};

struct drm_ipvr_gem_busy {
	/* Handle of the buffer to check for busy */
	__u32 handle;

	/*
	 * Return busy status (1 if busy, 0 if idle).
	 * The high word is used to indicate on which rings the object
	 * currently resides:
	 *  16:31 - busy (r or r/w) rings (16 render, 17 bsd, 18 blt, etc)
	 */
	__u32 busy;
};

struct drm_ipvr_gem_mmap_offset {
	/** Handle for the object being mapped. */
	__u32 handle;
	__u32 pad64;
	/**
	 * Fake offset to use for subsequent mmap call
	 *
	 * This is a fixed-size type for 32/64 compatibility.
	 */
	__u64 offset;
};

struct drm_ipvr_gem_wait {
	/* Handle of BO we shall wait on */
	__u32 handle;
	__u32 flags;
	/** Number of nanoseconds to wait, Returns time remaining. */
	__s64 timeout_ns;
};

/*
 * IPVR GEM specific ioctls
 */
#define DRM_IPVR_CONTEXT_CREATE     0x00
#define DRM_IPVR_CONTEXT_DESTROY    0x01
#define DRM_IPVR_GET_INFO           0x02
#define DRM_IPVR_GEM_EXECBUFFER     0x03
#define DRM_IPVR_GEM_BUSY           0x04
#define DRM_IPVR_GEM_CREATE         0x05
#define DRM_IPVR_GEM_WAIT           0x06
#define DRM_IPVR_GEM_MMAP_OFFSET    0x07

#define DRM_IOCTL_IPVR_CONTEXT_CREATE	\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_CONTEXT_CREATE, struct drm_ipvr_context_create)
#define DRM_IOCTL_IPVR_CONTEXT_DESTROY	\
	DRM_IOW(DRM_COMMAND_BASE + DRM_IPVR_CONTEXT_DESTROY, struct drm_ipvr_context_destroy)
#define DRM_IOCTL_IPVR_GET_INFO		\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_GET_INFO, struct drm_ipvr_get_info)
#define DRM_IOCTL_IPVR_GEM_EXECBUFFER	\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_GEM_EXECBUFFER, struct drm_ipvr_gem_execbuffer)
#define DRM_IOCTL_IPVR_GEM_BUSY		\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_GEM_BUSY, struct drm_ipvr_gem_busy)
#define DRM_IOCTL_IPVR_GEM_CREATE	\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_GEM_CREATE, struct drm_ipvr_gem_create)
#define DRM_IOCTL_IPVR_GEM_WAIT		\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_GEM_WAIT, struct drm_ipvr_gem_wait)
#define DRM_IOCTL_IPVR_GEM_MMAP_OFFSET	\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IPVR_GEM_MMAP_OFFSET, struct drm_ipvr_gem_mmap_offset)

#endif
