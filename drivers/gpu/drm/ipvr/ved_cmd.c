/**************************************************************************
 * ved_cmd.c: VED command handling between host driver and VED firmware
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

#include "ipvr_gem.h"
#include "ipvr_mmu.h"
#include "ipvr_bo.h"
#include "ipvr_trace.h"
#include "ipvr_fence.h"
#include "ved_cmd.h"
#include "ved_fw.h"
#include "ved_msg.h"
#include "ved_reg.h"
#include "ved_pm.h"
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/delay.h>

#ifndef list_first_entry
#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)
#endif

int ved_mtx_send(struct ved_private *ved_priv, const void *msg)
{
	static struct fw_padding_msg pad_msg;
	const u32 *p_msg = (u32 *)msg;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	u32 msg_num, words_free, ridx, widx, buf_size, buf_offset;
	int ret = 0;
	int i;
	union msg_header *header;
	header = (union msg_header *)msg;

	IPVR_DEBUG_ENTRY("enter.\n");

	/* we need clocks enabled before we touch VEC local ram,
	 * but fw will take care of the clock after fw is loaded
	 */

	msg_num = (header->bits.msg_size + 3) / 4;

	/* debug code for msg dump */
	IPVR_DEBUG_VED("MSVDX: ved_mtx_send is %dDW\n", msg_num);

	for (i = 0; i < msg_num; i++)
		IPVR_DEBUG_VED("   0x%08x\n", p_msg[i]);

	buf_size = IPVR_REG_READ32(MSVDX_COMMS_TO_MTX_BUF_SIZE) &
		   ((1 << 16) - 1);

	if (msg_num > buf_size) {
		ret = -EINVAL;
		IPVR_ERROR("VED: message exceed maximum, ret:%d\n", ret);
		goto out;
	}

	ridx = IPVR_REG_READ32(MSVDX_COMMS_TO_MTX_RD_INDEX);
	widx = IPVR_REG_READ32(MSVDX_COMMS_TO_MTX_WRT_INDEX);


	buf_size = IPVR_REG_READ32(MSVDX_COMMS_TO_MTX_BUF_SIZE) &
		   ((1 << 16) - 1);
	/*0x2000 is VEC Local Ram offset*/
	buf_offset =
		(IPVR_REG_READ32(MSVDX_COMMS_TO_MTX_BUF_SIZE) >> 16) + 0x2000;

	/* message would wrap, need to send a pad message */
	if (widx + msg_num > buf_size) {
		/* Shouldn't happen for a PAD message itself */
		if (header->bits.msg_type == MTX_MSGID_PADDING)
			IPVR_DEBUG_WARN("VED: should not wrap pad msg, "
				"buf_size is %d, widx is %d, msg_num is %d.\n",
				buf_size, widx, msg_num);

		/* if the read pointer is at zero then we must wait for it to
		 * change otherwise the write pointer will equal the read
		 * pointer,which should only happen when the buffer is empty
		 *
		 * This will only happens if we try to overfill the queue,
		 * queue management should make
		 * sure this never happens in the first place.
		 */
		if (0 == ridx) {
			ret = -EINVAL;
			IPVR_ERROR("MSVDX: RIndex=0, ret:%d\n", ret);
			goto out;
		}

		/* Send a pad message */
		pad_msg.header.bits.msg_size = (buf_size - widx) << 2;
		pad_msg.header.bits.msg_type = MTX_MSGID_PADDING;
		ved_mtx_send(ved_priv, (void *)&pad_msg);
		widx = IPVR_REG_READ32(MSVDX_COMMS_TO_MTX_WRT_INDEX);
	}

	if (widx >= ridx)
		words_free = buf_size - (widx - ridx) - 1;
	else
		words_free = ridx - widx - 1;

	if (msg_num > words_free) {
		ret = -EINVAL;
		IPVR_ERROR("MSVDX: msg_num > words_free, ret:%d\n", ret);
		goto out;
	}
	while (msg_num > 0) {
		IPVR_REG_WRITE32(*p_msg++, buf_offset + (widx << 2));
		msg_num--;
		widx++;
		if (buf_size == widx)
			widx = 0;
	}

	IPVR_REG_WRITE32(widx, MSVDX_COMMS_TO_MTX_WRT_INDEX);

	/* Make sure clocks are enabled before we kick
	 * but fw will take care of the clock after fw is loaded
	 */

	/* signal an interrupt to let the mtx know there is a new message */
	IPVR_REG_WRITE32(1, MTX_KICK_INPUT_OFFSET);

	/* Read MSVDX Register several times in case Idle signal assert */
	IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET);
	IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET);
	IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET);
	IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET);

