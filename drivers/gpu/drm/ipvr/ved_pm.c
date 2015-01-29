/**************************************************************************
 * ved_pm.c: VED power management support
 *
 * Copyright (c) 2014 Intel Corporation, Hillsboro, OR, USA
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
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/


#include "ipvr_trace.h"
#include "ved_pm.h"
#include "ved_reg.h"
#include "ved_cmd.h"
#include "ved_fw.h"
#ifdef CONFIG_INTEL_SOC_PMC
#include <linux/intel_mid_pm.h>
#endif
#include <linux/module.h>
#include <linux/pm_runtime.h>

extern int drm_ipvr_freq;

#define PCI_ROOT_MSGBUS_CTRL_REG      0xD0
#define PCI_ROOT_MSGBUS_DATA_REG      0xD4
#define PCI_ROOT_MSGBUS_CTRL_EXT_REG  0xD8
#define PCI_ROOT_MSGBUS_READ          0x10
#define PCI_ROOT_MSGBUS_WRITE         0x11
#define PCI_ROOT_MSGBUS_DWORD_ENABLE  0xf0

/* VED power state set/get */
#define PUNIT_PORT			0x04
#define VEDSSPM0 			0x32
#define VEDSSPM1 			0x33
#define VEDSSC				0x1

/* VED frequency set/get */
#define IP_FREQ_VALID     0x80     /* Freq is valid bit */

#define IP_FREQ_SIZE         5     /* number of bits in freq fields */
#define IP_FREQ_MASK      0x1f     /* Bit mask for freq field */

/*  Positions of various frequency fields */
#define IP_FREQ_POS          0     /* Freq control [4:0] */
#define IP_FREQ_GUAR_POS     8     /* Freq guar   [12:8] */
#define IP_FREQ_STAT_POS    24     /* Freq status [28:24] */

enum APM_VED_STATUS {
	VED_APM_STS_D0 = 0,
	VED_APM_STS_D1,
	VED_APM_STS_D2,
	VED_APM_STS_D3
};

#define GET_FREQ_NUMBER(freq_code)	((1600 * 2)/((freq_code) + 1))
/* valid freq code: 0x9, 0xb, 0xd, 0xf, 0x11, 0x13 */
#define FREQ_CODE_CLAMP(code) ((code < 0x9)? 0x9: ((code > 0x13)? 0x13: code))
#define GET_FREQ_CODE(freq_num)	FREQ_CODE_CLAMP((((1600 * 2/freq_num + 1) >> 1) << 1) - 1)

#ifdef CONFIG_INTEL_SOC_PMC
extern int pmc_nc_set_power_state(int islands, int state_type, int reg);
extern int pmc_nc_get_power_state(int islands, int reg);
#endif

static int ved_save_context(struct ved_private *ved_priv)
{
	int offset;
	int ret;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;

	ved_priv->ved_needs_reset = 1;
	/* Reset MTX */
	IPVR_REG_WRITE32(MTX_SOFT_RESET_MTXRESET, MTX_SOFT_RESET_OFFSET);

	/* why need reset msvdx before power off it, need check IMG */
	ret = ved_core_reset(ved_priv);
	if (unlikely(ret))
		IPVR_DEBUG_WARN("failed to call ved_core_reset: %d\n", ret);

	/* Initialize VEC Local RAM */
	for (offset = 0; offset < VEC_LOCAL_MEM_BYTE_SIZE / 4; ++offset)
		IPVR_REG_WRITE32(0, VEC_LOCAL_MEM_OFFSET + offset * 4);

	return 0;
}

static u32 ipvr_msgbus_read32(struct pci_dev *pci_root, u8 port, u32 addr)
{
    u32 data;
    u32 cmd;
    u32 cmdext;

    cmd = (PCI_ROOT_MSGBUS_READ << 24) | (port << 16) |
        ((addr & 0xff) << 8) | PCI_ROOT_MSGBUS_DWORD_ENABLE;
    cmdext = addr & 0xffffff00;

    if (cmdext) {
        /* This resets to 0 automatically, no need to write 0 */
        pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_EXT_REG,
                    cmdext);
    }

    pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
    pci_read_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, &data);

    return data;
}

