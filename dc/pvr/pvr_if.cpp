#include "types.h"
#include "pvr_if.h"
#include "pvr_vram.h"
#include "pvrLock.h"
#include "dc/sh4/intc.h"
#include "dc/mem/_vmem.h"
#include "dc/mem/sb.h"
#include "dc/mem/sh4_mem.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"
#include "ps3/ps3_presets.h"

// Swirl debug: TA input per render, printed by ps3/ps3_rend.cpp.
// vram_check: begin (misc/vram_check builds the rest of this file on PC)

// PowerVR Interface Implementation Handles YUV conversion, register access, and VRAM operations
// YUV Converter Constants
namespace {
    constexpr u32 YUV_TEMP_BUFFER_SIZE = 512; // bytes
    constexpr u32 YUV_BLOCK_SIZE_420 = 384;   // YUV 4:2:0 format
    constexpr u32 YUV_BLOCK_SIZE_422 = 512;   // YUV 4:2:2 format
    constexpr u32 YUV_MACROBLOCK_SIZE = 16;   // 16x16 pixels per macroblock
    
    // Register addresses
    constexpr u32 STARTRENDER_ADDR     = 0x5F8014;
    constexpr u32 TA_YUV_TEX_BASE_ADDR = 0x5F8148;
    constexpr u32 TA_YUV_TEX_CTRL_ADDR = 0x5F814C;
    constexpr u32 TA_YUV_TEX_CNT_ADDR  = 0x5F8150;
}

// YUV Converter State
static u32 YUV_tempdata[YUV_TEMP_BUFFER_SIZE / 4]; // Temporary conversion buffer
static u32 YUV_index = 0;          // Current position in temp buffer
static u32 YUV_dest = 0;           // Destination address in VRAM
static u32 YUV_doneblocks = 0;     // Number of completed macroblocks
static u32 YUV_blockcount = 0;     // Total macroblocks to process

static u32 YUV_x_curr = 0;         // Current X position in output
static u32 YUV_y_curr = 0;         // Current Y position in output
static u32 YUV_x_size = 0;         // Output width in pixels
static u32 YUV_y_size = 0;         // Output height in pixels

// P43: TA_YUV_TEX_CTRL's format, read in YUV_init (every CTRL write calls it); the region is noted once per frame.
static bool YUV_is420 = true;
static u32 YUV_block_bytes = YUV_BLOCK_SIZE_420;
static bool YUV_region_due = false;

VramSplit g_vram_split;
u32 g_fb_watch_lo, g_fb_watch_len;
bool g_fb_dirty;

// YUV converter output regions: the converter writes rows of the video's real width with no padding,
// so the renderer looks texture addresses up here for the real pitch (TA_YUV_TEX_CTRL is often stale).
struct YuvConvRegion { u32 base; u32 w; u32 h; u32 gen; };
static const u32 YUV_REGION_SLOTS = 4;
static YuvConvRegion YUV_regions[YUV_REGION_SLOTS];
static u32 YUV_region_next = 0;
// Bumped once per completed video frame, so the texture cache detects a new
// FMV frame with an int compare instead of re-hashing the surface.
static u32 YUV_generation = 0;

// Records or refreshes the region the converter is writing (once per macroblock).
static void YUV_RememberRegion(u32 base, u32 w, u32 h)
{
    if (w == 0 || h == 0)
        return;

    for (u32 i = 0; i < YUV_REGION_SLOTS; i++) {
        if (YUV_regions[i].w != 0 && YUV_regions[i].base == base) {
            YUV_regions[i].w = w;
            YUV_regions[i].h = h;
            return;
        }
    }

    YUV_regions[YUV_region_next].base = base;
    YUV_regions[YUV_region_next].w    = w;
    YUV_regions[YUV_region_next].h    = h;
    YUV_regions[YUV_region_next].gen  = 0;
    YUV_region_next = (YUV_region_next + 1) % YUV_REGION_SLOTS;
}

// Marks the region at base as holding a full frame; called when the converter
// finishes the frame's last macroblock.
static void YUV_MarkRegionComplete(u32 base)
{
    YUV_generation++;
    for (u32 i = 0; i < YUV_REGION_SLOTS; i++) {
        if (YUV_regions[i].w != 0 && YUV_regions[i].base == base) {
            YUV_regions[i].gen = YUV_generation;
            return;
        }
    }
}

