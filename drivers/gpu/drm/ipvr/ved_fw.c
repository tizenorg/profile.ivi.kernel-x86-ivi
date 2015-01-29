/**************************************************************************
 * ved_fw.c: VED initialization and mtx-firmware upload
 *
 * Copyright (c) 2014 Intel Corporation, Hillsboro, OR, USA
 * Copyright (c) Imagination Technologies Limited, UK
 * Copyright (c) 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
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

#include "ipvr_bo.h"
#include "ipvr_mmu.h"
#include "ipvr_gem.h"
#include "ved_fw.h"
#include "ved_cmd.h"
#include "ved_msg.h"
#include "ved_reg.h"
#include <linux/firmware.h>
#include <linux/module.h>
#include <asm/cacheflush.h>

#define STACKGUARDWORD			0x10101010
#define MSVDX_MTX_DATA_LOCATION		0x82880000
#define UNINITILISE_MEM			0xcdcdcdcd
#define FIRMWARE_NAME "msvdx_fw_mfld_DE2.0.bin"

/* VED FW header */
struct ved_fw {
	u32 ver;
	u32 text_size;
	u32 data_size;
	u32 data_location;
};


void ved_clear_irq(struct ved_private *ved_priv)
{
	u32 mtx_int = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	/* Clear MTX interrupt */
	REGIO_WRITE_FIELD_LITE(mtx_int, MSVDX_INTERRUPT_STATUS, MTX_IRQ, 1);
	IPVR_REG_WRITE32(mtx_int, MSVDX_INTERRUPT_CLEAR_OFFSET);
}

/* following two functions also works for CLV and MFLD */
/* IPVR_INT_ENABLE_R is set in ipvr_irq_(un)install_islands */
void ved_disable_irq(struct ved_private *ved_priv)
{
	u32 enables = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	REGIO_WRITE_FIELD_LITE(enables, MSVDX_INTERRUPT_STATUS, MTX_IRQ, 0);
	IPVR_REG_WRITE32(enables, MSVDX_HOST_INTERRUPT_ENABLE_OFFSET);
}

void ved_enable_irq(struct ved_private *ved_priv)
{
	u32 enables = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	/* Only enable the master core IRQ*/
	REGIO_WRITE_FIELD_LITE(enables, MSVDX_INTERRUPT_STATUS, MTX_IRQ,
			       1);
	IPVR_REG_WRITE32(enables, MSVDX_HOST_INTERRUPT_ENABLE_OFFSET);
}

/*
 * the original 1000 of udelay is derive from reference driver
 * From Liu, Haiyang, changed the originial udelay value from 1000 to 5
 * can save 3% C0 residence
 */
int
ved_wait_for_register(struct ved_private *ved_priv,
			    u32 offset, u32 value, u32 enable,
			    u32 poll_cnt, u32 timeout)
{
	u32 reg_value = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	while (poll_cnt) {
		reg_value = IPVR_REG_READ32(offset);
		if (value == (reg_value & enable))
			return 0;

		/* Wait a bit */
		IPVR_UDELAY(timeout);
		poll_cnt--;
	}
	IPVR_DEBUG_REG("MSVDX: Timeout while waiting for register %08x:"
		       " expecting %08x (mask %08x), got %08x\n",
		       offset, value, enable, reg_value);

	return -EFAULT;
}

void
ved_set_clocks(struct ved_private *ved_priv, u32 clock_state)
{
	u32 old_clock_state = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	/* IPVR_DEBUG_VED("SetClocks to %x.\n", clock_state); */
	old_clock_state = IPVR_REG_READ32(MSVDX_MAN_CLK_ENABLE_OFFSET);
	if (old_clock_state == clock_state)
		return;

	if (clock_state == 0) {
		/* Turn off clocks procedure */
		if (old_clock_state) {
			/* Turn off all the clocks except core */
			IPVR_REG_WRITE32(
				MSVDX_MAN_CLK_ENABLE_CORE_MAN_CLK_ENABLE_MASK,
				MSVDX_MAN_CLK_ENABLE_OFFSET);

			/* Make sure all the clocks are off except core */
			ved_wait_for_register(ved_priv,
				MSVDX_MAN_CLK_ENABLE_OFFSET,
				MSVDX_MAN_CLK_ENABLE_CORE_MAN_CLK_ENABLE_MASK,
				0xffffffff, 2000000, 5);

			/* Turn off core clock */
			IPVR_REG_WRITE32(0, MSVDX_MAN_CLK_ENABLE_OFFSET);
		}
	} else {
		u32 clocks_en = clock_state;

		/*Make sure that core clock is not accidentally turned off */
		clocks_en |= MSVDX_MAN_CLK_ENABLE_CORE_MAN_CLK_ENABLE_MASK;

		/* If all clocks were disable do the bring up procedure */
		if (old_clock_state == 0) {
			/* turn on core clock */
			IPVR_REG_WRITE32(
				MSVDX_MAN_CLK_ENABLE_CORE_MAN_CLK_ENABLE_MASK,
				MSVDX_MAN_CLK_ENABLE_OFFSET);

			/* Make sure core clock is on */
			ved_wait_for_register(ved_priv,
				MSVDX_MAN_CLK_ENABLE_OFFSET,
				MSVDX_MAN_CLK_ENABLE_CORE_MAN_CLK_ENABLE_MASK,
				0xffffffff, 2000000, 5);

			/* turn on the other clocks as well */
			IPVR_REG_WRITE32(clocks_en, MSVDX_MAN_CLK_ENABLE_OFFSET);

			/* Make sure that all they are on */
			ved_wait_for_register(ved_priv,
					MSVDX_MAN_CLK_ENABLE_OFFSET,
					clocks_en, 0xffffffff, 2000000, 5);
		} else {
			IPVR_REG_WRITE32(clocks_en, MSVDX_MAN_CLK_ENABLE_OFFSET);

			/* Make sure that they are on */
			ved_wait_for_register(ved_priv,
					MSVDX_MAN_CLK_ENABLE_OFFSET,
					clocks_en, 0xffffffff, 2000000, 5);
		}
	}
}