static void ipvr_msgbus_write32(struct pci_dev *pci_root, u8 port, u32 addr, u32 data)
{
    u32 cmd;
    u32 cmdext;

    cmd = (PCI_ROOT_MSGBUS_WRITE << 24) | (port << 16) |
        ((addr & 0xFF) << 8) | PCI_ROOT_MSGBUS_DWORD_ENABLE;
    cmdext = addr & 0xffffff00;

    pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, data);

    if (cmdext) {
        /* This resets to 0 automatically, no need to write 0 */
        pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_EXT_REG,
            cmdext);
    }

    pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
}

static int ipvr_pm_cmd_freq_wait(struct pci_dev *pci_root, u32 reg_freq, u32 *freq_code_rlzd)
{
	int tcount;
	u32 freq_val;

	for (tcount = 0; ; tcount++) {
		freq_val = ipvr_msgbus_read32(pci_root, PUNIT_PORT, reg_freq);
		if ((freq_val & IP_FREQ_VALID) == 0)
			break;
		if (tcount > 500) {
			IPVR_ERROR("P-Unit freq request wait timeout %x",
				freq_val);
			return -EBUSY;
		}
		udelay(1);
	}

	if (freq_code_rlzd) {
		*freq_code_rlzd = ((freq_val >> IP_FREQ_STAT_POS) &
			IP_FREQ_MASK);
	}

	return 0;
}

static int ipvr_pm_cmd_freq_get(struct pci_dev *pci_root, u32 reg_freq)
{
	u32 freq_val;
	int freq_code = 0;

	ipvr_pm_cmd_freq_wait(pci_root, reg_freq, NULL);

	freq_val = ipvr_msgbus_read32(pci_root, PUNIT_PORT, reg_freq);
	freq_code =(int)((freq_val>>IP_FREQ_STAT_POS) & ~IP_FREQ_VALID);
	return freq_code;
}

static int ipvr_pm_cmd_freq_set(struct pci_dev *pci_root, u32 reg_freq, u32 freq_code, u32 *p_freq_code_rlzd)
{
	u32 freq_val;
	u32 freq_code_realized;
	int rva;

	rva = ipvr_pm_cmd_freq_wait(pci_root, reg_freq, NULL);
	if (rva < 0) {
		IPVR_ERROR("pm_cmd_freq_wait 1 failed: %d\n", rva);
		return rva;
	}

	freq_val = IP_FREQ_VALID | freq_code;
	ipvr_msgbus_write32(pci_root, PUNIT_PORT, reg_freq, freq_val);

	rva = ipvr_pm_cmd_freq_wait(pci_root, reg_freq, &freq_code_realized);
	if (rva < 0) {
		IPVR_ERROR("pm_cmd_freq_wait 2 failed: %d\n", rva);
		return rva;
	}

	if (p_freq_code_rlzd)
		*p_freq_code_rlzd = freq_code_realized;

	return rva;
}

static int ved_set_freq(struct drm_ipvr_private *dev_priv, u32 freq_code)
{
	u32 freq_code_rlzd = 0;
	int ret;

	ret = ipvr_pm_cmd_freq_set(dev_priv->pci_root,
		VEDSSPM1, freq_code, &freq_code_rlzd);
	if (ret < 0) {
		IPVR_ERROR("failed to set freqency, current is %x\n",
			freq_code_rlzd);
	}

	return ret;
}

static int ved_get_freq(struct drm_ipvr_private *dev_priv)
{
	return ipvr_pm_cmd_freq_get(dev_priv->pci_root, VEDSSPM1);
}