// Real pitch and rows of a YUV422 texture at VRAM address addr; returns 1 if the converter wrote it.
extern "C" int yuv_conv_lookup(u32 addr, u32 *out_w, u32 *out_h)
{
    for (u32 i = 0; i < YUV_REGION_SLOTS; i++) {
        const YuvConvRegion &r = YUV_regions[i];
        if (r.w == 0 || r.h == 0)
            continue;
        if (addr >= r.base && addr < r.base + r.w * r.h * 2) {
            *out_w = r.w;
            *out_h = r.h;
            return 1;
        }
    }
    return 0;
}

extern "C" int yuv_conv_generation(u32 addr, u32 *out_gen)
{
    for (u32 i = 0; i < YUV_REGION_SLOTS; i++) {
        const YuvConvRegion &r = YUV_regions[i];
        if (r.w == 0 || r.h == 0 || r.gen == 0)
            continue;
        if (addr >= r.base && addr < r.base + r.w * r.h * 2) {
            *out_gen = r.gen;
            return 1;
        }
    }
    return 0;
}

// CH2 copies main RAM verbatim and guest u32s are stored natively, so a guest
// byte sits at host offset ^3; host_ptr_xor() corrects every plane byte read.

// One 8x8 luma quadrant (qx/qy = 0 or 8) of a 4:2:0 macroblock to YUYV in VRAM.
// Per quadrant so the quadrant test and row address are hoisted; runs on the SH4 thread.
INLINE void YUV_ConvertQuadrant(const u8* Yb, const u8* U, const u8* V, u32 qx, u32 qy)
{
    for (u32 ry = 0; ry < 8; ry++)
    {
        u32 y = qy + ry;
        u32 cy = (qy >> 1) + (ry >> 1);
        u32 row_off = YUV_dest + (YUV_x_curr + (YUV_y_curr + y) * YUV_x_size) * 2;
        u32* drow = (u32*)&vram.data[row_off];
        const u8* Yrow = Yb + ry * 8;
        const u8* Urow = U + cy * 8;
        const u8* Vrow = V + cy * 8;
        for (u32 rx = 0; rx < 8; rx += 2)
        {
            u32 cx = (qx >> 1) + (rx >> 1);
            // DC LE UYVY u32: U|Y0<<8|V<<16|Y1<<24. Built as a value, not a u8[4]
            // pun, so it is endian-neutral and safe under strict aliasing.
            u32 px = ((u32)*host_ptr_xor(Yrow + rx + 1) << 24)
                   | ((u32)*host_ptr_xor(Vrow + cx)      << 16)
                   | ((u32)*host_ptr_xor(Yrow + rx)      <<  8)
                   |  (u32)*host_ptr_xor(Urow + cx);
            drow[cx] = px;
        }
    }
}

// Initializes the YUV converter state from the hardware registers.
void YUV_init()
{
    YUV_index = 0;
    YUV_x_curr = 0;
    YUV_y_curr = 0;
    YUV_doneblocks = 0;
    
    // Read destination address and mask to valid VRAM range
    YUV_dest = pvr_readreg_TA(TA_YUV_TEX_BASE_ADDR, 4) & VRAM_MASK;
    
    // Read texture control register
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    YUV_is420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);
    YUV_block_bytes = YUV_is420 ? YUV_BLOCK_SIZE_420 : YUV_BLOCK_SIZE_422;
    YUV_region_due = true;

    // Calculate total number of macroblocks
    u32 blocks_x = ((TA_YUV_TEX_CTRL >> 0) & 0x3F) + 1;
    u32 blocks_y = ((TA_YUV_TEX_CTRL >> 8) & 0x3F) + 1;
    YUV_blockcount = blocks_x * blocks_y;
    
    // Determine output size
    if ((TA_YUV_TEX_CTRL >> 16) & 1) {
        // Small texture mode (single 16x16 block)
        YUV_x_size = YUV_MACROBLOCK_SIZE;
        YUV_y_size = YUV_MACROBLOCK_SIZE;
    } else {
        // Normal mode (multiple blocks)
        YUV_x_size = blocks_x * YUV_MACROBLOCK_SIZE;
        YUV_y_size = blocks_y * YUV_MACROBLOCK_SIZE;
    }
}