out:
	return ret;
}

static int ved_cmd_send(struct ved_private *ved_priv, void *cmd,
			u32 cmd_size, struct ipvr_context *ipvr_ctx)
{
	int ret = 0;
	union msg_header *header;
	u32 cur_seq = 0xffffffff;

	while (cmd_size > 0) {
		u32 cur_cmd_size, cur_cmd_id;
		header = (union msg_header *)cmd;
		cur_cmd_size = header->bits.msg_size;
		cur_cmd_id = header->bits.msg_type;

		cur_seq = ((struct fw_msg_header *)cmd)->header.bits.msg_fence;

		if (cur_seq != 0xffffffff) {
			ipvr_ctx->cur_seq = cur_seq;
		}

		if (cur_cmd_size > cmd_size) {
			ret = -EINVAL;
			IPVR_ERROR("VED: cmd_size %u cur_cmd_size %u.\n",
				  cmd_size, cur_cmd_size);
			goto out;
		}

		/* Send the message to h/w */
		trace_ved_cmd_send(ipvr_ctx->ctx_id, cur_cmd_id, cur_seq);
		ret = ved_mtx_send(ved_priv, cmd);
		if (ret) {
			IPVR_DEBUG_WARN("VED: ret:%d\n", ret);
			goto out;
		}
		cmd += cur_cmd_size;
		cmd_size -= cur_cmd_size;
		if (cur_cmd_id == MTX_MSGID_HOST_BE_OPP ||
			cur_cmd_id == MTX_MSGID_DEBLOCK ||
			cur_cmd_id == MTX_MSGID_INTRA_OOLD) {
			cmd += (sizeof(struct fw_deblock_msg) - cur_cmd_size);
			cmd_size -=
				(sizeof(struct fw_deblock_msg) - cur_cmd_size);
		}
	}
out:
	IPVR_DEBUG_VED("VED: ret:%d\n", ret);
	return ret;
}

int ved_cmd_dequeue_send(struct ved_private *ved_priv)
{
	struct ved_cmd_queue *ved_cmd = NULL;
	int ret = 0;
	unsigned long irq_flags;

	spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);
	if (list_empty(&ved_priv->ved_queue)) {
		IPVR_DEBUG_VED("VED: ved cmd queue empty.\n");
		ved_priv->ved_busy = false;
		spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
		return -ENODATA;
	}

	ved_cmd = list_first_entry(&ved_priv->ved_queue,
				     struct ved_cmd_queue, head);
	list_del(&ved_cmd->head);
	spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);

	IPVR_DEBUG_VED("VED: cmd queue seq is %08x.\n", ved_cmd->cmd_seq);

	ipvr_set_tile(ved_priv->dev_priv, ved_cmd->tiling_scheme,
				   ved_cmd->tiling_stride);

	ret = ved_cmd_send(ved_priv, ved_cmd->cmd,
			   ved_cmd->cmd_size, ved_cmd->ipvr_ctx);
	if (ret) {
		IPVR_ERROR("VED: ved_cmd_send failed.\n");
		ret = -EFAULT;
	}

	kfree(ved_cmd->cmd);
	kfree(ved_cmd);

	return ret;
}

void ved_flush_cmd_queue(struct ved_private *ved_priv)
{
	struct ved_cmd_queue *ved_cmd;
	struct list_head *list, *next;
	unsigned long irq_flags;
	spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);
	/* Flush the VED cmd queue and signal all fences in the queue */
	list_for_each_safe(list, next, &ved_priv->ved_queue) {
		ved_cmd = list_entry(list, struct ved_cmd_queue, head);
		list_del(list);
		IPVR_DEBUG_VED("VED: flushing sequence:0x%08x.\n",
				  ved_cmd->cmd_seq);
		ved_priv->ved_cur_seq = ved_cmd->cmd_seq;

		ipvr_fence_process(ved_priv->dev_priv, ved_cmd->cmd_seq, IPVR_CMD_SKIP);
		kfree(ved_cmd->cmd);
		kfree(ved_cmd);
	}
	ved_priv->ved_busy = false;
	spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
}

