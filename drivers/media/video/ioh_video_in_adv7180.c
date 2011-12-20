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

MODULE_DESCRIPTION("IOH video-in driver for ADV7180.");
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

#define REG_RESET	0x0F	/* RESET Register */
#define   VAL_RESET	0x80	/* RESET Value */

/*
 * Information we maintain about a known sensor.
 */
struct adv7180_format_struct;  /* coming later */
struct adv7180_info {
	struct v4l2_subdev sd;
	struct adv7180_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
};

static inline struct adv7180_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7180_info, sd);
}



/*
 * The default register settings.
 */

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

static struct regval_list adv7180_default_regs[] = {
	{0x00,  0x50},  //to select the NTSC-M input port-1
//	{0x00,  0x51},  //to select the NTSC-M input port-2
	{0xf4,  0x1b},  //clock out 3x & sync out 4x
	{0x01,  0xCC},  //square pixel mode
	{0x03,  0x0C},  //(default)
	{0x04,  0x45},  //(default)
	{0x07,  0x7F},  //(default)
	{0x37,  0x81},  //(HS active low)
	{0xff, 0xff},		/* end */
};


static struct regval_list adv7180_fmt_yuv422[] = {
	{0x00,  0x50},  //to select the NTSC-M input port-1
//	{0x00,  0x51},  //to select the NTSC-M input port-2
	{0xf4,  0x1b},  //clock out 3x & sync out 4x
	{0x01,  0xCC},  //square pixel mode
	{0x03,  0x0C},  //(default)
	{0x04,  0x45},  //(default)
	{0x07,  0x7F},  //(default)
	{0x37,  0x81},  //(HS active low)
	{0xff, 0xff},		/* end */
};


/*
 * Low-level register I/O.
 */

static int ioh_video_in_read_value(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	unsigned char data1w[1] = { reg };
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = data1w;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = val;

	ret = i2c_transfer(client->adapter, msg, 2);
	if(ret != 2)
		goto err;
	msleep(2);
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
	int ret;
	unsigned char data2[2] = { reg, val };

	client->flags = 0;
	ret = i2c_master_send(client, data2, 2);

	if (((reg == REG_RESET) && (val & VAL_RESET)) || (ret == 2)) {
		msleep(2);
		v4l_dbg(1, debug, client, "Function %s A(0x%02X) <-- 0x%02X "
					"end.", __func__, reg, val);
		return 0;
	} else {
		v4l_err(client, "Function %s A(0x%02X) <-- 0x%02X "
				"write error failed.", __func__, reg, val);
		return -EINVAL;
	}
}

