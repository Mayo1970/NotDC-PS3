// _vmem: _vmem_MemInfo_ptr[addr>>24] holds a host pointer (low bits = mirror shift)
// or a small handler index (< HANDLER_COUNT*4) into the _vmem_RF*/_vmem_WF* tables.

#include "_vmem.h"
#include "dc/aica/aica_if.h"
#include "dc/pvr/pvr_if.h"
#include "sh4_mem.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#if HOST_OS == OS_PS3
#include <malloc.h>
#endif

// ---- Constants ----
#define HANDLER_MAX   0x1F
#define HANDLER_COUNT (HANDLER_MAX + 1)

// Unmapped reads return 0: the old 0xDEADC0D3 got used as a pointer and caused
// garbage writes to 0xDEADE5F3+ (a freeze).
#define MEM_ERROR_RETURN_VALUE 0u

// ---- Global tables ----
static _vmem_handler     _vmem_lrp;   // next free handler slot

static _vmem_ReadMem8FP*  _vmem_RF8  [HANDLER_COUNT];
static _vmem_WriteMem8FP* _vmem_WF8  [HANDLER_COUNT];

static _vmem_ReadMem16FP*  _vmem_RF16[HANDLER_COUNT];
static _vmem_WriteMem16FP* _vmem_WF16[HANDLER_COUNT];

static _vmem_ReadMem32FP*  _vmem_RF32[HANDLER_COUNT];
static _vmem_WriteMem32FP* _vmem_WF32[HANDLER_COUNT];

// Top-level dispatch table: 256 entries, one per 16 MB page.
ALIGN(65536) void* _vmem_MemInfo_ptr[0x100];

// ---- Internal helpers ----

// Low address bits to mask away for a block: 16 MB (0x00FFFFFF) gives 0,
// smaller blocks (e.g. 2 MB VRAM) give the mirror shift.
static u32 FindMask(u32 msk)
{
    u32 s  = (u32)-1;
    u32 rv = 0;
    while (msk != s >> rv)
        rv++;
    return rv;
}

// Core read dispatcher (templated on access size).
template<typename T>
static INLINE T fastcall _vmem_readt(u32 addr)
{
    const u32 sz   = sizeof(T);
    const u32 page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
    {
        // MMIO / handler path
        const u32 id = (u32)iirf;
        if (sz == 1)
            return (T)_vmem_RF8[id / 4](addr);
        else if (sz == 2)
            return (T)_vmem_RF16[id / 4](addr);
        else if (sz == 4)
            return (T)_vmem_RF32[id / 4](addr);
        else if (sz == 8)
        {
            // 64-bit: two 32-bit reads. The word at addr goes where the direct RAM path puts it:
            // the high half on big-endian hosts, where dr_hex overlays fr[2n] on the high half.
            u64 lo_addr = _vmem_RF32[id / 4](addr);
            u64 hi_addr = _vmem_RF32[id / 4](addr + 4);
#if HOST_ENDIAN == ENDIAN_BIG
            return (T)((lo_addr << 32) | hi_addr);
#else
            return (T)(lo_addr | (hi_addr << 32));
#endif
        }
        die("_vmem_readt: invalid size");
    }
    else
    {
        // Direct RAM/VRAM path
        u32 shift = (u32)(iirf & HANDLER_MAX);
        addr <<= shift;
        addr >>= shift;
#if HOST_ENDIAN == ENDIAN_BIG
        if (sz < 4)
            addr ^= 4 - sz;
#endif
        return *((T*)&((u8*)ptr)[addr]);
    }
}

