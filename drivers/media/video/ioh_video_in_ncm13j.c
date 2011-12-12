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

MODULE_DESCRIPTION("IOH video-in driver for NIPPON CEMI-CON NCM13J.");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */
#define QVGA_WIDTH	320
#define QVGA_HEIGHT	240
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
#define HDTV_WIDTH	1280
#define HDTV_HEIGHT	720
#define SXGA_WIDTH	1280
#define SXGA_HEIGHT	1024

/* Registers */

#define REG_RESET		0x000d
#define   ASSERT_RESET		0x0023
#define   DEASSERT_RESET	0x0008

#define REG_UNIQUE_ID		0x0000	/* Manuf. ID address */
#define   REG_UNIQUE_VAL	0x148c	/* Manuf. ID value */

/*
 * Information we maintain about a known sensor.
 */
struct ncm13j_format_struct;  /* coming later */
struct ncm13j_info {
	struct v4l2_subdev sd;
	struct ncm13j_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
};

static inline struct ncm13j_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ncm13j_info, sd);
}



/*
 * The default register settings.
 */

struct regval_list {
	u16 reg_num;
	u16 value;
};

static struct regval_list ncm13j_default_regs[] = {
	{ 0x0066, 0x1b01},	/* PLL M=27 N=1 */
	{ 0x0067, 0x0503},	/* PLL P=3 */
	{ 0x0065, 0xa000},	/* PLL power up */
	{ 0x0065, 0x2000},	/* PLL enable */
	{ 0xffff, 0xffff},	/* end */
};


static struct regval_list ncm13j_fmt_yuv422[] = {
	{ 0x013a, 0x0200},	/* Output Format Control A */
	{ 0x019b, 0x0200},	/* Output Format Control B */
	{ 0xffff, 0xffff},	/* end */
};



/*
 * Low-level register I/O.
 */

