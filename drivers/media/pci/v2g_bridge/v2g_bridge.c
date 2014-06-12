/*-----------------------------------------------------------------------------
* Filename: v2g_bridge.c - Video input to Intel graphics output bridge driver
*-----------------------------------------------------------------------------
* Copyright (c) 2002-2013, Intel Corporation.
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
*
*-----------------------------------------------------------------------------
* Description:
*
* This driver acts as a bridge between the V4L2 TW6869 PCIe video input driver  and
* the Intel EMGD Graphics driver.  The purpose of the bridge is to re-map the video input
* buffers into frame buffer memory space by dma-buf sharing, to diplay on the Sprite C plane
*-----------------------------------------------------------------------------
*/

#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-core.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/dma-buf.h>
#include <drm/drmP.h>
#include <linux/wait.h>
#include <uapi/drm/i915_drm.h>

#include "v2g_bridge.h"

#define INTEL_V2G_VER "0.1.0.0"

#undef ATOMISP

#ifdef ATOMISP
#include <linux/atomisp.h>
#endif

#define V2G_DEBUG(...) \
  do {													\
	if (v2g_debug == 1) {								\
	    printk("[V2G_DEBUG] %s ", __FUNCTION__);		\
	    printk(__VA_ARGS__);							\
	    printk("\n");									\
	}													\
  } while(0); 

#define V2G_ERROR(...) 									\
  do { 													\
    printk(KERN_ERR "%s ERROR: ", __FUNCTION__); 		\
    printk(__VA_ARGS__); 								\
    printk("\n"); 										\
  } while(0);

#define V2G_TRACE_ENTER() V2G_DEBUG("ENTER")
#define V2G_TRACE_EXIT() V2G_DEBUG("EXIT") 

/* 
* In fact, v4l2 max devices number is VIDEO_NUM_DEVICES = 256.
* But we only enumerate VFL_TYPE_GRABBER device in 
* v4l2_enumerate_camera, not sure about the exact number
* 
* On Bayley Bay, due to the usage of TW6869 video decoder, 
* eight devices from /dev/video0 ~ /dev/video7. So we set 
* CAMERA_MAX_COUNT as 8
*/
#define CAMERA_MAX_COUNT (8)

#define CAMERA_DEV_ID (0)

/* In unit of second, camera show time in macroseconds */
#define CAMERA_INTERVAL (10000)

/* TODO: figure out how this value comes */
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

typedef enum _v2g_state {
	STATE_UDEF = -1,
	STATE_INIT,
	STATE_PREPARE,
	STATE_RUNNING,
	STATE_EXIT,
	STATE_PAUSE,
}v2g_state;

typedef struct _v2g_v4l2 {

	struct inode v4l2_inode;
	struct dentry v4l2_dentry;
	struct file v4l2_file;
	struct v4l2_format fmt;	
	
} v2g_v4l2;

typedef struct _v2g_drm {
	/* drm */
	struct inode inode;
	struct dentry dentry;
	struct file file;
	struct drm_device *drm_dev;

	/* crtc */
	u32 crtc_id;
	u16 crtc_active_width; // current crtc width & height
	u16 crtc_active_height;
	struct drm_mode_crtc crtc;

	/* plane */
	u32 plane_id;

	/* framebuffer */
	u32 fb_id;

} v2g_drm;

typedef struct _v2g_context {
	v2g_drm    drm;
	v2g_v4l2 v4l2;

	struct buffer buffer[MAX_BUF_NUM];

	/* task info */
	v2g_state state;

	struct task_struct *kt_streamd;
	bool streamd_exit;

	struct task_struct *kt_controld;
	bool geard_exit;

	struct mutex stream_lock;

	struct timer_list timer;

	wait_queue_head_t wait_queue;

} v2g_context; 

static v2g_context *p_v2gctx;

extern long uvc_v4l2_ioctl_kernel(struct file *file, unsigned int cmd,
						unsigned long arg);
#ifdef ATOMISP
extern int atomisp_v4l2_do_ioctl(struct file *file, unsigned int cmd, 
						 unsigned long arg);
#endif

static void v2g_cleanup(v2g_context *ctx);

/* Boot up parameters. */
static int screen_x = 0;
static int screen_y = 0;
static int screen_width = 0;
static int screen_height = 0;

static int v2g_enable = 0;
static int v2g_debug = 0;
static unsigned long expire_seconds = 15;