int ved_core_reset(struct ved_private *ved_priv)
{
	int ret = 0;
	int loop;
	u32 cmd;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	/* Enable Clocks */
	IPVR_DEBUG_GENERAL("Enabling clocks.\n");
	ved_set_clocks(ved_priv, clk_enable_all);

	/* Always pause the MMU as the core may be still active
	 * when resetting.  It is very bad to have memory
	 * activity at the same time as a reset - Very Very bad
	 */
	IPVR_REG_WRITE32(2, MSVDX_MMU_CONTROL0_OFFSET);

	/* BRN26106, BRN23944, BRN33671 */
	/* This is neccessary for all cores up to Tourmaline */
	if ((IPVR_REG_READ32(MSVDX_CORE_REV_OFFSET) < 0x00050502) &&
		(IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET)
			& MSVDX_INTERRUPT_STATUS_MMU_FAULT_IRQ_MASK) &&
		(IPVR_REG_READ32(MSVDX_MMU_STATUS_OFFSET) & 1)) {
		u32 *pptd;
		int loop;
		unsigned long ptd_addr;

		/* do work around */
		ptd_addr = page_to_pfn(ved_priv->mmu_recover_page) << PAGE_SHIFT;
		/* fixme: check ptd_addr bit length */
		pptd = kmap(ved_priv->mmu_recover_page);
		if (!pptd) {
			IPVR_ERROR("failed to kmap mmu recover page.\n");
			return -ENOMEM;
		}
		for (loop = 0; loop < 1024; loop++)
			pptd[loop] = ptd_addr | 0x00000003;
		IPVR_REG_WRITE32(ptd_addr, MSVDX_MMU_DIR_LIST_BASE_OFFSET +  0);
		IPVR_REG_WRITE32(ptd_addr, MSVDX_MMU_DIR_LIST_BASE_OFFSET +  4);
		IPVR_REG_WRITE32(ptd_addr, MSVDX_MMU_DIR_LIST_BASE_OFFSET +  8);
		IPVR_REG_WRITE32(ptd_addr, MSVDX_MMU_DIR_LIST_BASE_OFFSET + 12);

		IPVR_REG_WRITE32(6, MSVDX_MMU_CONTROL0_OFFSET);
		IPVR_REG_WRITE32(MSVDX_INTERRUPT_STATUS_MMU_FAULT_IRQ_MASK,
					MSVDX_INTERRUPT_STATUS_OFFSET);
		kunmap(ved_priv->mmu_recover_page);
	}

	/* make sure *ALL* outstanding reads have gone away */
	for (loop = 0; loop < 10; loop++)
		ret = ved_wait_for_register(ved_priv, MSVDX_MMU_MEM_REQ_OFFSET,
					    0, 0xff, 100, 1);
	if (ret) {
		IPVR_DEBUG_WARN("MSVDX_MMU_MEM_REQ is %d,\n"
			"indicate outstanding read request 0.\n",
			IPVR_REG_READ32(MSVDX_MMU_MEM_REQ_OFFSET));
		ret = -1;
		return ret;
	}
	/* disconnect RENDEC decoders from memory */
	cmd = IPVR_REG_READ32(MSVDX_RENDEC_CONTROL1_OFFSET);
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_CONTROL1, RENDEC_DEC_DISABLE, 1);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTROL1_OFFSET);

	/* Issue software reset for all but core */
	IPVR_REG_WRITE32((unsigned int)~MSVDX_CONTROL_MSVDX_SOFT_RESET_MASK,
			MSVDX_CONTROL_OFFSET);
	IPVR_REG_READ32(MSVDX_CONTROL_OFFSET);
	/* bit format is set as little endian */
	IPVR_REG_WRITE32(0, MSVDX_CONTROL_OFFSET);
	/* make sure read requests are zero */
	ret = ved_wait_for_register(ved_priv, MSVDX_MMU_MEM_REQ_OFFSET,
				    0, 0xff, 100, 100);
	if (!ret) {
		/* Issue software reset */
		IPVR_REG_WRITE32(MSVDX_CONTROL_MSVDX_SOFT_RESET_MASK,
				MSVDX_CONTROL_OFFSET);

		ret = ved_wait_for_register(ved_priv, MSVDX_CONTROL_OFFSET, 0,
					MSVDX_CONTROL_MSVDX_SOFT_RESET_MASK,
					2000000, 5);
		if (!ret) {
			/* Clear interrupt enabled flag */
			IPVR_REG_WRITE32(0, MSVDX_HOST_INTERRUPT_ENABLE_OFFSET);

			/* Clear any pending interrupt flags */
			IPVR_REG_WRITE32(0xFFFFFFFF, MSVDX_INTERRUPT_CLEAR_OFFSET);
		} else {
			IPVR_DEBUG_WARN("MSVDX_CONTROL_OFFSET is %d,\n"
				"indicate software reset failed.\n",
				IPVR_REG_READ32(MSVDX_CONTROL_OFFSET));
		}
	} else {
		IPVR_DEBUG_WARN("MSVDX_MMU_MEM_REQ is %d,\n"
			"indicate outstanding read request 1.\n",
			IPVR_REG_READ32(MSVDX_MMU_MEM_REQ_OFFSET));
	}
	return ret;
}

/*
 * Reset chip and disable interrupts.
 * Return 0 success, 1 failure
 * use ved_core_reset instead of ved_reset
 */
int ved_reset(struct ved_private *ved_priv)
{
	int ret = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	/* Issue software reset */
	/* IPVR_REG_WRITE32(msvdx_sw_reset_all, MSVDX_CONTROL); */
	IPVR_REG_WRITE32(MSVDX_CONTROL_MSVDX_SOFT_RESET_MASK,
			MSVDX_CONTROL_OFFSET);

	ret = ved_wait_for_register(ved_priv, MSVDX_CONTROL_OFFSET, 0,
			MSVDX_CONTROL_MSVDX_SOFT_RESET_MASK, 2000000, 5);
	if (!ret) {
		/* Clear interrupt enabled flag */
		IPVR_REG_WRITE32(0, MSVDX_HOST_INTERRUPT_ENABLE_OFFSET);

		/* Clear any pending interrupt flags */
		IPVR_REG_WRITE32(0xFFFFFFFF, MSVDX_INTERRUPT_CLEAR_OFFSET);
	} else {
		IPVR_DEBUG_WARN("MSVDX_CONTROL_OFFSET is %d,\n"
			"indicate software reset failed.\n",
			IPVR_REG_READ32(MSVDX_CONTROL_OFFSET));
	}

	return ret;
}

