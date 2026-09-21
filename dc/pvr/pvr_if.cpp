#include "types.h"
#include "pvr_if.h"
#include "pvrLock.h"
#include "dc/sh4/intc.h"
#include "dc/mem/_vmem.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

// Swirl debug: TA input per render, printed by ps3/ps3_rend.cpp.

// PowerVR Interface Implementation Handles YUV conversion, register access, and VRAM operations
// YUV Converter Constants
namespace {
    constexpr u32 YUV_TEMP_BUFFER_SIZE = 512; // bytes
    constexpr u32 YUV_BLOCK_SIZE_420 = 384;   // YUV 4:2:0 format
    constexpr u32 YUV_BLOCK_SIZE_422 = 512;   // YUV 4:2:2 format
    constexpr u32 YUV_MACROBLOCK_SIZE = 16;   // 16x16 pixels per macroblock
    
    // Register addresses
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

// YUV converter output regions: the converter writes rows of the video's real width with no padding,
// so the renderer looks texture addresses up here for the real pitch (TA_YUV_TEX_CTRL is often stale).
struct YuvConvRegion { u32 base; u32 w; u32 h; };
static const u32 YUV_REGION_SLOTS = 4;
static YuvConvRegion YUV_regions[YUV_REGION_SLOTS];
static u32 YUV_region_next = 0;

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
    YUV_region_next = (YUV_region_next + 1) % YUV_REGION_SLOTS;
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

// YUV Helper Functions
// Writes 2 YUYV pixels (4 bytes) at offset (x, y) from the current position.
// First word of each surface, kept so the decoder's read of the same address
// can be compared against it: equal means the encoding is not the fault.
u32 g_yuv_grab, g_yuv_first_off, g_yuv_first_word;

INLINE void YUV_putpixel2(u32 x, u32 y, u32 pixdata)
{
    u32 offset = YUV_dest + (YUV_x_curr + x + (YUV_y_curr + y) * YUV_x_size) * 2;
    if (g_yuv_grab) { g_yuv_grab = 0; g_yuv_first_off = offset; g_yuv_first_word = pixdata; }
    *(u32*)(&vram.data[offset]) = pixdata;
}

// Y of a 4:2:0 macroblock at (x, y), 0-15; base = 256 bytes of Y.
INLINE u8 GetY420(int x, int y, u8* base)
{
    // 4:2:0 format stores Y in 8x8 sub-blocks
    if (x > 7) {
        x -= 8;
        base += 64;  // Move to right 8x8 block
    }
    
    if (y > 7) {
        y -= 8;
        base += 128; // Move to bottom 8x8 block
    }
    
    // CH2 memcpy's main RAM verbatim, and this host stores guest u32s natively,
    // so a guest byte still lives at host offset ^3. Indexing linearly here mixes
    // the two models.
    return *host_ptr_xor(&base[x + y * 8]);
}

// U or V of a 4:2:0 macroblock at (x, y), 0-15; subsampled 2:1, base = 64 bytes.
INLINE u8 GetUV420(int x, int y, u8* base)
{
    // Subsample coordinates
    int realx = x >> 1;
    int realy = y >> 1;

    return *host_ptr_xor(&base[realx + realy * 8]);
}

// Producer-side counters for the SC5 FMV backdrop hunt: they separate
// "converter never fed" from "fed but writing where the cache never hashes".
u32 g_yuv_inits, g_yuv_calls, g_yuv_bytes, g_yuv_blk420, g_yuv_blk422;
u32 g_yuv_dest, g_yuv_w, g_yuv_h, g_yuv_ctrl;
u32 g_yuv_midinit, g_yuv_midblk;
// Raw macroblock input: first words of the U, V and Y planes. Real chroma
// clusters near 128 and luma spreads, so this says whether the stream itself is
// sane before any of our extraction touches it.
u32 g_yuv_inU, g_yuv_inV, g_yuv_inY;
u32 g_yuv_mean0, g_yuv_mean1, g_yuv_mean2;

extern "C" u32 yuv_probe_off() { return g_yuv_first_off; }

extern "C" void yuv_stats(u32* v)
{
    v[0] = g_yuv_inits; v[1] = g_yuv_calls; v[2] = g_yuv_bytes;
    v[3] = g_yuv_blk420; v[4] = g_yuv_blk422;
    v[5] = g_yuv_dest;  v[6] = g_yuv_w;     v[7] = g_yuv_h;
    v[8] = g_yuv_ctrl;
    v[9] = g_yuv_first_off; v[10] = g_yuv_first_word;
    v[11] = g_yuv_midinit; v[12] = g_yuv_midblk; v[13] = YUV_blockcount;
    v[14] = g_yuv_inU; v[15] = g_yuv_inV; v[16] = g_yuv_inY;
    v[17] = g_yuv_mean0; v[18] = g_yuv_mean1; v[19] = g_yuv_mean2;
    g_yuv_midinit = 0;
    g_yuv_calls = g_yuv_bytes = g_yuv_blk420 = g_yuv_blk422 = 0;
}

// Initializes the YUV converter state from the hardware registers.
void YUV_init()
{
    g_yuv_inits++;
    // An init part-way through a surface rewinds the macroblock cursor to (0,0),
    // so every block after it lands on top of the start of the frame.
    if (YUV_blockcount && YUV_doneblocks && YUV_doneblocks < YUV_blockcount)
    {
        g_yuv_midinit++;
        g_yuv_midblk = YUV_doneblocks;
    }
    YUV_index = 0;
    YUV_x_curr = 0;
    YUV_y_curr = 0;
    YUV_doneblocks = 0;
    
    // Read destination address and mask to valid VRAM range
    YUV_dest = pvr_readreg_TA(TA_YUV_TEX_BASE_ADDR, 4) & VRAM_MASK;
    
    // Read texture control register
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
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
    g_yuv_dest = YUV_dest; g_yuv_w = YUV_x_size; g_yuv_h = YUV_y_size;
    g_yuv_ctrl = TA_YUV_TEX_CTRL;
}

// Converts one 16x16 YUV macroblock to YUYV and writes it to VRAM.
INLINE void YUV_ConvertMacroBlock()
{
    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    YUV_doneblocks++;
    YUV_index = 0;
    // doneblocks increments before converting, so this captures index 149:
    // row 7, column 9 of the 20x15 grid, pixel (144,112).
    if (YUV_doneblocks == 150)
    {
        g_yuv_grab = 1;
        g_yuv_inU = YUV_tempdata[0];
        g_yuv_inV = YUV_tempdata[64 / 4];
        g_yuv_inY = YUV_tempdata[128 / 4];
        // Both chroma planes average close to 128 in any real frame; luma
        // averages the picture's brightness. Three means locate the planes.
        const u8* b = (const u8*)YUV_tempdata;
        u32 s0 = 0, s1 = 0, s2 = 0;
        for (u32 i = 0; i < 64; i++)   s0 += b[i];
        for (u32 i = 64; i < 128; i++) s1 += b[i];
        for (u32 i = 128; i < 384; i++) s2 += b[i];
        g_yuv_mean0 = s0 / 64; g_yuv_mean1 = s1 / 64; g_yuv_mean2 = s2 / 256;
    }
    
    // Determine block format
    bool is_yuv420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);
    