static int
ved_map_command(struct ved_private *ved_priv,
				struct drm_ipvr_gem_object *cmd_buffer,
				u32 cmd_size, void **ved_cmd,
				u16 sequence, s32 copy_cmd,
				struct ipvr_context *ipvr_ctx)
{
	int ret = 0;
	u32 cmd_size_remain;
	void *cmd, *cmd_copy, *cmd_start;
	union msg_header *header;
	struct ipvr_fence *fence = NULL;

	/* command buffers may not exceed page boundary */
	if (cmd_size > PAGE_SIZE)
		return -EINVAL;

	cmd_start = kmap(sg_page(cmd_buffer->sg_table->sgl));
	if (!cmd_start) {
		IPVR_ERROR("VED: kmap failed.\n");
		return -EFAULT;
	}

	cmd = cmd_start;
	cmd_size_remain = cmd_size;

	while (cmd_size_remain > 0) {
		u32 cur_cmd_size, cur_cmd_id, mmu_ptd, msvdx_mmu_invalid;
		if (cmd_size_remain < MTX_GENMSG_HEADER_SIZE) {
			ret = -EINVAL;
			goto out;
		}
		header = (union msg_header *)cmd;
		cur_cmd_size = header->bits.msg_size;
		cur_cmd_id = header->bits.msg_type;
		mmu_ptd = 0;
		msvdx_mmu_invalid = 0;

		IPVR_DEBUG_VED("cmd start at %p cur_cmd_size = %d"
			       " cur_cmd_id = %02x fence = %08x\n",
			       cmd, cur_cmd_size,
			       cur_cmd_id, sequence);
		if ((cur_cmd_size % sizeof(u32))
		    || (cur_cmd_size > cmd_size_remain)) {
			ret = -EINVAL;
			IPVR_ERROR("VED: cmd size err, ret:%d.\n", ret);
			goto out;
		}

		switch (cur_cmd_id) {
		case MTX_MSGID_DECODE_FE: {
			struct fw_decode_msg *decode_msg;
			if (sizeof(struct fw_decode_msg) > cmd_size_remain) {
				/* Msg size is not correct */
				ret = -EINVAL;
				IPVR_DEBUG_WARN("MSVDX: wrong msg size.\n");
				goto out;
			}
			decode_msg = (struct fw_decode_msg *)cmd;
			decode_msg->header.bits.msg_fence = sequence;

			mmu_ptd = ipvr_get_default_pd_addr32(ved_priv->dev_priv->mmu);
			if (mmu_ptd == 0) {
				ret = -EINVAL;
				IPVR_DEBUG_WARN("MSVDX: invalid PD addr32.\n");
				goto out;
			}
			msvdx_mmu_invalid = atomic_cmpxchg(&ved_priv->dev_priv->ipvr_mmu_invaldc, 1, 0);
			if (msvdx_mmu_invalid == 1) {
				decode_msg->flag_size.bits.flags |= FW_INVALIDATE_MMU;
				IPVR_DEBUG_VED("VED: Set MMU invalidate\n");
			}
			/* if ctx_id is not passed, use default id */
			if (decode_msg->mmu_context.bits.context == 0)
				decode_msg->mmu_context.bits.context =
					ved_priv->dev_priv->default_ctx.ctx_id;

			decode_msg->mmu_context.bits.mmu_ptd = mmu_ptd >> 8;
			IPVR_DEBUG_VED("VED: MSGID_DECODE_FE:"
					" - fence: %08x"
					" - flags: %08x - buffer_size: %08x"
					" - crtl_alloc_addr: %08x"
					" - context: %08x - mmu_ptd: %08x"
					" - operating_mode: %08x.\n",
					decode_msg->header.bits.msg_fence,
					decode_msg->flag_size.bits.flags,
					decode_msg->flag_size.bits.buffer_size,
					decode_msg->crtl_alloc_addr,
					decode_msg->mmu_context.bits.context,
					decode_msg->mmu_context.bits.mmu_ptd,
					decode_msg->operating_mode);
			break;
		}
		default:
			/* Msg not supported */
			ret = -EINVAL;
			IPVR_DEBUG_WARN("VED: msg not supported.\n");
			goto out;
		}

		cmd += cur_cmd_size;
		cmd_size_remain -= cur_cmd_size;
	}

