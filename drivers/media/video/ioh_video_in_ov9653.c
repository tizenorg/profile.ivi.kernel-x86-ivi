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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

MODULE_DESCRIPTION("IOH video-in driver for OmniVision ov9653 sensor.");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */
#define HDTV_WIDTH	1280
#define HDTV_HEIGHT	720

/* Registers */
#define REG_COM1	0x04	/* Control 1 */

#define REG_COM7	0x12	/* Control 7 */
#define   COM7_RESET	  0x80	  /* Register reset */
#define   COM7_FMT_HDTV	  0x00
#define   COM7_FMT_VGA	  0x40    /* VGA format */
#define	  COM7_FMT_CIF	  0x20	  /* CIF format */
#define   COM7_FMT_QVGA	  0x10	  /* QVGA format */
#define   COM7_FMT_QCIF	  0x08	  /* QCIF format */
#define REG_COM8	0x13	/* Control 8 */
#define   COM8_AEC	  0x01	  /* Auto exposure enable */


#define REG_PID		0x0a	/* Product ID MSB */
#define REG_VER		0x0b	/* Product ID LSB */

#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */

#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */

#define REG_HREF	0x32	/* HREF pieces */
#define REG_VREF	0x03	/* Pieces of GAIN, VSTART, VSTOP */

#define REG_AECHM	0xa1	/* AEC MSC 5bit */
#define REG_AECH	0x10	/* AEC value */


/*
 * Information we maintain about a known sensor.
 */
struct ov9653_format_struct;  /* coming later */
struct ov9653_info {
	struct v4l2_subdev sd;
	struct ov9653_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
};

static inline struct ov9653_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov9653_info, sd);
}



/*
 * The default register settings.
 */

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

static struct regval_list ov9653_default_regs[] = {
	{ REG_COM7, 0x00 },
	{ 0x11, 0x80 },
	{ 0x39, 0x43 },
	{ 0x38, 0x12 },
	{ 0x0e, 0x00 },
	{ 0x13, 0xc7 },
	{ 0x1e, 0x34 },
	{ 0x01, 0x80 },
	{ 0x02, 0x80 },
	{ 0x00, 0x00 },
	{ 0x10, 0xf0 },
	{ 0x1b, 0x00 },
	{ 0x16, 0x06 },
	{ 0x33, 0x10 },
	{ 0x34, 0xbf },
	{ 0xa8, 0x81 },
	{ 0x41, 0x10 },
	{ 0x96, 0x04 },
	{ 0x3d, 0x19 },
	{ 0x3a, 0x01 },
	{ 0x1b, 0x01 },
	{ 0x8e, 0x00 },
	{ 0x3c, 0x60 },
	{ 0x8f, 0xcf },
	{ 0x8b, 0x06 },
	{ 0x35, 0x91 },
	{ 0x94, 0x99 },
	{ 0x95, 0x99 },
	{ 0x40, 0xc1 },
	{ 0x29, 0x2f },
	{ 0x0f, 0x42 },
	{ 0x3a, 0x01 },
	{ 0xa5, 0x80 },
	{ 0x41, 0x00 },
	{ 0x13, 0xc5 },
	{ 0x3d, 0x92 },
	{ 0x69, 0x80 },
	{ 0x5c, 0x96 },
	{ 0x5d, 0x96 },
	{ 0x5e, 0x10 },
	{ 0x59, 0xeb },
	{ 0x5a, 0x9c },
	{ 0x5b, 0x55 },
	{ 0x43, 0xf0 },
	{ 0x44, 0x10 },
	{ 0x45, 0x55 },
	{ 0x46, 0x86 },
	{ 0x47, 0x64 },
	{ 0x48, 0x86 },
	{ 0x5f, 0xf0 },
	{ 0x60, 0x8c },
	{ 0x61, 0x20 },
	{ 0xa5, 0xd9 },
	{ 0xa4, 0x74 },
	{ 0x8d, 0x02 },
	{ 0x13, 0xc7 },
	{ 0x4f, 0x46 },
	{ 0x50, 0x36 },
	{ 0x51, 0x0f },
	{ 0x52, 0x17 },
	{ 0x53, 0x7f },
	{ 0x54, 0x96 },
	{ 0x41, 0x32 },
	{ 0x8c, 0x23 },
	{ 0x3d, 0x92 },
	{ 0x3e, 0x02 },
	{ 0xa9, 0x97 },
	{ 0x3a, 0x00 },
	{ 0x8f, 0xcf },
	{ 0x90, 0x00 },
	{ 0x91, 0x00 },
	{ 0x9f, 0x00 },
	{ 0xa0, 0x00 },
	{ 0x3a, 0x0d },
	{ 0x94, 0x88 },
	{ 0x95, 0x88 },
	{ 0x24, 0x68 },
	{ 0x25, 0x5c },
	{ 0x26, 0xc3 },
	{ 0x3b, 0x19 },
	{ 0x14, 0x2a },
	{ 0x3f, 0xa6 },
	{ 0x6a, 0x21 },
	{ 0xff, 0xff },		/* end */
};


