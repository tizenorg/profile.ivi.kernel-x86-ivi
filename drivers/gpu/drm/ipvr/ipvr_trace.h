/**************************************************************************
 * ipvr_trace.h: IPVR header file for trace support
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
 *    Fei Jiang <fei.jiang@intel.com>
 *    Yao Cheng <yao.cheng@intel.com>
 *
 **************************************************************************/

#if !defined(_IPVR_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _IPVR_TRACE_H_

#include "ipvr_bo.h"
#include "ipvr_fence.h"
#include "ved_msg.h"
#include <drm/drmP.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ipvr
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE ipvr_trace

/* object tracking */

TRACE_EVENT(ipvr_create_object,
	TP_PROTO(struct drm_ipvr_gem_object *obj, u64 mmu_offset),
	TP_ARGS(obj, mmu_offset),
	TP_STRUCT__entry(
		__field(struct drm_ipvr_gem_object *, obj)
		__field(u32, size)
		__field(bool, tiling)
		__field(u32, cache_level)
		__field(u64, mmu_offset)
	),
	TP_fast_assign(
		__entry->obj = obj;
		__entry->size = obj->base.size;
		__entry->tiling = obj->tiling;
		__entry->cache_level = obj->cache_level;
		__entry->mmu_offset = mmu_offset;
	),
	TP_printk("obj=0x%p, size=%u, tiling=%u, cache=%u, mmu_offset=0x%llx",
		__entry->obj, __entry->size, __entry->tiling,
		__entry->cache_level, __entry->mmu_offset)
);

TRACE_EVENT(ipvr_free_object,
	TP_PROTO(struct drm_ipvr_gem_object *obj),
	TP_ARGS(obj),
	TP_STRUCT__entry(
		__field(struct drm_ipvr_gem_object *, obj)
	),
	TP_fast_assign(
		__entry->obj = obj;
	),
	TP_printk("obj=0x%p", __entry->obj)
);

TRACE_EVENT(ipvr_fence_wait_begin,
	TP_PROTO(struct ipvr_fence *fence,
		u32 signaled_seq,
		u16 sync_seq),
	TP_ARGS(fence, signaled_seq, sync_seq),
	TP_STRUCT__entry(
		__field(struct ipvr_fence *, fence)
		__field(u16, fence_seq)
		__field(u32, signaled_seq)
		__field(u16, sync_seq)
	),
	TP_fast_assign(
		__entry->fence = fence;
		__entry->fence_seq = fence->seq;
		__entry->signaled_seq = signaled_seq;
		__entry->sync_seq = sync_seq;
	),
	TP_printk("fence=%p, fence_seq=%d, signaled_seq=%d, sync_seq=%d",
		__entry->fence, __entry->fence_seq,
		__entry->signaled_seq, __entry->sync_seq)
);

TRACE_EVENT(ipvr_fence_wait_end,
	TP_PROTO(struct ipvr_fence *fence,
		u32 signaled_seq,
		u16 sync_seq),
	TP_ARGS(fence, signaled_seq, sync_seq),
	TP_STRUCT__entry(
		__field(struct ipvr_fence *, fence)
		__field(u16, fence_seq)
		__field(u32, signaled_seq)
		__field(u16, sync_seq)
	),
	TP_fast_assign(
		__entry->fence = fence;
		__entry->fence_seq = fence->seq;
		__entry->signaled_seq = signaled_seq;
		__entry->sync_seq = sync_seq;
	),
	TP_printk("fence=%p, fence_seq=%d, signaled_seq=%d, sync_seq=%d",
		__entry->fence, __entry->fence_seq,
		__entry->signaled_seq, __entry->sync_seq)
);


TRACE_EVENT(ipvr_fence_wait_lockup,
	TP_PROTO(struct ipvr_fence *fence,
		u32 signaled_seq,
		u16 sync_seq),
	TP_ARGS(fence, signaled_seq, sync_seq),
	TP_STRUCT__entry(
		__field(struct ipvr_fence *, fence)
		__field(u16, fence_seq)
		__field(u32, signaled_seq)
		__field(u16, sync_seq)
	),
	TP_fast_assign(
		__entry->fence = fence;
		__entry->fence_seq = fence->seq;
		__entry->signaled_seq = signaled_seq;
		__entry->sync_seq = sync_seq;
	),
	TP_printk("fence=%p, fence_seq=%d, signaled_seq=%d, sync_seq=%d",
		__entry->fence, __entry->fence_seq,
		__entry->signaled_seq, __entry->sync_seq)
);

TRACE_EVENT(ipvr_execbuffer,
	TP_PROTO(struct drm_ipvr_gem_execbuffer *exec),
	TP_ARGS(exec),
	TP_STRUCT__entry(
		__field(u64, buffers_ptr)
		__field(u32, buffer_count)
		__field(u32, exec_start_offset)
		__field(u32, exec_len)
		__field(u32, ctx_id)
	),
	TP_fast_assign(
		__entry->buffers_ptr = exec->buffers_ptr;
		__entry->buffer_count = exec->buffer_count;
		__entry->exec_start_offset = exec->exec_start_offset;
		__entry->exec_len = exec->exec_len;
		__entry->ctx_id = exec->ctx_id;
	),
	TP_printk("buffers_ptr=0x%llx, buffer_count=0x%d, "
		"exec_start_offset=0x%x, exec_len=%u, ctx_id=%d",
		__entry->buffers_ptr, __entry->buffer_count,
		__entry->exec_start_offset, __entry->exec_len,
		__entry->ctx_id)
);