// Converts one 16x16 YUV macroblock to YUYV and writes it to VRAM.
INLINE void YUV_ConvertMacroBlock()
{
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    YUV_doneblocks++;
    YUV_index = 0;

    // Determine block format
    bool is_yuv420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);
    
    if (is_yuv420) {
        // Remember the written region so the renderer can tell converter output from uploaded YUV422.
        YUV_RememberRegion(YUV_dest, YUV_x_size, YUV_y_size);

        // YUV 4:2:0 format (384 bytes per macroblock)
        // Layout: 64 bytes U, 64 bytes V, 256 bytes Y, Y itself split TL/TR/BL/BR.
        const u8* U = (const u8*)&YUV_tempdata[0];
        const u8* V = (const u8*)&YUV_tempdata[64 / 4];
        const u8* Y = (const u8*)&YUV_tempdata[(64 + 64) / 4];

        YUV_ConvertQuadrant(Y,       U, V, 0, 0);
        YUV_ConvertQuadrant(Y + 64,  U, V, 8, 0);
        YUV_ConvertQuadrant(Y + 128, U, V, 0, 8);
        YUV_ConvertQuadrant(Y + 192, U, V, 8, 8);

        // Advance to next macroblock position
        YUV_x_curr += YUV_MACROBLOCK_SIZE;
        if (YUV_x_curr >= YUV_x_size) {
            YUV_x_curr = 0;
            YUV_y_curr += YUV_MACROBLOCK_SIZE;
            if (YUV_y_curr >= YUV_y_size) {
                YUV_y_curr = 0;
            }
        }
    }
    // 4:2:2 blocks are consumed and acknowledged, never written.

    // No reset here: TA_YUV_TEX_CNT keeps the full count until more data arrives (Flycast 805d014b5).
    if (YUV_doneblocks == YUV_blockcount) {
        YUV_MarkRegionComplete(YUV_dest);
        asic_RaiseInterrupt(holly_YUV_DMA);
    }
}

