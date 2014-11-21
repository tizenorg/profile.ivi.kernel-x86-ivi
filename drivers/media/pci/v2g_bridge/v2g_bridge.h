/*
 * Copyright (C) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __V2GBRIDGE_H
#define __V2GBRIDGE_H

#include <linux/types.h>

#define V2G_MAX_FRAME_BUFS	0x5	/* Maximum number of DMA buffers */

/*
 * In fact, v4l2 max devices number is VIDEO_NUM_DEVICES = 256.
 * But we only enumerate VFL_TYPE_GRABBER device in
 * v4l2_enumerate_camera, not sure about the exact number.
 * On Bayley Bay, due to the usage of TW6869 video decoder,
 * eight devices from /dev/video0 ~ /dev/video7. So we set
 * CAMERA_MAX_COUNT as 8
 */
#define CAMERA_MAX_COUNT (8)

#define CAMERA_DEV_ID (0)

/* In unit of second, camera show time in macroseconds */
#define CAMERA_INTERVAL (10000)

#define DRM_DEV_ID (226 << 20 | 0)

#define MAX_BUF_NUM (7)

#define FRAME_WAIT_INTERVAL (33)
#define CONTROL_INTERVAL (500)

#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

/* v2g buffer */
struct buffer {
	unsigned int bo_handle;
	unsigned int fb_handle;
	int dbuf_fd;
};

enum v2g_state {
	STATE_UDEF = -1,
	STATE_INIT,
	STATE_PREPARE,
	STATE_RUNNING,
	STATE_EXIT,
	STATE_PAUSE,
};

struct v2g_v4l2 {
	struct inode v4l2_inode;
	struct dentry v4l2_dentry;
	struct file v4l2_file;
	struct v4l2_format fmt;
};

struct v2g_drm {
	/* drm */
	struct inode inode;
	struct dentry dentry;
	struct file file;
	struct drm_device *drm_dev;

	/* crtc */
	u32 crtc_id;
	u16 crtc_active_width;
	u16 crtc_active_height;
	struct drm_mode_crtc crtc;

	/* plane */
	u32 plane_id;

	/* framebuffer */
	u32 fb_id;
};

struct v2g_context {
	struct v2g_drm drm;
	struct v2g_v4l2 v4l2;

	struct buffer buffer[MAX_BUF_NUM];

	/* task info */
	enum v2g_state state;

	struct task_struct *kt_streamd;
	bool streamd_exit;

	struct task_struct *kt_controld;
	bool geard_exit;

	/* protect state which is set by streamd and controld */
	struct mutex stream_lock;

	struct timer_list timer;

	wait_queue_head_t wait_queue;
};

#define V2G_IOC_MAGIC		'v'
#define V2G_ENABLE_BRIDGE	_IOWR(V2G_IOC_MAGIC, 0, v2g_bridge_t *)
#define V2G_DISABLE_BRIDGE	_IOWR(V2G_IOC_MAGIC, 1, int *)
#define V2G_DISPLAY_FRAME	_IOWR(V2G_IOC_MAGIC, 2, v2g_new_frame_t *)
#define V2G_TERMINATE_CAMERVIDEO	_IO(V2G_IOC_MAGIC, 3)

long uvc_v4l2_ioctl_kernel(struct file *file,
			   unsigned int cmd,
			   unsigned long arg);
#ifdef ATOMISP
int atomisp_v4l2_do_ioctl(struct file *file,
			  unsigned int cmd,
			  unsigned long arg);
#endif

int v4l2_enumerate_camera(unsigned int *camera_minor_lst,
			  int *camera_id_lst,
			  int *cnt);
int v4l2_open_kernel(struct inode *inode, struct file *filp);
int v4l2_release_kernel(struct inode *inode, struct file *filp);

int drm_open_kernel(struct inode *inode,
		    struct file *filp,
		    struct drm_device **pdev);
int drm_release_kernel(struct inode *inode, struct file *filp);
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data,
		     struct drm_file *file_priv);
int drm_mode_getresources_kernel(struct drm_device *dev,
				 void *data,
				 struct drm_file *file_priv);
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data,
		     struct drm_file *file_priv);
int drm_mode_getplane_res(struct drm_device *dev,
			  void *data,
			  struct drm_file *file_priv);
int drm_mode_getplane(struct drm_device *dev,
		      void *data,
		      struct drm_file *file_priv);
int drm_mode_setplane(struct drm_device *dev,
		      void *data,
		      struct drm_file *file_priv);
int drm_mode_setcrtc(struct drm_device *dev,
		     void *data,
		     struct drm_file *file_priv);
int drm_mode_addfb2(struct drm_device *dev,
		    void *data,
		    struct drm_file *file_priv);
int drm_mode_rmfb(struct drm_device *dev,
		  void *data,
		  struct drm_file *file_priv);
int drm_mode_create_dumb_ioctl(struct drm_device *dev,
			       void *data,
			       struct drm_file *file_priv);
int drm_prime_handle_to_fd_ioctl(struct drm_device *dev,
				 void *data,
				 struct drm_file *file_priv);
int intel_get_pipe_from_crtc_id(struct drm_device *dev,
				void *data,
				struct drm_file *file);

#endif /* __V2GBRIDGE_H */
