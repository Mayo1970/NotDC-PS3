#pragma once
// OPTIMIZATION_PLAN bundle 1 (P43, P55, P7, P26): VRAM and TA-area writes, and their probe. Code in pvr_if.cpp.
#include "types.h"

// Probe counts per [ta-split] window: SQ flushes and ch2 DMA bytes by target, YUV macroblocks by source.
struct VramSplit
{
	u32 sq_yuv, sq_v64, sq_v32, sq_mem;
	u32 dma_ta, dma_yuv, dma_v64, dma_v32, dma_stub;
	u32 yuv_inplace, yuv_copied, lm_changes;
};
extern VramSplit g_vram_split;

// P26: the watched framebuffer bytes (len 0 = off) and the mark a 32-bit-path write inside them sets.
extern u32 g_fb_watch_lo, g_fb_watch_len;
extern bool g_fb_dirty;
void fb_watch_set(u32 start, u32 size);
void fb_watch_update();   // plugs/drkPvr/regs.cpp: from FB_R_SIZE, FB_R_SOF1/2 and SPG_CONTROL

// P7: one RAM run of a ch2 DMA to 0x11/0x13 with LMMODE on (dmac.cpp).
void pvr_vram_dma(u32 dst, u32* data, u32 bytes);

// The bundle 1 part of the [ta-split] line; starts a new window.
void vram_split_format(char* buf, u32 size, double seconds, u32 sq_calls);