static int ved_alloc_ccb_for_rendec(struct ved_private *ved_priv,
										int32_t ccb0_size,
										int32_t ccb1_size)
{
	int ret;
	size_t size;
	u8 *ccb0_addr = NULL;
	u8 *ccb1_addr = NULL;

	IPVR_DEBUG_INIT("VED: setting up RENDEC, allocate CCB 0/1\n");

	/*handling for ccb0*/
	if (ved_priv->ccb0 == NULL) {
		size = roundup(ccb0_size, PAGE_SIZE);
		if (size == 0)
			return -EINVAL;

		/* Allocate the new object */
		ved_priv->ccb0 = ipvr_gem_create(ved_priv->dev_priv, size, 0, 0);
		if (IS_ERR(ved_priv->ccb0)) {
			ret = PTR_ERR(ved_priv->ccb0);
			IPVR_ERROR("VED: failed to allocate ccb0 buffer: %d.\n", ret);
			ved_priv->ccb0 = NULL;
			return -ENOMEM;
		}

		ved_priv->base_addr0 = ipvr_gem_object_mmu_offset(ved_priv->ccb0);

		ccb0_addr = ipvr_gem_object_vmap(ved_priv->ccb0);
		if (!ccb0_addr) {
			ret = -ENOMEM;
			IPVR_ERROR("VED: kmap failed for ccb0 buffer: %d.\n", ret);
			goto err_free_ccb0;
		}

		memset(ccb0_addr, 0, size);
		vunmap(ccb0_addr);
	}

	/*handling for ccb1*/
	if (ved_priv->ccb1 == NULL) {
		size = roundup(ccb1_size, PAGE_SIZE);
		if (size == 0) {
			return -EINVAL;
			goto err_free_ccb0;
		}

		/* Allocate the new object */
		ved_priv->ccb1 = ipvr_gem_create(ved_priv->dev_priv, size, 0, 0);
		if (IS_ERR(ved_priv->ccb1)) {
			ret = PTR_ERR(ved_priv->ccb1);
			IPVR_ERROR("VED: failed to allocate ccb1 buffer: %d.\n", ret);
			goto err_free_ccb0;
		}

		ved_priv->base_addr1 = ipvr_gem_object_mmu_offset(ved_priv->ccb1);

		ccb1_addr = ipvr_gem_object_vmap(ved_priv->ccb1);
		if (!ccb1_addr) {
			ret = -ENOMEM;
			IPVR_ERROR("VED: kmap failed for ccb1 buffer: %d.\n", ret);
			goto err_free_ccb1;
		}

		memset(ccb1_addr, 0, size);
		vunmap(ccb1_addr);
	}

	IPVR_DEBUG_INIT("VED: RENDEC A: %08x RENDEC B: %08x\n",
			ved_priv->base_addr0, ved_priv->base_addr1);

	return 0;
err_free_ccb1:
	drm_gem_object_unreference_unlocked(&ved_priv->ccb1->base);
	ved_priv->ccb1 = NULL;
err_free_ccb0:
	drm_gem_object_unreference_unlocked(&ved_priv->ccb0->base);
	ved_priv->ccb0 = NULL;
	return ret;
}

static void ved_free_ccb(struct ved_private *ved_priv)
{
	if (ved_priv->ccb0) {
		drm_gem_object_unreference_unlocked(&ved_priv->ccb0->base);
		ved_priv->ccb0 = NULL;
	}
	if (ved_priv->ccb1) {
		drm_gem_object_unreference_unlocked(&ved_priv->ccb1->base);
		ved_priv->ccb1 = NULL;
	}
}

static void ved_rendec_init_by_reg(struct ved_private *ved_priv)
{
	u32 cmd;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	IPVR_REG_WRITE32(ved_priv->base_addr0, MSVDX_RENDEC_BASE_ADDR0_OFFSET);
	IPVR_REG_WRITE32(ved_priv->base_addr1, MSVDX_RENDEC_BASE_ADDR1_OFFSET);

	cmd = 0;
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_BUFFER_SIZE,
			RENDEC_BUFFER_SIZE0, RENDEC_A_SIZE / 4096);
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_BUFFER_SIZE,
			RENDEC_BUFFER_SIZE1, RENDEC_B_SIZE / 4096);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_BUFFER_SIZE_OFFSET);

	cmd = 0;
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_CONTROL1,
			RENDEC_DECODE_START_SIZE, 0);
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_CONTROL1,
			RENDEC_BURST_SIZE_W, 1);
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_CONTROL1,
			RENDEC_BURST_SIZE_R, 1);
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_CONTROL1,
			RENDEC_EXTERNAL_MEMORY, 1);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTROL1_OFFSET);

	cmd = 0x00101010;
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTEXT0_OFFSET);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTEXT1_OFFSET);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTEXT2_OFFSET);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTEXT3_OFFSET);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTEXT4_OFFSET);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTEXT5_OFFSET);

	cmd = 0;
	REGIO_WRITE_FIELD(cmd, MSVDX_RENDEC_CONTROL0, RENDEC_INITIALISE, 1);
	IPVR_REG_WRITE32(cmd, MSVDX_RENDEC_CONTROL0_OFFSET);
}

int ved_rendec_init_by_msg(struct ved_private *ved_priv)
{
	/* at this stage, FW is uplaoded successfully,
	 * can send RENDEC init message */
	struct fw_init_msg init_msg;
	init_msg.header.bits.msg_size = sizeof(struct fw_init_msg);
	init_msg.header.bits.msg_type = MTX_MSGID_INIT;
	init_msg.rendec_addr0 = ved_priv->base_addr0;
	init_msg.rendec_addr1 = ved_priv->base_addr1;
	init_msg.rendec_size.bits.rendec_size0 = RENDEC_A_SIZE / (4 * 1024);
	init_msg.rendec_size.bits.rendec_size1 = RENDEC_B_SIZE / (4 * 1024);
	return ved_mtx_send(ved_priv, (void *)&init_msg);
}

static void ved_get_mtx_control_from_dash(struct ved_private *ved_priv)
{
	int count = 0;
	u32 reg_val = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	REGIO_WRITE_FIELD(reg_val, MSVDX_MTX_DEBUG, MTX_DBG_IS_SLAVE, 1);
	REGIO_WRITE_FIELD(reg_val, MSVDX_MTX_DEBUG, MTX_DBG_GPIO_IN, 0x02);
	IPVR_REG_WRITE32(reg_val, MSVDX_MTX_DEBUG_OFFSET);

	do {
		reg_val = IPVR_REG_READ32(MSVDX_MTX_DEBUG_OFFSET);
		count++;
	} while (((reg_val & 0x18) != 0) && count < 50000);

	if (count >= 50000)
		IPVR_DEBUG_VED("VED: timeout in get_mtx_control_from_dash.\n");

	/* Save the access control register...*/
	ved_priv->ved_dash_access_ctrl = IPVR_REG_READ32(MTX_RAM_ACCESS_CONTROL_OFFSET);
}

static void
ved_release_mtx_control_from_dash(struct ved_private *ved_priv)
{
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	/* restore access control */
	IPVR_REG_WRITE32(ved_priv->ved_dash_access_ctrl, MTX_RAM_ACCESS_CONTROL_OFFSET);
	/* release bus */
	IPVR_REG_WRITE32(0x4, MSVDX_MTX_DEBUG_OFFSET);
}

