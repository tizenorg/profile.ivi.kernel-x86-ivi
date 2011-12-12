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

MODULE_DESCRIPTION("IOH video-in driver for OKI SEMICONDUCTOR ML86V76651.");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */
#define VGA_WIDTH	640
#define VGA_HEIGHT	480

/* Registers */
#define REG_SSEPL	0x37	/* Sync Separation Level Contrl */

#define REG_COM7	0x12	/* Control 7 */
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


#define REG_HREF	0x32	/* HREF pieces */
#define REG_VREF	0x03	/* Pieces of GAIN, VSTART, VSTOP */

#define REG_AECHM	0xa1	/* AEC MSC 5bit */
#define REG_AECH	0x10	/* AEC value */


/*
 * Information we maintain about a known sensor.
 */
struct ml86v76651_format_struct;  /* coming later */
struct ml86v76651_info {
	struct v4l2_subdev sd;
	struct ml86v76651_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
};

static inline struct ml86v76651_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ml86v76651_info, sd);
}



/*
 * The default register settings.
 */

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

static struct regval_list ml86v76651_default_regs[] = {
#ifdef IOH_VIDEO_IN_ML86V76653
#else
	{0x71, 0x00},		/* for Device Workaround */
	{0x71, 0x80},		/* 32MHz SQ_PIXEL */
#endif
	{0x00, 0x02},		/* 32MHz SQ_PIXEL */
	{0x01, 0x00},		/* BT656 */
	{0x51, 0x80},		/* Normal out */
	{0x50, 0x89},		/* Normal out */
	{0x68, 0xe0},		/* analog control */
	{0x78, 0x22},		/* status1 is ODD/EVEN */
#ifdef IOH_VIDEO_IN_ML86V76653
#else
	{0x6f, 0x80},		/* Others */
#endif
	{0xff, 0xff},		/* end */
};