static int ioh_video_in_read_value(struct i2c_client *client, u8 reg, u8 *val)
{
	u8 data = 0;

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
	unsigned char data2[2] = { reg, val };

	client->flags = 0;

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

static int ncm13j_read(struct v4l2_subdev *sd, u16 reg, u16 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	unsigned char reg8;
	unsigned char val8, valh, vall;

	/* Page_h setting */
	reg8 = 0xf0;
	val8 = 0x00;
	ret = ioh_video_in_write_value(client, reg8, val8);

	/* Page_l setting */
	reg8 = 0xf1;
	val8 = (0x0700 & reg) >> 8;
	ret = ioh_video_in_write_value(client, reg8, val8);

	/* MSB8 Read */
	reg8 = (0x00ff & reg);
	ret = ioh_video_in_read_value(client, reg8, &valh);

	/* LSB8 Read */
	reg8 = 0xf1;
	ret = ioh_video_in_read_value(client, reg8, &vall);

	*value = ((0x00ff & valh) << 8) | (0x00ff & vall);

	return ret;
}
static int ncm13j_write(struct v4l2_subdev *sd, u16 reg, u16 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	unsigned char reg8;
	unsigned char val8;

	/* Page_h Write */
	reg8 = 0xf0;
	val8 = 0x00;
	ret = ioh_video_in_write_value(client, reg8, val8);

	/* Page_l Write */
	reg8 = 0xf1;
	val8 = (0x0700 & reg) >> 8;
	ret = ioh_video_in_write_value(client, reg8, val8);

	/* MSB8 Write */
	reg8 = (0x00ff & reg);
	val8 = (0xff00 & value) >> 8;
	ret = ioh_video_in_write_value(client, reg8, val8);

	/* LSB8 Write */
	reg8 = 0xf1;
	val8 = (0x00ff & value);
	ret = ioh_video_in_write_value(client, reg8, val8);

	return ret;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ncm13j_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	while (vals->reg_num != 0xffff || vals->value != 0xffff) {
		int ret = ncm13j_write(sd, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */
static int ncm13j_reset(struct v4l2_subdev *sd, u32 val)
{
	ncm13j_write(sd, REG_RESET, ASSERT_RESET);
	msleep(1);
	ncm13j_write(sd, REG_RESET, DEASSERT_RESET);
	msleep(1);
	return 0;
}


static int ncm13j_init(struct v4l2_subdev *sd, u32 val)
{
	return ncm13j_write_array(sd, ncm13j_default_regs);
}


static int ncm13j_detect(struct v4l2_subdev *sd)
{
	u16 v;
	int ret;

	ret = ncm13j_reset(sd, 0);
	if (ret < 0)
		return ret;
	ret = ncm13j_read(sd, REG_UNIQUE_ID, &v);
	if (ret < 0)
		return ret;
	if (v != REG_UNIQUE_VAL) /* id. */
		return -ENODEV;
	ret = ncm13j_init(sd, 0);
	if (ret < 0)
		return ret;
	return 0;
}

static struct ncm13j_format_struct {
	enum v4l2_mbus_pixelcode mbus_code;
	enum v4l2_colorspace colorspace;
	struct regval_list *regs;
} ncm13j_formats[] = {
	{
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.regs		= ncm13j_fmt_yuv422,
	},
};
#define N_NCM13J_FMTS ARRAY_SIZE(ncm13j_formats)

static struct regval_list ncm13j_qvga_regs[] = {
	{ 0x01a7, QVGA_WIDTH},	/* Horizontal Output Size A = 320 */
	{ 0x01aa, QVGA_HEIGHT},	/* Vertical Output Size A = 240 */
	/* { 0x01a6, QVGA_WIDTH},  Horizontal Zoom = 320 */
	/* { 0x01a9, QVGA_HEIGHT}, Vertical Zoom = 240 */
	{ 0x01ae, 0x0c09},	/* Reducer Zoom Step Size */
	{ 0x00c8, 0x0000},	/* Context A */
	{ 0x02c8, 0x0000},	/* Context A */
	{ 0xffff, 0xffff},	/* end */
};

static struct regval_list ncm13j_vga_regs[] = {
	{ 0x01a7, VGA_WIDTH},	/* Horizontal Output Size A = 640 */
	{ 0x01aa, VGA_HEIGHT},	/* Vertical Output Size A = 480 */
	/* { 0x01a6, VGA_WIDTH},   Horizontal Zoom = 640 */
	/* { 0x01a9, VGA_HEIGHT},  Vertical Zoom = 480 */
	{ 0x01ae, 0x0c09},	/* Reducer Zoom Step Size */
	{ 0x00c8, 0x0000},	/* Context A */
	{ 0x02c8, 0x0000},	/* Context A */
	{ 0xffff, 0xffff},	/* end */
};

static struct regval_list ncm13j_hdtv_regs[] = {
	{ 0x01a1, HDTV_WIDTH},	/* Horizontal Output Size B = 1280 */
	{ 0x01a4, HDTV_HEIGHT},	/* Vertical Output Size B = 720 */
	{ 0x01a6, HDTV_WIDTH},	/* Horizontal Zoom = 1280 */
	{ 0x01a9, HDTV_HEIGHT},	/* Vertical Zoom = 720 */
	{ 0x01ae, 0x1009},	/* Reducer Zoom Step Size */
	{ 0x00c8, 0x000b},	/* Context B */
	{ 0x02c8, 0x070b},	/* Context B */
	{ 0xffff, 0xffff},	/* end */
};

static struct regval_list ncm13j_sxga_regs[] = {
	{ 0x01a1, SXGA_WIDTH},	/* Horizontal Output Size B = 1280 */
	{ 0x01a4, SXGA_HEIGHT},	/* Vertical Output Size B = 1024 */
	{ 0x01a6, SXGA_WIDTH},	/* Horizontal Zoom = 1280 */
	{ 0x01a9, SXGA_HEIGHT},	/* Vertical Zoom = 1024 */
	{ 0x01ae, 0x0a08},	/* Reducer Zoom Step Size */
	{ 0x00c8, 0x000b},	/* Context B */
	{ 0x02c8, 0x070b},	/* Context B */
	{ 0xffff, 0xffff},	/* end */
};

static struct ncm13j_win_size {
	int	width;
	int	height;
	struct regval_list *regs; /* Regs to tweak */
/* h/vref stuff */
} ncm13j_win_sizes[] = {
	/* SXGA */
	{
		.width		= SXGA_WIDTH,
		.height		= SXGA_HEIGHT,
		.regs		= ncm13j_sxga_regs,
	},
	/* HDTV */
	{
		.width		= HDTV_WIDTH,
		.height		= HDTV_HEIGHT,
		.regs		= ncm13j_hdtv_regs,
	},
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
		.regs		= ncm13j_vga_regs,
	},
	/* QVGA */
	{
		.width		= QVGA_WIDTH,
		.height		= QVGA_HEIGHT,
		.regs		= ncm13j_qvga_regs,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(ncm13j_win_sizes))

static int ncm13j_try_fmt_internal(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt,
		struct ncm13j_format_struct **ret_fmt,
		struct ncm13j_win_size **ret_wsize)
{
	int index;
	struct ncm13j_win_size *wsize;

	for (index = 0; index < N_NCM13J_FMTS; index++)
		if (ncm13j_formats[index].mbus_code == fmt->code)
			break;
	if (index >= N_NCM13J_FMTS) {
		/* default to first format */
		index = 0;
		fmt->code = ncm13j_formats[0].mbus_code;
	}
	if (ret_fmt != NULL)
		*ret_fmt = ncm13j_formats + index;

	fmt->field = V4L2_FIELD_NONE;

	for (wsize = ncm13j_win_sizes;
			 wsize < ncm13j_win_sizes + N_WIN_SIZES; wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)
			break;
	if (wsize >= ncm13j_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->colorspace = ncm13j_formats[index].colorspace;

	return 0;
}

static int ncm13j_try_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	return ncm13j_try_fmt_internal(sd, fmt, NULL, NULL);
}

/*
 * Set a format.
 */
static int ncm13j_s_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	struct ncm13j_format_struct *ovfmt;
	struct ncm13j_win_size *wsize;
	struct ncm13j_info *info = to_state(sd);

	ret = ncm13j_try_fmt_internal(sd, fmt, &ovfmt, &wsize);

	if (ret)
		return ret;

	/* Reset */
	ncm13j_reset(sd, 0);

	/*
	 * Now write the rest of the array.  Also store start/stops
	 */
	ncm13j_write_array(sd, ovfmt->regs /* + 1*/);
	ret = 0;
	if (wsize->regs)
		ret = ncm13j_write_array(sd, wsize->regs);
	info->fmt = ovfmt;

	return ret;
}


/*
 * Code for dealing with controls.
 */

static int ncm13j_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	/* Fill in min, max, step and default value for these controls. */
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ncm13j_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ncm13j_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ncm13j_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV7670, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops ncm13j_core_ops = {
	.g_chip_ident = ncm13j_g_chip_ident,
	.g_ctrl = ncm13j_g_ctrl,
	.s_ctrl = ncm13j_s_ctrl,
	.queryctrl = ncm13j_queryctrl,
	.reset = ncm13j_reset,
	.init = ncm13j_init,
};

static const struct v4l2_subdev_video_ops ncm13j_video_ops = {
	.try_mbus_fmt = ncm13j_try_mbus_fmt,
	.s_mbus_fmt = ncm13j_s_mbus_fmt,
};

static const struct v4l2_subdev_ops ncm13j_ops = {
	.core = &ncm13j_core_ops,
	.video = &ncm13j_video_ops,
};

/* ----------------------------------------------------------------------- */

static int ncm13j_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct ncm13j_info *info;
	int ret;

	info = kzalloc(sizeof(struct ncm13j_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &ncm13j_ops);

	/* Make sure it's an ncm13j */
	ret = ncm13j_detect(sd);
	if (ret) {
		v4l_dbg(1, debug, client,
			"chip found @ 0x%x (%s) is not an ncm13j chip.\n",
			client->addr << 1, client->adapter->name);
		kfree(info);
		return ret;
	}
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	info->fmt = &ncm13j_formats[0];
	info->sat = 128;	/* Review this */

	return 0;
}


static int ncm13j_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id ncm13j_id[] = {
	{ "ioh_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ncm13j_id);

static struct i2c_driver ncm13j_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ioh_i2c",
	},
	.probe = ncm13j_probe,
	.remove = ncm13j_remove,
	.id_table = ncm13j_id,
};

static __init int init_ncm13j(void)
{
	return i2c_add_driver(&ncm13j_driver);
}

static __exit void exit_ncm13j(void)
{
	i2c_del_driver(&ncm13j_driver);
}

module_init(init_ncm13j);
module_exit(exit_ncm13j);