	fence = ipvr_fence_create(ved_priv->dev_priv);
	if (IS_ERR(fence)) {
		ret = PTR_ERR(fence);
		IPVR_ERROR("Failed calling ipvr_fence_create: %d\n", ret);
		goto out;
	}

	ipvr_fence_buffer_objects(&ved_priv->dev_priv->validate_ctx.validate_list,
				fence);

	if (copy_cmd) {
		IPVR_DEBUG_VED("VED: copying command.\n");

		cmd_copy = kzalloc(cmd_size, GFP_KERNEL);
		if (cmd_copy == NULL) {
			ret = -ENOMEM;
			IPVR_ERROR("VED: fail to callc, ret=:%d\n", ret);
			goto out;
		}
		memcpy(cmd_copy, cmd_start, cmd_size);
		*ved_cmd = cmd_copy;
	} else {
		IPVR_DEBUG_VED("VED: did NOT copy command.\n");
		ipvr_set_tile(ved_priv->dev_priv, ved_priv->default_tiling_scheme,
					ved_priv->default_tiling_stride);

		ret = ved_cmd_send(ved_priv, cmd_start, cmd_size, ipvr_ctx);
		if (ret) {
			IPVR_ERROR("VED: ved_cmd_send failed\n");
			ret = -EINVAL;
		}
	}

out:
	kunmap(sg_page(cmd_buffer->sg_table->sgl));

	return ret;
}

static int
ved_submit_cmdbuf_copy(struct ved_private *ved_priv,
				struct drm_ipvr_gem_object *cmd_buffer,
				u32 cmd_size,
				struct ipvr_context *ipvr_ctx,
				u32 fence_flag)
{
	struct ved_cmd_queue *ved_cmd;
	u16 sequence =  (ved_priv->dev_priv->last_seq << 4);
	unsigned long irq_flags;
	void *cmd = NULL;
	int ret;
	union msg_header *header;

	/* queue the command to be sent when the h/w is ready */
	IPVR_DEBUG_VED("VED: queueing sequence:%08x.\n",
			  sequence);
	ved_cmd = kzalloc(sizeof(struct ved_cmd_queue),
			    GFP_KERNEL);
	if (ved_cmd == NULL) {
		IPVR_ERROR("MSVDXQUE: Out of memory...\n");
		return -ENOMEM;
	}

	ret = ved_map_command(ved_priv, cmd_buffer, cmd_size,
				&cmd, sequence, 1, ipvr_ctx);
	if (ret) {
		IPVR_ERROR("VED: Failed to extract cmd\n");
		kfree(ved_cmd);
		/* -EINVAL or -EFAULT or -ENOMEM */
		return ret;
	}
	header = (union msg_header *)cmd;
	ved_cmd->cmd = cmd;
	ved_cmd->cmd_size = cmd_size;
	ved_cmd->cmd_seq = sequence;

	ved_cmd->tiling_scheme = ved_priv->default_tiling_scheme;
	ved_cmd->tiling_stride = ved_priv->default_tiling_stride;
	ved_cmd->ipvr_ctx = ipvr_ctx;
	spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);
	list_add_tail(&ved_cmd->head, &ved_priv->ved_queue);
	spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
	if (!ved_priv->ved_busy) {
		ved_priv->ved_busy = true;
		IPVR_DEBUG_VED("VED: Need immediate dequeue.\n");
		ved_cmd_dequeue_send(ved_priv);
	}
	trace_ved_cmd_copy(ipvr_ctx->ctx_id, header->bits.msg_type, sequence);

	return ret;
}