    if (is_yuv420) {
        // Remember the written region so the renderer can tell converter output from uploaded YUV422.
        YUV_RememberRegion(YUV_dest, YUV_x_size, YUV_y_size);
        g_yuv_blk420++;

        // YUV 4:2:0 format (384 bytes per macroblock)
        // Layout: 64 bytes U, 64 bytes V, 256 bytes Y
        u8* U = (u8*)&YUV_tempdata[0];
        u8* V = (u8*)&YUV_tempdata[64 / 4];
        u8* Y = (u8*)&YUV_tempdata[(64 + 64) / 4];
        
        u8 yuyv[4]; // Temporary buffer for 2 pixels
        
        // Convert 16x16 block
        for (u32 y = 0; y < YUV_MACROBLOCK_SIZE; y++) {
            for (u32 x = 0; x < YUV_MACROBLOCK_SIZE; x += 2) {
                // Pack as DC LE UYVY u32 (U|Y0<<8|V<<16|Y1<<24); stored as a BE u32 this matches CPU
                // 32-bit stores, so one decoder handles TA-YUV and CPU-written YUV422.
                yuyv[0] = GetY420(x + 1, y, Y);  // Y1 = Wii BE MSB = DC byte 3
                yuyv[1] = GetUV420(x, y, V);      // V  = DC byte 2
                yuyv[2] = GetY420(x, y, Y);       // Y0 = DC byte 1
                yuyv[3] = GetUV420(x, y, U);      // U  = Wii BE LSB = DC byte 0
                
                // Write 2 pixels at once
                YUV_putpixel2(x, y, *(u32*)yuyv);
            }
        }
        
        // Advance to next macroblock position
        YUV_x_curr += YUV_MACROBLOCK_SIZE;
        if (YUV_x_curr >= YUV_x_size) {
            YUV_x_curr = 0;
            YUV_y_curr += YUV_MACROBLOCK_SIZE;
            if (YUV_y_curr >= YUV_y_size) {
                YUV_y_curr = 0;
            }
        }
    } else {
        // Consumed and acknowledged but never written: counted, not printed per block.
        g_yuv_blk422++;
    }
    