// P43: a 4:2:0 macroblock from its plane words (U 0-63, V 64-127, Y quadrants 128-383). The host word is the
// guest LE word, so plane byte k is bits 8k..8k+7, and each output word U|Y0<<8|V<<16|Y1<<24 is masks and shifts.
static void YUV_ConvertWords(const u32* blk)
{
    for (u32 q = 0; q < 4; q++)
    {
        const u32 qx = (q & 1) * 8, qy = (q >> 1) * 8;
        const u32* Yq = blk + 32 + q * 16;
        for (u32 ry = 0; ry < 8; ry++)
        {
            const u32 c = ((qy >> 1) + (ry >> 1)) * 2 + (q & 1);
            const u32 u = blk[c], v = blk[16 + c];
            const u32 y0 = Yq[ry * 2], y1 = Yq[ry * 2 + 1];
            u32* d = (u32*)&vram.data[YUV_dest + (YUV_x_curr + qx + (YUV_y_curr + qy + ry) * YUV_x_size) * 2];
            d[0] = (u & 0xFF) | ((y0 & 0xFF) << 8) | ((v & 0xFF) << 16) | ((y0 & 0xFF00) << 16);
            d[1] = ((u >> 8) & 0xFF) | ((y0 >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (y0 & 0xFF000000);
            d[2] = ((u >> 16) & 0xFF) | ((y1 & 0xFF) << 8) | (v & 0xFF0000) | ((y1 & 0xFF00) << 16);
            d[3] = (u >> 24) | ((y1 >> 8) & 0xFF00) | ((v >> 8) & 0xFF0000) | (y1 & 0xFF000000);
        }
    }
}

// P43 form of YUV_ConvertMacroBlock: the format from YUV_init, the region noted once per frame.
static void YUV_MacroBlockWords(const u32* blk)
{
    YUV_doneblocks++;
    YUV_index = 0;
    if (YUV_is420) {
        if (YUV_region_due) {
            YUV_region_due = false;
            YUV_RememberRegion(YUV_dest, YUV_x_size, YUV_y_size);
        }
        YUV_ConvertWords(blk);
        YUV_x_curr += YUV_MACROBLOCK_SIZE;
        if (YUV_x_curr >= YUV_x_size) {
            YUV_x_curr = 0;
            YUV_y_curr += YUV_MACROBLOCK_SIZE;
            if (YUV_y_curr >= YUV_y_size)
                YUV_y_curr = 0;
        }
    }
    if (YUV_doneblocks == YUV_blockcount) {
        YUV_MarkRegionComplete(YUV_dest);
        asic_RaiseInterrupt(holly_YUV_DMA);
    }
}

// P43: a whole block converts where it lies (Flycast); one split across calls collects in YUV_tempdata.
static void YUV_DataWords(u32* data, u32 count)
{
    const u32 block_size = YUV_block_bytes;
    count *= 32;
    while (count > 0) {
        if (YUV_index == 0 && YUV_doneblocks >= YUV_blockcount)
            YUV_init();
        if (YUV_index == 0 && count >= block_size) {
            g_vram_split.yuv_inplace++;
            YUV_MacroBlockWords(data);
            data += block_size >> 2;
            count -= block_size;
            continue;
        }
        const u32 til = block_size - YUV_index;
        const u32 n = count < til ? count : til;
        memcpy(&YUV_tempdata[YUV_index >> 2], data, n);
        data += n >> 2;
        count -= n;
        YUV_index += n;
        if (YUV_index == block_size) {
            g_vram_split.yuv_copied++;
            YUV_MacroBlockWords(YUV_tempdata);
        }
    }
}

// Feeds count 32-byte blocks of YUV data to the converter.
void YUV_data(u32* data, u32 count)
{
    // Initialize if not already done
    if (YUV_blockcount == 0) {
        printf("YUV_data: YUV decoder not initialized, initializing now\n");
        YUV_init();
    }

    if (g_yuv_conv_fast_preset) {
        YUV_DataWords(data, count);
        return;
    }

    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    // Determine block size based on format
    u32 block_size = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0) 
                     ? YUV_BLOCK_SIZE_420 
                     : YUV_BLOCK_SIZE_422;
    
    // Convert count from 32-byte blocks to bytes
    count *= 32;
    
    // Process data in chunks
    while (count > 0) {
        // A finished frame resets when the next block starts, also mid-transfer.
        if (YUV_index == 0 && YUV_doneblocks >= YUV_blockcount)
            YUV_init();

        u32 bytes_til_block_end = block_size - YUV_index;
        
        if (count >= bytes_til_block_end) {
            // Complete at least one full block
            memcpy(&YUV_tempdata[YUV_index >> 2], data, bytes_til_block_end);
            data += bytes_til_block_end >> 2;
            count -= bytes_til_block_end;
            YUV_ConvertMacroBlock();
        } else {
            // Partial block - buffer it
            memcpy(&YUV_tempdata[YUV_index >> 2], data, count);
            YUV_index += count;
            count = 0;
        }
    }
}

// Register Access Functions
// Reads a PVR TA register.
u32 pvr_readreg_TA(u32 addr, u32 sz)
{
    // Handle YUV block counter register specially
    if ((addr & 0xFFFFFF) == TA_YUV_TEX_CNT_ADDR) {
        return YUV_doneblocks;
    }
    
    // Delegate to plugin implementation
    return libPvr_ReadReg(addr, sz);
}

// Writes a PVR TA register.
void pvr_writereg_TA(u32 addr, u32 data, u32 sz)
{
    // Delegate to plugin implementation
    libPvr_WriteReg(addr, data, sz);
    
    // BASE or CTRL (reicast pvr_sb_regs.cpp:382): CTRL holds the block grid.
    // STARTRENDER also resets the converter on real hw (Flycast 805d014b5).
    u32 reg = addr & 0xFFFFFF;
    if (reg == TA_YUV_TEX_BASE_ADDR || reg == TA_YUV_TEX_CTRL_ADDR || reg == STARTRENDER_ADDR) {
        YUV_init();
    }
}

// VRAM Access Functions (32-bit to 64-bit address conversion)
u8 FASTCALL pvr_read_area1_8(u32 addr)
{
    printf("Warning: 8-bit VRAM reads are not supported by hardware\n");
    return 0;
}

u16 FASTCALL pvr_read_area1_16(u32 addr)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    return *host_ptr_xor((u16*)&vram[addr]);
}

u32 FASTCALL pvr_read_area1_32(u32 addr)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    return *(u32*)&vram[addr];
}

void FASTCALL pvr_write_area1_8(u32 addr, u8 data)
{
    printf("Warning: 8-bit VRAM writes are not supported by hardware\n");
}