static struct regval_list ml86v76651_fmt_yuv422[] = {
#ifdef IOH_VIDEO_IN_ML86V76653
#else
	{ 0x71, 0x00 },		/* for Device Workaround */
	{ 0x71, 0x80 },		/* 32MHz SQ_PIXEL */
#endif
	{ 0x00, 0x02 },		/* 32MHz SQ_PIXEL */
	{ 0x01, 0x00 },		/* BT656 */
	{ 0x51, 0x80 },		/* Normal out */
	{ 0x50, 0x89 },		/* Normal out */
	{ 0x68, 0xe0 },		/* analog control */
	{ 0x78, 0x22 },		/* status1 is ODD/EVEN */
#ifdef IOH_VIDEO_IN_ML86V76653
#else
	{ 0x6f, 0x80 },		/* Others */
#endif
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

static int ml86v76651_read(struct v4l2_subdev *sd, unsigned char reg,
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
static int ml86v76651_write(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
#if 0
	int ret = i2c_smbus_write_byte_data(client, reg, value);
#else
	int ret = ioh_video_in_write_value(client, reg, value);
#endif

	/* This sensor doesn't have reset register... */

	return ret;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ml86v76651_write_array(struct v4l2_subdev *sd,
						struct regval_list *vals)
{
	while (vals->reg_num != 0xff || vals->value != 0xff) {
		int ret = ml86v76651_write(sd, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */
static int ml86v76651_reset(struct v4l2_subdev *sd, u32 val)
{
	/* This sensor doesn't have reset register... */
	return 0;
}


static int ml86v76651_init(struct v4l2_subdev *sd, u32 val)
{
	return ml86v76651_write_array(sd, ml86v76651_default_regs);
}


static int ml86v76651_detect(struct v4l2_subdev *sd)
{
	int ret;

	ret = ml86v76651_init(sd, 0);
	if (ret < 0)
		return ret;

	/* This sensor doesn't have id register... */

	return 0;
}

static struct ml86v76651_format_struct {
	enum v4l2_mbus_pixelcode mbus_code;
	enum v4l2_colorspace colorspace;
	struct regval_list *regs;
} ml86v76651_formats[] = {
	{
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.regs		= ml86v76651_fmt_yuv422,
	},
};
#define N_ML86V76651_FMTS ARRAY_SIZE(ml86v76651_formats)

static struct regval_list ml86v76651_vga_regs[] = {
	{ 0xff, 0xff },
};


static struct ml86v76651_win_size {
	int	width;
	int	height;
	struct regval_list *regs; /* Regs to tweak */
/* h/vref stuff */
} ml86v76651_win_sizes[] = {
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
		.regs		= ml86v76651_vga_regs,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(ml86v76651_win_sizes))

static int ml86v76651_try_fmt_internal(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt,
		struct ml86v76651_format_struct **ret_fmt,
		struct ml86v76651_win_size **ret_wsize)
{
	int index;
	struct ml86v76651_win_size *wsize;

	for (index = 0; index < N_ML86V76651_FMTS; index++)
		if (ml86v76651_formats[index].mbus_code == fmt->code)
			break;
	if (index >= N_ML86V76651_FMTS) {
		/* default to first format */
		index = 0;
		fmt->code = ml86v76651_formats[0].mbus_code;
	}
	if (ret_fmt != NULL)
		*ret_fmt = ml86v76651_formats + index;

	fmt->field = V4L2_FIELD_NONE;

	for (wsize = ml86v76651_win_sizes;
			wsize < ml86v76651_win_sizes + N_WIN_SIZES; wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)
			break;
	if (wsize >= ml86v76651_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->colorspace = ml86v76651_formats[index].colorspace;

	return 0;
}

static int ml86v76651_try_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	return ml86v76651_try_fmt_internal(sd, fmt, NULL, NULL);
}

/*
 * Set a format.
 */
static int ml86v76651_s_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	struct ml86v76651_format_struct *ovfmt;
	struct ml86v76651_win_size *wsize;
	struct ml86v76651_info *info = to_state(sd);

	ret = ml86v76651_try_fmt_internal(sd, fmt, &ovfmt, &wsize);
	if (ret)
		return ret;

	/* Reset */
	ml86v76651_reset(sd, 0);

	/*
	 * Now write the rest of the array.
	 */
	ml86v76651_write_array(sd, ovfmt->regs);
	ret = 0;
	if (wsize->regs)
		ret = ml86v76651_write_array(sd, wsize->regs);
	info->fmt = ovfmt;

	return ret;
}


/*
 * Code for dealing with controls.
 */

static unsigned char ml86v76651_sm_to_abs(unsigned char v)
{
	if ((v & 0x40) == 0)
		return 63 - (v & 0x3f);
	return 127 - (v & 0x3f);
}


static unsigned char ml86v76651_abs_to_sm(unsigned char v)
{
	if (v > 63)
		return ((63 - v) | 0x40) & 0x7f;
	return (63 - v) & 0x7f;
}

static int ml86v76651_s_brightness(struct v4l2_subdev *sd, int value)
{
	unsigned char v;
	int ret;

	v = ml86v76651_abs_to_sm(value);

	ret = ml86v76651_write(sd, REG_SSEPL, v);

	return ret;
}

static int ml86v76651_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	unsigned char v = 0;
	int ret;

	ret = ml86v76651_read(sd, REG_SSEPL, &v);

	*value = ml86v76651_sm_to_abs(v);

	return ret;
}



static int ml86v76651_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	/* Fill in min, max, step and default value for these controls. */
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 127, 1, 63);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ml86v76651_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ml86v76651_g_brightness(sd, &ctrl->value);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ml86v76651_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ml86v76651_s_brightness(sd, ctrl->value);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ml86v76651_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV7670, 0);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ml86v76651_g_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char val = 0;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = ml86v76651_read(sd, reg->reg & 0xff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int ml86v76651_s_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ml86v76651_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops ml86v76651_core_ops = {
	.g_chip_ident = ml86v76651_g_chip_ident,
	.g_ctrl = ml86v76651_g_ctrl,
	.s_ctrl = ml86v76651_s_ctrl,
	.queryctrl = ml86v76651_queryctrl,
	.reset = ml86v76651_reset,
	.init = ml86v76651_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ml86v76651_g_register,
	.s_register = ml86v76651_s_register,
#endif
};

static const struct v4l2_subdev_video_ops ml86v76651_video_ops = {
	.try_mbus_fmt = ml86v76651_try_mbus_fmt,
	.s_mbus_fmt = ml86v76651_s_mbus_fmt,
};

static const struct v4l2_subdev_ops ml86v76651_ops = {
	.core = &ml86v76651_core_ops,
	.video = &ml86v76651_video_ops,
};

/* ----------------------------------------------------------------------- */

static int ml86v76651_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct ml86v76651_info *info;
	int ret;

	info = kzalloc(sizeof(struct ml86v76651_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &ml86v76651_ops);

	/* Make sure it's an ml86v76651 */
	ret = ml86v76651_detect(sd);
	if (ret) {
		v4l_dbg(1, debug, client,
			"chip found @ 0x%x (%s) is not an ml86v76651 chip.\n",
			client->addr << 1, client->adapter->name);
		kfree(info);
		return ret;
	}
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	info->fmt = &ml86v76651_formats[0];
	info->sat = 128;	/* Review this */

	return 0;
}


static int ml86v76651_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id ml86v76651_id[] = {
	{ "ioh_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ml86v76651_id);

static struct i2c_driver ml86v76651_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ioh_i2c",
	},
	.probe = ml86v76651_probe,
	.remove = ml86v76651_remove,
	.id_table = ml86v76651_id,
};

static __init int init_ml86v76651(void)
{
	return i2c_add_driver(&ml86v76651_driver);
}

static __exit void exit_ml86v76651(void)
{
	i2c_del_driver(&ml86v76651_driver);
}

module_init(init_ml86v76651);
module_exit(exit_ml86v76651);