static struct regval_list ov9653_fmt_yuv422[] = {
	{ REG_COM7, 0x00 },
	{ 0x11, 0x80 },
	{ 0x39, 0x43 },
	{ 0x38, 0x12 },
	{ 0x0e, 0x00 },
	{ 0x13, 0xc7 },
	{ 0x1e, 0x34 },
	{ 0x01, 0x80 },
	{ 0x02, 0x80 },
	{ 0x00, 0x00 },
	{ 0x10, 0xf0 },
	{ 0x1b, 0x00 },
	{ 0x16, 0x06 },
	{ 0x33, 0x10 },
	{ 0x34, 0xbf },
	{ 0xa8, 0x81 },
	{ 0x41, 0x10 },
	{ 0x96, 0x04 },
	{ 0x3d, 0x19 },
	{ 0x3a, 0x01 },
	{ 0x1b, 0x01 },
	{ 0x8e, 0x00 },
	{ 0x3c, 0x60 },
	{ 0x8f, 0xcf },
	{ 0x8b, 0x06 },
	{ 0x35, 0x91 },
	{ 0x94, 0x99 },
	{ 0x95, 0x99 },
	{ 0x40, 0xc1 },
	{ 0x29, 0x2f },
	{ 0x0f, 0x42 },
	{ 0x3a, 0x01 },
	{ 0xa5, 0x80 },
	{ 0x41, 0x00 },
	{ 0x13, 0xc5 },
	{ 0x3d, 0x92 },
	{ 0x69, 0x80 },
	{ 0x5c, 0x96 },
	{ 0x5d, 0x96 },
	{ 0x5e, 0x10 },
	{ 0x59, 0xeb },
	{ 0x5a, 0x9c },
	{ 0x5b, 0x55 },
	{ 0x43, 0xf0 },
	{ 0x44, 0x10 },
	{ 0x45, 0x55 },
	{ 0x46, 0x86 },
	{ 0x47, 0x64 },
	{ 0x48, 0x86 },
	{ 0x5f, 0xf0 },
	{ 0x60, 0x8c },
	{ 0x61, 0x20 },
	{ 0xa5, 0xd9 },
	{ 0xa4, 0x74 },
	{ 0x8d, 0x02 },
	{ 0x13, 0xc7 },
	{ 0x4f, 0x46 },
	{ 0x50, 0x36 },
	{ 0x51, 0x0f },
	{ 0x52, 0x17 },
	{ 0x53, 0x7f },
	{ 0x54, 0x96 },
	{ 0x41, 0x32 },
	{ 0x8c, 0x23 },
	{ 0x3d, 0x92 },
	{ 0x3e, 0x02 },
	{ 0xa9, 0x97 },
	{ 0x3a, 0x00 },
	{ 0x8f, 0xcf },
	{ 0x90, 0x00 },
	{ 0x91, 0x00 },
	{ 0x9f, 0x00 },
	{ 0xa0, 0x00 },
	{ 0x3a, 0x0d },
	{ 0x94, 0x88 },
	{ 0x95, 0x88 },
	{ 0x24, 0x68 },
	{ 0x25, 0x5c },
	{ 0x26, 0xc3 },
	{ 0x3b, 0x19 },
	{ 0x14, 0x2a },
	{ 0x3f, 0xa6 },
	{ 0x6a, 0x21 },
	{ 0xff, 0xff },		/* end */
};


