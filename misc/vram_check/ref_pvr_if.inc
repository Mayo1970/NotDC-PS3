// Swirl debug: TA input per render, printed by ps3/ps3_rend.cpp.

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

// Feeds count 32-byte blocks of YUV data to the converter.
void YUV_data(u32* data, u32 count)
{
    // Initialize if not already done
    if (YUV_blockcount == 0) {
        printf("YUV_data: YUV decoder not initialized, initializing now\n");
        YUV_init();
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