static int adv7180_read(struct v4l2_subdev *sd, unsigned char reg,
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
static int adv7180_write(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
#if 0
	int ret = i2c_smbus_write_byte_data(client, reg, value);
#else
	int ret = ioh_video_in_write_value(client, reg, value);
#endif

	if (reg == REG_RESET && (value & VAL_RESET))
		msleep(5);  /* Wait for reset to run */

	return ret;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int adv7180_write_array(struct v4l2_subdev *sd,
						struct regval_list *vals)
{
	while (vals->reg_num != 0xff || vals->value != 0xff) {
		int ret = adv7180_write(sd, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */
static int adv7180_reset(struct v4l2_subdev *sd, u32 val)
{
	adv7180_write(sd, REG_RESET, VAL_RESET);
	msleep(5);
	return 0;
}


static int adv7180_init(struct v4l2_subdev *sd, u32 val)
{
	return adv7180_write_array(sd, adv7180_default_regs);
}


static int adv7180_detect(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret = adv7180_init(sd, 0);
	if (ret < 0)
		return ret;

	/* Skip ID checking... */

	return 0;
}

static struct adv7180_format_struct {
	enum v4l2_mbus_pixelcode mbus_code;
	enum v4l2_colorspace colorspace;
	struct regval_list *regs;
} adv7180_formats[] = {
	{
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.regs		= adv7180_fmt_yuv422,
	},
};
#define N_ADV7180_FMTS ARRAY_SIZE(adv7180_formats)

static struct regval_list adv7180_vga_regs[] = {
	{ 0xff, 0xff },
};


static struct adv7180_win_size {
	int	width;
	int	height;
	struct regval_list *regs; /* Regs to tweak */
/* h/vref stuff */
} adv7180_win_sizes[] = {
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
		.regs		= adv7180_vga_regs,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(adv7180_win_sizes))

static int adv7180_try_fmt_internal(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt,
		struct adv7180_format_struct **ret_fmt,
		struct adv7180_win_size **ret_wsize)
{
	int index;
	struct adv7180_win_size *wsize;

	for (index = 0; index < N_ADV7180_FMTS; index++)
		if (adv7180_formats[index].mbus_code == fmt->code)
			break;
	if (index >= N_ADV7180_FMTS) {
		/* default to first format */
		index = 0;
		fmt->code = adv7180_formats[0].mbus_code;
	}
	if (ret_fmt != NULL)
		*ret_fmt = adv7180_formats + index;

	fmt->field = V4L2_FIELD_NONE;

	for (wsize = adv7180_win_sizes;
			wsize < adv7180_win_sizes + N_WIN_SIZES; wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)
			break;
	if (wsize >= adv7180_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->colorspace = adv7180_formats[index].colorspace;

	return 0;
}

static int adv7180_try_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	return adv7180_try_fmt_internal(sd, fmt, NULL, NULL);
}

/*
 * Set a format.
 */
static int adv7180_s_mbus_fmt(struct v4l2_subdev *sd,
					struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	struct adv7180_format_struct *ovfmt;
	struct adv7180_win_size *wsize;
	struct adv7180_info *info = to_state(sd);

	ret = adv7180_try_fmt_internal(sd, fmt, &ovfmt, &wsize);
	if (ret)
		return ret;

	/* Reset */
	adv7180_reset(sd, 0);

	/*
	 * Now write the rest of the array.
	 */
	adv7180_write_array(sd, ovfmt->regs);
	ret = 0;
	if (wsize->regs)
		ret = adv7180_write_array(sd, wsize->regs);
	info->fmt = ovfmt;

	return ret;
}


/*
 * Code for dealing with controls.
 */

static int adv7180_queryctrl(struct v4l2_subdev *sd,
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

static int adv7180_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return -EINVAL;
	}
	return -EINVAL;
}

static int adv7180_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return -EINVAL;
	}
	return -EINVAL;
}

static int adv7180_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV7670, 0);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int adv7180_g_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char val = 0;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = adv7180_read(sd, reg->reg & 0xff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int adv7180_s_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	adv7180_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops adv7180_core_ops = {
	.g_chip_ident = adv7180_g_chip_ident,
	.g_ctrl = adv7180_g_ctrl,
	.s_ctrl = adv7180_s_ctrl,
	.queryctrl = adv7180_queryctrl,
	.reset = adv7180_reset,
	.init = adv7180_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = adv7180_g_register,
	.s_register = adv7180_s_register,
#endif
};

static const struct v4l2_subdev_video_ops adv7180_video_ops = {
	.try_mbus_fmt = adv7180_try_mbus_fmt,
	.s_mbus_fmt = adv7180_s_mbus_fmt,
};

static const struct v4l2_subdev_ops adv7180_ops = {
	.core = &adv7180_core_ops,
	.video = &adv7180_video_ops,
};

/* ----------------------------------------------------------------------- */

static int adv7180_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct adv7180_info *info;
	int ret;

	info = kzalloc(sizeof(struct adv7180_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7180_ops);

	/* Make sure it's an adv7180 */
	ret = adv7180_detect(sd);
	if (ret) {
		//v4l_dbg(1, debug, client,
		v4l_info(client,
			"chip found @ 0x%x (%s) is not an adv7180 chip.\n",
			client->addr << 1, client->adapter->name);
		kfree(info);
		return ret;
	}
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	info->fmt = &adv7180_formats[0];
	info->sat = 128;	/* Review this */

	return 0;
}


static int adv7180_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id adv7180_id[] = {
	{ "ioh_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7180_id);

static struct i2c_driver adv7180_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ioh_i2c",
	},
	.probe = adv7180_probe,
	.remove = adv7180_remove,
	.id_table = adv7180_id,
};

static __init int init_adv7180(void)
{
	return i2c_add_driver(&adv7180_driver);
}

static __exit void exit_adv7180(void)
{
	i2c_del_driver(&adv7180_driver);
}

module_init(init_adv7180);
module_exit(exit_adv7180);