TRACE_EVENT(ved_cmd_send,
	TP_PROTO(u32 ctx_id, u32 cmd_id, u32 seq),
	TP_ARGS(ctx_id, cmd_id, seq),
	TP_STRUCT__entry(
		__field(u32, ctx_id)
		__field(u32, cmd_id)
		__field(u32, seq)
	),
	TP_fast_assign(
		__entry->ctx_id = ctx_id;
		__entry->cmd_id = cmd_id;
		__entry->seq = seq;
	),
	TP_printk("ctx_id=0x%08x, cmd_id=0x%08x, seq=0x%08x",
		__entry->ctx_id, __entry->cmd_id, __entry->seq)
);

TRACE_EVENT(ved_cmd_copy,
	TP_PROTO(u32 ctx_id, u32 cmd_id, u32 seq),
	TP_ARGS(ctx_id, cmd_id, seq),
	TP_STRUCT__entry(
		__field(u32, ctx_id)
		__field(u32, cmd_id)
		__field(u32, seq)
	),
	TP_fast_assign(
		__entry->ctx_id = ctx_id;
		__entry->cmd_id = cmd_id;
		__entry->seq = seq;
	),
	TP_printk("ctx_id=0x%08x, cmd_id=0x%08x, seq=0x%08x",
		__entry->ctx_id, __entry->cmd_id, __entry->seq)
);

TRACE_EVENT(ipvr_get_power,
	TP_PROTO(int usage, int pending),
	TP_ARGS(usage, pending),
	TP_STRUCT__entry(
		__field(int, usage)
		__field(int, pending)
	),
	TP_fast_assign(
		__entry->usage = usage;
		__entry->pending = pending;
	),
	TP_printk("power usage %d, pending events %d",
		__entry->usage,
		__entry->pending)
);

TRACE_EVENT(ipvr_put_power,
	TP_PROTO(int usage, int pending),
	TP_ARGS(usage, pending),
	TP_STRUCT__entry(
		__field(int, usage)
		__field(int, pending)
	),
	TP_fast_assign(
		__entry->usage = usage;
		__entry->pending = pending;
	),
	TP_printk("power usage %d, pending events %d",
		__entry->usage,
		__entry->pending)
);

TRACE_EVENT(ved_power_on,
	TP_PROTO(int freq),
	TP_ARGS(freq),
	TP_STRUCT__entry(
		__field(int, freq)
	),
	TP_fast_assign(
		__entry->freq = freq;
	),
	TP_printk("frequency %d MHz", __entry->freq)
);

TRACE_EVENT(ved_power_off,
	TP_PROTO(int freq),
	TP_ARGS(freq),
	TP_STRUCT__entry(
		__field(int, freq)
	),
	TP_fast_assign(
		__entry->freq = freq;
	),
	TP_printk("frequency %d MHz", __entry->freq)
);

TRACE_EVENT(ved_irq_completed,
	TP_PROTO(struct ipvr_context *ctx, struct fw_completed_msg *completed_msg),
	TP_ARGS(ctx, completed_msg),
	TP_STRUCT__entry(
		__field(s64, ctx_id)
		__field(u16, seqno)
		__field(u32, flags)
		__field(u32, vdebcr)
		__field(u16, start_mb)
		__field(u16, last_mb)
	),
	TP_fast_assign(
		__entry->ctx_id = ctx? ctx->ctx_id: -1;
		__entry->seqno = completed_msg->header.bits.msg_fence;
		__entry->flags = completed_msg->flags;
		__entry->vdebcr = completed_msg->vdebcr;
		__entry->start_mb = completed_msg->mb.bits.start_mb;
		__entry->last_mb = completed_msg->mb.bits.last_mb;
	),
	TP_printk("ctx=%lld, seq=0x%04x, flags=0x%08x, vdebcr=0x%08x, mb=[%u, %u]",
		__entry->ctx_id,
		__entry->seqno,
		__entry->flags,
		__entry->vdebcr,
		__entry->start_mb,
		__entry->last_mb)
);

TRACE_EVENT(ved_irq_panic,
	TP_PROTO(struct fw_panic_msg *panic_msg, u32 err_trig,
		u32 irq_status, u32 mmu_status, u32 dmac_status),
	TP_ARGS(panic_msg, err_trig, irq_status, mmu_status, dmac_status),
	TP_STRUCT__entry(
		__field(u16, seqno)
		__field(u32, fe_status)
		__field(u32, be_status)
		__field(u16, rsvd)
		__field(u16, last_mb)
		__field(u32, err_trig)
		__field(u32, irq_status)
		__field(u32, mmu_status)
		__field(u32, dmac_status)

	),
	TP_fast_assign(
		__entry->seqno = panic_msg->header.bits.msg_fence;
		__entry->fe_status = panic_msg->fe_status;
		__entry->be_status = panic_msg->be_status;
		__entry->rsvd = panic_msg->mb.bits.reserved2;
		__entry->last_mb = panic_msg->mb.bits.last_mb;
		__entry->err_trig = err_trig;
		__entry->irq_status = irq_status;
		__entry->mmu_status = mmu_status;
		__entry->dmac_status = dmac_status;
	),
	TP_printk("seq=0x%04x, status=[fe 0x%08x be 0x%08x], rsvd=0x%04x, "
		"last_mb=%u, err_trig=0x%08x, irq_status=0x%08x, "
		"mmu_status=0x%08x, dmac_status=0x%08x",
		__entry->seqno,
		__entry->fe_status,
		__entry->be_status,
		__entry->rsvd,
		__entry->last_mb,
		__entry->err_trig,
		__entry->irq_status,
		__entry->mmu_status,
		__entry->dmac_status)
);

#endif /* _IPVR_TRACE_H_ */

 /* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