// Core write dispatcher (templated on access size).
template<typename T>
static INLINE void fastcall _vmem_writet(u32 addr, T data)
{
    const u32 sz   = sizeof(T);
    const u32 page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
    {
        // MMIO / handler path
        const u32 id = (u32)iirf;
        if (sz == 1)
            _vmem_WF8[id / 4](addr, (u8)data);
        else if (sz == 2)
            _vmem_WF16[id / 4](addr, (u16)data);
        else if (sz == 4)
            _vmem_WF32[id / 4](addr, (u32)data);
        else if (sz == 8)
        {
            // Same word order as the 64-bit read above.
#if HOST_ENDIAN == ENDIAN_BIG
            _vmem_WF32[id / 4](addr,     (u32)((u64)data >> 32));
            _vmem_WF32[id / 4](addr + 4, (u32)(u64)data);
#else
            _vmem_WF32[id / 4](addr,     (u32)(u64)data);
            _vmem_WF32[id / 4](addr + 4, (u32)((u64)data >> 32));
#endif
        }
        else
            die("_vmem_writet: invalid size");
    }
    else
    {
        // Direct RAM/VRAM path
        u32 shift = (u32)(iirf & HANDLER_MAX);
        addr <<= shift;
        addr >>= shift;
#if HOST_ENDIAN == ENDIAN_BIG
        if (sz < 4)
            addr ^= 4 - sz;
#endif
        *((T*)&((u8*)ptr)[addr]) = data;
    }
}

// Resolves [Address, Address+size) to one host pointer for a single memmove, or 0
// if MMIO, sub-word, mirror-wrapping or page-crossing (those need the per-word path).
u8* fastcall _vmem_GetBlockPtr(u32 Address, u32 size)
{
    if (size == 0 || (size & 3) != 0 || (Address & 3) != 0)
        return 0;

    const u32   page = Address >> 24;
    const unat  iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr  = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
        return 0;                       // MMIO / handler region

    const u32 last = Address + size - 1;
    if ((last >> 24) != page)
        return 0;                       // crosses into another region

    const u32 shift = (u32)(iirf & HANDLER_MAX);
    const u32 base  = (Address << shift) >> shift;
    if (((last << shift) >> shift) != base + size - 1)
        return 0;                       // mirror wrap inside the range

    return &((u8*)ptr)[base];
}

// ---- Public read/write accessors ----
u8  fastcall _vmem_ReadMem8  (u32 addr) { CCALL_MEM(CC_READMEM, 0, addr); return _vmem_readt<u8> (addr); }
u16 fastcall _vmem_ReadMem16 (u32 addr) { CCALL_MEM(CC_READMEM, 0, addr); return _vmem_readt<u16>(addr); }
u32 fastcall _vmem_ReadMem32 (u32 addr) { CCALL_MEM(CC_READMEM, 0, addr); return _vmem_readt<u32>(addr); }
u64 fastcall _vmem_ReadMem64 (u32 addr) { CCALL_MEM64(CC_READMEM, 0, addr); return _vmem_readt<u64>(addr); }

void fastcall _vmem_WriteMem8  (u32 addr, u8  data) { CCALL_MEM(CC_WRITEMEM, 1, addr); _vmem_writet<u8> (addr, data); }
void fastcall _vmem_WriteMem16 (u32 addr, u16 data) { CCALL_MEM(CC_WRITEMEM, 1, addr); _vmem_writet<u16>(addr, data); }
void fastcall _vmem_WriteMem32 (u32 addr, u32 data) { CCALL_MEM(CC_WRITEMEM, 1, addr); _vmem_writet<u32>(addr, data); }
void fastcall _vmem_WriteMem64 (u32 addr, u64 data) { CCALL_MEM64(CC_WRITEMEM, 1, addr); _vmem_writet<u64>(addr, data); }

// ---- Default "not mapped" handlers: rate-limited log, then the safe sentinel ----

// Simple rate-limiter: skip logging if we've already seen this page recently.
static u32 s_last_unmapped_page = 0xFFFFFFFFu;
static u32 s_unmapped_log_count = 0;
static const u32 MAX_LOG_PER_PAGE = 8;

static bool should_log_unmapped(u32 addr)
{
    u32 page = addr >> 12; // 4 KB granularity for rate limiting
    if (page != s_last_unmapped_page)
    {
        s_last_unmapped_page  = page;
        s_unmapped_log_count  = 0;
    }
    if (s_unmapped_log_count < MAX_LOG_PER_PAGE)
    {
        s_unmapped_log_count++;
        return true;
    }
    return false;
}