int
ved_submit_video_cmdbuf(struct ved_private *ved_priv,
				struct drm_ipvr_gem_object *cmd_buffer,
				u32 cmd_size,
				struct ipvr_context *ipvr_ctx,
				u32 fence_flag)
{
	unsigned long irq_flags;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	u16 sequence =  (dev_priv->last_seq << 4) & 0xffff;
	int ret = 0;

	if (sequence == IPVR_FENCE_SIGNALED_SEQ) {
		sequence =  (++ved_priv->dev_priv->last_seq << 4) & 0xffff;
	}

	if (!ipvr_ctx) {
		IPVR_ERROR("VED: null ctx\n");
		return -ENOENT;
	}

	spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);

	IPVR_DEBUG_VED("sequence is 0x%x, needs_reset is 0x%x.\n",
			sequence, ved_priv->ved_needs_reset);

	if (WARN_ON(ipvr_runtime_pm_get(ved_priv->dev_priv) < 0)) {
		IPVR_ERROR("Failed to get ipvr power\n");
		spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
		return -EBUSY;
	}

	if (ved_priv->ved_busy) {
		spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
		ret = ved_submit_cmdbuf_copy(ved_priv, cmd_buffer,
			    cmd_size, ipvr_ctx, fence_flag);

		return ret;
	}

	if (ved_priv->ved_needs_reset) {
		spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
		IPVR_DEBUG_VED("VED: will reset msvdx.\n");

		if (ved_core_reset(ved_priv)) {
			ret = -EBUSY;
			IPVR_ERROR("VED: Reset failed.\n");
			goto out_power_put;
		}

		ved_priv->ved_needs_reset = 0;
		ved_priv->ved_busy = false;

		if (ved_core_init(ved_priv->dev_priv)) {
			ret = -EBUSY;
			IPVR_DEBUG_WARN("VED: ved_core_init fail.\n");
			goto out_power_put;
		}

		spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);
	}

	if (!ved_priv->ved_fw_loaded) {
		spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
		IPVR_DEBUG_VED("VED: reload FW to MTX\n");
		ret = ved_setup_fw(ved_priv);
		if (ret) {
			IPVR_ERROR("VED: fail to load FW\n");
			/* FIXME: find a proper return value */
			ret = -EFAULT;
			goto out_power_put;
		}
		ved_priv->ved_fw_loaded = true;

		IPVR_DEBUG_VED("VED: load firmware successfully\n");
		spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);
	}

	ved_priv->ved_busy = true;
	spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
	IPVR_DEBUG_VED("VED: commit command to HW,seq=0x%08x\n",
			  sequence);
	ret = ved_map_command(ved_priv, cmd_buffer, cmd_size,
				NULL, sequence, 0, ipvr_ctx);
	if (ret) {
		IPVR_ERROR("VED: Failed to extract cmd.\n");
		goto out_power_put;
	}

	return 0;
out_power_put:
	if (WARN_ON(ipvr_runtime_pm_put(ved_priv->dev_priv, false) < 0))
		IPVR_ERROR("Failed to put ipvr power\n");
	return ret;
}

int ved_cmdbuf_video(struct ved_private *ved_priv,
						struct drm_ipvr_gem_object *cmd_buffer,
						u32 cmdbuf_size,
						struct ipvr_context *ipvr_ctx)
{
	return ved_submit_video_cmdbuf(ved_priv, cmd_buffer, cmdbuf_size, ipvr_ctx, 0);
}