/* for future debug info of msvdx related registers */
static void
ved_setup_fw_dump(struct ved_private *ved_priv, u32 dma_channel)
{
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	IPVR_DEBUG_REG("dump registers during fw upload for debug:\n");
	/* for DMAC REGISTER */
	IPVR_DEBUG_REG("MTX_SYSC_CDMAA is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_CDMAA_OFFSET));
	IPVR_DEBUG_REG("MTX_SYSC_CDMAC value is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_CDMAC_OFFSET));
	IPVR_DEBUG_REG("DMAC_SETUP value is 0x%x\n",
			IPVR_REG_READ32(DMAC_DMAC_SETUP_OFFSET + dma_channel));
	IPVR_DEBUG_REG("DMAC_DMAC_COUNT value is 0x%x\n",
			IPVR_REG_READ32(DMAC_DMAC_COUNT_OFFSET + dma_channel));
	IPVR_DEBUG_REG("DMAC_DMAC_PERIPH_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(DMAC_DMAC_PERIPH_OFFSET + dma_channel));
	IPVR_DEBUG_REG("DMAC_DMAC_PERIPHERAL_ADDR value is 0x%x\n",
			IPVR_REG_READ32(DMAC_DMAC_PERIPHERAL_ADDR_OFFSET +
				       dma_channel));
	IPVR_DEBUG_REG("MSVDX_CONTROL value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_CONTROL_OFFSET));
	IPVR_DEBUG_REG("DMAC_DMAC_IRQ_STAT value is 0x%x\n",
			IPVR_REG_READ32(DMAC_DMAC_IRQ_STAT_OFFSET));
	IPVR_DEBUG_REG("MSVDX_MMU_CONTROL0 value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_MMU_CONTROL0_OFFSET));
	IPVR_DEBUG_REG("DMAC_DMAC_COUNT 2222 value is 0x%x\n",
			IPVR_REG_READ32(DMAC_DMAC_COUNT_OFFSET + dma_channel));

	/* for MTX REGISTER */
	IPVR_DEBUG_REG("MTX_ENABLE_OFFSET is 0x%x\n",
			IPVR_REG_READ32(MTX_ENABLE_OFFSET));
	IPVR_DEBUG_REG("MTX_KICK_INPUT_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_KICK_INPUT_OFFSET));
	IPVR_DEBUG_REG("MTX_REG_READ_WRITE_REQUEST_OFFSET value is 0x%x\n",
		IPVR_REG_READ32(MTX_REGISTER_READ_WRITE_REQUEST_OFFSET));
	IPVR_DEBUG_REG("MTX_RAM_ACCESS_CONTROL_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_RAM_ACCESS_CONTROL_OFFSET));
	IPVR_DEBUG_REG("MTX_RAM_ACCESS_STATUS_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_RAM_ACCESS_STATUS_OFFSET));
	IPVR_DEBUG_REG("MTX_SYSC_TIMERDIV_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_TIMERDIV_OFFSET));
	IPVR_DEBUG_REG("MTX_SYSC_CDMAC_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_CDMAC_OFFSET));
	IPVR_DEBUG_REG("MTX_SYSC_CDMAA_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_CDMAA_OFFSET));
	IPVR_DEBUG_REG("MTX_SYSC_CDMAS0_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_CDMAS0_OFFSET));
	IPVR_DEBUG_REG("MTX_SYSC_CDMAT_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MTX_SYSC_CDMAT_OFFSET));

	/* for MSVDX CORE REGISTER */
	IPVR_DEBUG_REG("MSVDX_CONTROL_OFFSET is 0x%x\n",
			IPVR_REG_READ32(MSVDX_CONTROL_OFFSET));
	IPVR_DEBUG_REG("MSVDX_INTERRUPT_CLEAR_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_INTERRUPT_CLEAR_OFFSET));
	IPVR_DEBUG_REG("MSVDX_INTERRUPT_STATUS_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET));
	IPVR_DEBUG_REG("MMSVDX_HOST_INTERRUPT_ENABLE_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_HOST_INTERRUPT_ENABLE_OFFSET));
	IPVR_DEBUG_REG("MSVDX_MAN_CLK_ENABLE_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_MAN_CLK_ENABLE_OFFSET));
	IPVR_DEBUG_REG("MSVDX_CORE_ID_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_CORE_ID_OFFSET));
	IPVR_DEBUG_REG("MSVDX_MMU_STATUS_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_MMU_STATUS_OFFSET));
	IPVR_DEBUG_REG("FE_MSVDX_WDT_CONTROL_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(FE_MSVDX_WDT_CONTROL_OFFSET));
	IPVR_DEBUG_REG("FE_MSVDX_WDTIMER_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(FE_MSVDX_WDTIMER_OFFSET));
	IPVR_DEBUG_REG("BE_MSVDX_WDT_CONTROL_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(BE_MSVDX_WDT_CONTROL_OFFSET));
	IPVR_DEBUG_REG("BE_MSVDX_WDTIMER_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(BE_MSVDX_WDTIMER_OFFSET));

	/* for MSVDX RENDEC REGISTER */
	IPVR_DEBUG_REG("VEC_SHIFTREG_CONTROL_OFFSET is 0x%x\n",
			IPVR_REG_READ32(VEC_SHIFTREG_CONTROL_OFFSET));
	IPVR_DEBUG_REG("MSVDX_RENDEC_CONTROL0_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_CONTROL0_OFFSET));
	IPVR_DEBUG_REG("MSVDX_RENDEC_BUFFER_SIZE_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_BUFFER_SIZE_OFFSET));
	IPVR_DEBUG_REG("MSVDX_RENDEC_BASE_ADDR0_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_BASE_ADDR0_OFFSET));
	IPVR_DEBUG_REG("MMSVDX_RENDEC_BASE_ADDR1_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_BASE_ADDR1_OFFSET));
	IPVR_DEBUG_REG("MSVDX_RENDEC_READ_DATA_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_READ_DATA_OFFSET));
	IPVR_DEBUG_REG("MSVDX_RENDEC_CONTEXT0_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_CONTEXT0_OFFSET));
	IPVR_DEBUG_REG("MSVDX_RENDEC_CONTEXT1_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_RENDEC_CONTEXT1_OFFSET));
	IPVR_DEBUG_REG("MSVDX_CMDS_END_SLICE_PICTURE_OFFSET value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_CMDS_END_SLICE_PICTURE_OFFSET));

	IPVR_DEBUG_REG("MSVDX_MMU_MEM_REQ value is 0x%x\n",
			IPVR_REG_READ32(MSVDX_MMU_MEM_REQ_OFFSET));
	IPVR_DEBUG_REG("MSVDX_SYS_MEMORY_DEBUG2 value is 0x%x\n",
			IPVR_REG_READ32(0x6fc));
}

static void ved_upload_fw(struct ved_private *ved_priv,
				u32 address, const u32 words)
{
	u32 reg_val = 0;
	u32 cmd;
	u32 uCountReg, offset, mmu_ptd;
	u32 size = words * 4; /* byte count */
	u32 dma_channel = 0; /* Setup a Simple DMA for Ch0 */
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	IPVR_DEBUG_VED("VED: Upload firmware by DMA.\n");
	ved_get_mtx_control_from_dash(ved_priv);

	/*
	 * dma transfers to/from the mtx have to be 32-bit aligned and
	 * in multiples of 32 bits
	 */
	IPVR_REG_WRITE32(address, MTX_SYSC_CDMAA_OFFSET);

	/* burst size in multiples of 64 bits (allowed values are 2 or 4) */
	REGIO_WRITE_FIELD_LITE(reg_val, MTX_SYSC_CDMAC, BURSTSIZE, 4);
	/* false means write to mtx mem, true means read from mtx mem */
	REGIO_WRITE_FIELD_LITE(reg_val, MTX_SYSC_CDMAC, RNW, 0);
	/* begin transfer */
	REGIO_WRITE_FIELD_LITE(reg_val, MTX_SYSC_CDMAC, ENABLE,	1);
	/* specifies transfer size of the DMA operation by 32-bit words */
	REGIO_WRITE_FIELD_LITE(reg_val, MTX_SYSC_CDMAC, LENGTH,	words);
	IPVR_REG_WRITE32(reg_val, MTX_SYSC_CDMAC_OFFSET);

	/* toggle channel 0 usage between mtx and other msvdx peripherals */
	{
		reg_val = IPVR_REG_READ32(MSVDX_CONTROL_OFFSET);
		REGIO_WRITE_FIELD(reg_val, MSVDX_CONTROL, DMAC_CH0_SELECT,  0);
		IPVR_REG_WRITE32(reg_val, MSVDX_CONTROL_OFFSET);
	}

	/* Clear the DMAC Stats */
	IPVR_REG_WRITE32(0 , DMAC_DMAC_IRQ_STAT_OFFSET + dma_channel);

	offset = ved_priv->fw_offset;
	IPVR_DEBUG_VED("fw mmu offset is 0x%x.\n", offset);

	/* use bank 0 */
	cmd = 0;
	IPVR_REG_WRITE32(cmd, MSVDX_MMU_BANK_INDEX_OFFSET);

	/* Write PTD to mmu base 0*/
	mmu_ptd = ipvr_get_default_pd_addr32(ved_priv->dev_priv->mmu);
	BUG_ON(mmu_ptd == 0);
	IPVR_REG_WRITE32(mmu_ptd, MSVDX_MMU_DIR_LIST_BASE_OFFSET + 0);
	IPVR_DEBUG_VED("mmu_ptd is %d.\n", mmu_ptd);

	/* Invalidate */
	reg_val = IPVR_REG_READ32(MSVDX_MMU_CONTROL0_OFFSET);
	reg_val &= ~0xf;
	REGIO_WRITE_FIELD(reg_val, MSVDX_MMU_CONTROL0, MMU_INVALDC, 1);
	IPVR_REG_WRITE32(reg_val, MSVDX_MMU_CONTROL0_OFFSET);

	IPVR_REG_WRITE32(offset, DMAC_DMAC_SETUP_OFFSET + dma_channel);

	/* Only use a single dma - assert that this is valid */
	if ((size >> 2) >= (1 << 15)) {
		IPVR_ERROR("DMA size beyond limit, abort firmware upload.\n");
		return;
	}

	uCountReg = IPVR_DMAC_VALUE_COUNT(IPVR_DMAC_BSWAP_NO_SWAP, 0,
					 IPVR_DMAC_DIR_MEM_TO_PERIPH, 0, (size >> 2));
	/* Set the number of bytes to dma*/
	IPVR_REG_WRITE32(uCountReg, DMAC_DMAC_COUNT_OFFSET + dma_channel);

	cmd = IPVR_DMAC_VALUE_PERIPH_PARAM(IPVR_DMAC_ACC_DEL_0,
					  IPVR_DMAC_INCR_OFF,
					  IPVR_DMAC_BURST_2);
	IPVR_REG_WRITE32(cmd, DMAC_DMAC_PERIPH_OFFSET + dma_channel);

	/* Set destination port for dma */
	cmd = 0;
	REGIO_WRITE_FIELD(cmd, DMAC_DMAC_PERIPHERAL_ADDR, ADDR,
			  MTX_SYSC_CDMAT_OFFSET);
	IPVR_REG_WRITE32(cmd, DMAC_DMAC_PERIPHERAL_ADDR_OFFSET + dma_channel);


	/* Finally, rewrite the count register with the enable bit set */
	IPVR_REG_WRITE32(uCountReg | DMAC_DMAC_COUNT_EN_MASK,
			DMAC_DMAC_COUNT_OFFSET + dma_channel);

	/* Wait for all to be done */
	if (ved_wait_for_register(ved_priv,
				  DMAC_DMAC_IRQ_STAT_OFFSET + dma_channel,
				  DMAC_DMAC_IRQ_STAT_TRANSFER_FIN_MASK,
				  DMAC_DMAC_IRQ_STAT_TRANSFER_FIN_MASK,
				  2000000, 5)) {
		ved_setup_fw_dump(ved_priv, dma_channel);
		ved_release_mtx_control_from_dash(ved_priv);
		return;
	}

	/* Assert that the MTX DMA port is all done aswell */
	if (ved_wait_for_register(ved_priv,
			MTX_SYSC_CDMAS0_OFFSET,
			1, 1, 2000000, 5)) {
		ved_release_mtx_control_from_dash(ved_priv);
		return;
	}

	ved_release_mtx_control_from_dash(ved_priv);

	IPVR_DEBUG_VED("VED: Upload done\n");
}

static int ved_get_fw_bo(struct ved_private *ved_priv,
				   const struct firmware **raw, char *name)
{
	int rc = 0;
	size_t fw_size;
	void *ptr = NULL;
	void *fw_bo_addr = NULL;
	u32 *last_word;
	struct ved_fw *fw;

	rc = request_firmware(raw, name, &ved_priv->dev_priv->dev->platformdev->dev);
	if (rc)
		return rc;

	if (!*raw) {
		rc = -ENOMEM;
		IPVR_ERROR("VED: %s request_firmware failed: Reason %d.\n", name, rc);
		goto out;
	}

	if ((*raw)->size < sizeof(struct ved_fw)) {
		rc = -ENOMEM;
		IPVR_ERROR("VED: %s is is not correct size(%zd).\n", name, (*raw)->size);
		goto out;
	}

	ptr = (void *)((*raw))->data;
	if (!ptr) {
		rc = -ENOMEM;
		IPVR_ERROR("VED: Failed to load %s.\n", name);
		goto out;
	}

	/* another sanity check... */
	fw_size = sizeof(struct ved_fw) +
		  sizeof(u32) * ((struct ved_fw *) ptr)->text_size +
		  sizeof(u32) * ((struct ved_fw *) ptr)->data_size;
	if ((*raw)->size < fw_size) {
		rc = -ENOMEM;
		IPVR_ERROR("VED: %s is is not correct size(%zd).\n",
			  name, (*raw)->size);
		goto out;
	}

	fw_bo_addr = ipvr_gem_object_vmap(ved_priv->fw_bo);
	if (!fw_bo_addr) {
		rc = -ENOMEM;
		IPVR_ERROR("VED: kmap failed for fw buffer.\n");
		goto out;
	}

	fw = (struct ved_fw *)ptr;
	memset(fw_bo_addr, UNINITILISE_MEM, ved_priv->mtx_mem_size);
	memcpy(fw_bo_addr, ptr + sizeof(struct ved_fw),
	       sizeof(u32) * fw->text_size);
	memcpy(fw_bo_addr + (fw->data_location - MSVDX_MTX_DATA_LOCATION),
	       (void *)ptr + sizeof(struct ved_fw) + sizeof(u32) * fw->text_size,
	       sizeof(u32) * fw->data_size);
	last_word = (u32 *)(fw_bo_addr + ved_priv->mtx_mem_size - 4);
	/*
	 * Write a know value to last word in mtx memory
	 * Usefull for detection of stack overrun
	 */
	*last_word = STACKGUARDWORD;

	vunmap(fw_bo_addr);
	IPVR_DEBUG_VED("VED: releasing firmware resouces.\n");
	IPVR_DEBUG_VED("VED: Load firmware into BO successfully.\n");
out:
	release_firmware(*raw);
	return rc;
}

static u32 *
ved_get_fw(struct ved_private *ved_priv, const struct firmware **raw, char *name)
{
	int rc = 0;
	size_t fw_size;
	void *ptr = NULL;
	struct ved_fw *fw;
	ved_priv->ved_fw_ptr = NULL;

	rc = request_firmware(raw, name, &ved_priv->dev_priv->dev->platformdev->dev);
	if (rc)
		return NULL;

	if (!*raw) {
		IPVR_ERROR("VED: %s request_firmware failed: Reason %d\n",
			  name, rc);
		goto out;
	}

	if ((*raw)->size < sizeof(struct ved_fw)) {
		IPVR_ERROR("VED: %s is is not correct size(%zd)\n",
			  name, (*raw)->size);
		goto out;
	}

	ptr = (int *)((*raw))->data;
	if (!ptr) {
		IPVR_ERROR("VED: Failed to load %s.\n", name);
		goto out;
	}
	fw = (struct ved_fw *)ptr;

	/* another sanity check... */
	fw_size = sizeof(fw) +
		  sizeof(u32) * fw->text_size +
		  sizeof(u32) * fw->data_size;
	if ((*raw)->size < fw_size) {
		IPVR_ERROR("VED: %s is is not correct size(%zd).\n",
			   name, (*raw)->size);
		goto out;
	}

	ved_priv->ved_fw_ptr = kzalloc(fw_size, GFP_KERNEL);
	if (!ved_priv->ved_fw_ptr)
		IPVR_ERROR("VED: allocate FW buffer failed.\n");
	else {
		memcpy(ved_priv->ved_fw_ptr, ptr, fw_size);
		ved_priv->ved_fw_size = fw_size;
	}

out:
	IPVR_DEBUG_VED("VED: releasing firmware resouces.\n");
	release_firmware(*raw);
	return ved_priv->ved_fw_ptr;
}

static void
ved_write_mtx_core_reg(struct ved_private *ved_priv,
			       const u32 core_reg, const u32 val)
{
	u32 reg = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	/* Put data in MTX_RW_DATA */
	IPVR_REG_WRITE32(val, MTX_REGISTER_READ_WRITE_DATA_OFFSET);

	/* DREADY is set to 0 and request a write */
	reg = core_reg;
	REGIO_WRITE_FIELD_LITE(reg, MTX_REGISTER_READ_WRITE_REQUEST,
			       MTX_RNW, 0);
	REGIO_WRITE_FIELD_LITE(reg, MTX_REGISTER_READ_WRITE_REQUEST,
			       MTX_DREADY, 0);
	IPVR_REG_WRITE32(reg, MTX_REGISTER_READ_WRITE_REQUEST_OFFSET);

	ved_wait_for_register(ved_priv,
			      MTX_REGISTER_READ_WRITE_REQUEST_OFFSET,
			      MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK,
			      MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK,
			      2000000, 5);
}

int ved_alloc_fw_bo(struct ved_private *ved_priv)
{
	u32 core_rev;
	int ret;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	core_rev = IPVR_REG_READ32(MSVDX_CORE_REV_OFFSET);

	if ((core_rev & 0xffffff) < 0x020000)
		ved_priv->mtx_mem_size = 16 * 1024;
	else
		ved_priv->mtx_mem_size = 56 * 1024;

	IPVR_DEBUG_INIT("VED: MTX mem size is 0x%08x bytes,"
			"allocate firmware BO size 0x%08x.\n",
			ved_priv->mtx_mem_size,
			ved_priv->mtx_mem_size + 4096);

	/* Allocate the new object */
	ved_priv->fw_bo = ipvr_gem_create(ved_priv->dev_priv,
						ved_priv->mtx_mem_size + 4096, 0, 0);
	if (IS_ERR(ved_priv->fw_bo)) {
		IPVR_ERROR("VED: failed to allocate fw buffer: %ld.\n",
			PTR_ERR(ved_priv->fw_bo));
		ved_priv->fw_bo = NULL;
		return -ENOMEM;
	}
	ved_priv->fw_offset = ipvr_gem_object_mmu_offset(ved_priv->fw_bo);
	if (IPVR_IS_ERR(ved_priv->fw_offset)) {
		ved_priv->fw_bo = NULL;
		goto err_free_fw_bo;
	}
	return 0;
err_free_fw_bo:
	drm_gem_object_unreference_unlocked(&ved_priv->fw_bo->base);
	return ret;
}

int ved_setup_fw(struct ved_private *ved_priv)
{
	u32 ram_bank_size;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	int ret = 0;
	struct ved_fw *fw;
	u32 *fw_ptr = NULL;
	u32 *text_ptr = NULL;
	u32 *data_ptr = NULL;
	const struct firmware *raw = NULL;

	/* todo : Assert the clock is on - if not turn it on to upload code */
	IPVR_DEBUG_VED("VED: ved_setup_fw.\n");

	ved_set_clocks(ved_priv, clk_enable_all);

	/* Reset MTX */
	IPVR_REG_WRITE32(MTX_SOFT_RESET_MTX_RESET_MASK,
			MTX_SOFT_RESET_OFFSET);

	IPVR_REG_WRITE32(FIRMWAREID, MSVDX_COMMS_FIRMWARE_ID);

	IPVR_REG_WRITE32(0, MSVDX_COMMS_ERROR_TRIG);
	IPVR_REG_WRITE32(199, MTX_SYSC_TIMERDIV_OFFSET); /* MTX_SYSC_TIMERDIV */
	IPVR_REG_WRITE32(0, MSVDX_EXT_FW_ERROR_STATE); /* EXT_FW_ERROR_STATE */
	IPVR_REG_WRITE32(0, MSVDX_COMMS_MSG_COUNTER);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_SIGNATURE);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_TO_HOST_RD_INDEX);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_TO_HOST_WRT_INDEX);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_TO_MTX_RD_INDEX);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_TO_MTX_WRT_INDEX);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_FW_STATUS);
	IPVR_REG_WRITE32(DSIABLE_IDLE_GPIO_SIG |
			DSIABLE_Auto_CLOCK_GATING |
			RETURN_VDEB_DATA_IN_COMPLETION |
			NOT_ENABLE_ON_HOST_CONCEALMENT,
			MSVDX_COMMS_OFFSET_FLAGS);
	IPVR_REG_WRITE32(0, MSVDX_COMMS_SIGNATURE);

	/* read register bank size */
	{
		u32 bank_size, reg;
		reg = IPVR_REG_READ32(MSVDX_MTX_RAM_BANK_OFFSET);
		bank_size =
			REGIO_READ_FIELD(reg, MSVDX_MTX_RAM_BANK,
					 MTX_RAM_BANK_SIZE);
		ram_bank_size = (u32)(1 << (bank_size + 2));
	}

	IPVR_DEBUG_VED("VED: RAM bank size = %d bytes\n", ram_bank_size);

	/* if FW already loaded from storage */
	if (ved_priv->ved_fw_ptr) {
		fw_ptr = ved_priv->ved_fw_ptr;
	} else {
		fw_ptr = ved_get_fw(ved_priv, &raw, FIRMWARE_NAME);
		IPVR_DEBUG_VED("VED:load msvdx_fw_mfld_DE2.0.bin by udevd\n");
	}
	if (!fw_ptr) {
		IPVR_ERROR("VED:load ved_fw.bin failed,is udevd running?\n");
		ret = 1;
		goto out;
	}

	if (!ved_priv->fw_loaded_to_bo) { /* Load firmware into BO */
		IPVR_DEBUG_VED("MSVDX:load ved_fw.bin by udevd into BO\n");
		ret = ved_get_fw_bo(ved_priv, &raw, FIRMWARE_NAME);
		if (ret) {
			IPVR_ERROR("VED: failed to call ved_get_fw_bo: %d.\n", ret);
			goto out;
		}
		ved_priv->fw_loaded_to_bo = true;
	}

	fw = (struct ved_fw *) fw_ptr;

	/* need check fw->ver? */
	text_ptr = (u32 *)((u8 *) fw_ptr + sizeof(struct ved_fw));
	data_ptr = text_ptr + fw->text_size;

	/* maybe we can judge fw version according to fw text size */

	IPVR_DEBUG_VED("VED: Retrieved pointers for firmware\n");
	IPVR_DEBUG_VED("VED: text_size: %d\n", fw->text_size);
	IPVR_DEBUG_VED("VED: data_size: %d\n", fw->data_size);
	IPVR_DEBUG_VED("VED: data_location: 0x%x\n", fw->data_location);
	IPVR_DEBUG_VED("VED: First 4 bytes of text: 0x%x\n", *text_ptr);
	IPVR_DEBUG_VED("VED: First 4 bytes of data: 0x%x\n", *data_ptr);
	IPVR_DEBUG_VED("VED: Uploading firmware\n");

	ved_upload_fw(ved_priv, 0, ved_priv->mtx_mem_size / 4);

	/*	-- Set starting PC address	*/
	ved_write_mtx_core_reg(ved_priv, MTX_PC, PC_START_ADDRESS);

	/*	-- Turn on the thread	*/
	IPVR_REG_WRITE32(MTX_ENABLE_MTX_ENABLE_MASK, MTX_ENABLE_OFFSET);

	/* Wait for the signature value to be written back */
	ret = ved_wait_for_register(ved_priv, MSVDX_COMMS_SIGNATURE,
				    MSVDX_COMMS_SIGNATURE_VALUE,
				    0xffffffff, /* Enabled bits */
				    2000000, 5);
	if (ret) {
		IPVR_ERROR("VED: firmware fails to initialize.\n");
		goto out;
	}

	IPVR_DEBUG_VED("VED: MTX Initial indications OK.\n");
	IPVR_DEBUG_VED("VED: MSVDX_COMMS_AREA_ADDR = %08x.\n",
		       MSVDX_COMMS_AREA_ADDR);
out:
	/* no need to put fw bo, we will do it at driver unload */
	return ret;
}


/* This value is hardcoded in FW */
#define WDT_CLOCK_DIVIDER 128
int ved_post_boot_init(struct ved_private *ved_priv)
{
	u32 device_node_flags =
			DSIABLE_IDLE_GPIO_SIG | DSIABLE_Auto_CLOCK_GATING |
			RETURN_VDEB_DATA_IN_COMPLETION |
			NOT_ENABLE_ON_HOST_CONCEALMENT;
	int reg_val = 0;

	/* DDK set fe_wdt_clks as 0x820 and be_wdt_clks as 0x8200 */
	u32 fe_wdt_clks = 0x334 * WDT_CLOCK_DIVIDER;
	u32 be_wdt_clks = 0x2008 * WDT_CLOCK_DIVIDER;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	IPVR_REG_WRITE32(FIRMWAREID, MSVDX_COMMS_FIRMWARE_ID);
	IPVR_REG_WRITE32(device_node_flags, MSVDX_COMMS_OFFSET_FLAGS);

	/* read register bank size */
	{
		u32 ram_bank_size;
		u32 bank_size, reg;
		reg = IPVR_REG_READ32(MSVDX_MTX_RAM_BANK_OFFSET);
		bank_size =
			REGIO_READ_FIELD(reg, MSVDX_MTX_RAM_BANK,
					 MTX_RAM_BANK_SIZE);
		ram_bank_size = (u32)(1 << (bank_size + 2));
		IPVR_DEBUG_INIT("VED: RAM bank size = %d bytes\n",
				ram_bank_size);
	}
	/* host end */

	/* DDK setup tiling region here */
	/* DDK set MMU_CONTROL2 register */

	/* set watchdog timer here */
	REGIO_WRITE_FIELD(reg_val, FE_MSVDX_WDT_CONTROL,
			  FE_WDT_CNT_CTRL, 0x3);
	REGIO_WRITE_FIELD(reg_val, FE_MSVDX_WDT_CONTROL,
			  FE_WDT_ENABLE, 0);
	REGIO_WRITE_FIELD(reg_val, FE_MSVDX_WDT_CONTROL,
			  FE_WDT_ACTION0, 1);
	REGIO_WRITE_FIELD(reg_val, FE_MSVDX_WDT_CONTROL,
			  FE_WDT_CLEAR_SELECT, 1);
	REGIO_WRITE_FIELD(reg_val, FE_MSVDX_WDT_CONTROL,
			  FE_WDT_CLKDIV_SELECT, 7);
	IPVR_REG_WRITE32(fe_wdt_clks / WDT_CLOCK_DIVIDER,
			FE_MSVDX_WDT_COMPAREMATCH_OFFSET);
	IPVR_REG_WRITE32(reg_val, FE_MSVDX_WDT_CONTROL_OFFSET);

	reg_val = 0;
	/* DDK set BE_WDT_CNT_CTRL as 0x5 and BE_WDT_CLEAR_SELECT as 0x1 */
	REGIO_WRITE_FIELD(reg_val, BE_MSVDX_WDT_CONTROL,
			  BE_WDT_CNT_CTRL, 0x7);
	REGIO_WRITE_FIELD(reg_val, BE_MSVDX_WDT_CONTROL,
			  BE_WDT_ENABLE, 0);
	REGIO_WRITE_FIELD(reg_val, BE_MSVDX_WDT_CONTROL,
			  BE_WDT_ACTION0, 1);
	REGIO_WRITE_FIELD(reg_val, BE_MSVDX_WDT_CONTROL,
			  BE_WDT_CLEAR_SELECT, 0xd);
	REGIO_WRITE_FIELD(reg_val, BE_MSVDX_WDT_CONTROL,
			  BE_WDT_CLKDIV_SELECT, 7);

	IPVR_REG_WRITE32(be_wdt_clks / WDT_CLOCK_DIVIDER,
			BE_MSVDX_WDT_COMPAREMATCH_OFFSET);
	IPVR_REG_WRITE32(reg_val, BE_MSVDX_WDT_CONTROL_OFFSET);

	return ved_rendec_init_by_msg(ved_priv);
}

int ved_post_init(struct ved_private *ved_priv)
{
	u32 cmd;
	int ret;
	struct drm_ipvr_private *dev_priv;

	if (!ved_priv)
		return -EINVAL;

	ved_priv->ved_busy = false;
	dev_priv = ved_priv->dev_priv;

	/* Enable MMU by removing all bypass bits */
	IPVR_REG_WRITE32(0, MSVDX_MMU_CONTROL0_OFFSET);

	ved_rendec_init_by_reg(ved_priv);
	if (!ved_priv->fw_bo) {
		ret = ved_alloc_fw_bo(ved_priv);
		if (ret) {
			IPVR_ERROR("VED: ved_alloc_fw_bo failed: %d.\n", ret);
			return ret;
		}
	}
	/* move fw loading to the place receiving first cmd buffer */
	ved_priv->ved_fw_loaded = false; /* need to load firware */
	/* it should be set at punit post boot init phase */
	IPVR_REG_WRITE32(820, FE_MSVDX_WDT_COMPAREMATCH_OFFSET);
	IPVR_REG_WRITE32(8200, BE_MSVDX_WDT_COMPAREMATCH_OFFSET);

	IPVR_REG_WRITE32(820, FE_MSVDX_WDT_COMPAREMATCH_OFFSET);
	IPVR_REG_WRITE32(8200, BE_MSVDX_WDT_COMPAREMATCH_OFFSET);

	ved_clear_irq(ved_priv);
	ved_enable_irq(ved_priv);

	cmd = 0;
	cmd = IPVR_REG_READ32(VEC_SHIFTREG_CONTROL_OFFSET);
	REGIO_WRITE_FIELD(cmd, VEC_SHIFTREG_CONTROL,
	  SR_MASTER_SELECT, 1);  /* Host */
	IPVR_REG_WRITE32(cmd, VEC_SHIFTREG_CONTROL_OFFSET);

	return 0;
}

int __must_check ved_core_init(struct drm_ipvr_private *dev_priv)
{
	int ret;
	struct ved_private *ved_priv;
	if (!dev_priv->ved_private) {
		ved_priv = kzalloc(sizeof(struct ved_private), GFP_KERNEL);
		if (!ved_priv) {
			IPVR_ERROR("VED: alloc ved_private failed.\n");
			return -ENOMEM;
		}

		dev_priv->ved_private = ved_priv;
		ved_priv->dev_priv = dev_priv;

		/* Initialize comand ved queueing */
		INIT_LIST_HEAD(&ved_priv->ved_queue);
		mutex_init(&ved_priv->ved_mutex);
		spin_lock_init(&ved_priv->ved_lock);
		ved_priv->mmu_recover_page = alloc_page(GFP_DMA32);
		if (!ved_priv->mmu_recover_page) {
			ret = -ENOMEM;
			IPVR_ERROR("VED: alloc mmu_recover_page failed: %d.\n", ret);
			goto err_free_ved_priv;
		}
		IPVR_DEBUG_INIT("VED: successfully initialized ved_private.\n");
		dev_priv->ved_private= ved_priv;
	}
	ved_priv = dev_priv->ved_private;

	ret = ved_alloc_ccb_for_rendec(dev_priv->ved_private,
			RENDEC_A_SIZE, RENDEC_B_SIZE);
	if (unlikely(ret)) {
		IPVR_ERROR("VED: msvdx_alloc_ccb_for_rendec failed: %d.\n", ret);
		goto err_free_mmu_recover_page;
	}

	ret = ved_post_init(ved_priv);
	if (unlikely(ret)) {
		IPVR_ERROR("VED: ved_post_init failed: %d.\n", ret);
		goto err_free_ccb;
	}

	return 0;
err_free_ccb:
	ved_free_ccb(ved_priv);
err_free_mmu_recover_page:
	__free_page(ved_priv->mmu_recover_page);
err_free_ved_priv:
	kfree(ved_priv);
	dev_priv->ved_private = NULL;
	return ret;
}

int ved_core_deinit(struct drm_ipvr_private *dev_priv)
{
	struct ved_private *ved_priv = dev_priv->ved_private;
	if (NULL == ved_priv) {
		IPVR_ERROR("VED: ved_priv is NULL!\n");
		return -1;
	}

	IPVR_DEBUG_INIT("VED: set the VED clock to 0.\n");
	ved_set_clocks(ved_priv, 0);

	if (ved_priv->ccb0 || ved_priv->ccb1)
		ved_free_ccb(ved_priv);

	if (ved_priv->fw_bo) {
		drm_gem_object_unreference_unlocked(&ved_priv->fw_bo->base);
		ved_priv->fw_bo = NULL;
	}

	if (ved_priv->ved_fw_ptr)
		kfree(ved_priv->ved_fw_ptr);

	if (ved_priv->mmu_recover_page)
		__free_page(ved_priv->mmu_recover_page);

	kfree(ved_priv);
	dev_priv->ved_private = NULL;

	return 0;
}