u8 fastcall _vmem_ReadMem8_not_mapped(u32 addr)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Read8  from unmapped 0x%08X\n", addr);
    return (u8)MEM_ERROR_RETURN_VALUE;
}
u16 fastcall _vmem_ReadMem16_not_mapped(u32 addr)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Read16 from unmapped 0x%08X\n", addr);
    return (u16)MEM_ERROR_RETURN_VALUE;
}
u32 fastcall _vmem_ReadMem32_not_mapped(u32 addr)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Read32 from unmapped 0x%08X\n", addr);
    return (u32)MEM_ERROR_RETURN_VALUE;
}

void fastcall _vmem_WriteMem8_not_mapped(u32 addr, u8 data)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Write8  to unmapped 0x%08X = 0x%02X\n", addr, data);
}
void fastcall _vmem_WriteMem16_not_mapped(u32 addr, u16 data)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Write16 to unmapped 0x%08X = 0x%04X\n", addr, data);
}
void fastcall _vmem_WriteMem32_not_mapped(u32 addr, u32 data)
{
    if (should_log_unmapped(addr))
        printf("[vmem] Write32 to unmapped 0x%08X = 0x%08X\n", addr, data);
}

// ---- Handler registration ----
_vmem_handler _vmem_register_handler(
    _vmem_ReadMem8FP*   read8,
    _vmem_ReadMem16FP*  read16,
    _vmem_ReadMem32FP*  read32,
    _vmem_WriteMem8FP*  write8,
    _vmem_WriteMem16FP* write16,
    _vmem_WriteMem32FP* write32)
{
    _vmem_handler rv = _vmem_lrp++;
    verify(rv < HANDLER_COUNT);

    _vmem_RF8 [rv] = read8   ? read8   : _vmem_ReadMem8_not_mapped;
    _vmem_RF16[rv] = read16  ? read16  : _vmem_ReadMem16_not_mapped;
    _vmem_RF32[rv] = read32  ? read32  : _vmem_ReadMem32_not_mapped;

    _vmem_WF8 [rv] = write8  ? write8  : _vmem_WriteMem8_not_mapped;
    _vmem_WF16[rv] = write16 ? write16 : _vmem_WriteMem16_not_mapped;
    _vmem_WF32[rv] = write32 ? write32 : _vmem_WriteMem32_not_mapped;

    return rv;
}

// ---- Mapping ----
void _vmem_map_handler(_vmem_handler Handler, u32 start, u32 end)
{
    verify(start < 0x100);
    verify(end   < 0x100);
    verify(start <= end);
    for (u32 i = start; i <= end; i++)
        _vmem_MemInfo_ptr[i] = (u8*)0 + (Handler * 4);
}

void _vmem_map_block(void* base, u32 start, u32 end, u32 mask)
{
    verify(start < 0x100);
    verify(end   < 0x100);
    verify(start <= end);
    verify(((unat)base & 0xFF) == 0);
    verify(base != 0);

    const u32 shift = FindMask(mask);
    u32 j = 0;
    for (u32 i = start; i <= end; i++)
    {
        _vmem_MemInfo_ptr[i] = &((u8*)base)[j] + shift;
        j += 0x1000000; // advance one 16 MB page
    }
}

void _vmem_mirror_mapping(u32 new_region, u32 start, u32 size)
{
    u32 end = start + size - 1;
    verify(start < 0x100);
    verify(end   < 0x100);
    verify(start <= end);
    // Destination region must not overlap source range
    verify(!((new_region >= start) && (new_region <= end)));

    for (u32 i = 0; i < size; i++)
        _vmem_MemInfo_ptr[(new_region + i) & 0xFF] = _vmem_MemInfo_ptr[(start + i) & 0xFF];
}