// P26 FB Write Watch (Flycast pvr_write32p): a 32-bit-path write inside the shown framebuffer marks it dirty.
static INLINE void fb_watch_word(u32 addr)
{
    if ((addr & VRAM_MASK) - g_fb_watch_lo < g_fb_watch_len)
        g_fb_dirty = true;
}

// The same for word stores at off, off+4, ... below off+bytes (off word-aligned).
static INLINE void fb_watch_run(u32 off, u32 bytes)
{
    if (!g_fb_watch_len)
        return;
    u32 first = off;
    if (off < g_fb_watch_lo)
        first += (g_fb_watch_lo - off + 3) & ~3u;
    if (first - g_fb_watch_lo < g_fb_watch_len && first - off < bytes)
        g_fb_dirty = true;
}

void fb_watch_set(u32 start, u32 size)
{
    g_fb_watch_lo = start & VRAM_MASK;
    g_fb_watch_len = g_fb_watch_preset ? size : 0;
}

void FASTCALL pvr_write_area1_16(u32 addr, u16 data)
{
    fb_watch_word(addr & ~1u);
    addr = vramlock_ConvOffset32toOffset64(addr);
    *host_ptr_xor((u16*)&vram[addr]) = data;
}

void FASTCALL pvr_write_area1_32(u32 addr, u32 data)
{
    fb_watch_word(addr & ~3u);
    addr = vramlock_ConvOffset32toOffset64(addr);
    *(u32*)&vram[addr] = data;
}

// Probe: LMMODE0/1 as the VRAM paths see them; the first 16 changes print.
static u32 s_lm_seen[2] = { ~0u, ~0u };
static u32 s_lm_logged;

static void lmmode_changed()
{
    if (s_lm_logged++ < 16)
        printf("[vram] LMMODE0=%X LMMODE1=%X (was %X/%X)\n", SB_LMMODE0, SB_LMMODE1, s_lm_seen[0], s_lm_seen[1]);
    s_lm_seen[0] = SB_LMMODE0;
    s_lm_seen[1] = SB_LMMODE1;
    g_vram_split.lm_changes++;
}

static INLINE void lmmode_note()
{
    if (SB_LMMODE0 != s_lm_seen[0] || SB_LMMODE1 != s_lm_seen[1])
        lmmode_changed();
}