static int ved_handle_panic_msg(struct ved_private *ved_priv,
					struct fw_panic_msg *panic_msg)
{
	/* For VXD385 firmware, fence value is not validate here */
	u32 diff = 0;
	u16 fence;
	u32 err_trig, irq_sts, mmu_sts, dmac_sts;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	IPVR_DEBUG_WARN("MSVDX: MSGID_CMD_HW_PANIC:"
		  "Fault detected"
		  " - Fence: %08x"
		  " - fe_status mb: %08x"
		  " - be_status mb: %08x"
		  " - reserved2: %08x"
		  " - last mb: %08x"
		  " - resetting and ignoring error\n",
		  panic_msg->header.bits.msg_fence,
		  panic_msg->fe_status,
		  panic_msg->be_status,
		  panic_msg->mb.bits.reserved2,
		  panic_msg->mb.bits.last_mb);
	/*
	 * If bit 8 of MSVDX_INTERRUPT_STATUS is set the fault
	 * was caused in the DMAC. In this case you should
	 * check bits 20:22 of MSVDX_INTERRUPT_STATUS.
	 * If bit 20 is set there was a problem DMAing the buffer
	 * back to host. If bit 22 is set you'll need to get the
	 * value of MSVDX_DMAC_STREAM_STATUS (0x648).
	 * If bit 1 is set then there was an issue DMAing
	 * the bitstream or termination code for parsing.
	 */
	err_trig = IPVR_REG_READ32(MSVDX_COMMS_ERROR_TRIG);
	irq_sts = IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET);
	mmu_sts = IPVR_REG_READ32(MSVDX_MMU_STATUS_OFFSET);
	dmac_sts = IPVR_REG_READ32(MSVDX_DMAC_STREAM_STATUS_OFFSET);
	IPVR_DEBUG_WARN("MSVDX: MSVDX_COMMS_ERROR_TRIG is 0x%x,"
		"MSVDX_INTERRUPT_STATUS is 0x%x,"
		"MSVDX_MMU_STATUS is 0x%x,"
		"MSVDX_DMAC_STREAM_STATUS is 0x%x.\n",
		err_trig, irq_sts, mmu_sts, dmac_sts);

	trace_ved_irq_panic(panic_msg, err_trig, irq_sts, mmu_sts, dmac_sts);

	fence = panic_msg->header.bits.msg_fence;

	ved_priv->ved_needs_reset = 1;

	diff = ved_priv->ved_cur_seq - dev_priv->last_seq;
	if (diff > 0x0FFFFFFF)
		ved_priv->ved_cur_seq++;

	IPVR_DEBUG_WARN("VED: Fence ID missing, assuming %08x\n",
			ved_priv->ved_cur_seq);

	ipvr_fence_process(dev_priv, ved_priv->ved_cur_seq, IPVR_CMD_FAILED);

	/* Flush the command queue */
	ved_flush_cmd_queue(ved_priv);
	ved_priv->ved_busy = false;
	return 0;
}

static int
ved_handle_completed_msg(struct ved_private *ved_priv,
				struct fw_completed_msg *completed_msg)
{
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	u16 fence, flags;
	int ret = 0;
	struct ipvr_context *ipvr_ctx;

	IPVR_DEBUG_VED("VED: MSGID_CMD_COMPLETED:"
		" - Fence: %08x - flags: %08x - vdebcr: %08x"
		" - first_mb : %d - last_mb: %d\n",
		completed_msg->header.bits.msg_fence,
		completed_msg->flags, completed_msg->vdebcr,
		completed_msg->mb.bits.start_mb,
		completed_msg->mb.bits.last_mb);

	flags = completed_msg->flags;
	fence = completed_msg->header.bits.msg_fence;

	ved_priv->ved_cur_seq = fence;

	ipvr_fence_process(dev_priv, fence, IPVR_CMD_SUCCESS);

	ipvr_ctx = ipvr_find_ctx_with_fence(dev_priv, fence);
	trace_ved_irq_completed(ipvr_ctx, completed_msg);
	if (unlikely(ipvr_ctx == NULL)) {
		IPVR_DEBUG_WARN("abnormal complete msg: seq=0x%04x.\n", fence);
		ret = -EINVAL;
		goto out_clear_busy;
	}

	if (flags & FW_VA_RENDER_HOST_INT) {
		/* Now send the next command from the msvdx cmd queue */
		if (ved_cmd_dequeue_send(ved_priv) == 0)
			goto out;
	}

out_clear_busy:
	ved_priv->ved_busy = false;
out:
	return 0;
}

/*
 * MSVDX MTX interrupt
 */
