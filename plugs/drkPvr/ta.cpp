#include "ta.h"
#include "Renderer_if.h"	// TaTicks / TaCalls / PERF_TICKS — see the split doc there

// Tile Accelerator (TA) state machine for PowerVR2 (Dreamcast) emulation
// Handles DMA and Store Queue writes, dispatches polygon/vertex/control params

namespace TASplitter
{
    // Current active command handler (state machine pointer)
    TaListFP* TaCmd = nullptr;
}

using namespace TASplitter;

// Store Queue path: single 32-byte write (e.g. via SQ registers)
// TR1: the record is only copied here; ps3ta_store_sq runs the store-stage
// splitter so the list-end interrupts still fire on time.
void libPvr_TaSQ(u32* data)
{
    const u64 _t0 = PERF_TICKS();
    ps3ta_store_sq(data);
    TaTicks += PERF_TICKS() - _t0;
    TaCalls++;
}

// DMA path: process a contiguous block of 32-byte TA entries
// 'size' is in units of Ta_Dma (32 bytes), not bytes
void libPvr_TaDMA(u32* data, u32 size)
{
    verify(size > 0);

    const u64 _t0 = PERF_TICKS();
    ps3ta_store_dma(data, size);
    TaTicks += PERF_TICKS() - _t0;
    TaCalls++;
}

namespace TASplitter
{
    // Called when the hardware signals list continuation (TA_LIST_CONT register)
    // Resets per-list finished flags so lists can be reopened
    void TA_ListCont()
    {
        // Decoder-side continuation is handled in FifoSplitter::ListCont()
    }

    // Called when the hardware initializes a new TA list (TA_LIST_INIT register)
    // Resets the full TA state and command pointer
    void TA_ListInit()
    {
        // TaCmd is reset to ta_main inside FifoSplitter::ListInit()
    }

    // Called on TA soft reset (SOFTRESET register write)
    // Clears all list state without a full hardware reset
    void TA_SoftReset()
    {
        // Decoder-side reset handled in FifoSplitter::SoftReset()
    }
}
