/*
 * Copyright (C) 2010 OKI SEMICONDUCTOR CO., LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/pch_dma.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#ifdef IOH_VIDEO_IN_DMA_CONTIG
#include <media/videobuf-dma-contig.h>
#else
#include <media/videobuf-vmalloc.h>
#endif
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include "ioh_video_in_main.h"

#define IOH_VIDEOIN_THREAD_NAME "ioh_videoin_thread"

#define IOH_VIN_MAJOR_VERSION 1
#define IOH_VIN_MINOR_VERSION 0
#define IOH_VIN_RELEASE 0
#define IOH_VIN_VERSION \
	KERNEL_VERSION(IOH_VIN_MAJOR_VERSION, IOH_VIN_MINOR_VERSION, \
							IOH_VIN_RELEASE)

#define OV7620_ADDR	(0x42 >> 1)
#define OV9653_ADDR	(0x60 >> 1)
#define ML86V76651_ADDR	(0x80 >> 1)
#define NCM13J_ADDR	(0xBA >> 1)

#define ALLOC_NUMBER			(3)
#define ALLOC_ORDER			(10)/* (9) */

/* Macros for bit positions. */
#define IOH_BIT_0	(0)
#define IOH_BIT_1	(1)
#define IOH_BIT_2	(2)
#define IOH_BIT_3	(3)
#define IOH_BIT_4	(4)
#define IOH_BIT_5	(5)
#define IOH_BIT_6	(6)
#define IOH_BIT_7	(7)
#define IOH_BIT_8	(8)
#define IOH_BIT_9	(9)
#define IOH_BIT_10	(10)
#define IOH_BIT_11	(11)
#define IOH_BIT_12	(12)
#define IOH_BIT_13	(13)
#define IOH_BIT_14	(14)
#define IOH_BIT_15	(15)

/* Macros for register offset. */
#define IOH_VIDEO_IN_VICTRL1	(0x0000U)

#define IOH_VIDEO_IN_VICTRL2	(0x0004U)
  #define IN2S	(IOH_BIT_6)

#define IOH_VIDEO_IN_VOCTRL1	(0x0008U)
  #define OUT2S	(IOH_BIT_8)
  #define LDSEL	(IOH_BIT_6)
  #define ORGBSEL (IOH_BIT_1)

#define IOH_VIDEO_IN_VOCTRL2	(0x000CU)
  #define O422SEL (IOH_BIT_8)
  #define CBON	(IOH_BIT_7)
  #define BBON	(IOH_BIT_6)
  #define SBON	(IOH_BIT_4)
  #define RGBLEV	(IOH_BIT_3)

#define IOH_VIDEO_IN_BLNKTIM	(0x0018U)
  #define CNTCTL	(IOH_BIT_7)

#define IOH_VIDEO_IN_LUMLEV	(0x0020U)
  #define NOSIG	(IOH_BIT_7)

#define IOH_VIDEO_IN_GGAIN	(0x0024U)

#define IOH_VIDEO_IN_BGAIN	(0x0028U)

#define IOH_VIDEO_IN_RGAIN	(0x002CU)

#define IOH_VIDEO_IN_THMOD1	(0x00F8U)

#define IOH_VIDEO_IN_THMOD2	(0x00FCU)

#define IOH_VIDEO_IN_INTENB	(0x0100U)

#define IOH_VIDEO_IN_INTSTS	(0x0104U)
  #define INTBITS		(0x7)
  #define OFINTSTS		(0x4)
  #define HSINTSTS		(0x2)
  #define VSINTSTS		(0x1)

#define IOH_VIDEO_IN_VDATA	(0x1000U)

#define IOH_VIDEO_IN_RESET	(0x1ffcU)
  #define ASSERT_RESET		(0x0001U)
  #define DE_ASSERT_RESET	(0x0000U)

#define DESC_SIZE		(1028 * MAXIMUM_FRAME_BUFFERS)

#define VSYNC_SYNC	0
#define VSYNC_NOT_SYNC	1

#define DISABLE		0
#define ENABLE		1

#define LIKELY(x) likely(x)
#define UNLIKELY(x) unlikely(x)

struct reg_intenb {
	u8 drevsem;
	u8 dmarenb;
	u8 ofintenb;
	u8 hsintenb;
	u8 vsintenb;
};

struct ioh_video_in_settings {
	/**< The current input data format. */
	struct ioh_video_in_input_format current_input_format;

	/**< The current frame size. */
	struct ioh_video_in_frame_size current_frame_size;

	/**< The current output data format. */
	struct ioh_video_in_output_format current_output_format;

	/**< The current scan mode conversion method. */
	enum ioh_video_in_scan_mode_method current_scan_mode_method;

	/**< The current luminance settings.	*/
	struct ioh_video_in_luminance_settings current_luminance_settings;

	/**< The current RGB gain settings. */
	struct ioh_video_in_rgb_gain_settings current_rgb_gain_settings;

	/**< The current blanking time settings.	*/
	struct ioh_video_in_blank_tim_settings current_blank_tim_settings;

	/**< The current Blue background settings.	*/
	enum ioh_video_in_bb_mode current_bb_mode;

	/**< The current Color bar settings.	*/
	struct ioh_video_in_cb_settings current_cb_settings;

	/**< The current interrupt settings.	*/
	/* Used only during suspension and resume operation. */
	struct reg_intenb current_interrupt_settings;
};
struct BT656_device {
	/* The base (remapped) address of the video device. */
	void __iomem *base_address;
	/* The physical address of the video device. */
	u32 physical_address;

	/* The pci_dev structure reference of the device. */
	struct pci_dev *p_device;
	/* The IRQ line reserved for the device. */
	int irq;

	/* The DMA channel obtained for capturing data. */
	struct dma_async_tx_descriptor	*desc_dma;
	struct pch_dma_slave		param_dma;
	struct dma_chan			*chan_dma;
	struct scatterlist		*sg_dma;	/* sg_tx_p */
	int				nent;

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	/* Wait queue variable for the thread sleep operation. */
	wait_queue_head_t thread_sleep_queue;
	wait_queue_head_t vsync_wait_queue;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	/* Information of Video Frame Buffers. */
	struct ioh_video_in_frame_buffer_info frame_buffer_info;

	/* Video Frame Buffers. */
	struct ioh_video_in_frame_buffers bufs;

	/* Read Buffer. (The current frame buffer that can be read.) */
	struct ioh_video_in_frame_buffer *read_buffer;

	/* Wait queue variable for the read operation. */
	wait_queue_head_t read_wait_queue;
	/* Wait queue variable for the thread sleep operation. */
	wait_queue_head_t thread_sleep_queue;
	wait_queue_head_t vsync_wait_queue;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	/* Lock variables. */
#ifdef IOH_VIDEO_IN_DMA_CONTIG
	spinlock_t dev_lock;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	spinlock_t read_buffer_lock;
	spinlock_t dev_lock;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	/* Flag variables. */
	u8 suspend_flag;	/* for denoting the suspend action. */
	u8 open_flag;		/* for denoting the open action. */
	u8 vsync_waiting_flag;
#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	u8 read_wait_flag;	/* for denoting the read wait process. */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
	u8 thread_sleep_flag;	/* for denoting the thread sleep process. */
	u8 thread_exit_flag;	/* for denoting the thread exit process. */
	u8 overflow_flag;	/* for denoting the overflow interrupt. */
	s8 dma_flag;		/* for denoting the DMA process completion. */

	struct reg_intenb intenb;

	/* current video setting details of the device. */
	struct ioh_video_in_settings video_settings;

	/* Used in thread */

	/* The number of frame skip */
	u32 frame_skip_num;
	/* DMA buffer virtual address. */
	void *dma_buffer_virt[MAXIMUM_FRAME_BUFFERS];
	/* DMA buffer physical address. */
	dma_addr_t dma_buffer_phy[MAXIMUM_FRAME_BUFFERS];

	struct scatterlist	*sg_dma_list[MAXIMUM_FRAME_BUFFERS];

};

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#define ioh_video_in_lock_buffer(lock_variable) spin_lock(lock_variable)
#define ioh_video_in_unlock_buffer(lock_variable) spin_unlock(lock_variable)
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

struct ioh_vin_fmt {
	char	*name;
	u32	fourcc;          /* v4l2 format id */
	int	depth;
	enum v4l2_mbus_pixelcode	mbus_code;
};

#ifdef IOH_VIDEO_IN_DMA_CONTIG
enum ioh_video_in_vidq_status {
	IOH_VIDEO_IN_VIDQ_IDLE,
	IOH_VIDEO_IN_VIDQ_INITIALISING,
	IOH_VIDEO_IN_VIDQ_RUNNING,
};
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

/* buffer for one video frame */
struct ioh_vin_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer		vb;

	struct ioh_vin_fmt		*fmt;

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	u32 channel;
	u32				frame_index;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
};

struct ioh_vin_dev {
	struct v4l2_device		v4l2_dev;

	spinlock_t			slock;
	struct mutex			mutex;

	int				users;

	/* various device info */
	struct video_device		*vfd;

	struct list_head		vidq_active;
#ifdef IOH_VIDEO_IN_DMA_CONTIG
	struct ioh_vin_buffer		*active_frm;
	enum ioh_video_in_vidq_status	vidq_status;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
	/* Control 'registers' */
	struct BT656_device		*video_in_dev;

	struct v4l2_subdev		*sensor;

};

struct ioh_vin_fh {
	struct ioh_vin_dev         *dev;

	/* video capture */
	struct ioh_vin_fmt         *fmt;
	struct v4l2_pix_format     pix_format;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
};

#define IOH_VIN_DRV_NAME		"ioh_videoin"
#define MAX_DEVICE_SUPPORTED		(1)
#define PCI_VENDOR_ID_IOH		(0x10DB)
#define PCI_DEVICE_ID_IVI_VIDEOIN	(0x802A)
#define PCI_DEVICE_ID_MP_VIDEOIN	(0x8011)

#define SET_BIT(value, bit)		((value) |= ((u32)0x1 << (bit)))
#define RESET_BIT(value, bit)		((value) &= ~((u32)0x1 << (bit)))
#define RESET_BIT_RANGE(value, lbit, hbit)			\
((value) &= ~((((u32)2 << ((hbit) - (lbit) + 1)) - 1) << (lbit)))