    // Check if all blocks are processed
    if (YUV_doneblocks >= YUV_blockcount) {
        YUV_init();
        // Raise interrupt to signal completion
        asic_RaiseInterrupt(holly_YUV_DMA);
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
    
    g_yuv_calls++;
    g_yuv_bytes += count * 32;

    u32 TA_YUV_TEX_CTRL = pvr_readreg_TA(TA_YUV_TEX_CTRL_ADDR, 4);
    
    // Determine block size based on format
    u32 block_size = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0) 
                     ? YUV_BLOCK_SIZE_420 
                     : YUV_BLOCK_SIZE_422;
    
    // Convert count from 32-byte blocks to bytes
    count *= 32;
    
    // Process data in chunks
    while (count > 0) {
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
    
    // Reinitialize YUV converter if base address is written
    if ((addr & 0xFFFFFF) == TA_YUV_TEX_BASE_ADDR) {
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

void FASTCALL pvr_write_area1_16(u32 addr, u16 data)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    *host_ptr_xor((u16*)&vram[addr]) = data;
}

void FASTCALL pvr_write_area1_32(u32 addr, u32 data)
{
    addr = vramlock_ConvOffset32toOffset64(addr);
    *(u32*)&vram[addr] = data;
}

// Tile Accelerator DMA Interface
// TA write routing: 0-7FFFFF polygon data, 800000-FFFFFF YUV converter, 1000000+ direct VRAM.
void FASTCALL TAWrite(u32 address, u32* data, u32 count)
{
    u32 address_masked = address & 0x1FFFFFF;
    
    if (address_masked < 0x800000) {
        // TA polygon data (0-8MB range)
        libPvr_TaDMA(data, count);
    } else if (address_masked < 0x1000000) {
        // YUV converter (8-16MB range)
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
        YUV_data(data, 1);
    } else {
        // Direct VRAM write (0x11xxxxxx textures via SQ): interleaved through pvr_write_area1_32 like every
        // other VRAM path (PSP port); the old linear copy scrambled SQ-uploaded textures (Rez).
        u32 off = address & VRAM_MASK;          // 32-bit VRAM offset
        for (int i = 0; i < 8; i++, off += 4)   // 8 * 4 = 32 bytes
            pvr_write_area1_32(off, data[i]);   // applies the 32->64 interleave
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

    for (u32 i = 0; i < YUV_REGION_SLOTS; i++) {
        YUV_regions[i].base = 0;
        YUV_regions[i].w    = 0;
        YUV_regions[i].h    = 0;
    }
    YUV_region_next = 0;
}