#ifdef CONFIG_INTEL_SOC_PMC
static inline bool do_power_on(struct drm_ipvr_private *dev_priv)
{
	if (pmc_nc_set_power_state(VEDSSC, 0, VEDSSPM0)) {
		IPVR_ERROR("VED: pmu_nc_set_power_state ON fail!\n");
		return false;
	}
	return true;
}
static inline bool do_power_off(struct drm_ipvr_private *dev_priv)
{
	if (pmc_nc_set_power_state(VEDSSC, 1, VEDSSPM0)) {
		IPVR_ERROR("VED: pmu_nc_set_power_state OFF fail!\n");
		return false;
	}
	return true;
}
#else
static inline bool do_power_on(struct drm_ipvr_private *dev_priv)
{
	u32 pwr_sts;
	do {
		ipvr_msgbus_write32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0, VED_APM_STS_D0);
		udelay(10);
		pwr_sts = ipvr_msgbus_read32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0);
	} while (pwr_sts != 0x0);
	do {
		ipvr_msgbus_write32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0, VED_APM_STS_D3);
		udelay(10);
		pwr_sts = ipvr_msgbus_read32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0);
	} while (pwr_sts != 0x03000003);
	do {
		ipvr_msgbus_write32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0, VED_APM_STS_D0);
		udelay(10);
		pwr_sts = ipvr_msgbus_read32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0);
	} while (pwr_sts != 0x0);
	return true;
}
static inline bool do_power_off(struct drm_ipvr_private *dev_priv)
{
	u32 pwr_sts;
	do {
		ipvr_msgbus_write32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0, VED_APM_STS_D3);
		udelay(10);
		pwr_sts = ipvr_msgbus_read32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0);
	} while (pwr_sts != 0x03000003);
	return true;
}
#endif

bool ved_power_on(struct drm_ipvr_private *dev_priv)
{
	int ved_freq_code_before, ved_freq_code_requested, ved_freq_code_after;
	IPVR_DEBUG_PM("VED: power on msvdx.\n");

	if (dev_priv->ved_private)
		dev_priv->ved_private->ved_busy = false;
	if (!do_power_on(dev_priv))
		return false;

	ved_freq_code_before = ved_get_freq(dev_priv);
	ved_freq_code_requested = GET_FREQ_CODE(drm_ipvr_freq);
	if (ved_set_freq(dev_priv, ved_freq_code_requested)) {
		IPVR_ERROR("Failed to set VED frequency\n");
	}

	ved_freq_code_after = ved_get_freq(dev_priv);
	IPVR_DEBUG_PM("VED freqency requested %dMHz: actual %dMHz => %dMHz\n",
		drm_ipvr_freq, GET_FREQ_NUMBER(ved_freq_code_before),
		GET_FREQ_NUMBER(ved_freq_code_after));
	drm_ipvr_freq = GET_FREQ_NUMBER(ved_freq_code_after);

	trace_ved_power_on(drm_ipvr_freq);
	return true;
}

bool ved_power_off(struct drm_ipvr_private *dev_priv)
{
	int ved_freq_code;
	int ret;
	IPVR_DEBUG_PM("VED: power off msvdx.\n");

	if (dev_priv->ved_private) {
		ret = ved_save_context(dev_priv->ved_private);
		if (unlikely(ret)) {
			IPVR_ERROR("Failed to save VED context: %d, stop powering off\n", ret);
			return false;
		}
		dev_priv->ved_private->ved_busy = false;
	}

	ved_freq_code = ved_get_freq(dev_priv);
	drm_ipvr_freq = GET_FREQ_NUMBER(ved_freq_code);
	IPVR_DEBUG_PM("VED freqency: code %d (%dMHz)\n", ved_freq_code, drm_ipvr_freq);

	if (!do_power_off(dev_priv))
		return false;

	trace_ved_power_off(drm_ipvr_freq);
	return true;
}

bool is_ved_on(struct drm_ipvr_private *dev_priv)
{
	u32 pwr_sts;
	pwr_sts = ipvr_msgbus_read32(dev_priv->pci_root, PUNIT_PORT, VEDSSPM0);
	return (pwr_sts == VED_APM_STS_D0);
}
