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

MODULE_DESCRIPTION("IOH video-in driver for OmniVision ov7620 sensor.");
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
#define QVGA_WIDTH	320
#define QVGA_HEIGHT	240

/* Registers */
#define REG_BRIGHT	0x06	/* Brightness */

#define REG_COMJ	0x2d	/* Common Control J */
#define   COMJ_4	0x10	/* Auto brightness enable */

#define REG_COMC	0x14	/* Common Control C */
#define   COMC_FMT_QVGA	0x20	/* OVGA digital output format selesction */

#define REG_COMA	0x12	/* Common Control A */
#define   COMA_RESET	0x80	/* Register reset */

#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */

#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */

/*
 * Information we maintain about a known sensor.
 */
struct ov7620_format_struct;  /* coming later */
struct ov7620_info {
	struct v4l2_subdev sd;
	struct ov7620_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
};

static inline struct ov7620_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov7620_info, sd);
}



/*
 * The default register settings.
 */

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

static struct regval_list ov7620_default_regs[] = {
	{ REG_COMC, 0x04 },
	{ 0x11, 0x40 },
	{ 0x13, 0x31 },
	{ 0x28, 0x20 },		/* Progressive */
	{ 0x2d, 0x91 },
	{ 0xff, 0xff },		/* end */
};