// ---- Dynarec helpers ----
void _vmem_get_ptrs(u32 sz, bool write, void*** vmap, void*** func)
{
    *vmap = _vmem_MemInfo_ptr;
    switch (sz)
    {
    case 1: *func = write ? (void**)_vmem_WF8  : (void**)_vmem_RF8;  return;
    case 2: *func = write ? (void**)_vmem_WF16 : (void**)_vmem_RF16; return;
    case 4:
    case 8: *func = write ? (void**)_vmem_WF32 : (void**)_vmem_RF32; return;
    default: die("_vmem_get_ptrs: invalid size");
    }
}

void* _vmem_read_const(u32 addr, bool& ismem, u32 sz)
{
    const u32  page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    void* const ptr  = (void*)(iirf & ~(unat)HANDLER_MAX);

    if (ptr == 0)
    {
        ismem = false;
        const u32 id = (u32)iirf;
        if (sz == 1) return (void*)_vmem_RF8 [id / 4];
        if (sz == 2) return (void*)_vmem_RF16[id / 4];
        if (sz == 4) return (void*)_vmem_RF32[id / 4];
        die("_vmem_read_const: invalid size");
    }
    else
    {
        ismem = true;
        u32 shift = (u32)(iirf & HANDLER_MAX);
        addr <<= shift;
        addr >>= shift;
#if HOST_ENDIAN == ENDIAN_BIG
        if (sz < 4)
            addr ^= 4 - sz;
#endif
        return &((u8*)ptr)[addr];
    }
}

// ---- Diagnostic helper ----
bool _vmem_is_mapped(u32 addr)
{
    const u32  page = addr >> 24;
    const unat iirf = (unat)_vmem_MemInfo_ptr[page];
    return (void*)(iirf & ~(unat)HANDLER_MAX) != nullptr;
}

// ---- Lifecycle ----
void _vmem_init()
{
    _vmem_reset();
}

void _vmem_reset()
{
    memset(_vmem_RF8,  0, sizeof(_vmem_RF8));
    memset(_vmem_RF16, 0, sizeof(_vmem_RF16));
    memset(_vmem_RF32, 0, sizeof(_vmem_RF32));
    memset(_vmem_WF8,  0, sizeof(_vmem_WF8));
    memset(_vmem_WF16, 0, sizeof(_vmem_WF16));
    memset(_vmem_WF32, 0, sizeof(_vmem_WF32));

    memset(_vmem_MemInfo_ptr, 0, sizeof(_vmem_MemInfo_ptr));

    _vmem_lrp = 0;

    // Slot 0 = the "not-mapped" default; must be registered first.
    verify(_vmem_register_handler(0, 0, 0, 0, 0, 0) == 0);

    // Reset log-rate-limiter state.
    s_last_unmapped_page = 0xFFFFFFFFu;
    s_unmapped_log_count = 0;
}

void _vmem_term()
{
    // Nothing to do for the software-only mapping tables.
}

// ---- Memory reservation ----
#if HOST_OS == OS_PS3
static u8* ps3_dc_mem;

bool _vmem_reserve()
{
    // 64 KB alignment is required by _vmem_map_block.
    const u32 total = ARAM_SIZE + VRAM_SIZE + RAM_SIZE;
    ps3_dc_mem = (u8*)memalign(65536, total);
    if (!ps3_dc_mem)
    {
        printf("[vmem] memalign(%u KB) FAILED\n", total / 1024u);
        return false;
    }
    memset(ps3_dc_mem, 0, total);

    u8* ram_alloc = ps3_dc_mem;
    aica_ram.size = ARAM_SIZE;
    aica_ram.data = ram_alloc;
    ram_alloc += ARAM_SIZE;

    vram.size = VRAM_SIZE;
    vram.data = ram_alloc;
    ram_alloc += VRAM_SIZE;

    mem_b.size = RAM_SIZE;
    mem_b.data = ram_alloc;

    printf("[vmem] DC memory %p (%u KB)\n", ps3_dc_mem, total / 1024u);
    return true;
}
#endif

void _vmem_release()
{
#if HOST_OS == OS_PS3
    free(ps3_dc_mem);
    ps3_dc_mem = 0;
#endif
}