// P7: 0x11 follows LMMODE0, 0x13 LMMODE1; 0 = the 64-bit bus, linear in vram (Flycast).
static INLINE bool vram_path64(u32 address)
{
    return ((address & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0) == 0;
}

// vramlock_ConvOffset32toOffset64 inline, for an offset already masked to VRAM_MASK.
static INLINE u32 vram_map32(u32 off)
{
    return ((off & 0x3FFFFC) << 1) | ((off >> 20) & 4) | (off & 3);
}

// The 32-bit bus: word i lands at map(off + 4i). P55 VRAM Direct maps once per stretch inside a 4 MB bank.
static void vram_write32_run(u32 off, const u32* data, u32 words)
{
    if (!g_vram_direct_preset) {
        for (u32 i = 0; i < words; i++, off += 4)
            pvr_write_area1_32(off, data[i]);
        return;
    }
    while (words) {
        off &= VRAM_MASK;
        u32 n = (0x400000 - (off & 0x3FFFFC)) >> 2;
        if (n > words)
            n = words;
        fb_watch_run(off & ~3u, n * 4);
        u32* d = (u32*)&vram.data[vram_map32(off)];
        for (u32 i = 0; i < n; i++)
            d[2 * i] = data[i];
        data += n;
        words -= n;
        off += n * 4;
    }
}

// An SQ block to 0x11/0x13. Without P7 always the 32-bit bus: the old linear copy scrambled Rez's SQ textures.
static void vram_write_sq(u32 address, const u32* data)
{
    const u32 off = address & VRAM_MASK;
    lmmode_note();
    if (g_lmmode_preset && vram_path64(address)) {
        g_vram_split.sq_v64++;
        memcpy(&vram.data[off], data, 32);
        return;
    }
    g_vram_split.sq_v32++;
    vram_write32_run(off, data, 8);
}

void pvr_vram_dma(u32 dst, u32* data, u32 bytes)
{
    lmmode_note();
    if (vram_path64(dst)) {
        g_vram_split.dma_v64 += bytes;
        WriteMemBlock_nommu_ptr((dst & 0x00FFFFFF) | 0xA4000000, data, bytes);
        return;
    }
    g_vram_split.dma_v32 += bytes;
    vram_write32_run(dst & 0x00FFFFFF, data, bytes >> 2);
}

void vram_split_format(char* buf, u32 size, double seconds, u32 sq_calls)
{
    VramSplit& v = g_vram_split;
    const u32 sq_ta = sq_calls - v.sq_yuv - v.sq_v64 - v.sq_v32 - v.sq_mem;
    const double kb = 1024.0 * seconds;
    snprintf(buf, size, "sq/s ta=%.0f yuv=%.0f v64=%.0f v32=%.0f mem=%.0f | dma KB/s ta=%.0f yuv=%.0f v64=%.0f"
             " v32=%.0f lm1stub=%.0f | yuv blk in=%u cp=%u | lm=%X/%X chg=%u | p43=%d p55=%d p7=%d p26=%d",
             sq_ta / seconds, v.sq_yuv / seconds, v.sq_v64 / seconds, v.sq_v32 / seconds, v.sq_mem / seconds,
             v.dma_ta / kb, v.dma_yuv / kb, v.dma_v64 / kb, v.dma_v32 / kb, v.dma_stub / kb,
             v.yuv_inplace, v.yuv_copied, SB_LMMODE0, SB_LMMODE1, v.lm_changes,
             g_yuv_conv_fast_preset, g_vram_direct_preset, g_lmmode_preset, g_fb_watch_preset);
    memset(&v, 0, sizeof(v));
}

// Tile Accelerator DMA Interface
// TA write routing: 0-7FFFFF polygon data, 800000-FFFFFF YUV converter, 1000000+ direct VRAM.
void FASTCALL TAWrite(u32 address, u32* data, u32 count)
{
    u32 address_masked = address & 0x1FFFFFF;

    if (address_masked < 0x800000) {
        // TA polygon data (0-8MB range)
        g_vram_split.dma_ta += count * 32;
        libPvr_TaDMA(data, count);
    } else if (address_masked < 0x1000000) {
        // YUV converter (8-16MB range)
        g_vram_split.dma_yuv += count * 32;
        YUV_data(data, count);
    } else {
        // Direct VRAM write: the 64-bit area is bank-interleaved, so go through pvr_write_area1_32
        // (not a linear memcpy) to match the renderer's read layout.
        u32 off    = address & VRAM_MASK;
        u32 words  = count * 8;                   // 8 u32 per 32-byte block
        for (u32 i = 0; i < words; i++, off += 4)
            pvr_write_area1_32(off, data[i]);
    }
}

// Writes one 32-byte store-queue block to the TA.
void FASTCALL TAWriteSQ(u32 address, u32* data)
{
    u32 address_masked = address & 0x1FFFFFF;

    if (address_masked < 0x800000) {
        // TA polygon data
        libPvr_TaSQ(data);
    } else if (address_masked < 0x1000000) {
        // YUV converter
        g_vram_split.sq_yuv++;
        YUV_data(data, 1);
    } else {
        // Direct VRAM write (0x11xxxxxx / 0x13xxxxxx textures via SQ)
        vram_write_sq(address, data);
    }
}

// Module Lifecycle
void pvr_Init()
{
    // Reserved for future initialization
}

void pvr_Term()
{
    // Reserved for future cleanup
}

void pvr_Reset(bool Manual)
{
    // Clear VRAM on automatic reset only
    if (!Manual) {
        vram.Zero();
    }
    
    // Reset YUV converter state
    YUV_index = 0;
    YUV_dest = 0;
    YUV_doneblocks = 0;
    YUV_blockcount = 0;
    YUV_x_curr = 0;
    YUV_y_curr = 0;
    YUV_x_size = 0;
    YUV_y_size = 0;
    YUV_is420 = true;
    YUV_block_bytes = YUV_BLOCK_SIZE_420;
    YUV_region_due = false;
    g_fb_dirty = false;

    for (u32 i = 0; i < YUV_REGION_SLOTS; i++) {
        YUV_regions[i].base = 0;
        YUV_regions[i].w    = 0;
        YUV_regions[i].h    = 0;
    }
    YUV_region_next = 0;
}