static struct regval_list ov7620_fmt_yuv422[] = {
	{ REG_COMC, 0x04 },
	{ 0x11, 0x40 },
	{ 0x13, 0x31 },
	{ 0x28, 0x20 },		/* Progressive */
	{ 0x2d, 0x91 },
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

static int ov7620_read(struct v4l2_subdev *sd, unsigned char reg,
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
static int ov7620_write(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
#if 0
	int ret = i2c_smbus_write_byte_data(client, reg, value);
#else
	int ret = ioh_video_in_write_value(client, reg, value);
#endif

	if (reg == REG_COMA && (value & COMA_RESET))
		msleep(2);  /* Wait for reset to run */

	return ret;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ov7620_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	while (vals->reg_num != 0xff || vals->value != 0xff) {
		int ret = ov7620_write(sd, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */
static int ov7620_reset(struct v4l2_subdev *sd, u32 val)
{
	ov7620_write(sd, REG_COMA, COMA_RESET);
	msleep(1);
	return 0;
}


static int ov7620_init(struct v4l2_subdev *sd, u32 val)
{
	return ov7620_write_array(sd, ov7620_default_regs);
}


static int ov7620_detect(struct v4l2_subdev *sd)
{
	unsigned char v;
	int ret;

	ret = ov7620_init(sd, 0);
	if (ret < 0)
		return ret;
	ret = ov7620_read(sd, REG_MIDH, &v);
	if (ret < 0)
		return ret;
	if (v != 0x7f) /* OV manuf. id. */
		return -ENODEV;
	ret = ov7620_read(sd, REG_MIDL, &v);
	if (ret < 0)
		return ret;
	if (v != 0xa2)
		return -ENODEV;
	return 0;
}

static struct ov7620_format_struct {
	enum v4l2_mbus_pixelcode mbus_code;
	enum v4l2_colorspace colorspace;
	struct regval_list *regs;
} ov7620_formats[] = {
	{
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.regs		= ov7620_fmt_yuv422,
	},
};
#define N_OV7620_FMTS ARRAY_SIZE(ov7620_formats)


/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct regval_list ov7620_vga_regs[] = {
	{ 0xff, 0xff },
};

static struct regval_list ov7620_qvga_regs[] = {
	{ 0xff, 0xff },
};

static struct ov7620_win_size {
	int	width;
	int	height;
	unsigned char comc_bit;
	int	hstart;
	int	hstop;
	int	vstart;
	int	vstop;
	struct regval_list *regs; /* Regs to tweak */
/* h/vref stuff */
} ov7620_win_sizes[] = {
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
		.comc_bit	= 0,
		.hstart		= 0x2f,
		.hstop		= 0xcf,
		.vstart		= 0x06,
		.vstop		= 0xf5,
		.regs		= ov7620_vga_regs,
	},
	/* QVGA */
	{
		.width		= QVGA_WIDTH,
		.height		= QVGA_HEIGHT,
		.comc_bit	= COMC_FMT_QVGA,
		.hstart		= 0x2f,
		.hstop		= 0xcf,
		.vstart		= 0x06,
		.vstop		= 0xf5,
		.regs		= ov7620_qvga_regs,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(ov7620_win_sizes))


/*
 * Store a set of start/stop values into the camera.
 */
static int ov7620_set_hw(struct v4l2_subdev *sd, int hstart, int hstop,
		int vstart, int vstop)
{
	int ret;

	ret =  ov7620_write(sd, REG_HSTART, hstart);
	ret += ov7620_write(sd, REG_HSTOP, hstop);

	ret += ov7620_write(sd, REG_VSTART, vstart);
	ret += ov7620_write(sd, REG_VSTOP, vstop);
	return ret;
}

static int ov7620_try_fmt_internal(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt,
		struct ov7620_format_struct **ret_fmt,
		struct ov7620_win_size **ret_wsize)
{
	int index;
	struct ov7620_win_size *wsize;

	for (index = 0; index < N_OV7620_FMTS; index++)
		if (ov7620_formats[index].mbus_code == fmt->code)
			break;
	if (index >= N_OV7620_FMTS) {
		/* default to first format */
		index = 0;
		fmt->code = ov7620_formats[0].mbus_code;
	}
	if (ret_fmt != NULL)
		*ret_fmt = ov7620_formats + index;

	fmt->field = V4L2_FIELD_NONE;

	for (wsize = ov7620_win_sizes;
			wsize < ov7620_win_sizes + N_WIN_SIZES; wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)
			break;
	if (wsize >= ov7620_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->colorspace = ov7620_formats[index].colorspace;

	return 0;
}

static int ov7620_try_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	return ov7620_try_fmt_internal(sd, fmt, NULL, NULL);
}

/*
 * Set a format.
 */
static int ov7620_s_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	struct ov7620_format_struct *ovfmt;
	struct ov7620_win_size *wsize;
	struct ov7620_info *info = to_state(sd);
	unsigned char comc;

	ret = ov7620_try_fmt_internal(sd, fmt, &ovfmt, &wsize);
	if (ret)
		return ret;

	/* Reset */
	ov7620_reset(sd, 0);

	comc = ovfmt->regs[0].value;
	comc |= wsize->comc_bit;
	ov7620_write(sd, REG_COMC, comc);
	/*
	 * Now write the rest of the array.  Also store start/stops
	 */
	ov7620_write_array(sd, ovfmt->regs + 1);
	ov7620_set_hw(sd, wsize->hstart, wsize->hstop, wsize->vstart,
			wsize->vstop);
	ret = 0;
	if (wsize->regs)
		ret = ov7620_write_array(sd, wsize->regs);
	info->fmt = ovfmt;

	return ret;
}


/*
 * Code for dealing with controls.
 */

static int ov7620_s_brightness(struct v4l2_subdev *sd, int value)
{
	unsigned char comj = 0;
	int ret;

	ov7620_read(sd, REG_COMJ, &comj);
	comj &= ~COMJ_4;
	ov7620_write(sd, REG_COMJ, comj);

	ret = ov7620_write(sd, REG_BRIGHT, value);
	return ret;
}

static int ov7620_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	unsigned char v = 0;
	int ret = ov7620_read(sd, REG_BRIGHT, &v);

	return ret;
}

static int ov7620_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	/* Fill in min, max, step and default value for these controls. */
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 128);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ov7620_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ov7620_g_brightness(sd, &ctrl->value);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ov7620_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ov7620_s_brightness(sd, ctrl->value);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ov7620_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV7670, 0);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov7620_g_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char val = 0;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = ov7620_read(sd, reg->reg & 0xff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int ov7620_s_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ov7620_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops ov7620_core_ops = {
	.g_chip_ident = ov7620_g_chip_ident,
	.g_ctrl = ov7620_g_ctrl,
	.s_ctrl = ov7620_s_ctrl,
	.queryctrl = ov7620_queryctrl,
	.reset = ov7620_reset,
	.init = ov7620_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov7620_g_register,
	.s_register = ov7620_s_register,
#endif
};

static const struct v4l2_subdev_video_ops ov7620_video_ops = {
	.try_mbus_fmt = ov7620_try_mbus_fmt,
	.s_mbus_fmt = ov7620_s_mbus_fmt,
};

static const struct v4l2_subdev_ops ov7620_ops = {
	.core = &ov7620_core_ops,
	.video = &ov7620_video_ops,
};

/* ----------------------------------------------------------------------- */

static int ov7620_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct ov7620_info *info;
	int ret;

	info = kzalloc(sizeof(struct ov7620_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &ov7620_ops);

	/* Make sure it's an ov7620 */
	ret = ov7620_detect(sd);
	if (ret) {
		v4l_dbg(1, debug, client,
			"chip found @ 0x%x (%s) is not an ov7620 chip.\n",
			client->addr << 1, client->adapter->name);
		kfree(info);
		return ret;
	}
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	info->fmt = &ov7620_formats[0];
	info->sat = 128;	/* Review this */

	return 0;
}


static int ov7620_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id ov7620_id[] = {
	{ "ioh_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov7620_id);

static struct i2c_driver ov7620_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ioh_i2c",
	},
	.probe = ov7620_probe,
	.remove = ov7620_remove,
	.id_table = ov7620_id,
};

static __init int init_ov7620(void)
{
	return i2c_add_driver(&ov7620_driver);
}

static __exit void exit_ov7620(void)
{
	i2c_del_driver(&ov7620_driver);
}

module_init(init_ov7620);
module_exit(exit_ov7620);