/* -- */
#define ioh_err(device, fmt, arg...) \
	dev_err(&(device)->p_device->dev, fmt, ##arg);
#define ioh_warn(device, fmt, arg...) \
	dev_warn(&(device)->p_device->dev, fmt, ##arg);
#define ioh_info(device, fmt, arg...) \
	dev_info(&(device)->p_device->dev, fmt, ##arg);
#define ioh_dbg(device, fmt, arg...) \
	dev_dbg(&(device)->p_device->dev, fmt, ##arg);
/* -- */

#define SLEEP_DELAY (500U)

static unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX number, -1 is autodetect");

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#define N_FRAME_BUF 2
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#define MIN_N_FRAME_BUF 3
#define MAX_N_FRAME_BUF 5
static unsigned n_frame_buf = 3;
module_param(n_frame_buf, uint, 0644);
MODULE_PARM_DESC(n_frame_buf, "the number of farme buffer to allocate[3-5].");
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

static unsigned int vid_limit = 16;

/* prototype */
static void ioh_videoin_thread_tick(struct ioh_vin_dev *dev);
static int ioh_video_in_open(struct ioh_vin_dev *dev);
static int ioh_video_in_close(struct ioh_vin_dev *dev);

static long ioh_video_in_ioctl(struct file *p_file, void *priv,
		int command, void *param);

#define sensor_call(dev, o, f, args...) \
	v4l2_subdev_call(dev->sensor, o, f, ##args)

static struct ioh_vin_fmt formats[] = {
	{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.depth    = 16,
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
	},
};

/* ---- HAL ---- */

void write_intenb(struct BT656_device *device)
{
	void __iomem *addr;
	u32 data;

	addr = device->base_address + IOH_VIDEO_IN_INTENB;
	data = device->intenb.drevsem << 5
		| device->intenb.dmarenb << 4
		| device->intenb.ofintenb << 2
		| device->intenb.hsintenb << 1
		| device->intenb.vsintenb << 0;

	iowrite32(data, addr);

	ioh_dbg(device, "In write_intenb -> 0x%04x write", data);
}

void ioh_video_in_wait_vsync(struct BT656_device *device)
{
	device->vsync_waiting_flag = true;

	ioh_dbg(device, "In %s waiting", __func__);

	wait_event_interruptible(device->vsync_wait_queue,
			false == device->vsync_waiting_flag);

	ioh_dbg(device, "In %s wake_up", __func__);

	return;
}

struct ioh_video_in_input_settings {
	/* Input format. */
	enum ioh_video_in_input_data_format format;
	/* VICTRL1 register value. */
	u32 victrl1;
	/* VICTRL2 register value. */
	u32 victrl2;
	/* Frame size. */
	struct ioh_video_in_frame_size frame_size;
};

static const struct ioh_video_in_input_settings ioh_input_settings[] = {
/*	<Input Format>          <VICTRL1>  <VICTRL2>    <X>  <Y> <Pitch> */
	{NT_SQPX_ITU_R_BT_656_4_8BIT,
				0x00000001, 0x00000000, {640, 480, 640 * 2} },
	{NT_SQPX_ITU_R_BT_656_4_10BIT,
				0x00000001, 0x00000200, {640, 480, 640 * 4} },
	{NT_SQPX_YCBCR_422_8BIT,
				0x00000009, 0x00000000, {640, 480, 640 * 4} },
	{NT_SQPX_YCBCR_422_10BIT,
				0x00000009, 0x00000200, {640, 480, 640 * 4} },

	{NT_BT601_ITU_R_BT_656_4_8BIT,
				0x00000000, 0x00000000, {720, 480, 768 * 2} },
	{NT_BT601_ITU_R_BT_656_4_10BIT,
				0x00000000, 0x00000200, {720, 480, 768 * 4} },
	{NT_BT601_YCBCR_422_8BIT,
				0x00000008, 0x00000000, {720, 480, 768 * 4} },
	{NT_BT601_YCBCR_422_10BIT,
				0x00000008, 0x00000200, {720, 480, 768 * 4} },

	{NT_RAW_8BIT,           0x00000000, 0x00000100, {640, 480, 640 * 2} },
	{NT_RAW_10BIT,          0x00000000, 0x00000300, {640, 480, 640 * 2} },
	{NT_RAW_12BIT,          0x00000000, 0x00000500, {640, 480, 640 * 2} },
};

#define DEFAULT_IP_TRANS_SETTINGS	(LINE_INTERPOLATION)
#define DEFAULT_BB_SETTINGS		(BB_OUTPUT_OFF)

static const struct ioh_video_in_input_format default_input_data_format = {
	NT_SQPX_ITU_R_BT_656_4_8BIT,
	OFFSET_BINARY_FORMAT,
};

static const struct ioh_video_in_output_format default_output_data_format = {
	YCBCR_422_8BIT,
	OFFSET_BINARY_FORMAT,
	BT601_LUMINANCE_RANGE,
	RGB_FULL_SCALE_MODE,
};

static const struct ioh_video_in_luminance_settings
default_luminance_settings = {
	NOSIG_NORMAL_MODE,
	LUMLEV_78_PERCENT,
};

static const struct ioh_video_in_rgb_gain_settings
default_rgb_gain_settings = {
	(unsigned char)0x00,
	(unsigned char)0x00,
	(unsigned char)0x00,
};

static const struct ioh_video_in_blank_tim_settings
default_blank_tim_settings = {
	CNTCTL_STANDARD_SIGNAL,
	BLKADJ_0_PIXEL,
};

static const struct ioh_video_in_cb_settings default_cb_settings = {
	CB_OUTPUT_OFF,
	CB_OUTLEV_25_PERCENT,
};

static const struct ioh_video_in_input_format invalid_input_format = {
	INVALID_INPUT_DATA_FORMAT,
	INVALID_NUMERICAL_FORMAT,
};

static const struct ioh_video_in_frame_size invalid_frame_size = {
	0,
	0,
	0,
};

static const struct ioh_video_in_output_format invalid_output_format = {
	INVALID_OUTPUT_DATA_FORMAT,
	INVALID_NUMERICAL_FORMAT,
	INVALID_LUMINANCE_RANGE,
	INVALID_RGB_GAIN_LEVEL,
};

#define INVALID_IP_TRANS_MODE		(INVALID_SCAN_MODE_METHOD)

static const struct ioh_video_in_luminance_settings invalid_luminance_level = {
	INVALID_LUMINANCE_NOSIG,
	INVALID_LUMINANCE_LUMLEV,
};

static const struct ioh_video_in_blank_tim_settings invalid_blank_tim = {
	INVALID_BLANK_TIM_CNTCTL,
	INVALID_BLANK_TIM_BLKADJ,
};

#define INVALID_BB_MODE		(INVALID_BB_MODE)

static const struct ioh_video_in_cb_settings invalid_cb_settings = {
	INVALID_CB_MODE,
	INVALID_CB_OUTLEV,
};

#define MAXIMUM_INPUT_FORMAT	(sizeof(ioh_input_settings)/(sizeof(struct \
ioh_video_in_input_settings)))

static s32
ioh_video_in_set_input_format(struct BT656_device *device,
			  struct ioh_video_in_input_format input_format)
{
	u32 victrl2;
	u32 counter;
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Obtaining the base address. */
	base_address = device->base_address;

	/* Checking for the input data format. */
	for (counter = 0; counter < MAXIMUM_INPUT_FORMAT; counter++) {
		if (input_format.format == ioh_input_settings[counter].format)
			break;
	}

	/* Data format valid. */
	if (counter >= MAXIMUM_INPUT_FORMAT) {
		ioh_err(device, "In %s -> "
			"Invalid input data format", __func__);

		retval = IOH_VIDEOIN_FAIL;

		goto out;
	}

	/* Obtaining the value of the VICTRL2 register. */
	victrl2 = ioh_input_settings[counter].victrl2;

	/* Setting IN2S bit of VICTRL2 */
	switch (input_format.numerical_format) {
	case OFFSET_BINARY_FORMAT:
		RESET_BIT(victrl2, IN2S);
		break;

	case COMPLEMENTARY_FORMAT_OF_2:
		SET_BIT(victrl2, IN2S);
		break;

	case DONT_CARE_NUMERICAL_FORMAT:
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid numerical format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Setting the VICTRL1 and VICTRL2 regsiter. */
	spin_lock(&device->dev_lock);

	iowrite32((ioh_input_settings[counter].victrl1),
				(base_address + IOH_VIDEO_IN_VICTRL1));

	iowrite32((victrl2), (base_address + IOH_VIDEO_IN_VICTRL2));

	/* Confirming the write operation. */
	ioh_dbg(device, "In %s -> victrl1 = %08x victrl2 = %08x",
		__func__, ioh_input_settings[counter].victrl1, victrl2);

	if (((ioh_input_settings[counter].victrl1) ==
			 (ioread32(base_address + IOH_VIDEO_IN_VICTRL1)))
	&& (victrl2 == (ioread32(base_address + IOH_VIDEO_IN_VICTRL2)))) {

		ioh_dbg(device, "In %s -> "
			"Register write successful", __func__);

		device->video_settings.current_input_format = input_format;
		device->video_settings.current_frame_size =
					ioh_input_settings[counter].frame_size;
	} else {
		ioh_err(device, "In %s -> "
			"Register write unsuccessful", __func__);

		device->video_settings.current_input_format =
							invalid_input_format;

		device->video_settings.current_frame_size =
							invalid_frame_size;

		retval = IOH_VIDEOIN_FAIL;
	}
	spin_unlock(&device->dev_lock);

out:
	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_input_format
ioh_video_in_get_input_format(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_input_format;
}

static s32
ioh_video_in_set_output_format(struct BT656_device *device,
			struct ioh_video_in_output_format output_format)
{
	u32 voctrl1;
	u32 voctrl2;
	void __iomem *base_address;
	struct ioh_video_in_input_format input_format;
	s32 retval = IOH_VIDEOIN_FAIL;

	/* Obtaining the device base address for read and write operations. */
	base_address = device->base_address;

	/* Obtaining the currently set input format. */
	input_format = device->video_settings.current_input_format;

	spin_lock(&device->dev_lock);

	/* Reading the VOCTRL1 and VOCTRL2 register value for updation. */
	voctrl1 = ioread32(base_address + IOH_VIDEO_IN_VOCTRL1);
	voctrl2 = ioread32(base_address + IOH_VIDEO_IN_VOCTRL2);

	/* Setting the register values depending on the output data format. */
	switch (output_format.format) {
	case YCBCR_422_8BIT:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_8BIT)
		    || (input_format.format == NT_BT601_ITU_R_BT_656_4_8BIT)
		    || (input_format.format == NT_SQPX_YCBCR_422_8BIT)
		    || (input_format.format == NT_BT601_YCBCR_422_8BIT)
		    || (input_format.format == NT_RAW_8BIT)) {
			/* NT_RAW_8BIT condition is for input setting is raw &
			    output setting is 422-8bit. */

			/* Clearing bit1(ORGBSEL) */
			RESET_BIT(voctrl1, ORGBSEL);

			/* Setting bit8 (O422SEL) */
			SET_BIT(voctrl2, O422SEL);

			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case YCBCR_422_10BIT:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_10BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_10BIT)) {
			/* Clearing bit1(ORGBSEL) */
			RESET_BIT(voctrl1, ORGBSEL);

			/* Setting bit8 (O422SEL) */
			SET_BIT(voctrl2, O422SEL);

			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bit9 (OBITSEL) */
			SET_BIT(voctrl1, IOH_BIT_9);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case YCBCR_444_8BIT:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_8BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_8BIT)) {
			/* Clearing bit1(ORGBSEL) */
			RESET_BIT(voctrl1, ORGBSEL);

			/* Resetting bit8 (O422SEL) */
			RESET_BIT(voctrl2, O422SEL);

			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case YCBCR_444_10BIT:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_10BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_10BIT)) {
			/* Clearing bit1(ORGBSEL) */
			RESET_BIT(voctrl1, ORGBSEL);

			/* Resetting bit8 (O422SEL) */
			RESET_BIT(voctrl2, O422SEL);

			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bit9 (OBITSEL) */
			SET_BIT(voctrl1, IOH_BIT_9);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case RGB888:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_8BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_8BIT)
		   || (input_format.format == NT_SQPX_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_10BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_10BIT)) {

			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bit1(ORGBSEL) */
			SET_BIT(voctrl1, ORGBSEL);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case RGB666:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_8BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_8BIT)
		   || (input_format.format == NT_SQPX_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_10BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_10BIT)) {

			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bits9(OBITSEL) */
			SET_BIT(voctrl1, IOH_BIT_9);

			/* Setting bit1(ORGBSEL) */
			SET_BIT(voctrl1, ORGBSEL);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case RGB565:
		if ((input_format.format == NT_SQPX_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_8BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_8BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_8BIT)
		   || (input_format.format == NT_SQPX_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_BT601_ITU_R_BT_656_4_10BIT)
		   || (input_format.format == NT_SQPX_YCBCR_422_10BIT)
		   || (input_format.format == NT_BT601_YCBCR_422_10BIT)) {
			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bits10(OBITSEL) */
			SET_BIT(voctrl1, IOH_BIT_10);

			/* Setting bit1(ORGBSEL) */
			SET_BIT(voctrl1, ORGBSEL);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case RAW_8BIT:
		if (input_format.format == NT_RAW_8BIT) {
			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case RAW_10BIT:
		if (input_format.format == NT_RAW_10BIT) {
			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bits9(OBITSEL) */
			SET_BIT(voctrl1, IOH_BIT_9);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	case RAW_12BIT:
		if (input_format.format == NT_RAW_12BIT) {
			/* Clearing bits9-10(OBITSEL) */
			RESET_BIT_RANGE(voctrl1, IOH_BIT_9, IOH_BIT_10);

			/* Setting bits10(OBITSEL) */
			SET_BIT(voctrl1, IOH_BIT_10);

			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	default:
		break;
	}

	if (IOH_VIDEOIN_FAIL == retval) {
		ioh_err(device, "In %s -> "
			"Invalid Output Data Format", __func__);
		goto out;
	}

	/* Checking and setting for output numerical format. */
	switch (output_format.numerical_format) {
	case OFFSET_BINARY_FORMAT:
		/* Clearing bit8(O2S) */
		RESET_BIT(voctrl1, OUT2S);
		break;

	case COMPLEMENTARY_FORMAT_OF_2:
		/* Setting bit8 (O2S) */
		SET_BIT(voctrl1, OUT2S);
		break;

	case DONT_CARE_NUMERICAL_FORMAT:
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid output numerical format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Clearing bit4 SBON */
	RESET_BIT(voctrl2, SBON);

	/* Checking and setting for output luminance range. */
	switch (output_format.luminance_range) {
	case BT601_LUMINANCE_RANGE:
		voctrl2 |= BT601_LUMINANCE_RANGE;
		break;

	case EXTENDENDED_LUMINANCE_RANGE:
		voctrl2 |= EXTENDENDED_LUMINANCE_RANGE;
		break;

	case DONT_CARE_LUMINANNCE_RANGE:
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid output luminance range format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Clearing bit3 RGBLEV */
	RESET_BIT(voctrl2, RGBLEV);

	/* Checking and setting for RGB Gain level. */
	switch (output_format.rgb_gain_level) {
	case RGB_FULL_SCALE_MODE:
		voctrl2 |= RGB_FULL_SCALE_MODE;
		break;

	case RGB_BT601_MODE:
		voctrl2 |= RGB_BT601_MODE;
		break;

	case DONT_CARE_RGBLEV:
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid output rgb gain level format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Updating the VOCTRL1 and VOCTRL2 registers. */
	iowrite32(voctrl1, (base_address + IOH_VIDEO_IN_VOCTRL1));
	iowrite32(voctrl2, (base_address + IOH_VIDEO_IN_VOCTRL2));

	/* Confirming the register write. */
	ioh_dbg(device, "In %s -> voctrl1 = %08x voctrl2 = %08x",
		__func__, voctrl1, voctrl2);

	if ((voctrl1 == ioread32(base_address + IOH_VIDEO_IN_VOCTRL1))
	&& (voctrl2 == ioread32(base_address + IOH_VIDEO_IN_VOCTRL2))) {
		ioh_dbg(device, "In %s -> "
			"Register write successful", __func__);

		device->video_settings.current_output_format = output_format;
	} else {
		ioh_err(device, "In %s -> "
			"Register write unsuccessful", __func__);

		device->video_settings.current_output_format =
							invalid_output_format;

		retval = IOH_VIDEOIN_FAIL;
	}
out:
	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_output_format
ioh_video_in_get_output_format(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_output_format;
}

static s32
ioh_video_in_set_size(struct BT656_device *device,
			struct ioh_video_in_frame_size frame_size)
{
	if ((frame_size.pitch_size % 128) != 0) {

		ioh_err(device, "In %s -> "
			"Invalid pitch size", __func__);

		return IOH_VIDEOIN_FAIL;
	} else {
		device->video_settings.current_frame_size = frame_size;

		ioh_dbg(device, "Function %s ended", __func__);

		return IOH_VIDEOIN_SUCCESS;
	}
}

static struct ioh_video_in_frame_size
ioh_video_in_get_size(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_frame_size;
}

static s32
ioh_video_in_set_ip_trans(struct BT656_device *device,
			  enum ioh_video_in_scan_mode_method scan_mode_method)
{
	u32 voctrl1;
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Obtaining the device base address. */
	base_address = device->base_address;

	/* Obtaining the value of VOCTRL1 for updation. */
	spin_lock(&device->dev_lock);
	voctrl1 = ioread32(base_address + IOH_VIDEO_IN_VOCTRL1);

	/* Clearing bit6(LDSEL) */
	RESET_BIT(voctrl1, LDSEL);

	/* Checking the interpolation type and updating the value. */
	switch (scan_mode_method) {
	case LINE_INTERPOLATION:
		voctrl1 |= LINE_INTERPOLATION;
		break;

	case LINE_DOUBLER:
		/* Setting bit6(LDSEL) */
		voctrl1 |= LINE_DOUBLER;
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid scan mode method", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Updating the register values. */
	iowrite32(voctrl1, (base_address + IOH_VIDEO_IN_VOCTRL1));

	/* Confirming the register write. */
	if ((voctrl1 == ioread32(base_address + IOH_VIDEO_IN_VOCTRL1))) {
		ioh_dbg(device, "In %s -> "
			"Register write successful", __func__);

		device->video_settings.
				current_scan_mode_method = scan_mode_method;
	} else {
		ioh_err(device, "In %s -> "
			"Register write unsuccessful", __func__);

		device->video_settings.current_scan_mode_method =
						INVALID_IP_TRANS_MODE;

		retval = IOH_VIDEOIN_FAIL;
	}
out:
	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static enum ioh_video_in_scan_mode_method
ioh_video_in_get_ip_trans(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_scan_mode_method;
}

static s32
ioh_video_in_set_luminance_level(struct BT656_device *device,
				 struct ioh_video_in_luminance_settings
				 luminance_settings)
{
	u32 lumlev;
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Obtaining the base address of the device. */
	base_address = device->base_address;

	spin_lock(&device->dev_lock);

	/* Reading the luminance register value for updation. */
	lumlev = ioread32(base_address + IOH_VIDEO_IN_LUMLEV);

	/* Resetting bit7(NOSIG) */
	RESET_BIT(lumlev, NOSIG);

	/* Checking and setting the NOSIG value. */
	switch (luminance_settings.luminance_nosig) {
	case NOSIG_NORMAL_MODE:
		/* Resetting bit7(NOSIG) */
		lumlev |= NOSIG_NORMAL_MODE;
		break;

	case NOSIG_NOINMASTER_MODE:
		/* Setting bit7(NOSIG) */
		lumlev |= NOSIG_NOINMASTER_MODE;
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid luminance NOSIG format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Resetting bits0-3(LUMLEV) */
	RESET_BIT_RANGE(lumlev, IOH_BIT_0, IOH_BIT_3);

	/* Checking and setting the luminance level values. */
	switch (luminance_settings.luminance_lumlev) {
	case LUMLEV_78_PERCENT:
		lumlev |= LUMLEV_78_PERCENT;
		break;

	case LUMLEV_81_PERCENT:
		lumlev |= LUMLEV_81_PERCENT;
		break;

	case LUMLEV_84_PERCENT:
		lumlev |= LUMLEV_84_PERCENT;
		break;

	case LUMLEV_87_PERCENT:
		lumlev |= LUMLEV_87_PERCENT;
		break;

	case LUMLEV_90_PERCENT:
		lumlev |= LUMLEV_90_PERCENT;
		break;

	case LUMLEV_93_PERCENT:
		lumlev |= LUMLEV_93_PERCENT;
		break;

	case LUMLEV_96_PERCENT:
		lumlev |= LUMLEV_96_PERCENT;
		break;

	case LUMLEV_100_PERCENT:
		lumlev |= LUMLEV_100_PERCENT;
		break;

	case LUMLEV_103_PERCENT:
		lumlev |= LUMLEV_103_PERCENT;
		break;

	case LUMLEV_106_PERCENT:
		lumlev |= LUMLEV_106_PERCENT;
		break;

	case LUMLEV_109_PERCENT:
		lumlev |= LUMLEV_109_PERCENT;
		break;

	case LUMLEV_112_PERCENT:
		lumlev |= LUMLEV_112_PERCENT;
		break;

	case LUMLEV_115_PERCENT:
		lumlev |= LUMLEV_115_PERCENT;
		break;

	case LUMLEV_118_PERCENT:
		lumlev |= LUMLEV_118_PERCENT;
		break;

	case LUMLEV_121_PERCENT:
		lumlev |= LUMLEV_121_PERCENT;
		break;

	case LUMLEV_125_PERCENT:
		lumlev |= LUMLEV_125_PERCENT;
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid luminance level format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Updating the register values. */
	iowrite32(lumlev, (base_address + IOH_VIDEO_IN_LUMLEV));

	/* Confirming the register write. */
	if (lumlev == ioread32(base_address + IOH_VIDEO_IN_LUMLEV)) {
		ioh_dbg(device, "In %s -> "
			"Register write successful", __func__);

		device->video_settings.current_luminance_settings =
							luminance_settings;

	} else {
		ioh_err(device, "In %s -> "
			"Register write unsuccessful", __func__);

		device->video_settings.current_luminance_settings =
						invalid_luminance_level;

		retval = IOH_VIDEOIN_FAIL;
	}
out:
	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_luminance_settings
ioh_video_in_get_luminance_level(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_luminance_settings;
}

static s32
ioh_video_in_set_rgb_gain(struct BT656_device *device,
			  struct ioh_video_in_rgb_gain_settings
			  rgb_gain_settings)
{
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	spin_lock(&device->dev_lock);

	/* Obtaining the base address of the device. */
	base_address = device->base_address;

	/* Updating the register values. */
	iowrite32((u32) rgb_gain_settings.r_gain,
			   (base_address + IOH_VIDEO_IN_RGAIN));
	iowrite32((u32) rgb_gain_settings.g_gain,
			   (base_address + IOH_VIDEO_IN_GGAIN));
	iowrite32((u32) rgb_gain_settings.b_gain,
			   (base_address + IOH_VIDEO_IN_BGAIN));

	/* Confirming the write operation. */
	if (((u32) rgb_gain_settings.r_gain ==
		 ioread32(base_address + IOH_VIDEO_IN_RGAIN))
		&& ((u32) rgb_gain_settings.g_gain ==
		ioread32(base_address + IOH_VIDEO_IN_GGAIN))
		&& ((u32) rgb_gain_settings.b_gain ==
		ioread32(base_address + IOH_VIDEO_IN_BGAIN))) {
		ioh_dbg(device, "In %s -> "
			"Register write successful", __func__);

		device->video_settings.
			current_rgb_gain_settings = rgb_gain_settings;

	} else {
		ioh_err(device, "In %s -> "
			"Register write unsuccessful", __func__);

		rgb_gain_settings.r_gain =
		  (u8) ioread32(base_address + IOH_VIDEO_IN_RGAIN);
		rgb_gain_settings.g_gain =
		  (u8) ioread32(base_address + IOH_VIDEO_IN_GGAIN);
		rgb_gain_settings.b_gain =
		  (u8) ioread32(base_address + IOH_VIDEO_IN_BGAIN);

		retval = IOH_VIDEOIN_FAIL;
	}

	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_rgb_gain_settings
ioh_video_in_get_rgb_gain(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_rgb_gain_settings;
}

static s32 ioh_video_in_cap_start(struct BT656_device *device)
{
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Thread exist and is sleeping. */
	if ((false == device->thread_exit_flag) &&
	(true == device->thread_sleep_flag)) {

		device->thread_sleep_flag = false;
		wake_up(&device->thread_sleep_queue);

		ioh_dbg(device, "In %s -> "
			"Video capturing started", __func__);

	} else if (true == device->thread_exit_flag) {
		ioh_err(device, "In %s -> "
			"Thread exited", __func__);

		retval = IOH_VIDEOIN_FAIL;
	}

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static void ioh_video_in_cap_stop(struct BT656_device *device)
{
	/* If thread has not exited and thread is not sleeping. */
	if ((false == device->thread_exit_flag)
	&& (false == device->thread_sleep_flag)) {

		device->thread_sleep_flag = true;
		wake_up(&device->thread_sleep_queue);

		ioh_dbg(device, "In %s -> "
			"Video capturing stopped", __func__);
	}

	ioh_dbg(device, "Function %s ended", __func__);
}

static s32
ioh_video_in_set_blank_tim(struct BT656_device *device,
			   struct ioh_video_in_blank_tim_settings
			   blank_tim_settings)
{
	u32 blnkim;
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Obtaining the base address of the device. */
	base_address = device->base_address;

	spin_lock(&device->dev_lock);

	/* Obtaining the BLNKTIM register value for updation. */
	blnkim = ioread32(base_address + IOH_VIDEO_IN_BLNKTIM);

	/* Resetting bit7(CNTCTL) */
	RESET_BIT(blnkim, CNTCTL);

	/* Checking and setting the CNTCTL values. */
	switch (blank_tim_settings.blank_tim_cntctl) {
	case CNTCTL_STANDARD_SIGNAL:
		/* Resetting bit7(CNTCTL) */
		blnkim |= CNTCTL_STANDARD_SIGNAL;
		break;

	case CNTCTL_NON_STANDARD_SIGNAL:
		/* Setting bit7(CNTCTL) */
		blnkim |= CNTCTL_NON_STANDARD_SIGNAL;
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid CNTCTL format", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS == retval) {

		/* Resetting bit0-3(BLKADJ) */
		RESET_BIT_RANGE(blnkim, IOH_BIT_0, IOH_BIT_3);

		/* Checking and setting the BLKADJ values. */
		switch (blank_tim_settings.blank_tim_blkadj) {
		case BLKADJ_MINUS_8_PIXEL:
			blnkim |= BLKADJ_MINUS_8_PIXEL;
			break;

		case BLKADJ_MINUS_7_PIXEL:
			blnkim |= BLKADJ_MINUS_7_PIXEL;
			break;

		case BLKADJ_MINUS_6_PIXEL:
			blnkim |= BLKADJ_MINUS_6_PIXEL;
			break;

		case BLKADJ_MINUS_5_PIXEL:
			blnkim |= BLKADJ_MINUS_5_PIXEL;
			break;

		case BLKADJ_MINUS_4_PIXEL:
			blnkim |= BLKADJ_MINUS_4_PIXEL;
			break;

		case BLKADJ_MINUS_3_PIXEL:
			blnkim |= BLKADJ_MINUS_3_PIXEL;
			break;

		case BLKADJ_MINUS_2_PIXEL:
			blnkim |= BLKADJ_MINUS_2_PIXEL;
			break;

		case BLKADJ_MINUS_1_PIXEL:
			blnkim |= BLKADJ_MINUS_1_PIXEL;
			break;

		case BLKADJ_0_PIXEL:
			blnkim |= BLKADJ_0_PIXEL;
			break;

		case BLKADJ_PLUS_1_PIXEL:
			blnkim |= BLKADJ_PLUS_1_PIXEL;
			break;

		case BLKADJ_PLUS_2_PIXEL:
			blnkim |= BLKADJ_PLUS_2_PIXEL;
			break;

		case BLKADJ_PLUS_3_PIXEL:
			blnkim |= BLKADJ_PLUS_3_PIXEL;
			break;

		case BLKADJ_PLUS_4_PIXEL:
			blnkim |= BLKADJ_PLUS_4_PIXEL;
			break;

		case BLKADJ_PLUS_5_PIXEL:
			blnkim |= BLKADJ_PLUS_5_PIXEL;
			break;

		case BLKADJ_PLUS_6_PIXEL:
			blnkim |= BLKADJ_PLUS_6_PIXEL;
			break;

		case BLKADJ_PLUS_7_PIXEL:
			blnkim |= BLKADJ_PLUS_7_PIXEL;
			break;

		default:
			ioh_err(device, "In %s -> "
				"Invalid BLKADJ format", __func__);

			retval = IOH_VIDEOIN_FAIL;
			break;
		}
	}

	if (IOH_VIDEOIN_SUCCESS == retval) {

		/* Updating the register values. */
		iowrite32(blnkim, (base_address + IOH_VIDEO_IN_BLNKTIM));

		/* confirming the register updation. */
		if ((blnkim ==
		 ioread32(base_address + IOH_VIDEO_IN_BLNKTIM))) {
			ioh_dbg(device, "In %s -> "
				"Register write successful", __func__);

			device->video_settings.current_blank_tim_settings
				= blank_tim_settings;

		} else {
			ioh_err(device, "In %s -> "
				"Register write unsuccessful", __func__);

			device->video_settings.
				current_blank_tim_settings = invalid_blank_tim;

			retval = IOH_VIDEOIN_FAIL;
		}
	}

	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_blank_tim_settings
ioh_video_in_get_blank_tim(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_blank_tim_settings;
}

static s32
ioh_video_in_set_bb(struct BT656_device *device,
			enum ioh_video_in_bb_mode output_bb_mode)
{
	u32 voctrl2;
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Obtaining the base address of the device. */
	base_address = device->base_address;

	spin_lock(&device->dev_lock);

	/* Reading the VOCTRL2 register value for updation. */
	voctrl2 = ioread32(base_address + IOH_VIDEO_IN_VOCTRL2);

	/* Resetting bit6(BBON) */
	RESET_BIT(voctrl2, BBON);

	/* Checking and setting the BBOB bit value. */
	switch (output_bb_mode) {
	case BB_OUTPUT_OFF:
		/* Resetting bit6(BBON) */
		voctrl2 |= BB_OUTPUT_OFF;
		break;

	case BB_OUTPUT_ON:
		/* Setting bit6(BBON) */
		voctrl2 |= BB_OUTPUT_ON;
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid Blue background mode", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if (IOH_VIDEOIN_SUCCESS != retval)
		goto out;

	/* Updationg the VOCTRL2 register value. */
	iowrite32(voctrl2, (base_address + IOH_VIDEO_IN_VOCTRL2));

	/* Confirming the register updation. */
	if (voctrl2 == ioread32(base_address + IOH_VIDEO_IN_VOCTRL2)) {
		ioh_dbg(device, "In %s -> "
			"Register write successful", __func__);

		device->video_settings.current_bb_mode = output_bb_mode;
	} else {
		ioh_err(device, "In %s -> "
			"Register write unsuccessful", __func__);

		device->video_settings.current_bb_mode = INVALID_BB_MODE;

		retval = IOH_VIDEOIN_FAIL;
	}
out:
	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static enum ioh_video_in_bb_mode
ioh_video_in_get_bb(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_bb_mode;
}

static s32
ioh_video_in_set_cb(struct BT656_device *device,
			struct ioh_video_in_cb_settings cb_settings)
{
	u32 voctrl2;
	void __iomem *base_address;
	s32 retval = IOH_VIDEOIN_SUCCESS;

	/* Obtaining the base address of the device. */
	base_address = device->base_address;

	spin_lock(&device->dev_lock);

	/* Reading the VOCTRL2 register value for updation. */
	voctrl2 = ioread32(base_address + IOH_VIDEO_IN_VOCTRL2);

	/* Resetting bit7(CBON) */
	RESET_BIT(voctrl2, CBON);

	/* checking and setting the CB_Mode bits. */
	switch (cb_settings.cb_mode) {
	case CB_OUTPUT_OFF:
		/* Resetting bit7(CBON) */
		voctrl2 |= CB_OUTPUT_OFF;
		cb_settings.cb_outlev =
			device->video_settings.current_cb_settings.cb_outlev;
		break;

	case CB_OUTPUT_ON:
		/* Setting bit7(CBON) */
		voctrl2 |= CB_OUTPUT_ON;
		break;

	default:
		ioh_err(device, "In %s -> "
			"Invalid Color Bar Mode", __func__);

		retval = IOH_VIDEOIN_FAIL;
		break;
	}

	if ((IOH_VIDEOIN_SUCCESS == retval)
	&& (CB_OUTPUT_ON == cb_settings.cb_mode)) {
		/* Resetting bit0-1(OUTLEV) */
		RESET_BIT_RANGE(voctrl2, IOH_BIT_0, IOH_BIT_1);

		/* Checking and setting the CB outlev values. */
		switch (cb_settings.cb_outlev) {
		case CB_OUTLEV_25_PERCENT:
			voctrl2 |= CB_OUTLEV_25_PERCENT;
			break;

		case CB_OUTLEV_50_PERCENT:
			voctrl2 |= CB_OUTLEV_50_PERCENT;
			break;

		case CB_OUTLEV_75_PERCENT:
			voctrl2 |= CB_OUTLEV_75_PERCENT;
			break;

		case CB_OUTLEV_100_PERCENT:
			voctrl2 |= CB_OUTLEV_100_PERCENT;
			break;

		default:
			ioh_err(device, "In %s -> "
				"Invalid Color Bar output level", __func__);

			retval = IOH_VIDEOIN_FAIL;
			break;
		}
	}

	if (IOH_VIDEOIN_SUCCESS == retval) {
		/* Updating the register values. */
		iowrite32(voctrl2, (base_address + IOH_VIDEO_IN_VOCTRL2));

		if ((voctrl2 ==	ioread32(
				(base_address + IOH_VIDEO_IN_VOCTRL2)))) {
			ioh_dbg(device, "In %s -> "
				"Register write successful", __func__);

			device->video_settings.
				current_cb_settings = cb_settings;
		} else {
			ioh_err(device, "In %s -> "
				"Register write unsuccessful", __func__);

			device->video_settings.
				current_cb_settings = invalid_cb_settings;

			retval = IOH_VIDEOIN_FAIL;
		}
	}

	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct
ioh_video_in_cb_settings ioh_video_in_get_cb(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->video_settings.current_cb_settings;
}

static u32 ioh_video_in_get_buffer_size(struct BT656_device *device)
{
	u32 retval;
	u32 X_comp;
	u32 Y_comp;
	u32 X_size;

	X_comp = device->video_settings.current_frame_size.X_component;
	Y_comp = device->video_settings.current_frame_size.Y_component;
	X_size = device->video_settings.current_frame_size.pitch_size;

	retval = X_size * Y_comp;

	ioh_dbg(device, "Function %s invoked successfully(%u)",
			__func__, retval);

	return retval;
}

static u32 ioh_video_in_get_image_size(struct BT656_device *device)
{
	u32 bytes_per_pixel;
	u32 X_comp;
	u32 Y_comp;
	u32 retval;

	X_comp = device->video_settings.current_frame_size.X_component;
	Y_comp = device->video_settings.current_frame_size.Y_component;

	switch (device->video_settings.current_output_format.format) {
	case YCBCR_422_8BIT:
		bytes_per_pixel = 2;
		break;

	case YCBCR_422_10BIT:
		bytes_per_pixel = 4;
		break;

	case YCBCR_444_8BIT:
		bytes_per_pixel = 4;
		break;

	case YCBCR_444_10BIT:
		bytes_per_pixel = 4;
		break;

	case RGB888:
		bytes_per_pixel = 4;
		break;

	case RGB666:
		bytes_per_pixel = 4;
		break;

	case RGB565:
		bytes_per_pixel = 4;
		break;

	case RAW_8BIT:
		bytes_per_pixel = 2;
		break;

	case RAW_10BIT:
		bytes_per_pixel = 2;
		break;

	case RAW_12BIT:
		bytes_per_pixel = 2;
		break;

	default:
		bytes_per_pixel = 0;
		break;
	}

	retval = (X_comp * Y_comp * bytes_per_pixel);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
static u32 ioh_video_in_alloc_frame_buffer(struct BT656_device *device,
				struct ioh_video_in_frame_buffer_info info)
{
	s32 retval = IOH_VIDEOIN_SUCCESS;
	int i;

	if (info.buffer_num > MAXIMUM_FRAME_BUFFERS) {
		retval = IOH_VIDEOIN_FAIL;
	} else if (info.order <= 0) {
		retval = IOH_VIDEOIN_FAIL;
	} else {
		device->frame_buffer_info.buffer_num = info.buffer_num;

		device->frame_buffer_info.order = info.order;

		for (i = 0; i < info.buffer_num; i++) {
			device->bufs.frame_buffer[i].virt_addr =
				(u32)dma_alloc_coherent(&device->p_device->dev,
					(PAGE_SIZE <<
					   device->frame_buffer_info.order),
					(dma_addr_t *)&(device->
					   bufs.frame_buffer[i].phy_addr),
					GFP_ATOMIC);

			if (0 == device->bufs.frame_buffer[i].virt_addr) {
				retval = IOH_VIDEOIN_FAIL;
				ioh_err(device, "In %s -> "
					"Function dma_alloc_coherent "
					"failed for Video Frame Buffer %d",
					__func__, i);
				break;
			}

			device->bufs.frame_buffer[i].index = i;

			device->bufs.frame_buffer[i].data_size = 0;
		}
	}

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static u32 ioh_video_in_free_frame_buffer(struct BT656_device *device,
				struct ioh_video_in_frame_buffer_info info)
{
	s32 retval = IOH_VIDEOIN_SUCCESS;
	int i;

	if (info.buffer_num != device->frame_buffer_info.buffer_num) {
		retval = IOH_VIDEOIN_FAIL;
	} else if (info.order != device->frame_buffer_info.order) {
		retval = IOH_VIDEOIN_FAIL;
	} else {
		for (i = 0; i < info.buffer_num; i++) {
			if (device->bufs.frame_buffer[i].virt_addr) {
				dma_free_coherent(&device->p_device->dev,
					(PAGE_SIZE <<
					    device->
					    frame_buffer_info.order),
					(void *)device->
					   bufs.frame_buffer[i].virt_addr,
					(dma_addr_t)device->
					   bufs.frame_buffer[i].phy_addr
				);
				device->bufs.frame_buffer[i].index = 0;

				device->bufs.frame_buffer[i].virt_addr = 0;

				device->bufs.frame_buffer[i].phy_addr = 0;

				device->bufs.frame_buffer[i].data_size = 0;
			}
		}
	}

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_frame_buffers
ioh_video_in_get_frame_buffers(struct BT656_device *device)
{
	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return device->bufs;
}

static ssize_t ioh_video_in_read_sub(struct BT656_device *device,
				char __user *p_buffer,
				struct ioh_video_in_frame_buffer *r_frame,
				size_t size,
				unsigned int f_flag)
{
	struct BT656_device *dev;
	int repeat_flag;
	ssize_t retval = -EAGAIN;
	 /* Reference to the current frame.*/
	struct ioh_video_in_frame_buffer *frame;
	dev = ((struct BT656_device *)device);

	do {
		repeat_flag = 0;

		/* Attaining the read lock
		and obtaining the size of data that can be read. */
		ioh_video_in_lock_buffer(&(dev->read_buffer_lock));

		if (NULL != dev->read_buffer) {
			ioh_dbg(device, "In %s -> "
				"Read Buffer Not Empty", __func__);

			/* Obtaing the current latest frame. */
			frame = dev->read_buffer;

			/* Updating the read buffer. */
			dev->read_buffer = NULL;

			if (frame->data_size < size)
				size = frame->data_size;

			if (p_buffer != NULL) {
				/* If copying of video data
				to the user space failed. */
				if (0 != copy_to_user((void *)p_buffer,
				(void *)frame->virt_addr, size)) {
					ioh_err(device,
						"In %s -> "
						"Function copy_to_user "
						"failed", __func__);
					retval = -EFAULT;
				} else {	/* Copying successful. */
					ioh_dbg(device, "In %s -> "
						 "Function copy_to_user "
						 "invoked successfully",
						 __func__);
					retval = size;
				}
			} else {
				/* read buffer info to r_frame */
				*r_frame = *frame;
				retval = size;
			}
			frame->data_size = 0;
			ioh_video_in_unlock_buffer(&(dev->read_buffer_lock));
		} else {
			ioh_video_in_unlock_buffer(&(dev->read_buffer_lock));

			ioh_dbg(device, "In %s -> "
				"The video buffer is empty", __func__);

			/* If device has been opened
			without non-block mode. */
			if (!(f_flag)) {
				/* If video capturing thread
				is not sleeping. */
				if (false == dev->thread_sleep_flag) {

					/* Preparing to enter
					the wait state for capturing
					data. */
					dev->read_wait_flag = true;

					ioh_dbg(device, "In %s -> "
						"Read wait flag set to true",
						__func__);

					ioh_dbg(device, "In %s -> "
						"Entering the wait state",
						__func__);

					retval = wait_event_interruptible(
					    dev->read_wait_queue,
					    (false == dev->read_wait_flag));

					/* If wait failed. */
					if (-ERESTARTSYS == retval) {
						ioh_err(device,
							"In %s -> "
							"Read wait failed",
							__func__);
						retval = -EIO;
					} else {
						ioh_dbg(device, "In %s -> "
							"Exiting from "
							"the wait state",
							__func__);

						/* Attaining the loack
						over the raed
						buffer. */
						ioh_video_in_lock_buffer
						(&(dev->read_buffer_lock));

						/* Checking whether read data
						has been updated
						after wait state. */
						if (NULL != dev->read_buffer) {
							ioh_dbg(device, "In %s"
							 " -> Data capturing "
							 "successfully after "
							 "wait", __func__);

							/* Setting flag for
							repeating. */
							repeat_flag = 1;
						} else {
						/* No video data is available
						for reading. */

							ioh_err(device, "In "
							 "%s -> "
							 "Video data cannot "
							 "be captured even "
							 "after wait",
							 __func__);
						}

						/* Releasing the lock over the
						read buffer. */
						ioh_video_in_unlock_buffer(&
						(dev->read_buffer_lock));
					}
				} else {
				/* Video capturing thread is sleeping. */

					ioh_err(device, "In %s -> "
						"Video capturing not "
						"initiated", __func__);

					retval = -EIO;
				}
			}
		}
	} while (repeat_flag);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static struct ioh_video_in_frame_buffer
ioh_video_in_read_frame_buffer(struct BT656_device *device)
{
	struct ioh_video_in_frame_buffer r_frame;
	ssize_t retval;

	r_frame.index = -1;
	r_frame.virt_addr = (unsigned int)NULL;
	r_frame.phy_addr = (unsigned int)NULL;
	r_frame.data_size = 0;

	retval = ioh_video_in_read_sub(device,
				NULL,
				&r_frame,
				0,
				0);		/* 1 means non blocking */

	ioh_dbg(device, "Function %s invoked successfully", __func__);

	return r_frame;
}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

static irqreturn_t ioh_video_in_interrupt(int irq, void *dev_id)
{
	u32 insts;
	u32 emask;
	void __iomem *base_address;
	irqreturn_t retval = IRQ_NONE;
	struct ioh_vin_dev *dev = dev_id;
	struct BT656_device *device = dev->video_in_dev;

	/* Obtaining the base address. */
	base_address = device->base_address;

	/* Reading the interrupt register. */
	insts = ((ioread32(base_address + IOH_VIDEO_IN_INTSTS)) & INTBITS);
	emask = ((ioread32(base_address + IOH_VIDEO_IN_INTENB)) & INTBITS);

	if ((emask & insts) == 0) {
		ioh_dbg(device, "In %s -> No interrupt source "
				"insts = 0x%08x emask = 0x%08x",
				__func__, insts, emask);
		goto out;
	}

	ioh_dbg(device, "In %s -> insts = 0x%08x, emask = 0x%08x",
			__func__, insts, emask);

	if (LIKELY(((emask & insts) & VSINTSTS) != 0)) {

		ioh_dbg(device, "In %s -> VSYNC interrupt handled", __func__);

		if (device->vsync_waiting_flag == true) {
			device->vsync_waiting_flag = false;
			wake_up_interruptible(&(device->vsync_wait_queue));

			ioh_dbg(device, "In %s -> wake up by vsync", __func__);
		}
		retval = IRQ_HANDLED;
	}
	if (((emask & insts) & HSINTSTS) != 0) {

		ioh_dbg(device, "In %s -> HSYNC interrupt handled", __func__);

		retval = IRQ_HANDLED;
	}
	if (((emask & insts) & OFINTSTS) != 0) {

		ioh_dbg(device, "In %s -> "
				"Buffer Overflow interrupt handled", __func__);

		if (device->overflow_flag == false) {

			ioh_dbg(device, "[OVERFLOW]In %s -> "
					"overflow_flag is true", __func__);

			device->overflow_flag = true;
		}
		retval = IRQ_HANDLED;
	}

out:
	/* Clearing of interrupts if any interrupt exists */
	iowrite32(insts, (base_address + IOH_VIDEO_IN_INTSTS));

	ioh_dbg(device, "In %s -> "
		  "Interrupt handled and cleared.", __func__);

	return retval;
}

static void ioh_video_in_dma_complete(void *arg)
{
	struct BT656_device *device = arg;

	async_tx_ack(device->desc_dma);

	ioh_dbg(device, "In %s -> "
			"dmarenb disable and wake up the thread_sleep_queue",
			__func__);

	device->dma_flag = 1;
	wake_up(&device->thread_sleep_queue);

	ioh_dbg(device, "Function %s ended", __func__);
}

static u32 ioh_video_in_get_additional_line_size(struct BT656_device *device)
{
	enum ioh_video_in_input_data_format format;
	u32 addition = 0;

	format = (device)->
		video_settings.current_input_format.format;

	if ((format != NT_RAW_8BIT)
	 && (format != NT_RAW_10BIT)
	 && (format != NT_RAW_12BIT))
		addition = 3;

	ioh_dbg(device, "Function %s ended(%d)", __func__, addition);

	return addition;
}

#ifdef IOH_VIDEO_IN_DMA_CONTIG
static s32 ioh_video_in_make_dma_descriptors(struct BT656_device *device,
								u32 idx)
#else /* IOH_VIDEO_IN_DMA_CONTIG */
static s32 ioh_video_in_make_dma_descriptors(struct BT656_device *device)
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
{
	u32 i, j;	/* DMA buffer and descriptor index variable. */

	struct BT656_device *priv = device;
	struct scatterlist *sg;
	dma_addr_t dma;
	void *buf;

	u32 Y_comp = 0;			/* The Y-component. */
	u32 addition = 0;
	u32 bytes_per_line_image = 0;	/* The bytes per line of image. */

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	u32 idx;
	int nent;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	Y_comp = device->video_settings.current_frame_size.Y_component;
	bytes_per_line_image = ioh_video_in_get_image_size(device);
	bytes_per_line_image = bytes_per_line_image / Y_comp;
	addition = ioh_video_in_get_additional_line_size(device);

	ioh_dbg(device, "In %s -> X = %d bytes, Y = %d+%d line",
			__func__, bytes_per_line_image, Y_comp, addition);

	spin_lock(&device->dev_lock);

	i = 0;

	priv->nent = (Y_comp + addition);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	sg = priv->sg_dma + (idx * (Y_comp + addition));
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	sg = priv->sg_dma;
	for (idx = 0; idx < device->frame_buffer_info.buffer_num; idx++) {
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

		/* Making one descriptor set */
		priv->sg_dma_list[idx] = sg;

		sg_init_table(sg, (Y_comp + addition));

		for (j = 0; j < Y_comp; j++, sg++) {
			dma = device->dma_buffer_phy[idx] +
						(j * bytes_per_line_image);
#ifdef IOH_VIDEO_IN_DMA_CONTIG
			buf = phys_to_virt(dma);
#else /* IOH_VIDEO_IN_DMA_CONTIG */
			buf = device->dma_buffer_virt[idx] +
						(j * bytes_per_line_image);
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

			sg_set_page(sg,
					virt_to_page(buf),
					bytes_per_line_image / 4,
					(unsigned long)buf & ~PAGE_MASK);
			sg_dma_address(sg) = dma;
			sg_dma_len(sg) = bytes_per_line_image / 4;
		}

		/* Last 3 descriptor are empty transfer if not RAW */
		for (j = 0; j < addition; j++, sg++) {
			dma = device->dma_buffer_phy[idx];
#ifdef IOH_VIDEO_IN_DMA_CONTIG
			buf = phys_to_virt(dma);
#else /* IOH_VIDEO_IN_DMA_CONTIG */
			buf = device->dma_buffer_virt[idx];
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

			sg_set_page(sg,
					virt_to_page(buf),
					0,
					(unsigned long)buf & ~PAGE_MASK);
			sg_dma_address(sg) = dma;
			sg_dma_len(sg) = 0;
		}
#ifdef IOH_VIDEO_IN_DMA_CONTIG
/* TODO ? */
#else /* IOH_VIDEO_IN_DMA_CONTIG */
		nent = dma_map_sg(&priv->p_device->dev,
					priv->sg_dma_list[idx],
					(Y_comp + addition),
					DMA_FROM_DEVICE);
		ioh_dbg(device, "In %s -> nent %d", __func__,  nent);
	}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended", __func__);

	return IOH_VIDEOIN_SUCCESS;
}

static s32 ioh_video_in_dma_submit(struct BT656_device *device,
					u32 index)
{
	struct BT656_device *priv = device;
	u32 Y_comp = 0;
	u32 addition = 0;
	struct dma_async_tx_descriptor *desc;

	Y_comp = device->video_settings.current_frame_size.Y_component;

	addition = ioh_video_in_get_additional_line_size(device);

	desc = priv->chan_dma->device->device_prep_slave_sg(priv->chan_dma,
			priv->sg_dma_list[index],
			((Y_comp + addition)),
			DMA_FROM_DEVICE,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!desc) {
		ioh_err(device, "In %s -> "
				"device_prep_slave_sg failed", __func__);
		goto out;
	}

	priv->desc_dma = desc;

	dma_sync_sg_for_device(&priv->p_device->dev,
				priv->sg_dma_list[index],
				priv->nent,
				DMA_FROM_DEVICE);

	desc->callback = ioh_video_in_dma_complete;
	desc->callback_param = priv;
	desc->tx_submit(desc);

/* TODO comment out for unexpexted callback */
#if 0
	dma_async_issue_pending(priv->chan_dma);
#endif

	spin_lock(&(device->dev_lock));
	device->intenb.dmarenb = ENABLE;
	device->intenb.ofintenb = ENABLE;
	device->intenb.vsintenb = ENABLE;
	write_intenb(device);
	spin_unlock(&(device->dev_lock));

	ioh_dbg(device, "Function %s (index=%d) ended", __func__, index);

	return IOH_VIDEOIN_SUCCESS;
out:
	return IOH_VIDEOIN_FAIL;
}

#ifdef IOH_VIDEO_IN_DMA_CONTIG
static s32 ioh_video_in_schedule_next(struct ioh_vin_dev *dev,
					struct ioh_vin_buffer *buf)
{
	struct BT656_device *device = dev->video_in_dev;
	static int idx;

	device->dma_buffer_phy[idx] = videobuf_to_dma_contig(&buf->vb);

	ioh_video_in_make_dma_descriptors(device, idx);

	buf->frame_index = idx;

	ioh_dbg(device, "%s -> addr[%d] = 0x%08x",
			__func__, idx, (u32)device->dma_buffer_phy[idx]);

	idx++;
	if (idx >= N_FRAME_BUF)
		idx = 0;

	ioh_dbg(device, "Function %s ended", __func__);

	return IOH_VIDEOIN_SUCCESS;
}
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

static s32 ioh_video_in_alloc_dma_desc(struct BT656_device *device)
{
	struct BT656_device *priv = device;
	int num = DESC_SIZE;

	priv->sg_dma = kzalloc(sizeof(struct scatterlist)*num, GFP_ATOMIC);
	if (priv->sg_dma == NULL) {
		ioh_err(device, "In %s -> kzalloc failed", __func__);
		goto out;
	}
	ioh_dbg(device, "Function %s ended", __func__);

	return IOH_VIDEOIN_SUCCESS;
out:
	return IOH_VIDEOIN_FAIL;
}

static s32 ioh_video_in_free_dma_desc(struct BT656_device *device)
{
	struct BT656_device *priv = device;
#ifdef IOH_VIDEO_IN_DMA_CONTIG
/* TODO ? */
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	int idx;

	for (idx = 0; idx < device->frame_buffer_info.buffer_num; idx++) {
		dma_unmap_sg(&priv->p_device->dev,
					priv->sg_dma_list[idx],
					priv->nent,
					DMA_FROM_DEVICE);
	}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	kfree(priv->sg_dma);

	ioh_dbg(device, "Function %s ended", __func__);

	return 0;
}

static s32 ioh_video_in_start_setting(struct ioh_vin_dev *dev)
{
#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	struct BT656_device *device = dev->video_in_dev;
	unsigned int i;
	u32 frame_index = 0;	/* The frame buffer index. */
	int ret_val;

	mutex_lock(&dev->mutex);

	for (i = 0; i < device->frame_buffer_info.buffer_num; i++) {
		device->dma_buffer_virt[i] = (void *)device->
			bufs.frame_buffer[i].virt_addr;
		device->dma_buffer_phy[i] = device->
			bufs.frame_buffer[i].phy_addr;
	}

	ioh_video_in_make_dma_descriptors(device);

	ret_val = ioh_video_in_dma_submit(device, frame_index);

	ioh_dbg(device, "In %s -> ioh_video_in_dma_submit returned(%d)",
			__func__,  ret_val);

	mutex_unlock(&dev->mutex);

	ioh_dbg(device, "Function %s ended", __func__);

#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	return 0;
}

static s32 ioh_video_in_thread_fn(void *data)
{
	struct ioh_vin_dev *dev = data;
	struct BT656_device *device = dev->video_in_dev;
	/* Accessing the device buffer and flag variables. */
	/* DMA descriptor start/end physical address */
	u32 thread_1st_flag = false;	/* thread 1st flag*/
#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	u32 frame_index = 0;	/* The frame buffer index. */
	u32 frame_count = 0;	/* temporary frame count value for debug */
	u32 frame_index_next;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
	u32 frame_skip_count = 0;

	/* Reference to thread sleep flag. */
	u8 *thread_sleep_flag = &device->thread_sleep_flag;

	/* Reference to thread exit flag. */
	u8 *thread_exit_flag = &device->thread_exit_flag;

	/* Reference to suspend flag. */
	u8 *suspend_flag = &device->suspend_flag;

	/* Reference to dma flag. */
	u8 *dma_flag = &device->dma_flag;

	/* Reference to overflow flag. */
	u8 *overflow_flag = &device->overflow_flag;

	/* The DMA enable re-try counter. */

	int ret_val;

	/* Obtaining DMA enabled space for descriptors around 8KB. */
	mutex_lock(&dev->mutex);
	if (ioh_video_in_alloc_dma_desc(device) != IOH_VIDEOIN_SUCCESS)
		goto out;

	ioh_dbg(device, "In %s -> Function "
		"ioh_video_in_alloc_dma_desc invoked successfully", __func__);

	mutex_unlock(&dev->mutex);

	while (false == *thread_exit_flag) {
		ioh_dbg(device, "In %s -> "
				"Thread preparing to sleep if capturing is "
				"stopped or suspension is enabled", __func__);

		/* Thread trying to sleep
		   if conditions are not suitable. */
		wait_event(device->thread_sleep_queue,
			(((false == *thread_sleep_flag)
			 && (false == *suspend_flag))
			|| (true == *thread_exit_flag)));

		if (true == *thread_exit_flag) {
			ioh_dbg(device, "In %s -> thread_exit_flag was "
				"true and break the thread", __func__);
			break;
		}

		/* Thread trying to capture the video data
		   by enabling the DMA transfer. */
		*dma_flag = 0;

		if (UNLIKELY(thread_1st_flag == false)) {
			ioh_video_in_start_setting(dev);
			thread_1st_flag = true;
		}

		ioh_dbg(device, "In %s -> "
			"thread preparing for capturing", __func__);

		wait_event(device->thread_sleep_queue,
			((true == (*thread_sleep_flag))
			/* Thread should not sleep. */
			|| (true == (*thread_exit_flag))
			/* Thread should not exit. */
			|| (true == (*suspend_flag))
			 /* System should not suspend. */
			|| (1 == (*dma_flag))
			 /* DMA transaction should complete. */
			));

		ioh_dbg(device, "In %s -> wait_event end "
			"thread_sleep_flag=%d, thread_exit_flag=%d, "
			"suspend_flag=%d, dma_flag=%d",
			__func__,
			*thread_sleep_flag, *thread_exit_flag,
			*suspend_flag, *dma_flag);

		if (UNLIKELY(1 != *dma_flag)) {
			ioh_dbg(device, "In %s -> dma_flag was not 1 "
				"and break the threead", __func__);
			break;
		}

		spin_lock(&dev->video_in_dev->dev_lock);
		dev->video_in_dev->intenb.dmarenb = DISABLE;
		write_intenb(dev->video_in_dev);
		spin_unlock(&dev->video_in_dev->dev_lock);

		/* if overflow then wait for next vsync */
		/* else dma is started. if vsync is already comes, start at
		   next frame */
		if (*overflow_flag == true) {
			ioh_dbg(device, "[OVERFLOW]In %s -> wait vsync",
				__func__);

			ioh_video_in_wait_vsync(device);
			ioh_video_in_wait_vsync(device);
		}

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
		/* next index value */
		if (frame_index == (device->frame_buffer_info.buffer_num - 1))
			frame_index_next = 0;
		else
			frame_index_next = frame_index + 1;

		ret_val = ioh_video_in_dma_submit(device, frame_index_next);

		ioh_dbg(device, "In %s -> ioh_video_in_dma_submit returned(%d)",
				__func__,  ret_val);

		dma_sync_sg_for_cpu(&device->p_device->dev,
			device->sg_dma_list[frame_index],
			device->nent,
			DMA_FROM_DEVICE);
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

		if (frame_skip_count == device->frame_skip_num) {
			frame_skip_count = 0;
#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
			/* Updating the size of the video farme captured */
			device->bufs.frame_buffer[frame_index].data_size
				= ioh_video_in_get_buffer_size(device);

			/* Attaining the read buffer lock
			   for updating the read buffer. */
			ioh_video_in_lock_buffer(&
					(device->read_buffer_lock));

			/* Updating the read buffer. */
			device->read_buffer = &(device->
					bufs.frame_buffer[frame_index]);

			ioh_dbg(device, "In %s -> "
				"Captured index is %d(p:0x%08x v:0x%08x)",
				__func__,
				frame_index,
				(u32)device->read_buffer->phy_addr,
				(u32)device->read_buffer->virt_addr);

			/* Releasing the read buffer lock. */
			ioh_video_in_unlock_buffer(&
					   (device->read_buffer_lock));

			/* Wake up the read system call
			   if it is waiting for capture data. */
			if ((true == device->read_wait_flag)
			    && (*overflow_flag == false)) {
				device->read_wait_flag = false;
				wake_up_interruptible(&device->
						read_wait_queue);
				frame_count++;
				ioh_dbg(device, "In %s -> "
				 "frame_count = %d", __func__, frame_count);
			}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

		} else {
			frame_skip_count++;
			ioh_dbg(device, "In %s -> "
				  "Video Data skipped", __func__);
		}

		ioh_videoin_thread_tick(dev);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
		frame_index = frame_index_next;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

		*overflow_flag = false;

		ioh_dbg(device, "In %s -> "
			 "Video Data captured and "
			 "updated to the video read buffer", __func__);

	} /* while */

	/* Disable the DMA mode. */
	spin_lock(&(device->dev_lock));
	device->intenb.dmarenb = DISABLE;
	device->intenb.ofintenb = DISABLE;
	write_intenb(device);
	spin_unlock(&(device->dev_lock));

	ioh_video_in_free_dma_desc(device);

	ioh_dbg(device, "In %s -> Function "
		  "ioh_video_in_free_dma_desc invoked", __func__);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	/* Wake up the read system call if it is waiting for capture data. */
	if (true == device->read_wait_flag) {
		device->read_wait_flag = false;
		wake_up_interruptible(&device->read_wait_queue);
	}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

out:
	*thread_exit_flag = true;
	*thread_sleep_flag = true;

	mutex_unlock(&dev->mutex);

	ret_val = IOH_VIDEOIN_SUCCESS;
	ioh_dbg(device, "Function %s ended", __func__);

	return ret_val;
}

static s32 ioh_video_in_init(struct BT656_device *device)
{
	s32 retval;

	/* Setting the default input format. */
	retval = ioh_video_in_set_input_format(device,
						default_input_data_format);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_input_format failed", __func__);
		goto out;
	}

	/* Setting the default output format. */
	retval = ioh_video_in_set_output_format(device,
						default_output_data_format);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_output_format failed", __func__);
		goto out;
	}

	/* Setting the default I/P conversion mode. */
	retval = ioh_video_in_set_ip_trans(device, DEFAULT_IP_TRANS_SETTINGS);

	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_ip_trans failed", __func__);
		goto out;
	}

	/* Setting the luminance level. */
	retval = ioh_video_in_set_luminance_level(device,
					 default_luminance_settings);

	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_luminance_level failed", __func__);
		goto out;
	}

	/* Setting the RGB gain settings. */
	retval = ioh_video_in_set_rgb_gain(device, default_rgb_gain_settings);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_rgb_gain failed", __func__);
		goto out;
	}

	/* Setting the Blnaking Signal Timing settings. */
	retval = ioh_video_in_set_blank_tim(device,
						default_blank_tim_settings);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_blank_tim failed", __func__);
		goto out;
	}

	/* Setting the Blue background settings. */
	retval = ioh_video_in_set_bb(device, DEFAULT_BB_SETTINGS);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_bb invoked failed", __func__);
		goto out;
	}

	/* Setting the Color bar settings. */
	retval = ioh_video_in_set_cb(device, default_cb_settings);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> Function "
			"ioh_video_in_set_cb invoked failed", __func__);
		goto out;
	}

	spin_lock(&device->dev_lock);
	device->intenb.drevsem = VSYNC_SYNC;
	device->intenb.dmarenb = DISABLE;
	device->intenb.ofintenb = ENABLE;
	device->intenb.hsintenb = DISABLE;
	device->intenb.vsintenb = ENABLE;
	write_intenb(device);
	spin_unlock(&device->dev_lock);

	/* Enabling the interrupts. */
	device->dma_flag = -1;

out:
	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static void ioh_video_in_exit(struct BT656_device *device)
{
	spin_lock(&device->dev_lock);
	(device)->intenb.drevsem = VSYNC_NOT_SYNC;
	(device)->intenb.dmarenb = DISABLE;
	(device)->intenb.ofintenb = DISABLE;
	(device)->intenb.hsintenb = DISABLE;
	(device)->intenb.vsintenb = DISABLE;
	write_intenb(device);

	iowrite32(ASSERT_RESET, device->base_address + IOH_VIDEO_IN_RESET);

	spin_unlock(&device->dev_lock);

	ioh_dbg(device, "Function %s ended", __func__);
}

/* ---- V4L2 ---- */

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

static void
ioh_vin_fillbuff(struct ioh_vin_dev *dev, struct ioh_vin_buffer *buf)
{
#ifdef IOH_VIDEO_IN_DMA_CONTIG
	struct BT656_device *device;
	struct timeval ts;

	device = dev->video_in_dev;

	/* Advice that buffer was filled */
	buf->vb.field_count++;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;
	buf->vb.state = VIDEOBUF_DONE;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	struct BT656_device *device;
	int pos = 0;
	int hmax  = buf->vb.height;
	int wmax  = buf->vb.width;
	struct timeval ts;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);

	if (!vbuf)
		return;

	device = dev->video_in_dev;

	/* -- */
	ioh_video_in_lock_buffer(&(device->read_buffer_lock));
	if (device->read_buffer != NULL) {
		memcpy(vbuf + pos, (void *)device->read_buffer->virt_addr,
					wmax * 2 * hmax); pos += wmax*2 * hmax;
		ioh_dbg(device, "In %s -> read_buffer is copied"
			"(p:0x%08x v:0x%08x)", __func__,
			(u32)device->read_buffer->phy_addr,
			(u32)device->read_buffer->virt_addr);
		device->read_buffer = NULL;
	} else {
		ioh_dbg(device, "In %s -> read_buffer is NULL", __func__);
	}
	ioh_video_in_unlock_buffer(&(device->read_buffer_lock));
	/* -- */

	/* Advice that buffer was filled */
	buf->vb.field_count++;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;
	buf->vb.state = VIDEOBUF_DONE;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
}

static void ioh_videoin_thread_tick(struct ioh_vin_dev *dev)
{
#ifdef IOH_VIDEO_IN_DMA_CONTIG
	struct ioh_vin_buffer *buf;
	unsigned long flags = 0;
	int ret_val;

	spin_lock_irqsave(&dev->slock, flags);
	if (UNLIKELY(list_empty(&dev->vidq_active))) {
		ioh_dbg(dev->video_in_dev, "In %s -> "
			"No active queue to serve", __func__);
		goto unlock;
	}
	if (UNLIKELY(!dev->active_frm)) {
		ioh_dbg(dev->video_in_dev, "In %s -> "
			"No active frame", __func__);
		goto unlock;
	}

	buf = dev->active_frm;

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	ioh_vin_fillbuff(dev, buf);

	wake_up(&buf->vb.done);

	if (UNLIKELY(list_empty(&dev->vidq_active))) {
		/* Stop */
		dev->active_frm = NULL;
		dev->vidq_status = IOH_VIDEO_IN_VIDQ_INITIALISING;
		ioh_dbg(dev->video_in_dev, "In %s -> vidq_active is empty",
			__func__);
		goto unlock;
	}

	dev->active_frm = list_entry(dev->vidq_active.next,
					struct ioh_vin_buffer, vb.queue);

	ret_val = ioh_video_in_dma_submit(dev->video_in_dev,
						dev->active_frm->frame_index);

	ioh_dbg(dev->video_in_dev, "In %s -> "
			"ioh_video_in_dma_submit returned(%d)",
			__func__,  ret_val);

	if (UNLIKELY(dev->active_frm->vb.queue.next != &dev->vidq_active)) {
		struct ioh_vin_buffer *new =
				list_entry(dev->active_frm->vb.queue.next,
					struct ioh_vin_buffer, vb.queue);

		ioh_video_in_schedule_next(dev, new);

		ioh_dbg(dev->video_in_dev, "In %s -> "
			"ioh_video_in_schedule_next was called", __func__);
	}

unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
/* --------------------------------- */
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	struct ioh_vin_buffer *buf;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dev->vidq_active)) {
		ioh_dbg(dev->video_in_dev, "In %s -> "
			"No active queue to serve", __func__);
		goto unlock;
	}

	buf = list_entry(dev->vidq_active.next,
			 struct ioh_vin_buffer, vb.queue);

	/* Nobody is waiting on this buffer, return */
	if (!waitqueue_active(&buf->vb.done)) {
		ioh_dbg(dev->video_in_dev, "In %s -> "
			"No active frame", __func__);
		goto unlock;
	}

#if 1
	if (dev->video_in_dev->read_buffer == NULL) {
		ioh_dbg(dev->video_in_dev, "In %s -> "
			"read_buffer is NULL", __func__);
		goto unlock;
	}
#endif

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	ioh_vin_fillbuff(dev, buf);

	wake_up(&buf->vb.done);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq,
			unsigned int *count, unsigned int *size)
{
	struct ioh_vin_fh  *fh = vq->priv_data;
	struct ioh_vin_dev *dev  = fh->dev;

	*size = fh->pix_format.sizeimage;

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	ioh_dbg(dev->video_in_dev, "%s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct ioh_vin_buffer *buf)
{
	struct ioh_vin_fh  *fh = vq->priv_data;
	struct ioh_vin_dev *dev  = fh->dev;

	ioh_dbg(dev->video_in_dev, "%s, state: %i\n", __func__, buf->vb.state);

	if (in_interrupt())
		BUG();

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	videobuf_dma_contig_free(vq, &buf->vb);
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	videobuf_vmalloc_free(&buf->vb);
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
	ioh_dbg(dev->video_in_dev, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_minw() (320)
#define norm_minh() (240)
#define norm_maxw() 1280
#define norm_maxh() (1024 + 3)
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct ioh_vin_fh     *fh  = vq->priv_data;
	struct ioh_vin_dev    *dev = fh->dev;
	struct ioh_vin_buffer *buf =
				container_of(vb, struct ioh_vin_buffer, vb);
	int rc;

	ioh_dbg(dev->video_in_dev, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == fh->fmt);

	if ((fh->pix_format.width  < norm_minw()) ||
	    (fh->pix_format.width  > norm_maxw()) ||
	    (fh->pix_format.height < norm_minh()) ||
	    (fh->pix_format.height > norm_maxh()))
		return -EINVAL;

	buf->vb.size = fh->pix_format.sizeimage;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->pix_format.width;
	buf->vb.height = fh->pix_format.height;
	buf->vb.field  = field;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}

	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

fail:
	free_buffer(vq, buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct ioh_vin_buffer    *buf  =
				container_of(vb, struct ioh_vin_buffer, vb);
	struct ioh_vin_fh        *fh   = vq->priv_data;
	struct ioh_vin_dev       *dev  = fh->dev;
#ifdef IOH_VIDEO_IN_DMA_CONTIG
	int ret_val;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	ioh_dbg(dev->video_in_dev, "%s", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &dev->vidq_active);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	if (dev->vidq_status == IOH_VIDEO_IN_VIDQ_RUNNING) {
		ioh_dbg(dev->video_in_dev, "%s running"
			"(vidq_status=%d)", __func__, dev->vidq_status);
		return;
	} else if (!dev->active_frm) {
		ioh_dbg(dev->video_in_dev, "%s first buffer"
			"(vidq_status=%d)", __func__, dev->vidq_status);
		dev->active_frm = buf;
		/* Ready */
		ioh_video_in_schedule_next(dev, buf);

		spin_unlock_irq(&dev->slock);

		ret_val = ioh_video_in_dma_submit(dev->video_in_dev,
						dev->active_frm->frame_index);

		spin_lock_irq(&dev->slock);

		ioh_dbg(dev->video_in_dev, "In %s -> "
				"ioh_video_in_dma_submit returned(%d)",
				__func__,  ret_val);

	} else if (dev->active_frm->vb.queue.next == &buf->vb.queue) {
		ioh_dbg(dev->video_in_dev, "%s second buffer"
			"(vidq_status=%d)",  __func__, dev->vidq_status);
		dev->vidq_status = IOH_VIDEO_IN_VIDQ_RUNNING;

		/* Ready for next */
		ioh_video_in_schedule_next(dev, buf);

	}
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct ioh_vin_buffer   *buf  =
				container_of(vb, struct ioh_vin_buffer, vb);
	struct ioh_vin_fh       *fh   = vq->priv_data;
	struct ioh_vin_dev      *dev  = (struct ioh_vin_dev *)fh->dev;

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);
	if (dev->active_frm == buf) {
		spin_lock(&dev->video_in_dev->dev_lock);
		dev->video_in_dev->intenb.dmarenb = DISABLE;
		write_intenb(dev->video_in_dev);
		spin_unlock(&dev->video_in_dev->dev_lock);
	}
	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED)) {
		vb->state = VIDEOBUF_ERROR;
		list_del(&vb->queue);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	ioh_dbg(dev->video_in_dev, "%s", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ioh_vin_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct ioh_vin_fh  *fh  = priv;
	struct ioh_vin_dev *dev = fh->dev;

	strcpy(cap->driver, "ioh_vin");
	strcpy(cap->card, "ioh_vin");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = IOH_VIN_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct ioh_vin_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct ioh_vin_fh *fh = priv;

	ioh_dbg(fh->dev->video_in_dev, "%s", __func__);

	f->fmt.pix = fh->pix_format;

	f->fmt.pix.field = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static struct ioh_vin_fmt *get_format(struct v4l2_format *f)
{
	struct ioh_vin_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == ARRAY_SIZE(formats))
		return NULL;

	return &formats[k];
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct ioh_vin_fh  *fh  = priv;
	struct ioh_vin_dev *dev = fh->dev;
	struct ioh_vin_fmt *fmt;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mbus_fmt;
	int ret;

	ioh_dbg(dev->video_in_dev, "%s", __func__);

	fmt = get_format(f);
	if (!fmt) {
		ioh_warn(dev->video_in_dev, "Fourcc format (0x%08x) invalid",
			pix->pixelformat);
		return -EINVAL;
	}
	ioh_dbg(dev->video_in_dev, "%s Fourcc format (0x%08x) valid",
			__func__, pix->pixelformat);

	pix->pixelformat = fmt->fourcc;
	v4l2_fill_mbus_format(&mbus_fmt, pix, fmt->mbus_code);

	ret = sensor_call(dev, video, try_mbus_fmt, &mbus_fmt);

	v4l2_fill_pix_format(pix, &mbus_fmt);
	pix->bytesperline = (pix->width * fmt->depth) >> 3;
	pix->sizeimage = pix->height * pix->bytesperline;

	switch (mbus_fmt.field) {
	case V4L2_FIELD_NONE:
		break;
	case V4L2_FIELD_ANY:
		pix->field = V4L2_FIELD_NONE;
		break;
	default:
		ioh_err(dev->video_in_dev, "Not supported sensor detected");
		ret = -EINVAL;
	}

	return ret;
}

static int ioh_video_in_setting_s_fmt(struct ioh_vin_dev *dev,
						struct v4l2_format *fmt)
{
	struct ioh_video_in_input_format input_format;
	struct ioh_video_in_output_format output_format;
	struct ioh_video_in_frame_size frame_size;
	struct BT656_device *device;
	int ret;

	device = dev->video_in_dev;

	/* input format setting for videoin */
	switch (v4l2_i2c_subdev_addr(dev->sensor)) {
	case OV7620_ADDR:
		ioh_info(dev->video_in_dev, "sensor is OV7620");
		input_format.format = NT_RAW_8BIT;
		input_format.numerical_format = DONT_CARE_NUMERICAL_FORMAT;
		device->frame_skip_num = 0;
		break;
	case OV9653_ADDR:
		ioh_info(dev->video_in_dev, "sensor is OV9653");
		input_format.format = NT_RAW_8BIT;
		input_format.numerical_format = DONT_CARE_NUMERICAL_FORMAT;
		device->frame_skip_num = 0;
		break;
	case ML86V76651_ADDR:
		ioh_info(dev->video_in_dev, "sensor is ML86V76651");
		input_format.format = NT_SQPX_ITU_R_BT_656_4_8BIT;
		input_format.numerical_format = DONT_CARE_NUMERICAL_FORMAT;
		device->frame_skip_num = 0;
		break;
	case NCM13J_ADDR:
		ioh_info(dev->video_in_dev, "sensor is NCM13J");
		input_format.format = NT_RAW_8BIT;
		input_format.numerical_format = DONT_CARE_NUMERICAL_FORMAT;
		device->frame_skip_num = 0;
		break;
	default:
		ioh_err(dev->video_in_dev, "Not supported sensor detected");
		return -ENODEV;
	}
	ret = ioh_video_in_set_input_format(device, input_format);
	if (ret != 0)
		return -ENODEV;

	/* output format setting for videoin */
	output_format.format = YCBCR_422_8BIT;
	output_format.numerical_format = DONT_CARE_NUMERICAL_FORMAT;
	output_format.luminance_range = DONT_CARE_LUMINANNCE_RANGE;
	output_format.rgb_gain_level = DONT_CARE_RGBLEV;
	ret = ioh_video_in_set_output_format(device, output_format);
	if (ret != 0)
		return -ENODEV;

	/* size setting for videoin */
	frame_size.X_component = fmt->fmt.pix.width;
	frame_size.Y_component = fmt->fmt.pix.height;
	frame_size.pitch_size = fmt->fmt.pix.bytesperline;
	ret = ioh_video_in_set_size(device, frame_size);
	if (ret != 0)
		return -ENODEV;

	ioh_dbg(dev->video_in_dev, "%s X=%d[pix], Y=%d[pix], "
			"pitch_size=%d[byte]", __func__,
			frame_size.X_component, frame_size.Y_component,
			frame_size.pitch_size);

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *fmt)
{
	struct ioh_vin_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct ioh_vin_dev *dev = fh->dev;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct ioh_vin_fmt *f;
	int ret;

	ioh_dbg(dev->video_in_dev, "%s start", __func__);

	f = get_format(fmt);

	ret = vidioc_try_fmt_vid_cap(file, fh, fmt);

	if (ret < 0)
		return ret;

	mutex_lock(&q->vb_lock);

	fh->fmt = get_format(fmt);
	fh->pix_format = fmt->fmt.pix;
	fh->vb_vidq.field = fmt->fmt.pix.field;

	v4l2_fill_mbus_format(&mbus_fmt, &fh->pix_format, fh->fmt->mbus_code);

	ret = sensor_call(dev, video, s_mbus_fmt, &mbus_fmt);
	if (ret != 0)
		goto out;

	ret = ioh_video_in_setting_s_fmt(dev, fmt);
	if (ret != 0)
		goto out;

out:
	mutex_unlock(&q->vb_lock);

	ioh_dbg(dev->video_in_dev, "%s ended(%d)", __func__, ret);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct ioh_vin_fh  *fh = priv;

	return videobuf_reqbufs(&fh->vb_vidq, p);
}

static int vidioc_querybuf(struct file *file,
					void *priv, struct v4l2_buffer *p)
{
	struct ioh_vin_fh  *fh = priv;

	return videobuf_querybuf(&fh->vb_vidq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ioh_vin_fh *fh = priv;

	return videobuf_qbuf(&fh->vb_vidq, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ioh_vin_fh  *fh = priv;

	return videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK);
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ioh_vin_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ioh_vin_fh  *fh = priv;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;
	/* -- */
	{
		int retval;
		struct BT656_device *device;
		device = fh->dev->video_in_dev;

		retval = ioh_video_in_cap_start(device);
		if (retval != IOH_VIDEOIN_SUCCESS)
			return -EINVAL;
	}
	/* -- */
	return videobuf_streamon(&fh->vb_vidq);
}

static int vidioc_streamoff(struct file *file,
					void *priv, enum v4l2_buf_type i)
{
	struct ioh_vin_fh  *fh = priv;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;
	/* -- */
	{
		struct BT656_device *device;
		device = fh->dev->video_in_dev;

		ioh_video_in_cap_stop(device);
	}
	/* -- */
	return videobuf_streamoff(&fh->vb_vidq);
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

/* only one input in this driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	/* -- */
	if (inp->index != 0)
		return -EINVAL;
	/* -- */

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_NTSC_M;
	sprintf(inp->name, "Camera %u", inp->index);

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}
static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

	/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	struct ioh_vin_fh *fh = priv;
	struct ioh_vin_dev *dev = fh->dev;
	int ret;

	ret = sensor_call(dev, core, queryctrl, qc);
	return ret;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ioh_vin_fh *fh = priv;
	struct ioh_vin_dev *dev = fh->dev;
	int ret;

	ret = sensor_call(dev, core, g_ctrl, ctrl);
	return ret;
}
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct ioh_vin_fh *fh = priv;
	struct ioh_vin_dev *dev = fh->dev;
	int ret;

	ret = sensor_call(dev, core, s_ctrl, ctrl);
	return ret;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int ioh_vin_v4l2_open(struct file *file)
{
	struct ioh_vin_dev *dev = video_drvdata(file);
	struct ioh_vin_fh *fh = NULL;
	int retval = 0;

	ioh_dbg(dev->video_in_dev, "%s start", __func__);

	mutex_lock(&dev->mutex);
	dev->users++;

	if (dev->users > 1) {
		dev->users--;
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}

	ioh_info(dev->video_in_dev, "open %s type=%s users=%d\n",
		video_device_node_name(dev->vfd),
		v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		ioh_err(dev->video_in_dev, "In %s -> "
			"kzalloc failed", __func__);
		dev->users--;
		retval = -ENOMEM;
	}

	if (retval) {
		mutex_unlock(&dev->mutex);
		return retval;
	}

	file->private_data = fh;
	fh->dev      = dev;

	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &formats[0];

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	dev->active_frm = NULL;
	dev->vidq_status = IOH_VIDEO_IN_VIDQ_IDLE;

	videobuf_queue_dma_contig_init(&fh->vb_vidq, &ioh_vin_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_ANY,
			sizeof(struct ioh_vin_buffer), fh,
			NULL);
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	videobuf_queue_vmalloc_init(&fh->vb_vidq, &ioh_vin_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_ANY,
			sizeof(struct ioh_vin_buffer), fh,
			NULL);
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	/* -- */
	retval = ioh_video_in_open(dev);
	mutex_unlock(&dev->mutex);
	if (retval < 0)
		return -1;

	/* -- */

	return 0;
}

static ssize_t
ioh_vin_v4l2_read(struct file *file, char __user *data, size_t count,
								loff_t *ppos)
{
	struct ioh_vin_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ioh_vin_v4l2_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ioh_vin_fh        *fh = file->private_data;
	struct ioh_vin_dev       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;
	unsigned int ret;

	ioh_dbg(dev->video_in_dev, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	ret = videobuf_poll_stream(file, q, wait);

	ioh_dbg(dev->video_in_dev, "%s ret=0x%04x\n", __func__, ret);

	return ret;
}

static int ioh_vin_v4l2_close(struct file *file)
{
	struct ioh_vin_fh         *fh = file->private_data;
	struct ioh_vin_dev *dev       = fh->dev;
	struct video_device  *vdev = video_devdata(file);

	ioh_dbg(dev->video_in_dev, "%s", __func__);

	mutex_lock(&dev->mutex);

	ioh_video_in_close(dev);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
	dev->active_frm = NULL;
	dev->vidq_status = IOH_VIDEO_IN_VIDQ_IDLE;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	videobuf_stop(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	kfree(fh);

	dev->users--;

	ioh_info(dev->video_in_dev, "close called (dev=%s, users=%d)\n",
		video_device_node_name(vdev), dev->users);

	mutex_unlock(&dev->mutex);

	return 0;
}

static int ioh_vin_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ioh_vin_fh  *fh = file->private_data;
	struct ioh_vin_dev *dev = fh->dev;
	int ret;

	ioh_dbg(dev->video_in_dev, "mmap called, vma=0x%08lx\n",
			(unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	ioh_dbg(dev->video_in_dev, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static long ioh_vin_ioctl(struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct ioh_vin_dev *dev = video_drvdata(file);
	long ret;

	mutex_lock(&dev->mutex);

	ret = video_ioctl2(file, cmd, arg);

	mutex_unlock(&dev->mutex);

	return ret;
}

static const struct v4l2_file_operations ioh_vin_fops = {
	.owner		= THIS_MODULE,
	.open           = ioh_vin_v4l2_open,
	.release        = ioh_vin_v4l2_close,
	.read           = ioh_vin_v4l2_read,
	.poll		= ioh_vin_v4l2_poll,
	.unlocked_ioctl	= ioh_vin_ioctl,
	.mmap           = ioh_vin_mmap,
};

static const struct v4l2_ioctl_ops ioh_vin_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
	.vidioc_default     = ioh_video_in_ioctl,
};

static struct video_device ioh_vin_template = {
	.name		= "ioh_vin",
	.fops		= &ioh_vin_fops,
	.ioctl_ops	= &ioh_vin_ioctl_ops,
	.release	= video_device_release_empty,

	.tvnorms	= V4L2_STD_NTSC_M,
	.current_norm	= V4L2_STD_NTSC_M,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int __devexit ioh_vin_remove(struct ioh_vin_dev *dev)
{
	v4l2_info(&dev->v4l2_dev, "V4L2 device unregistering %s\n",
		video_device_node_name(dev->vfd));
	video_unregister_device(dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);

	ioh_dbg(dev->video_in_dev, "Function %s ended", __func__);

	return 0;
}

static int __devinit
ioh_video_in_subdev_open(struct ioh_vin_dev *dev)
{
	int ret = 0;
	struct i2c_adapter *adap;
	static unsigned short addrs[] = {
		OV7620_ADDR,
		OV9653_ADDR,
		ML86V76651_ADDR,
		NCM13J_ADDR,
		I2C_CLIENT_END
	};

	adap = i2c_get_adapter(0);
	dev->sensor = v4l2_i2c_new_subdev(&dev->v4l2_dev, adap,
						"ioh_i2c", 0, addrs);
	if (!dev->sensor) {
		ioh_err(dev->video_in_dev, "%s "
				"v4l2_i2c_new_subdev failed\n", __func__);
		ret = -ENODEV;
	}
	return ret;
}

static int __devinit
ioh_vin_dev_initialize(struct ioh_vin_dev *dev)
{
	struct video_device *vfd;
	int ret;

	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
			"%s", IOH_VIN_DRV_NAME);
	ret = v4l2_device_register(NULL, &dev->v4l2_dev);
	if (ret)
		goto ini_ret;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq_active);

	ret = ioh_video_in_subdev_open(dev);
	if (ret < 0)
		goto unreg_dev;

	ret = -ENOMEM;
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;

	*vfd = ioh_vin_template;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		goto rel_vdev;

	video_set_drvdata(vfd, dev);

	if (video_nr >= 0)
		video_nr++;

	dev->vfd = vfd;
	v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
		  video_device_node_name(vfd));

	return 0;

rel_vdev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
ini_ret:
	return ret;
}

static bool filter(struct dma_chan *chan, void *slave)
{
	struct pch_dma_slave *param = slave;

	if ((chan->chan_id == param->chan_id) && (param->dma_dev ==
						  chan->device->dev)) {
		chan->private = param;
		return true;
	} else {
		return false;
	}
}

static void ioh_request_dma(struct BT656_device *device)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct pci_dev *dma_dev;
	struct pch_dma_slave *param;
	struct BT656_device *priv = device;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	dma_dev = pci_get_bus_and_slot(2, PCI_DEVFN(0xa, 0)); /* Get DMA's dev
								information */
	/* Set Tx DMA */
	param = &priv->param_dma;
	param->dma_dev = &dma_dev->dev;
	param->width = PCH_DMA_WIDTH_4_BYTES;


#define IOH_DMA_TX_DATA_REQ0 6

	param->chan_id = IOH_DMA_TX_DATA_REQ0;

	param->tx_reg = (unsigned int)NULL;
	param->rx_reg = device->physical_address + IOH_VIDEO_IN_VDATA;

	chan = dma_request_channel(mask, filter, param);
	if (!chan) {
		ioh_err(device, "In %s -> dma_request_channel failed",
			__func__);
		return;
	}
	priv->chan_dma = chan;

	ioh_dbg(device, "Function %s ended", __func__);
	return;
}

static void ioh_free_dma(struct BT656_device *device)
{
	struct BT656_device *priv = device;
	int retval;

	if (priv->chan_dma) {

		retval = priv->chan_dma->device->device_control(priv->chan_dma,
							DMA_TERMINATE_ALL, 0);
		ioh_dbg(device, "In %s -> device_control returned(%d)",
			__func__, retval);

		dma_release_channel(priv->chan_dma);
		priv->chan_dma = NULL;
	}

	ioh_dbg(device, "Function %s ended", __func__);
	return;
}

/* ---- MAIN ---- */

static int ioh_video_in_open(struct ioh_vin_dev *dev)
{
	int retval;
	struct BT656_device *device;

	/* Attaining the device specific structure. */
	device = dev->video_in_dev;

	if (true == device->suspend_flag) {
		retval = -EAGAIN;
		ioh_err(device, "In %s -> The device is in suspend mode(%d)",
			__func__, retval);
		goto out;
	}

	/* Device already in use. */
	if (true == device->open_flag) {
		retval = -EBUSY;
		ioh_err(device, "In %s -> Device already opened(%d)",
			__func__, retval);
		goto out;
	}
	/* Device not in use. */

	spin_lock(&(device->dev_lock));

	iowrite32(ASSERT_RESET, device->base_address + IOH_VIDEO_IN_RESET);

	iowrite32(DE_ASSERT_RESET, device->base_address + IOH_VIDEO_IN_RESET);

	spin_unlock(&(device->dev_lock));

	/* Allocating the DMA channel. */
	device->chan_dma = NULL;
	ioh_request_dma(device);
	if (!device->chan_dma) {
		retval = -EINVAL;
		ioh_err(device, "In %s -> ioh_request_dma failed(%d)",
			__func__, retval);
		goto out;
	}

	/* Initializing the device variables and locks. */
#ifdef IOH_VIDEO_IN_DMA_CONTIG
	init_waitqueue_head(&(device->vsync_wait_queue));
	init_waitqueue_head(&(device->thread_sleep_queue));
	device->vsync_waiting_flag = false;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	init_waitqueue_head(&(device->read_wait_queue));
	init_waitqueue_head(&(device->thread_sleep_queue));

	spin_lock_init(&(device->read_buffer_lock));
	device->read_buffer = NULL;
	device->read_wait_flag = false;

	init_waitqueue_head(&(device->vsync_wait_queue));
	device->vsync_waiting_flag = false;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	/* Registering the interrupt handler. */
	retval = request_irq(device->irq,
				ioh_video_in_interrupt,
				IRQF_SHARED, IOH_VIN_DRV_NAME,
				(void *)dev);
	if (0 != retval) {
		ioh_err(device, "In %s -> request_irq failed(%d)",
			__func__, retval);
		goto out_free_dma;
	}

	device->thread_sleep_flag = true;
	device->thread_exit_flag = false;
	device->overflow_flag = false;
	device->dma_flag = -1;

	/* Creating the video capturing thread. */
	if (NULL ==
		(kthread_run(ioh_video_in_thread_fn, (void *)dev,
			IOH_VIDEOIN_THREAD_NAME))) {
		retval = -EIO;
		ioh_err(device, "In %s -> kthread_run failed(%d)",
			__func__, retval);
		goto out_free_irq;
	}

	msleep(SLEEP_DELAY);

	/* Device hardware initialization. */
	retval = ioh_video_in_init(device);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		ioh_err(device, "In %s -> ioh_video_in_init failed(%d)",
			__func__, retval);
		goto out_thread_exit;
	}

	/* setting the open flag. */
	device->open_flag = true;

	retval = IOH_VIDEOIN_SUCCESS;

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;

	/* If open failed. */
out_thread_exit:
	/* Stopping the video capturing thread. */
	device->thread_exit_flag = true;
	wake_up(&(device->thread_sleep_queue));

out_free_irq:
	/* Un-registering the interrupt handler. */
	free_irq(device->irq, (void *)dev);

out_free_dma:
	/* Freeing the DMA channel. */
	ioh_free_dma(device);

out:
	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;
}

static int ioh_video_in_close(struct ioh_vin_dev *dev)
{
	struct BT656_device *device = dev->video_in_dev;

	/* Attaining the device specific structure. */

	if (true == device->open_flag) {

		/* Stopping the kernel thread if running. */
		device->thread_exit_flag = true;
		wake_up(&(device->thread_sleep_queue));

		msleep(SLEEP_DELAY);

		/* De-initializing the device hardware. */
		ioh_video_in_exit(device);

		/* Un-registering the interrupt handler. */
		free_irq(device->irq, (void *)dev);

		/* Freeing the DMA channel. */
		ioh_free_dma(device);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
		/* Re-setting the read buffer pointer. */
		device->read_buffer = NULL;
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

		/* Re-setting the open flag variable. */
		device->open_flag = false;
	}

	ioh_dbg(device, "Function %s ended", __func__);

	return IOH_VIDEOIN_SUCCESS;
}

static long ioh_video_in_ioctl(struct file *p_file, void *priv,
		int command, void *param)
{
	int retval = -EAGAIN;
	struct ioh_vin_fh  *fh  = priv;
	struct ioh_vin_dev *dev = fh->dev;
	struct BT656_device *device = dev->video_in_dev;
	/* Attaining the device specific structure. */

	if (true == device->suspend_flag) {
		ioh_err(device, "In %s -> The device is in suspend mode",
			__func__);
		goto out;
	}

	/* if not suspend. */
	switch (command) {
	/* For setting input format. */
	case IOH_VIDEO_SET_INPUT_FORMAT:
		{
			struct ioh_video_in_input_format format;
			memcpy((void *)&format, (void *)param, sizeof(format));
			retval = ioh_video_in_set_input_format(device, format);
		}
		break;

	/* For getting input format. */
	case IOH_VIDEO_GET_INPUT_FORMAT:
		{
			struct ioh_video_in_input_format format;
			format = ioh_video_in_get_input_format(device);
			memcpy((void *)param, (void *)&format, sizeof(format));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the output format. */
	case IOH_VIDEO_SET_OUTPUT_FORMAT:
		{
			struct ioh_video_in_output_format format;
			memcpy((void *)&format, (void *)param, sizeof(format));
			retval = ioh_video_in_set_output_format(device,
								format);
		}
		break;

	/* For getting the output format. */
	case IOH_VIDEO_GET_OUTPUT_FORMAT:
		{
			struct ioh_video_in_output_format format;
			format = ioh_video_in_get_output_format(device);
			memcpy((void *)param, (void *)&format, sizeof(format));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the frame size. */
	case IOH_VIDEO_SET_SIZE:
		{
			struct ioh_video_in_frame_size frame_size;
			memcpy((void *)&frame_size, (void *)param,
							sizeof(frame_size));
			retval = ioh_video_in_set_size(device, frame_size);
		}
		break;

	/* For getting the frame size. */
	case IOH_VIDEO_GET_SIZE:
		{
			struct ioh_video_in_frame_size frame_size;
			frame_size = ioh_video_in_get_size(device);
			memcpy((void *)param, (void *)&frame_size,
							sizeof(frame_size));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the scan mode conversion method. */
	case IOH_VIDEO_SET_IP_TRANS:
		{
			enum ioh_video_in_scan_mode_method scan_mode;
			memcpy((void *)&scan_mode, (void *)param,
							sizeof(scan_mode));
			retval = ioh_video_in_set_ip_trans(device, scan_mode);
		}
		break;

	/* For getting the scan mode conversion method. */
	case IOH_VIDEO_GET_IP_TRANS:
		{
			enum ioh_video_in_scan_mode_method scan_mode;
			scan_mode = ioh_video_in_get_ip_trans(device);
			memcpy((void *)param, (void *)&scan_mode,
							sizeof(scan_mode));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the luminance level settings. */
	case IOH_VIDEO_SET_LUMINENCE_LEVEL:
		{
			struct ioh_video_in_luminance_settings
						luminance_settings;
			memcpy((void *)&luminance_settings, (void *)param,
						sizeof(luminance_settings));
			retval = ioh_video_in_set_luminance_level(device,
							luminance_settings);
		}
		break;

	/* For getting the luminance level settings. */
	case IOH_VIDEO_GET_LUMINENCE_LEVEL:
		{
			struct
			  ioh_video_in_luminance_settings luminance_settings;
			luminance_settings =
				ioh_video_in_get_luminance_level(device);
			memcpy((void *)param, (void *)&luminance_settings,
						sizeof(luminance_settings));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the RGB gain settings. */
	case IOH_VIDEO_SET_RGB_GAIN:
		{
			struct ioh_video_in_rgb_gain_settings rgb_settings;
			memcpy((void *)&rgb_settings, (void *)param,
							sizeof(rgb_settings));
			retval = ioh_video_in_set_rgb_gain(device,
								rgb_settings);
		}
		break;

	/* For getting the RGB gain settings. */
	case IOH_VIDEO_GET_RGB_GAIN:
		{
			struct ioh_video_in_rgb_gain_settings rgb_settings;
			rgb_settings = ioh_video_in_get_rgb_gain(device);
			memcpy((void *)param, (void *)&rgb_settings,
							sizeof(rgb_settings));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For initiating the video capturing process. */
	case IOH_VIDEO_CAP_START:
		retval = ioh_video_in_cap_start(device);
		break;

	/* For stopping the video capturing process. */
	case IOH_VIDEO_CAP_STOP:
		ioh_video_in_cap_stop(device);
		retval = IOH_VIDEOIN_SUCCESS;

		break;

	/* For setting the blanking signal timing settings. */
	case IOH_VIDEO_SET_BLANK_TIM:
		{
			struct ioh_video_in_blank_tim_settings blank_tim;
			memcpy((void *)&blank_tim, (void *)param,
							sizeof(blank_tim));
			retval = ioh_video_in_set_blank_tim(device, blank_tim);
		}
		break;

	/* For getting the blanking signal timing settings. */
	case IOH_VIDEO_GET_BLANK_TIM:
		{
			struct ioh_video_in_blank_tim_settings blank_tim;
			blank_tim = ioh_video_in_get_blank_tim(device);
			memcpy((void *)param, (void *)&blank_tim,
							sizeof(blank_tim));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the blue background mode. */
	case IOH_VIDEO_SET_BB:
		{
			enum ioh_video_in_bb_mode bb_mode;
			memcpy((void *)&bb_mode, (void *)param,
							sizeof(bb_mode));
			retval = ioh_video_in_set_bb(device, bb_mode);
		}
		break;

	/* For getting the blue background mode. */
	case IOH_VIDEO_GET_BB:
		{
			enum ioh_video_in_bb_mode bb_mode;
			bb_mode = ioh_video_in_get_bb(device);
			memcpy((void *)param, (void *)&bb_mode,
							sizeof(bb_mode));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For setting the color bar mode. */
	case IOH_VIDEO_SET_CB:
		{
			struct ioh_video_in_cb_settings cb_settings;
			memcpy((void *)&cb_settings, (void *)param,
							sizeof(cb_settings));
			retval = ioh_video_in_set_cb(device, cb_settings);
		}
		break;

	/* For getting the color bar mode. */
	case IOH_VIDEO_GET_CB:
		{
			struct ioh_video_in_cb_settings cb_settings;
			cb_settings = ioh_video_in_get_cb(device);
			memcpy((void *)param, (void *)&cb_settings,
							sizeof(cb_settings));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For getting the buffer size. */
	case IOH_VIDEO_GET_BUFFER_SIZE:
		{
			unsigned long buffer_size;
			buffer_size = ioh_video_in_get_buffer_size(device);
			memcpy((void *)param, (void *)&buffer_size,
							sizeof(buffer_size));
			retval = IOH_VIDEOIN_SUCCESS;
		}
		break;

	/* For getting the frame buffer info. */
	case IOH_VIDEO_GET_FRAME_BUFFERS:
#ifdef IOH_VIDEO_IN_DMA_CONTIG
		ioh_err(device, "In %s -> Invalid ioctl command", __func__);
		retval = -EINVAL;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
		{
			struct ioh_video_in_frame_buffers buffers;
			buffers = ioh_video_in_get_frame_buffers(device);
			memcpy((void *)param, (void *)&buffers,
							sizeof(buffers));
			retval = IOH_VIDEOIN_SUCCESS;
		}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
		break;

	/* For reading the frame buffer. */
	case IOH_VIDEO_READ_FRAME_BUFFER:
#ifdef IOH_VIDEO_IN_DMA_CONTIG
		ioh_err(device, "In %s -> Invalid ioctl command", __func__);
		retval = -EINVAL;
#else /* IOH_VIDEO_IN_DMA_CONTIG */
		{
			struct ioh_video_in_frame_buffer buffer;
			buffer = ioh_video_in_read_frame_buffer(device);
			memcpy((void *)param, (void *)&buffer, sizeof(buffer));
			retval = IOH_VIDEOIN_SUCCESS;
		}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */
		break;

	default:
		ioh_err(device, "In %s -> Invalid ioctl command", __func__);
		retval = -EINVAL;
		break;

	}
	/* End of switch */
out:
	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);
	return retval;
}

/* ---- PCI ---- */
#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
static int __devinit
ioh_video_in_set_frame_buffer(struct BT656_device *device)
{
	struct ioh_video_in_frame_buffer_info info;
	int retval;

	if (n_frame_buf < MIN_N_FRAME_BUF)
		n_frame_buf = MIN_N_FRAME_BUF;
	if (n_frame_buf > MAX_N_FRAME_BUF)
		n_frame_buf = MAX_N_FRAME_BUF;

	info.buffer_num = n_frame_buf;
	info.order = ALLOC_ORDER;

	retval = ioh_video_in_alloc_frame_buffer(device, info);

	if (retval != IOH_VIDEOIN_SUCCESS) {
		ioh_err(device, "In %s -> "
			"ioh_video_in_alloc_frame_buffer failed(%d)",
			__func__, retval);
		goto out;
	}

out:
	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);
	return retval;
}
static int __devexit ioh_video_in_clr_frame_buffer(struct BT656_device *device)
{
	struct ioh_video_in_frame_buffer_info info;
	int retval;

	info.buffer_num = n_frame_buf;
	info.order = ALLOC_ORDER;

	retval = ioh_video_in_free_frame_buffer(device, info);

	if (retval != IOH_VIDEOIN_SUCCESS) {
		ioh_err(device, "In %s -> "
			"ioh_video_in_free_frame_buffer failed(%d)",
			__func__, retval);
		goto out;
	}
out:
	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);
	return retval;
}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

static int __devinit
ioh_video_in_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int retval;
	void __iomem *base_address = NULL;
	u32 physical_address = 0;
	struct BT656_device *device = NULL;
	struct ioh_vin_dev *dev = NULL;

	/* Enabling the PCI device. */
	retval = pci_enable_device(pdev);
	if (0 != retval) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"pci_enable_device failed (%d)",
				__func__, retval);
		goto out;
	}

	/* Setting the device as the PCI master. */
	pci_set_master(pdev);

	/* Obtaining the physical address for further DMA use. */
	physical_address = pci_resource_start(pdev, 1);
	if (0 == physical_address) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"Cannot obtain the physical address",
				__func__);
		retval = -ENOMEM;
		goto out_pcidev;
	}

	/* Remapping the entire device regions to the kernel space memory. */
	base_address = pci_iomap(pdev, 1, 0);
	if (base_address == NULL) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"pci_iomap failed(0x%08x)",
				__func__, (u32) base_address);
		retval = -ENOMEM;
		goto out_pcidev;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"kzalloc(dev) failed", __func__);
		retval = -ENOMEM;
		goto out_iounmap;
	}

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"kzalloc(device) failed", __func__);
		retval = -ENOMEM;
		goto out_free_dev;
	}

	/* initialize locks */
	spin_lock_init(&dev->slock);
	mutex_init(&dev->mutex);

	mutex_lock(&dev->mutex);

	dev->video_in_dev = device;

	/* Filling in the device specific data structure fields. */
	spin_lock_init(&device->dev_lock);

	device->base_address = base_address;
	device->physical_address = physical_address;

	device->p_device = pdev;
	device->irq = pdev->irq;

	device->suspend_flag = false;
	device->open_flag = false;
	device->frame_skip_num = 0;

	/* Saving the device structure for further use. */
	pci_set_drvdata(pdev, dev);

	retval = ioh_vin_dev_initialize(dev);
	if (0 != retval) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"ioh_vin_dev_initialize failed(%d)",
				__func__, retval);
		mutex_unlock(&dev->mutex);
		goto out_free_device;
	}

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	retval = ioh_video_in_set_frame_buffer(device);
	if (IOH_VIDEOIN_SUCCESS != retval) {
		printk(KERN_ERR "ioh_video_in : In %s "
				"iioh_video_in_set_frame_buffer failed(%d)",
				__func__, retval);
		mutex_unlock(&dev->mutex);
		goto out_free_device;
	}
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	retval = IOH_VIDEOIN_SUCCESS;	/* Probe Successful */

	mutex_unlock(&dev->mutex);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);

	return retval;

	/* Releasing all the registered components
			if probe is not successful. */
out_free_device:
	/* Free the device memory */
	kfree(device);
out_free_dev:
	kfree(dev);
out_iounmap:
	/* Un-mapping the remapped memory regions. */
	pci_iounmap(pdev, base_address);
out_pcidev:
	/* Disabling the PCI device. */
	pci_disable_device(pdev);
out:
	printk(KERN_DEBUG "Function %s ended(%d)", __func__, retval);
	return retval;
}

static void __devexit ioh_video_in_pci_remove(struct pci_dev *pdev)
{
	struct ioh_vin_dev *dev;
	struct BT656_device *device;

	/* Obtaining the associated device structure. */
	dev = (struct ioh_vin_dev *)pci_get_drvdata(pdev);

	mutex_lock(&dev->mutex);

	device = dev->video_in_dev;

	ioh_vin_remove(dev);

#ifdef IOH_VIDEO_IN_DMA_CONTIG
#else /* IOH_VIDEO_IN_DMA_CONTIG */
	ioh_video_in_clr_frame_buffer(device);
#endif /* IOH_VIDEO_IN_DMA_CONTIG */

	/* Un-mapping the remapped device address. */
	pci_iounmap(pdev, device->base_address);

	/* Disabling the PCI device. */
	pci_disable_device(pdev);

	/* Resetting the driver data. */
	pci_set_drvdata(pdev, NULL);

	mutex_unlock(&dev->mutex);

	kfree(dev);

	kfree(device);
}

#ifdef CONFIG_PM
static int ioh_video_in_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int retval;
	struct ioh_vin_dev *dev;
	struct BT656_device *device;

	/* Obtaining the associated device structure. */
	dev = (struct ioh_vin_dev *)pci_get_drvdata(pdev);

	mutex_lock(&dev->mutex);

	device = dev->video_in_dev;

	/* Saving the current state */
	retval = pci_save_state(pdev);

	if (0 != retval) {
		ioh_err(device, "In %s -> Function "
			"pci_save_state failed(%d)", __func__, retval);
	} else {
		/* Setting flag for suspension. */
		device->suspend_flag = true;

		/* Disabling the interrupts if enabled. */
		if (true == device->open_flag) {
			/* Saving the current interrupt settings. */
			device->video_settings.current_interrupt_settings =
							device->intenb;

			/* Disabling all the interrupts. */
			spin_lock(&(device->dev_lock));
			device->intenb.drevsem = VSYNC_NOT_SYNC;
			device->intenb.dmarenb = DISABLE;
			device->intenb.ofintenb = DISABLE;
			device->intenb.hsintenb = DISABLE;
			device->intenb.vsintenb = DISABLE;
			write_intenb(device);

			iowrite32(ASSERT_RESET,
				device->base_address + IOH_VIDEO_IN_RESET);

			iowrite32(DE_ASSERT_RESET,
				device->base_address + IOH_VIDEO_IN_RESET);

			spin_unlock(&(device->dev_lock));
		}

		/* Disabling the wake up */
		pci_enable_wake(pdev, PCI_D3hot, 0);

		/* Putting the device to a new power state. */
		pci_set_power_state(pdev, pci_choose_state(pdev, state));

		/* Disabling the device. */
		pci_disable_device(pdev);

		retval = IOH_VIDEOIN_SUCCESS;
	}

	mutex_unlock(&dev->mutex);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);
	return retval;
}

static int ioh_video_in_pci_resume(struct pci_dev *pdev)
{
	int retval = 0;
	struct ioh_vin_dev *dev;
	struct BT656_device *device;

	/* Obtaining the associated device structure. */
	dev = (struct ioh_vin_dev *)pci_get_drvdata(pdev);

	mutex_lock(&dev->mutex);

	device = dev->video_in_dev;

	/* Setting the state to power on state. */
	pci_set_power_state(pdev, PCI_D0);

	/* Restoring the state. */
	pci_restore_state(pdev);

	/* Enabling the device. */
	retval = pci_enable_device(pdev);
	if (0 != retval) {
		ioh_err(device, "In %s -> Function "
			"pci_enable_device failed(%d)", __func__, retval);
	} else {
		/* Disabling wake up feature. */
		pci_enable_wake(pdev, PCI_D3hot, 0);

		/* Setting the device as PCI bus master. */
		pci_set_master(pdev);

		/* If device is in use. */
		if (true == device->open_flag) {
			spin_lock(&(device->dev_lock));

			iowrite32(ASSERT_RESET,
				device->base_address + IOH_VIDEO_IN_RESET);

			iowrite32(DE_ASSERT_RESET,
				device->base_address + IOH_VIDEO_IN_RESET);

			spin_unlock(&(device->dev_lock));

			(void)ioh_video_in_set_input_format(device,
						device->
						video_settings.
						current_input_format);
			(void)ioh_video_in_set_output_format(device,
						device->
						video_settings.
						current_output_format);
			(void)ioh_video_in_set_ip_trans(device,
						device->video_settings.
						current_scan_mode_method);
			(void)ioh_video_in_set_luminance_level(device,
						device->
						video_settings.
						current_luminance_settings);
			(void)ioh_video_in_set_rgb_gain(device,
						device->video_settings.
						current_rgb_gain_settings);
			(void)ioh_video_in_set_blank_tim(device,
						 device->video_settings.
						 current_blank_tim_settings);
			(void)ioh_video_in_set_bb(device,
						device->video_settings.
						current_bb_mode);
			(void)ioh_video_in_set_cb(device,
						device->video_settings.
						current_cb_settings);

			/* Re-enabling the interrupt settings. */
			spin_lock(&(device->dev_lock));
			device->intenb = device->video_settings.
						current_interrupt_settings;
			write_intenb(device);
			spin_unlock(&(device->dev_lock));
		}

		/* Updating the suspend flag. */
		device->suspend_flag = false;

		retval = IOH_VIDEOIN_SUCCESS;
	}

	mutex_unlock(&dev->mutex);

	ioh_dbg(device, "Function %s ended(%d)", __func__, retval);
	return retval;
}
#endif	/* CONFIG_PM */

static struct pci_device_id ioh_video_pcidev_id[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_IOH, PCI_DEVICE_ID_IVI_VIDEOIN)},
	{PCI_DEVICE(PCI_VENDOR_ID_IOH, PCI_DEVICE_ID_MP_VIDEOIN)},
	{}
};

MODULE_DEVICE_TABLE(pci, ioh_video_pcidev_id);

static struct pci_driver ioh_video_driver = {
	.name     = "ioh_video_in",
	.id_table = ioh_video_pcidev_id,
	.probe    = ioh_video_in_pci_probe,
	.remove   = __devexit_p(ioh_video_in_pci_remove),
#ifdef CONFIG_PM
	.suspend  = ioh_video_in_pci_suspend,
	.resume   = ioh_video_in_pci_resume,
#endif
};

static int __init ioh_video_in_pci_init(void)
{
	int retval;

	retval = pci_register_driver(&ioh_video_driver);

	if (0 != retval) {
		printk(KERN_ERR "ioh_video_in "
				"pci_register_driver failed (%d).", retval);
	}

	return retval;
}

static void __exit ioh_video_in_pci_exit(void)
{
	pci_unregister_driver(&ioh_video_driver);
}

module_init(ioh_video_in_pci_init);
module_exit(ioh_video_in_pci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IOH video-in PCI Driver");