static void ved_mtx_interrupt(struct ved_private *ved_priv)
{
	static u32 buf[128]; /* message buffer */
	u32 ridx, widx, buf_size, buf_offset;
	u32 num, ofs; /* message num and offset */
	union msg_header *header;
	int cmd_complete = 0;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	IPVR_DEBUG_VED("VED: Got a VED MTX interrupt.\n");

	/* we need clocks enabled before we touch VEC local ram,
	 * but fw will take care of the clock after fw is loaded
	 */

loop: /* just for coding style check */
	ridx = IPVR_REG_READ32(MSVDX_COMMS_TO_HOST_RD_INDEX);
	widx = IPVR_REG_READ32(MSVDX_COMMS_TO_HOST_WRT_INDEX);

	/* Get out of here if nothing */
	if (ridx == widx)
		goto done;

	buf_size = IPVR_REG_READ32(MSVDX_COMMS_TO_HOST_BUF_SIZE) &
		((1 << 16) - 1);
	/*0x2000 is VEC Local Ram offset*/
	buf_offset = (IPVR_REG_READ32(MSVDX_COMMS_TO_HOST_BUF_SIZE) >> 16)
		+ 0x2000;

	ofs = 0;
	buf[ofs] = IPVR_REG_READ32(buf_offset + (ridx << 2));
	header = (union msg_header *)buf;

	/* round to nearest word */
	num = (header->bits.msg_size + 3) / 4;

	/* ASSERT(num <= sizeof(buf) / sizeof(u32)); */

	if (++ridx >= buf_size)
		ridx = 0;

	for (ofs++; ofs < num; ofs++) {
		buf[ofs] = IPVR_REG_READ32(buf_offset + (ridx << 2));

		if (++ridx >= buf_size)
			ridx = 0;
	}

	/* Update the Read index */
	IPVR_REG_WRITE32(ridx, MSVDX_COMMS_TO_HOST_RD_INDEX);

	if (ved_priv->ved_needs_reset)
		goto loop;

	switch (header->bits.msg_type) {
	case MTX_MSGID_HW_PANIC: {
		struct fw_panic_msg *panic_msg = (struct fw_panic_msg *)buf;
		cmd_complete = 1;
		ved_handle_panic_msg(ved_priv, panic_msg);
		/**
		 * panic msg clears all pending cmds and breaks the cmd<->irq pairing
		 */
		if (WARN_ON(ipvr_runtime_pm_put_all(ved_priv->dev_priv, true) < 0)) {
			IPVR_ERROR("Error clearing pending events and put power\n");
		}
		goto done;
	}

	case MTX_MSGID_COMPLETED: {
		struct fw_completed_msg *completed_msg =
					(struct fw_completed_msg *)buf;
		cmd_complete = 1;
		if (ved_handle_completed_msg(ved_priv, completed_msg))
			cmd_complete = 0;
		/**
		 * for VP8, cmd and COMPLETED msg are paired. we can safely call
		 * get in execbuf_ioctl and call put here
		 */
		if (WARN_ON(ipvr_runtime_pm_put(ved_priv->dev_priv, true) < 0)) {
			IPVR_ERROR("Error put power\n");
		}
		break;
	}

	default:
		IPVR_ERROR("VED: unknown message from MTX, ID:0x%08x.\n",
			header->bits.msg_type);
		goto done;
	}

done:
	IPVR_DEBUG_VED("VED Interrupt: finish process a message.\n");
	if (ridx != widx) {
		IPVR_DEBUG_VED("VED: there are more message to be read.\n");
		goto loop;
	}

	mb();	/* TBD check this... */
}

/*
 * MSVDX interrupt.
 */