/*
 * Low-level register I/O.
 */

#if 1
static int ioh_video_in_read_value(struct i2c_client *client, u8 reg, u8 *val)
{
	u8 data = 0;

	client->flags = 0;
	data = 0;
	if (i2c_master_send(client, &data, 1) != 1)
		goto err;
	msleep(2);

	client->flags = 0;
	data = reg;
	if (i2c_master_send(client, &data, 1) != 1)
		goto err;
	msleep(2);

	if (i2c_master_recv(client, &data, 1) != 1)
		goto err;
	msleep(2);

	*val = data;

	v4l_dbg(1, debug, client, "Function %s A(0x%02X) --> 0x%02X end.",
						__func__, reg, *val);

	return 0;

err:
	v4l_err(client, "Function %s A(0x%02X) 0x%02X read error failed.",
						__func__, reg, *val);

	return -EINVAL;
}

static int ioh_video_in_write_value(struct i2c_client *client, u8 reg, u8 val)
{
	u8 data = 0;
	unsigned char data2[2] = { reg, val };

	client->flags = 0;
	data = 0;
	if (i2c_master_send(client, &data, 1) != 1)
		goto err;
	msleep(2);

	if (i2c_master_send(client, data2, 2) != 2)
		goto err;
	msleep(2);

	v4l_dbg(1, debug, client, "Function %s A(0x%02X) <-- 0x%02X end.",
						__func__, reg, val);

	return 0;

err:
	v4l_err(client, "Function %s A(0x%02X) <-- 0x%02X write error failed.",
						__func__, reg, val);

	return -EINVAL;
}

#endif

static int ov9653_read(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

#if 0
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret >= 0) {
		*value = (unsigned char)ret;
		ret = 0;
	}
#else
	ret = ioh_video_in_read_value(client, reg, value);
