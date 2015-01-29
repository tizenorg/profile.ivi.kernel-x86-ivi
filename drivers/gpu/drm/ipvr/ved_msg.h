/**************************************************************************
 * ved_msg.h: VED message definition
 *
 * Copyright (c) 2014 Intel Corporation, Hillsboro, OR, USA
 * Copyright (c) 2003 Imagination Technologies Limited, UK
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
 *    Li Zeng <li.zeng@intel.com>
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/

#ifndef _VED_MSG_H_
#define _VED_MSG_H_

/* Start of parser specific Host->MTX messages. */
#define	FWRK_MSGID_START_PSR_HOSTMTX_MSG	(0x80)

/* Start of parser specific MTX->Host messages. */
#define	FWRK_MSGID_START_PSR_MTXHOST_MSG	(0xC0)

/* Host defined msg, just for host use, MTX not recgnize */
#define	FWRK_MSGID_HOST_EMULATED		(0x40)

/* This type defines the framework specified message ids */
enum {
	/* ! Sent by the VA driver on the host to the mtx firmware.
	 */
	MTX_MSGID_PADDING = 0,
	MTX_MSGID_INIT = FWRK_MSGID_START_PSR_HOSTMTX_MSG,
	MTX_MSGID_DECODE_FE,
	MTX_MSGID_DEBLOCK,
	MTX_MSGID_INTRA_OOLD,
	MTX_MSGID_DECODE_BE,
	MTX_MSGID_HOST_BE_OPP,

	/*! Sent by the mtx firmware to itself.
	 */
	MTX_MSGID_RENDER_MC_INTERRUPT,

	/* used to ditinguish mrst and mfld */
	MTX_MSGID_DEBLOCK_MFLD = FWRK_MSGID_HOST_EMULATED,
	MTX_MSGID_INTRA_OOLD_MFLD,
	MTX_MSGID_DECODE_BE_MFLD,
	MTX_MSGID_HOST_BE_OPP_MFLD,

	/*! Sent by the DXVA firmware on the MTX to the host.
	 */
	MTX_MSGID_COMPLETED = FWRK_MSGID_START_PSR_MTXHOST_MSG,
	MTX_MSGID_COMPLETED_BATCH,
	MTX_MSGID_DEBLOCK_REQUIRED,
	MTX_MSGID_TEST_RESPONCE,
	MTX_MSGID_ACK,
	MTX_MSGID_FAILED,
	MTX_MSGID_CONTIGUITY_WARNING,
	MTX_MSGID_HW_PANIC,
};

#define MTX_GENMSG_SIZE_TYPE		u8
#define MTX_GENMSG_SIZE_MASK		(0xFF)
#define MTX_GENMSG_SIZE_SHIFT		(0)
#define MTX_GENMSG_SIZE_OFFSET		(0x0000)

#define MTX_GENMSG_ID_TYPE		u8
#define MTX_GENMSG_ID_MASK		(0xFF)
#define MTX_GENMSG_ID_SHIFT		(0)
#define MTX_GENMSG_ID_OFFSET		(0x0001)

#define MTX_GENMSG_HEADER_SIZE		2

#define MTX_GENMSG_FENCE_TYPE		u16
#define MTX_GENMSG_FENCE_MASK		(0xFFFF)
#define MTX_GENMSG_FENCE_OFFSET		(0x0002)
#define MTX_GENMSG_FENCE_SHIFT		(0)

#define FW_INVALIDATE_MMU		(0x0010)

union msg_header {
	struct {
		u32 msg_size:8;
		u32 msg_type:8;
		u32 msg_fence:16;
	} bits;
	u32 value;
};

struct fw_init_msg {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
			u32 reserved:16;
		} bits;
		u32 value;
	} header;
	u32 rendec_addr0;
	u32 rendec_addr1;
	union {
		struct {
			u32 rendec_size0:16;
			u32 rendec_size1:16;
		} bits;
		u32 value;
	} rendec_size;
};

struct fw_decode_msg {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
			u32 msg_fence:16;
		} bits;
		u32 value;
	} header;
	union {
		struct {
			u32 flags:16;
			u32 buffer_size:16;
		} bits;
		u32 value;
	} flag_size;
	u32 crtl_alloc_addr;
	union {
		struct {
			u32 context:8;
			u32 mmu_ptd:24;
		} bits;
		u32 value;
	} mmu_context;
	u32 operating_mode;
};

struct fw_deblock_msg {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
			u32 msg_fence:16;
		} bits;
		u32 value;
	} header;
	union {
		struct {
			u32 flags:16;
			u32 slice_field_type:2;
			u32 reserved:14;
		} bits;
		u32 value;
	} flag_type;
	u32 operating_mode;
	union {
		struct {
			u32 context:8;
			u32 mmu_ptd:24;
		} bits;
		u32 value;
	} mmu_context;
	union {
		struct {
			u32 frame_height_mb:16;
			u32 pic_width_mb:16;
		} bits;
		u32 value;
	} pic_size;
	u32 address_a0;
	u32 address_a1;
	u32 mb_param_address;
	u32 ext_stride_a;
	u32 address_b0;
	u32 address_b1;
	u32 alt_output_flags_b;
	/* additional msg outside of IMG msg */
	u32 address_c0;
	u32 address_c1;
};

#define MTX_PADMSG_SIZE 2
struct fw_padding_msg {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
		} bits;
		u16 value;
	} header;
};

struct fw_msg_header {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
			u32 msg_fence:16;
		} bits;
		u32 value;
	} header;
};

struct fw_completed_msg {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
			u32 msg_fence:16;
		} bits;
		u32 value;
	} header;
	union {
		struct {
			u32 start_mb:16;
			u32 last_mb:16;
		} bits;
		u32 value;
	} mb;
	u32 flags;
	u32 vdebcr;
};

struct fw_panic_msg {
	union {
		struct {
			u32 msg_size:8;
			u32 msg_type:8;
			u32 msg_fence:16;
		} bits;
		u32 value;
	} header;
	u32 fe_status;
	u32 be_status;
	union {
		struct {
			u32 last_mb:16;
			u32 reserved2:16;
		} bits;
		u32 value;
	} mb;
};


#endif