int ved_irq_handler(struct ved_private *ved_priv)
{
	u32 msvdx_stat;
	struct drm_ipvr_private *dev_priv = ved_priv->dev_priv;
	msvdx_stat = IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET);

	/* driver only needs to handle mtx irq
	 * For MMU fault irq, there's always a HW PANIC generated
	 * if HW/FW is totally hang, the lockup function will handle
	 * the reseting
	 */
	if (msvdx_stat & MSVDX_INTERRUPT_STATUS_MMU_FAULT_IRQ_MASK) {
		/*Ideally we should we should never get to this */
		IPVR_DEBUG_IRQ("VED: MMU Fault:0x%x\n", msvdx_stat);

		/* Pause MMU */
		IPVR_REG_WRITE32(MSVDX_MMU_CONTROL0_MMU_PAUSE_MASK,
			     MSVDX_MMU_CONTROL0_OFFSET);
		wmb();

		/* Clear this interupt bit only */
		IPVR_REG_WRITE32(MSVDX_INTERRUPT_STATUS_MMU_FAULT_IRQ_MASK,
			     MSVDX_INTERRUPT_CLEAR_OFFSET);
		IPVR_REG_READ32(MSVDX_INTERRUPT_CLEAR_OFFSET);
		rmb();

		ved_priv->ved_needs_reset = 1;
	} else if (msvdx_stat & MSVDX_INTERRUPT_STATUS_MTX_IRQ_MASK) {
		IPVR_DEBUG_IRQ("VED: msvdx_stat: 0x%x(MTX)\n", msvdx_stat);

		/* Clear all interupt bits */
		IPVR_REG_WRITE32(0xffff, MSVDX_INTERRUPT_CLEAR_OFFSET);

		IPVR_REG_READ32(MSVDX_INTERRUPT_CLEAR_OFFSET);
		rmb();

		ved_mtx_interrupt(ved_priv);
	}

	return 0;
}

int ved_check_idle(struct ved_private *ved_priv)
{
	int loop, ret;
	struct drm_ipvr_private *dev_priv;
	if (!ved_priv)
		return 0;

	dev_priv = ved_priv->dev_priv;
	if (!ved_priv->ved_fw_loaded)
		return 0;

	if (ved_priv->ved_busy) {
		IPVR_DEBUG_PM("VED: ved_busy was set, return busy.\n");
		return -EBUSY;
	}

	/* on some cores below 50502, there is one instance that
	 * read requests may not go to zero is in the case of a page fault,
	 * check core revision by reg MSVDX_CORE_REV, 385 core is 0x20001
	 * check if mmu page fault happend by reg MSVDX_INTERRUPT_STATUS,
	 * check was it a page table rather than a protection fault
	 * by reg MSVDX_MMU_STATUS, for such case,
	 * need call ved_core_reset as the work around */
	if ((IPVR_REG_READ32(MSVDX_CORE_REV_OFFSET) < 0x00050502) &&
		(IPVR_REG_READ32(MSVDX_INTERRUPT_STATUS_OFFSET)
			& MSVDX_INTERRUPT_STATUS_MMU_FAULT_IRQ_MASK) &&
		(IPVR_REG_READ32(MSVDX_MMU_STATUS_OFFSET) & 1)) {
		IPVR_DEBUG_WARN("mmu page fault, recover by core_reset.\n");
		return 0;
	}

	/* check MSVDX_MMU_MEM_REQ to confirm there's no memory requests */
	for (loop = 0; loop < 10; loop++)
		ret = ved_wait_for_register(ved_priv,
					MSVDX_MMU_MEM_REQ_OFFSET,
					0, 0xff, 100, 1);
	if (ret) {
		IPVR_DEBUG_WARN("MSVDX: MSVDX_MMU_MEM_REQ reg is 0x%x,\n"
				"indicate mem busy, prevent power off ved,"
				"MSVDX_COMMS_FW_STATUS reg is 0x%x,"
				"MSVDX_COMMS_ERROR_TRIG reg is 0x%x,",
				IPVR_REG_READ32(MSVDX_MMU_MEM_REQ_OFFSET),
				IPVR_REG_READ32(MSVDX_COMMS_FW_STATUS),
				IPVR_REG_READ32(MSVDX_COMMS_ERROR_TRIG));
		return -EBUSY;
	}

	return 0;
}

void ved_check_reset_fw(struct ved_private *ved_priv)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);

	/* handling fw upload here if required */
	/* power off first, then hw_begin will power up/upload FW correctly */
	if (ved_priv->ved_needs_reset & MSVDX_RESET_NEEDS_REUPLOAD_FW) {
		ved_priv->ved_needs_reset &= ~MSVDX_RESET_NEEDS_REUPLOAD_FW;
		spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
		IPVR_DEBUG_VED("VED: force power off VED due to decode err\n");
		spin_lock_irqsave(&ved_priv->ved_lock, irq_flags);
	}
	spin_unlock_irqrestore(&ved_priv->ved_lock, irq_flags);
}