#endif
	return ret;
}
static int ov9653_write(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
#if 0
	int ret = i2c_smbus_write_byte_data(client, reg, value);
#else
	int ret = ioh_video_in_write_value(client, reg, value);
#endif

	if (reg == REG_COM7 && (value & COM7_RESET))
		msleep(2);  /* Wait for reset to run */

	return ret;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ov9653_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	while (vals->reg_num != 0xff || vals->value != 0xff) {
		int ret = ov9653_write(sd, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */
static int ov9653_reset(struct v4l2_subdev *sd, u32 val)
{
	ov9653_write(sd, REG_COM7, COM7_RESET);
	msleep(1);
	return 0;
}


static int ov9653_init(struct v4l2_subdev *sd, u32 val)
{
	return ov9653_write_array(sd, ov9653_default_regs);
}


static int ov9653_detect(struct v4l2_subdev *sd)
{
	unsigned char v;
	int ret;

	ret = ov9653_init(sd, 0);
	if (ret < 0)
		return ret;
	ret = ov9653_read(sd, REG_MIDH, &v);
	if (ret < 0)
		return ret;
	if (v != 0x7f) /* OV manuf. id. */
		return -ENODEV;
	ret = ov9653_read(sd, REG_MIDL, &v);
	if (ret < 0)
		return ret;
	if (v != 0xa2)
		return -ENODEV;

	ret = ov9653_read(sd, REG_PID, &v);
	if (ret < 0)
		return ret;
	if (v != 0x96)  /* PID + VER = 0x96 / 0x52 */
		return -ENODEV;
	ret = ov9653_read(sd, REG_VER, &v);
	if (ret < 0)
		return ret;
	if (v != 0x52)  /* PID + VER = 0x96 / 0x52 */
		return -ENODEV;
	return 0;
}

static struct ov9653_format_struct {
	enum v4l2_mbus_pixelcode mbus_code;
	enum v4l2_colorspace colorspace;
	struct regval_list *regs;
} ov9653_formats[] = {
	{
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.regs		= ov9653_fmt_yuv422,
	},
};
#define N_OV9653_FMTS ARRAY_SIZE(ov9653_formats)

static struct regval_list ov9653_hdtv_regs[] = {
	{ 0xff, 0xff },
};


static struct ov9653_win_size {
	int	width;
	int	height;
	unsigned char com7_bit;
	int	hstart;
	int	hstop;
	int	vstart;
	int	vstop;
	struct regval_list *regs; /* Regs to tweak */
/* h/vref stuff */
} ov9653_win_sizes[] = {
	/* HDTV */
	{
		.width		= HDTV_WIDTH,
		.height		= HDTV_HEIGHT,
		.com7_bit	= COM7_FMT_HDTV,
		.hstart		=  238,
		.hstop		= 1518,
		.vstart		=  130,
		.vstop		=  850,
		.regs		= ov9653_hdtv_regs,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(ov9653_win_sizes))


/*
 * Store a set of start/stop values into the camera.
 */
static int ov9653_set_hw(struct v4l2_subdev *sd, int hstart, int hstop,
		int vstart, int vstop)
{
	int ret;
	unsigned char v;

	ret =  ov9653_write(sd, REG_HSTART, (hstart >> 3) & 0xff);
	ret += ov9653_write(sd, REG_HSTOP, (hstop >> 3) & 0xff);
	ret += ov9653_read(sd, REG_HREF, &v);
	v = (v & 0xc0) | ((hstop & 0x7) << 3) | (hstart & 0x7);
	msleep(10);
	ret += ov9653_write(sd, REG_HREF, v);

	ret += ov9653_write(sd, REG_VSTART, (vstart >> 3) & 0xff);
	ret += ov9653_write(sd, REG_VSTOP, (vstop >> 3) & 0xff);
	ret += ov9653_read(sd, REG_VREF, &v);
	v = (v & 0xc0) | ((vstop & 0x7) << 3) | (vstart & 0x7);
	msleep(10);
	ret += ov9653_write(sd, REG_VREF, v);
	return ret;
}

static int ov9653_try_fmt_internal(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt,
		struct ov9653_format_struct **ret_fmt,
		struct ov9653_win_size **ret_wsize)
{
	int index;
	struct ov9653_win_size *wsize;

	for (index = 0; index < N_OV9653_FMTS; index++)
		if (ov9653_formats[index].mbus_code == fmt->code)
			break;
	if (index >= N_OV9653_FMTS) {
		/* default to first format */
		index = 0;
		fmt->code = ov9653_formats[0].mbus_code;
	}
	if (ret_fmt != NULL)
		*ret_fmt = ov9653_formats + index;

	fmt->field = V4L2_FIELD_NONE;

	for (wsize = ov9653_win_sizes;
			wsize < ov9653_win_sizes + N_WIN_SIZES; wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)
			break;
	if (wsize >= ov9653_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->colorspace = ov9653_formats[index].colorspace;

	return 0;
}

static int ov9653_try_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	return ov9653_try_fmt_internal(sd, fmt, NULL, NULL);
}

/*
 * Set a format.
 */
static int ov9653_s_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	struct ov9653_format_struct *ovfmt;
	struct ov9653_win_size *wsize;
	struct ov9653_info *info = to_state(sd);
	unsigned char com7;

	ret = ov9653_try_fmt_internal(sd, fmt, &ovfmt, &wsize);
	if (ret)
		return ret;

	/* Reset */
	ov9653_reset(sd, 0);

	com7 = ovfmt->regs[0].value;
	com7 |= wsize->com7_bit;
	ov9653_write(sd, REG_COM7, com7);
	/*
	 * Now write the rest of the array.  Also store start/stops
	 */
	ov9653_write_array(sd, ovfmt->regs + 1);
	ov9653_set_hw(sd, wsize->hstart, wsize->hstop, wsize->vstart,
			wsize->vstop);
	ret = 0;
	if (wsize->regs)
		ret = ov9653_write_array(sd, wsize->regs);
	info->fmt = ovfmt;

	return ret;
}


/*
 * Code for dealing with controls.
 */

static int ov9653_s_brightness(struct v4l2_subdev *sd, int value)
{
	unsigned char com8 = 0, v;
	int ret;

	ov9653_read(sd, REG_COM8, &com8);
	com8 &= ~COM8_AEC;
	ov9653_write(sd, REG_COM8, com8);

	ret = ov9653_write(sd, REG_AECH, (value >> 2) & 0xff);
	ret += ov9653_write(sd, REG_AECHM, (value >> 10) & 0x3f);
	ret += ov9653_read(sd, REG_COM1, &v);
	v = (v & 0xfc) | (value & 0x03);
	msleep(10);
	ret += ov9653_write(sd, REG_COM1, v);
	return ret;
}

static int ov9653_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	unsigned char v = 0;
	int val = 0;
	int ret;

	ret = ov9653_read(sd, REG_COM1, &v);
	val = v & 0x03;
	ret += ov9653_read(sd, REG_AECH, &v);
	val |= ((v & 0xff) << 2);
	ret += ov9653_read(sd, REG_AECHM, &v);
	val |= ((v & 0x3f) << 10);

	*value = val;
	return ret;
}



static int ov9653_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	/* Fill in min, max, step and default value for these controls. */
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 65535, 1, 256);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ov9653_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ov9653_g_brightness(sd, &ctrl->value);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ov9653_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ov9653_s_brightness(sd, ctrl->value);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ov9653_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV7670, 0);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov9653_g_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char val = 0;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = ov9653_read(sd, reg->reg & 0xff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int ov9653_s_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ov9653_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops ov9653_core_ops = {
	.g_chip_ident = ov9653_g_chip_ident,
	.g_ctrl = ov9653_g_ctrl,
	.s_ctrl = ov9653_s_ctrl,
	.queryctrl = ov9653_queryctrl,
	.reset = ov9653_reset,
	.init = ov9653_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov9653_g_register,
	.s_register = ov9653_s_register,
#endif
};

static const struct v4l2_subdev_video_ops ov9653_video_ops = {
	.try_mbus_fmt = ov9653_try_mbus_fmt,
	.s_mbus_fmt = ov9653_s_mbus_fmt,
};

static const struct v4l2_subdev_ops ov9653_ops = {
	.core = &ov9653_core_ops,
	.video = &ov9653_video_ops,
};

/* ----------------------------------------------------------------------- */

static int ov9653_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct ov9653_info *info;
	int ret;

	info = kzalloc(sizeof(struct ov9653_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &ov9653_ops);

	/* Make sure it's an ov9653 */
	ret = ov9653_detect(sd);
	if (ret) {
		v4l_dbg(1, debug, client,
			"chip found @ 0x%x (%s) is not an ov9653 chip.\n",
			client->addr << 1, client->adapter->name);
		kfree(info);
		return ret;
	}
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	info->fmt = &ov9653_formats[0];
	info->sat = 128;	/* Review this */

	return 0;
}


static int ov9653_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id ov9653_id[] = {
	{ "ioh_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov9653_id);

static struct i2c_driver ov9653_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ioh_i2c",
	},
	.probe = ov9653_probe,
	.remove = ov9653_remove,
	.id_table = ov9653_id,
};

static __init int init_ov9653(void)
{
	return i2c_add_driver(&ov9653_driver);
}

static __exit void exit_ov9653(void)
{
	i2c_del_driver(&ov9653_driver);
}

module_init(init_ov9653);
module_exit(exit_ov9653);

