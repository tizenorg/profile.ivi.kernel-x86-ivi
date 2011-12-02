/*
 * crossville.c - platform code for Intel IVI Crossville platform
 *
 * Copyright (c) 2011, Intel Corporation.
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
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>

/* Definition of system GPIO info */
#define GPIO_CVL_TSC2007	251
#define TSC2007_IRQ	33

static int ts_init(void)
{
	int ret = -1;

	ret = gpio_request(GPIO_CVL_TSC2007, "tsc2007_irq");
	if (ret)
		goto exit;

	ret = gpio_direction_input(GPIO_CVL_TSC2007);
	if (ret)
		goto err_free_gpio;

	ret = irq_set_irq_type(TSC2007_IRQ, IRQ_TYPE_EDGE_FALLING);
	if (ret)
		goto err_free_gpio;

	return 0;

err_free_gpio:
	gpio_free(GPIO_CVL_TSC2007);
exit:
	return ret;
}


static struct tsc2007_platform_data tsc2007_info = {
	.model			= 2007,
	.x_plate_ohms		= 180,
	.poll_period		= 30,
	.init_platform_hw	= ts_init,
};


static struct i2c_board_info cvl_i2c_devs[] = {
	{
		I2C_BOARD_INFO("tsc2007", 0x48),
		.type		= "tsc2007",
		.platform_data	= &tsc2007_info,
		.irq		= TSC2007_IRQ,
	},
};

static int __init cvl_platform_init(void)
{
	/* We first need enumerate all the I2C client devices */

	/* Add TouchScreen board info */
	i2c_register_board_info(1, cvl_i2c_devs, ARRAY_SIZE(cvl_i2c_devs));

	return 0;
}

static void __exit cvl_platform__exit(void)
{
}

subsys_initcall(cvl_platform_init);
module_exit(cvl_platform__exit);