MODULE_PARM_DESC(screen_x, "Video screen start point. (e.g. \"0\")");
MODULE_PARM_DESC(screen_y, "Video screen start point. (e.g. \"0\")");
MODULE_PARM_DESC(screen_width, "Video screen width. (e.g. \"1366\")");
MODULE_PARM_DESC(screen_height, "Video screen height. (e.g. \"768\")");

module_param(v2g_enable, int, 0600);
module_param(v2g_debug, int, 0600);
module_param(screen_x, int, 0600);
module_param(screen_y, int, 0600);
module_param(screen_width, int, 0600);
module_param(screen_height, int, 0600);

extern int v4l2_enumerate_camera(unsigned int *camera_minor_lst, int *camera_id_lst, int *cnt);
extern int v4l2_open_kernel(struct inode *inode, struct file *filp);
extern int v4l2_release_kernel(struct inode *inode, struct file *filp);

extern int drm_open_kernel(struct inode *inode, struct file *filp, struct drm_device** pdev);
extern int drm_release_kernel(struct inode *inode, struct file *filp);
extern int drm_mode_getcrtc(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_getresources_kernel(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_getcrtc(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_getplane_res(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_getplane(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_setplane(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_setcrtc(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_addfb2(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_rmfb(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_mode_create_dumb_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data, struct drm_file *file);

////////////////////////////////////////////////////////
static long v2g_ioctl(struct file *fp, unsigned int cmd, 
					unsigned long arg)
{
	v2g_context *ctx = p_v2gctx;	

	switch (cmd) {
		case V2G_DISABLE_BRIDGE:
			ctx->state = STATE_EXIT;		
		break;

		default: break;
	}

	return 0;
}

static int v2g_open(struct inode *inode, struct file *fp)
{
	return 0;
}

static int v2g_release(struct inode *inode, struct file *fp)
{
	return 0;
}

static const struct file_operations v2g_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= v2g_ioctl,
	.open           = v2g_open,
	.release        = v2g_release,
};

static struct miscdevice v2g_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,    /* dynamic allocation */
	.name = "v2gbridge",            /* /dev/v2gbridge */
	.fops = &v2g_fops,
};
////////////////////////////////////////////////////////

static int v2g_enumerate_camera(v2g_context *ctx, unsigned int dev_id)
{
	int count = CAMERA_MAX_COUNT;
	unsigned int dev_minors[CAMERA_MAX_COUNT];
	int ids[CAMERA_MAX_COUNT] = {0};
	int i = 0;
	int ret = -1;

	if (0 == v4l2_enumerate_camera(dev_minors, ids, &count)) {
		V2G_DEBUG("Detect %d V4L2 video devices.\n", count);
	} else {
		V2G_ERROR("Failed to find cameras.");
		return ret;
	}

	ret = 0;
	for (i = 0; i < count; i++) {
		if (dev_id == ids[i]) {
			ctx->v4l2.v4l2_inode.i_rdev = (dev_t)dev_minors[i];
			ctx->v4l2.v4l2_dentry.d_inode = &ctx->v4l2.v4l2_inode;
			ctx->v4l2.v4l2_file.f_path.dentry = &ctx->v4l2.v4l2_dentry;
			ctx->v4l2.v4l2_file.f_inode = &ctx->v4l2.v4l2_inode;
			ret = 1;
			break;
		}
	}

	return ret;
}

static void v2g_timer_callback(unsigned long data)
{
	V2G_DEBUG("Timer triggered.");
	//p_v2gctx->need_quit = 1;
	p_v2gctx->state = STATE_EXIT;
	wake_up_interruptible(&p_v2gctx->wait_queue);
}

static long v2g_v4l2_ioctl(struct file *fp, unsigned int cmd,
			unsigned long arg) 
{
#ifdef ATOMISP
	return atomisp_v4l2_do_ioctl(fp, cmd, arg);
#else
	return uvc_v4l2_ioctl_kernel(fp, cmd, arg);
#endif
}

/*
* Open v4l2 device with device id
* 
* @dev_id: when opening /dev/video0 in user mode, dev_id is 0
*/
static int v2g_v4l2_open(v2g_context *ctx, unsigned int dev_id)
{

	if (0 == v4l2_open_kernel(NULL, &ctx->v4l2.v4l2_file)) {
		V2G_DEBUG("Open Camera successfully");
	} else {	
		V2G_ERROR("Fail to open camera!");
		return (-ENODEV);
	}	

	return 0;
}

/*
* Initialize v4l2 device such as format settins
*/
static int v2g_v4l2_init(v2g_context *ctx)
{
	struct v4l2_format fmt;
#ifdef ATOMISP
    struct v4l2_streamparm param;
#endif
	int err;

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = v2g_v4l2_ioctl(&ctx->v4l2.v4l2_file, VIDIOC_G_FMT, 
						(unsigned long)&fmt);
	if (err) {
		V2G_ERROR("V4L2 failed to get format!\n");
		return err;
	}

	V2G_DEBUG("V4L2 format: width x height (%d x %d) " 
				"color format: 0x%x\n", 
					fmt.fmt.pix.width, 
					fmt.fmt.pix.height,
					fmt.fmt.pix.pixelformat);
#ifdef ATOMISP
	//If ATOMISP enabled, the fmt should be V4L2_PIX_FMT_RGB565
	if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
		V2G_ERROR("In Atomisp, the format must be RGB565.\n");
		err = -1;
		return err;
	}
#endif 
	err = v2g_v4l2_ioctl(&ctx->v4l2.v4l2_file, VIDIOC_S_FMT, 
						(unsigned long)&fmt);
	if (err) {
		V2G_ERROR("V4L2 failed to set format! \n");
		return err;
	}

	memcpy(&ctx->v4l2.fmt, &fmt, sizeof(fmt));

#ifdef ATOMISP
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.capturemode=CI_MODE_STILL_CAPTURE;
	v2g_v4l2_ioctl(&ctx->v4l2.v4l2_file, VIDIOC_S_PARM, &param);

#endif

	return 0;
}


static int v2g_drm_open(v2g_context *ctx, unsigned int dev_id)
{
	int err = 0;
	struct inode *inode = &ctx->drm.inode;
	struct dentry *dentry = &ctx->drm.dentry;
	struct file *file = &ctx->drm.file;

	V2G_DEBUG("dev_id = %d\n", dev_id);
	
	inode->i_rdev = (dev_t)dev_id;
	
	/* To tell drm_open and drm_release, 
	   this call is from kernel space */
	inode->i_data.host = NULL;
	dentry->d_inode = inode;
	file->f_path.dentry = dentry;
	file->f_inode = inode;

	/* No exclusive opens, without setting it to O_RDWR */
	file->f_flags = O_RDWR;
	
	err = drm_open_kernel(inode, file, &ctx->drm.drm_dev);
	if (err) {
		V2G_ERROR("Fail to open DRM!\n");
		return (-ENODEV);
	} else {	
		V2G_DEBUG("Open DRM successfully! \n");
	}	

	return 0;
}

static int v2g_drm_check_fb_ids(v2g_context *ctx, struct drm_file *drmfile)
{
	struct drm_mode_card_res res;
	int ret;

	memset(&res, 0, sizeof res);
	ret = drm_mode_getresources_kernel(ctx->drm.drm_dev, (void*)&res, drmfile);
	if (res.count_fbs > 0) {
		res.fb_id_ptr = VOID2U64(kmalloc(sizeof(uint32_t) * res.count_fbs, GFP_KERNEL));
		if (res.fb_id_ptr == 0) {
			V2G_ERROR("Memory is not enough!\n");
			return (-ENOMEM);
		}
		
		res.count_encoders = res.count_crtcs = res.count_connectors = 0;
		
	} else {
		V2G_DEBUG("No fb.\n");
		return -ENODEV;
	}

	ret = drm_mode_getresources_kernel(ctx->drm.drm_dev, (void*)&res, drmfile);
	if (ret) {
		V2G_ERROR("DRM fail to get resources.\n");
		goto free_fbs;
	}

free_fbs:
	if (res.fb_id_ptr != 0) {
		kfree((void*)res.fb_id_ptr);
	}

	return ret;
}

/*
 * Get first valid crtc
 */
static int v2g_drm_get_crtc(v2g_context *ctx, 
							struct drm_mode_crtc *crtc)
{
	struct drm_mode_card_res res;
	uint32_t *crtc_id_ptr;
	struct drm_file* drmfile = ctx->drm.file.private_data;
	int ret;

	memset(&res, 0, sizeof(res));
	ret = drm_mode_getresources_kernel(ctx->drm.drm_dev, (void*)&res, drmfile);
	if (ret < 0) {
		V2G_ERROR("DRM fail to get resources!\n");
		return ret;
	}

	if (res.count_crtcs > 0) {
		res.crtc_id_ptr = \
			VOID2U64(kmalloc(sizeof(uint32_t) * res.count_crtcs, \
						GFP_KERNEL));
		if (res.crtc_id_ptr == 0) {
			V2G_ERROR("Memory is not enough!\n");
			return (-ENOMEM);
		}

		/* 
		 *  Trick: make sure no need to allocate buffer for 
		 *  encoders, fbs and connectors 
		 */
		res.count_encoders = \
			res.count_fbs = \
			res.count_connectors = 0;
	} else {
		V2G_DEBUG("no crtcs! no need to initialize v2g module!\n");
		return (-ENODEV);
	}

	/* 
	 *  Need to call DRM_IOCTL_MODE_GETRESOURCES twice, 1st to get 
	 *  crtc number, 2nd to get crtc data.
	 */
	ret = drm_mode_getresources_kernel(ctx->drm.drm_dev, (void*)&res, drmfile);
	if (ret) {
		V2G_ERROR("DRM fail to get resources!\n");
		goto free_crtc;
	}

	crtc_id_ptr = (uint32_t *)(unsigned long)res.crtc_id_ptr;

	/* Choose 1st valid crtc */
	crtc->crtc_id = crtc_id_ptr[0];
	ret = drm_mode_getcrtc(ctx->drm.drm_dev, (void*)crtc, drmfile);

	if (ret) {
		V2G_ERROR("DRM fail to get crtc!\n");
		goto free_crtc;
	}

	V2G_DEBUG("=======crtc [%d] info: fb_id = %d width = %d, height = %d======\n", 
				crtc->crtc_id, 
				crtc->fb_id,
				crtc->mode.hdisplay, 
				crtc->mode.vdisplay);

free_crtc:
	if (res.crtc_id_ptr != 0)
		kfree((void *)(unsigned long)res.crtc_id_ptr);

	return ret;
}

/*
 * Get first valid plane of chosen crtc
 */
static int v2g_drm_get_plane(v2g_context *ctx, 
				unsigned int crtc_id,
				struct drm_mode_get_plane *plane)
{
	int ret, i;
	struct drm_mode_get_plane_res plane_res;
	struct drm_i915_get_pipe_from_crtc_id pipe;
	struct drm_file* drmfile = ctx->drm.file.private_data;
	uint32_t *plane_id_ptr;

	memset(&pipe, 0, sizeof pipe);
    pipe.pipe = 0;
    pipe.crtc_id = crtc_id;
	ret = intel_get_pipe_from_crtc_id(ctx->drm.drm_dev, (void*)&pipe, drmfile);
    if (ret) {
        V2G_ERROR("DRM fail to get pipe.\n");
        return ret;
    }
    V2G_DEBUG("pipe id = %d", pipe.pipe);
	
	memset(&plane_res, 0, sizeof plane_res);
	ret = drm_mode_getplane_res(ctx->drm.drm_dev, (void*)&plane_res, drmfile);

	if (ret) {
		V2G_ERROR("DRM fail to get plane resources!\n");
		return ret;
	}

	if (plane_res.count_planes > 0) {
		plane_res.plane_id_ptr = \
			VOID2U64(kmalloc(sizeof(uint32_t) * plane_res.count_planes, \
							GFP_KERNEL));
		if (plane_res.plane_id_ptr == 0) {
			V2G_ERROR("Memory is not enough!\n");
		}
		else 
			plane_id_ptr = (uint32_t *)(unsigned long)plane_res.plane_id_ptr;
	} else {
		V2G_DEBUG("no planes! no need to initialize v2g module!\n");
		return (-ENODEV);
	}

	V2G_DEBUG("count_planes=%d", plane_res.count_planes);

	/* 
	 * Also need to call DRM_IOCTL_MODE_GETPLANERESOURCES twice, 1st to get 
	 * planes count, 2nd to get 1 plane data.
	 */
	ret = drm_mode_getplane_res(ctx->drm.drm_dev, (void*)&plane_res, drmfile);
	if (ret) {
		V2G_ERROR("DRM fail to get plane resources!\n");
		goto free_planes;
	}

	plane->format_type_ptr = VOID2U64(kmalloc(sizeof(uint32_t) * 32, GFP_KERNEL));
	if (plane->format_type_ptr == 0) {
		V2G_ERROR("Failed to alloc format_type_ptr for plane, memory is not enough!\n");
		return -ENODEV;
	}
									 
	   
	for (i=0; i<plane_res.count_planes; i++) {
		plane->plane_id = plane_id_ptr[i];
		ret = drm_mode_getplane(ctx->drm.drm_dev, (void*)plane, drmfile);
		if (ret) {
			V2G_ERROR("DRM fail to get plane!\n");
			goto free_planes;
		} 
		/* Get 1st valid plane of current crtc */
		if (plane->possible_crtcs & (1 << pipe.pipe)) {
            V2G_DEBUG("Get plane id = %d\n", plane->plane_id);
            break;
        }
	}

	/* No proper plane for current crtc */
	if (i == plane_res.count_planes)
		ret = -ENODEV;
	
free_planes:
	if (plane_id_ptr) {
		kfree(plane_id_ptr);
	}

	return ret;
}

/*
 * Get 1st valid crtc and 1st valid plan of choosen crtc
 */
static int v2g_drm_set_mode(v2g_context *ctx)
{
	struct drm_mode_crtc crtc;
	struct drm_mode_get_plane plane;
	int err;

	/* DRM get crtc */
	err = v2g_drm_get_crtc(ctx, &crtc);
	if (err)
		return err;

	ctx->drm.crtc_id = crtc.crtc_id;
	ctx->drm.crtc_active_width = crtc.mode.hdisplay;
	ctx->drm.crtc_active_height = crtc.mode.vdisplay;

	ctx->drm.fb_id = crtc.fb_id;
	V2G_DEBUG("fb_id=%d", ctx->drm.fb_id);

	ctx->drm.crtc = crtc;
	/* DRM get plane */
	memset(&plane, 0, sizeof(plane));
	V2G_DEBUG("crtc_id=%x", ctx->drm.crtc_id);
	err = v2g_drm_get_plane(ctx, ctx->drm.crtc_id, &plane);
	if (err) {
		return err;
	}

	ctx->drm.plane_id = plane.plane_id;

	return 0;
}

/*
 * Import v4l2 exported buf to drm according to the capture format
 */
static int v2g_drm_import_buf(struct drm_file *drm_file, 
							  struct drm_device* dev,
				struct buffer *buf,
				struct v4l2_format *fmt)
{
	struct drm_prime_handle prime;
	int ret;
	
	unsigned int pitch = fmt->fmt.pix.bytesperline;
	unsigned int offsets[4] = { 0 };
	unsigned int pitches[4] = { pitch };
	unsigned int bo_handles[4] = { 0};

	struct drm_mode_fb_cmd2 f;
	
	/* DRM creates dumb */
	struct drm_mode_create_dumb drm_dumb;

	drm_dumb.width = fmt->fmt.pix.width;
	drm_dumb.height = fmt->fmt.pix.height;
	drm_dumb.bpp = 16;
	drm_dumb.size = fmt->fmt.pix.sizeimage;
	drm_dumb.pitch = fmt->fmt.pix.bytesperline;
	
	ret = drm_mode_create_dumb_ioctl(dev, (void*)&drm_dumb, drm_file);
	if (ret) {
		return ret;
	}

	memset(&prime, 0, sizeof prime);
	buf->bo_handle = drm_dumb.handle;
	prime.handle = drm_dumb.handle;

	ret = drm_prime_handle_to_fd_ioctl(dev, (void*)&prime, drm_file);
	if (ret) {
		V2G_ERROR("DRM fail to get fd to handle!\n");
		return ret;
	}
	
	buf->dbuf_fd = prime.fd;

	/* Add to frame buffer */
	f.width  = fmt->fmt.pix.width;
	f.height = fmt->fmt.pix.height;
#ifdef ATOMISP
	/* Atomisp only supports planar pixel format which i915 overlay does not support.
	   RGB565 is the only video format both supported by Atomisp and overlay. */
	f.pixel_format = DRM_FORMAT_RGB565;
#else
	f.pixel_format =  fmt->fmt.pix.pixelformat;
#endif
	f.flags = 0;
	bo_handles[0] = buf->bo_handle;
	
	memcpy(f.handles, bo_handles, 4 * sizeof(bo_handles[0]));
	memcpy(f.pitches, pitches, 4 * sizeof(pitches[0]));
	memcpy(f.offsets, offsets, 4 * sizeof(offsets[0]));
	
	ret = drm_mode_addfb2(dev, (void*)&f, drm_file);
	if (ret) {
		V2G_ERROR("DRM fail to get fd to handle!\n");
		return ret;
	}

	/* Get frame buffer id, used by setplane */
	buf->fb_handle = f.fb_id;

	return 0;
}

static int v2g_start_streaming(v2g_context *ctx) 
{
	struct v4l2_buffer buf;
	struct drm_file *drmfile;
	struct file *v4l2_file;
	struct v4l2_requestbuffers rqbufs;

	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int i = 0;
	int err = 0;


	drmfile = ctx->drm.file.private_data;
	v4l2_file = &ctx->v4l2.v4l2_file;

	/* Open v4l2 device accordint to device id */
	err = v2g_v4l2_open(ctx, CAMERA_DEV_ID);
	if (err)
		return err;

	err = v2g_v4l2_init(ctx);
	if (err) 
		return err;

	/* V4L2 request buffers */
	memset(&rqbufs, 0, sizeof(rqbufs));

	/* Currently only request 1 buffer in the video buffer queue*/
	rqbufs.count = MAX_BUF_NUM; 
	rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rqbufs.memory = V4L2_MEMORY_DMABUF;
	
	err = v2g_v4l2_ioctl(v4l2_file, VIDIOC_REQBUFS, 
							(unsigned long)&rqbufs);
	if (err) {
		V2G_ERROR("failed to request v4l2 video buffer!\n");
		goto clean_up;
	}

	memset(&buf, 0, sizeof buf);
	
	V2G_DEBUG("v2g_drm import buffer for camera.");

	for (i = 0; i < MAX_BUF_NUM; i++) {
		/* DRM import this buf */
		err = v2g_drm_import_buf(drmfile, ctx->drm.drm_dev, &ctx->buffer[i], &ctx->v4l2.fmt);
		if (err) {
			V2G_ERROR("failed to export v4l2 video buffer!\n");
			goto clean_up;
		}

		buf.index = i;
		buf.m.fd = ctx->buffer[i].dbuf_fd;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_DMABUF;

		V2G_DEBUG("%s called, %d, buf index = %d, fd = %d\n", 
					__FUNCTION__, __LINE__,
					buf.index, buf.m.fd);

		err = v2g_v4l2_ioctl(v4l2_file, VIDIOC_QBUF, (unsigned long)&buf);
		if (err) {
			V2G_ERROR("failed to queue v4l2 video buffer!\n");
			goto clean_up;
		}
	}

	/* Start streaming */ 	
	
	err = v2g_v4l2_ioctl(v4l2_file, VIDIOC_STREAMON, 
							(unsigned long)&type);
	if (err) {
		V2G_ERROR("failed to start streaming!\n");
		goto clean_up;
	}

	ctx->state = STATE_RUNNING;
	V2G_DEBUG("Start streaming.");

	return 0;

clean_up:
	v2g_cleanup(ctx);
	return err;
}

static int v2g_streamd(void *ptr)
{
	v2g_context *ctx = (v2g_context *)ptr;
	struct v4l2_buffer buf;
	int err = 0;
	struct drm_file *drmfile;
	struct file *v4l2_file;
	struct drm_mode_set_plane s;

	drmfile = ctx->drm.file.private_data;
	v4l2_file = &ctx->v4l2.v4l2_file;

	/* DRM: set plane */
	memset(&s, 0, sizeof s);

	s.plane_id = ctx->drm.plane_id;
	s.crtc_id = ctx->drm.crtc_id;
	s.flags = 0;
	s.crtc_x = (ctx->drm.crtc_active_width - ctx->v4l2.fmt.fmt.pix.width) / 2;
	s.crtc_y = (ctx->drm.crtc_active_height - ctx->v4l2.fmt.fmt.pix.height) / 2;
	s.crtc_w = ctx->v4l2.fmt.fmt.pix.width;
	s.crtc_h = ctx->v4l2.fmt.fmt.pix.height;
	s.src_x = 0;
	s.src_y = 0;
	s.src_w = ctx->v4l2.fmt.fmt.pix.width << 16;
	s.src_h = ctx->v4l2.fmt.fmt.pix.height << 16;

	/*  Dequeue buffer */
	memset(&buf, 0, sizeof buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_DMABUF;

	while (1) {
		if (kthread_should_stop()) 
			break;

		mutex_lock(&ctx->stream_lock);
		if (ctx->state == STATE_RUNNING) {
			
			err = v2g_v4l2_ioctl(v4l2_file, VIDIOC_DQBUF, (unsigned long)&buf);
			if (err == -EAGAIN) {
				/* buffer is not ready, try again */
				V2G_DEBUG("%s: buffer is not ready, try again...\n", __FILE__);
				msleep(FRAME_WAIT_INTERVAL);
			} else {
				s.fb_id = ctx->buffer[buf.index].fb_handle;
		
				err = drm_mode_setplane(ctx->drm.drm_dev, (void*)&s, drmfile);
				if (err) {
					V2G_ERROR("failed to set plane!\n");
					mutex_unlock(&ctx->stream_lock);
					break;
				} 
	
				err = v2g_v4l2_ioctl(v4l2_file, VIDIOC_QBUF, (unsigned long)&buf);
				if (err) {
					V2G_ERROR("failed to queue v4l2 video buffer, err =%d!\n", err);
					mutex_unlock(&ctx->stream_lock);
					break;
				}
			}
			mutex_unlock(&ctx->stream_lock);
		} else if (ctx->state == STATE_PAUSE) {
			mutex_unlock(&ctx->stream_lock);
			kthread_parkme();
		} else if (ctx->state == STATE_EXIT) {
			mutex_unlock(&ctx->stream_lock);
			break;
		} else {
			V2G_ERROR("Should not reach here.\n");
			break;
		}
	}
	return 0;
}

static void v2g_cleanup(v2g_context *ctx)
{
	struct file *v4l2_file;
		
	v4l2_file = &ctx->v4l2.v4l2_file;

	if (v4l2_file) {
		v4l2_release_kernel(NULL, v4l2_file);
	}

	if (ctx->drm.file.private_data != NULL)
		drm_release_kernel(&ctx->drm.inode, &ctx->drm.file);

	// Clean up ctx.
	kfree(ctx);
	
}

// When pause streaming, kernel threads still keep v4l2 file and drm file open.
static int v2g_pause_streaming(v2g_context* ctx)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int err = 0;

	struct drm_mode_set_plane s;
	struct drm_file *drmfile;
	struct file* v4l2_file;

	V2G_DEBUG("v2g pause streaming.\n");
	drmfile = ctx->drm.file.private_data;
	v4l2_file = &ctx->v4l2.v4l2_file;

	// Pause streaming
	err = v2g_v4l2_ioctl(v4l2_file, VIDIOC_STREAMOFF, (unsigned long)&type);
	if (err) {
        V2G_ERROR("failed to stop streaming.");
        return -1;
    }

    memset(&s, 0, sizeof s);
    s.plane_id = ctx->drm.plane_id;
    s.crtc_id = ctx->drm.crtc_id;
    s.fb_id = 0;
    err = drm_mode_setplane(ctx->drm.drm_dev, (void*)&s, drmfile);
    if (err) {
        V2G_ERROR("failed to set plane.");
    }

	return 0;
}

static int v2g_exit_streaming(v2g_context* ctx)
{
	struct drm_file *drmfile;
	struct file *v4l2_file;
	int i = 0;
	
	drmfile = ctx->drm.file.private_data;
	v4l2_file = &ctx->v4l2.v4l2_file;

	// Pause streaming
	v2g_pause_streaming(ctx);

	// Release frame buffers.
    for (i = 0; i < MAX_BUF_NUM; i++) {
        if (ctx->buffer[i].fb_handle != 0) {
            drm_mode_rmfb(ctx->drm.drm_dev, (void*)&(ctx->buffer[i].fb_handle), drmfile);
        }
    }

	// Release all of the resources.
	v2g_cleanup(ctx);

	return 0;
}

static int v2g_control(void* ptr)
{
	v2g_context* ctx = (v2g_context *)ptr;
	int err = 0;

	while (1) {
		if (kthread_should_stop())
			break;
		
		if (ctx->state == STATE_INIT) {
			mutex_lock(&ctx->stream_lock);

			// Open DRM.
			err = v2g_drm_open (ctx, DRM_DEV_ID);
			if (err) {
				V2G_ERROR ("Failed to open drm.");
				mutex_unlock(&ctx->stream_lock);
				return err;
			}

			err = v2g_drm_set_mode(ctx);
			if (err) {
				drm_release_kernel(&ctx->drm.inode, ctx->drm.file.private_data);
				mutex_unlock(&ctx->stream_lock);
				return err;
			}

			setup_timer(&ctx->timer, v2g_timer_callback, 0);
			mod_timer(&ctx->timer, jiffies + msecs_to_jiffies(1000 * expire_seconds));

			err = v2g_enumerate_camera(ctx, CAMERA_DEV_ID);
			if (err <= 0) {
				mutex_unlock(&ctx->stream_lock);
				V2G_ERROR("Failed to find any camera.");
				return err;
			}

			ctx->state = STATE_PREPARE;
			mutex_unlock(&ctx->stream_lock);
			continue;

		} else if (ctx->state == STATE_PREPARE) {
			v2g_start_streaming(ctx);
			ctx->state = STATE_RUNNING;
			ctx->kt_streamd = kthread_run (v2g_streamd, ctx, "v2g_streamd");
			if (IS_ERR(ctx->kt_streamd)) {
				V2G_ERROR("Failed to create thread v2g_streamd.");
				v2g_exit_streaming(ctx);

				return -1;
			}
			wait_event_interruptible(ctx->wait_queue, ctx->state != STATE_RUNNING);

		} else if (ctx->state == STATE_PAUSE) {
			v2g_pause_streaming(ctx);
			kthread_stop(ctx->kt_streamd);
			wait_event_interruptible(ctx->wait_queue, ctx->state != STATE_PAUSE);

		} else if (ctx->state == STATE_EXIT) {
			ctx->state = STATE_EXIT;
			v2g_exit_streaming(ctx);
			break;

		} else {
			V2G_DEBUG("Keep state %d\n", ctx->state);
		}
		msleep(CONTROL_INTERVAL);
	}

	if (ctx != NULL) {
		kfree(ctx);
		ctx = NULL;
	}

	return 0;
}

static int v2g_start(void)
{
	v2g_context *ctx;

	if (v2g_enable == 0) {
		return 0;
	}

	/* Allocate v2g context */
	ctx = kmalloc(sizeof(v2g_context), GFP_KERNEL);
	if (NULL == ctx) {
		V2G_ERROR("failed to allocte v2g context!\n");
		return -ENOMEM;
	}

	memset(ctx, 0, sizeof(v2g_context));
	p_v2gctx = ctx;

	mutex_init(&ctx->stream_lock);
	ctx->state = STATE_INIT;

	init_waitqueue_head(&ctx->wait_queue);
	// Enable control daemon
	ctx->kt_controld = kthread_run(v2g_control, ctx, "v2g_controller");
	if (IS_ERR(ctx->kt_controld)) {
        V2G_ERROR("Failed to create thread v2g_control.");
        goto stop_streamd;
    }

	return 0;

stop_streamd:
	if (!IS_ERR_OR_NULL(ctx->kt_streamd))
		kthread_stop(ctx->kt_streamd);

	return -1;
}

/*
 *	v2g_init	- initialize the v2gbridge device node
 *
 *	Register the driver as a misc device and return the result.  (Each
 *	instance of open() will allocate & initialize the relevant context
 *	information.)
 */
static int __init v2g_init(void)
{
	int err;

	if (v2g_enable == 0) {
		V2G_DEBUG("Early camera is disabled.\n");
		return 0;
	}

	err = misc_register(&v2g_miscdev);
	if (err) {
		V2G_ERROR("v2gbridge: misc driver registration FAILED !");
		return err;
	}

	if (!v2g_start()) {
		V2G_DEBUG("v2g_init ok!");
	}
	else {
		V2G_DEBUG("v2g_init failed!");
	}
	
	return 0;
}

/**
 *	v2g_exit	- unregister and unload the v2g bridge driver
 *
 *	Unregister the device and release any resources
 */
static void __exit v2g_exit(void)
{
	V2G_DEBUG("v2gbridge: Unload camera module");
	//del_timer(&p_v2gctx->timer);
	kfree(p_v2gctx);
	misc_deregister(&v2g_miscdev);
}


module_init(v2g_init);
module_exit(v2g_exit);


MODULE_AUTHOR("James<junx.tang@intel.com>");
MODULE_DESCRIPTION("Video Input to Intel Graphics Bridge Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(INTEL_V2G_VER);

