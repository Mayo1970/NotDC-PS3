#include "ta.h"
#include "Renderer_if.h"	// TaTicks / TaCalls / PERF_TICKS — see the split doc there
#include "ps3/ps3_tasplit.h"
#include "ps3/ps3_presets.h"
#include "dc/pvr/pvr_vram.h"
#include <stdio.h>

// Tile Accelerator (TA) state machine for PowerVR2 (Dreamcast) emulation
// Handles DMA and Store Queue writes, dispatches polygon/vertex/control params

namespace TASplitter
{
    // Current active command handler (state machine pointer)
    TaListFP* TaCmd = nullptr;
}

using namespace TASplitter;

TaSplit g_ta_split = { 0, 64, 0, 0, 0, 0, 0, 0 };
static u32 s_split_gap = 64;           // calls the next sample stands for
static u32 s_split_seed = 0x9E3779B9u;
static u64 s_split_sum[4];             // in, copy, fsm, out, in timebase ticks
static u32 s_split_taken, s_split_n, s_split_long, s_split_calls0;
static u64 s_ram_sum[3];               // RAM samples: in, write, out
static u32 s_ram_n, s_ram_long;
static u64 s_dma_ticks;
static u32 s_dma_calls, s_dma_blocks;

// A preempted sample, or one across the Cell timebase carry bug, is longer than this (~20 us): dropped.
#define TA_SPLIT_MAX_TICKS 1600

// do_sqw exit of a sampled call. TaTicks takes the store part (copy + FSM) for the calls it stands for.
void ta_split_leave(u64 t_entry)
{
    const u64 t_exit = gettime();
    TaSplit& g = g_ta_split;
    g.armed = 0;
    s_split_taken++;
    const u32 weight = s_split_gap;
    // Gaps of 48..79 calls, so the sample cannot lock onto a repeating record pattern.
    s_split_seed = s_split_seed * 1664525u + 1013904223u;
    s_split_gap = 48 + (s_split_seed >> 27);
    g.next = g.calls + s_split_gap;
    if (!g.hit)
    {
        // RAM, VRAM or the YUV converter; only RAM writes are timed.
        if (g.ram)
        {
            g.ram = 0;
            const u64 r[3] = { g.t_store - t_entry, g.t_copy - g.t_store, t_exit - g.t_copy };
            if (r[0] > TA_SPLIT_MAX_TICKS || r[1] > TA_SPLIT_MAX_TICKS || r[2] > TA_SPLIT_MAX_TICKS)
                s_ram_long++;
            else
            {
                for (int i = 0; i < 3; i++)
                    s_ram_sum[i] += r[i];
                s_ram_n++;
            }
        }
        return;
    }
    g.hit = 0;
    const u64 d[4] = { g.t_store - t_entry, g.t_copy - g.t_store, g.t_fsm - g.t_copy, t_exit - g.t_fsm };
    for (int i = 0; i < 4; i++)
    {
        if (d[i] > TA_SPLIT_MAX_TICKS)
        {
            s_split_long++;
            return;
        }
    }
    for (int i = 0; i < 4; i++)
        s_split_sum[i] += d[i];
    s_split_n++;
    TaTicks += (d[1] + d[2]) * weight;
}

// Per sample, ns: in = do_sqw entry to the store, copy, fsm, out = to do_sqw's exit (one body with TA SQ
// Direct). Each segment holds one mftb; "path" takes 4 of them out and scales to the TA flushes per second.
void ta_split_dump(double seconds)
{
    const u32 calls = g_ta_split.calls - s_split_calls0;
    s_split_calls0 = g_ta_split.calls;
    const double tick_ns = 1e9 / (double)ps3_timebase_hz;
    const int reads = 4096;
    const u64 m0 = gettime();
    for (int i = 0; i < reads; i++)
        (void)gettime();
    const double mftb_ns = (double)(gettime() - m0) * tick_ns / (reads + 1);

    double seg[4] = { 0.0, 0.0, 0.0, 0.0 };
    double sum = 0.0, path = 0.0;
    if (s_split_n)
    {
        for (int i = 0; i < 4; i++)
        {
            seg[i] = (double)s_split_sum[i] * tick_ns / s_split_n;
            sum += seg[i];
        }
        const u32 hits = s_split_n + s_split_long;
        const double ta_per_s = calls / seconds * hits / s_split_taken;
        path = (sum - 4.0 * mftb_ns) * ta_per_s / 1e9 * 100.0;
    }
    // RAM flushes: 3 segments, 3 mftb taken out of "path".
    double rseg[3] = { 0.0, 0.0, 0.0 };
    double rsum = 0.0, rpath = 0.0;
    if (s_ram_n)
    {
        for (int i = 0; i < 3; i++)
        {
            rseg[i] = (double)s_ram_sum[i] * tick_ns / s_ram_n;
            rsum += rseg[i];
        }
        const double ram_per_s = calls / seconds * (s_ram_n + s_ram_long) / s_split_taken;
        rpath = (rsum - 3.0 * mftb_ns) * ram_per_s / 1e9 * 100.0;
    }
    char vbuf[320];
    vram_split_format(vbuf, sizeof(vbuf), seconds, calls);
    printf("[ta-split] sq=%.0fk/s n=%u/%u long=%u ns: in %.1f copy %.1f fsm %.1f out %.1f = %.1f mftb %.1f"
           " | sq path %.1f%% | dma calls=%u blocks=%u %.1f%% | direct=%d ramsq=%d sqst=%u sqla=%u sqcp=%u sqcold=%u | %s"
           " | ram ns: in %.1f write %.1f out %.1f = %.1f n=%u long=%u path %.1f%%\n",
           calls / seconds / 1000.0, s_split_n, s_split_taken, s_split_long,
           seg[0], seg[1], seg[2], seg[3], sum, mftb_ns, path,
           s_dma_calls, s_dma_blocks, (double)s_dma_ticks * tick_ns / 1e9 / seconds * 100.0,
           g_ta_sq_direct_preset, g_ram_sq_direct_preset, g_jit_sq_sites, g_jit_sq_la_sites, g_jit_sq_cp_sites, g_jit_sq_cold_sites,
           vbuf,
           rseg[0], rseg[1], rseg[2], rsum, s_ram_n, s_ram_long, rpath);

    for (int i = 0; i < 4; i++)
        s_split_sum[i] = 0;
    for (int i = 0; i < 3; i++)
        s_ram_sum[i] = 0;
    s_ram_n = s_ram_long = 0;
    s_split_taken = s_split_n = s_split_long = 0;
    s_dma_ticks = 0;
    s_dma_calls = s_dma_blocks = 0;
}

// One 32-byte SQ block. ps3ta_store_sq copies it and runs the store stage, so list ends interrupt on
// time; its TaTicks come from the 5.17 samples, so no block pays a bracket.
void libPvr_TaSQ(u32* data)
{
    ps3ta_store_sq(data);
    TaCalls++;
}

// DMA path: process a contiguous block of 32-byte TA entries
// 'size' is in units of Ta_Dma (32 bytes), not bytes
void libPvr_TaDMA(u32* data, u32 size)
{
    verify(size > 0);

    const u64 _t0 = PERF_TICKS();
    ps3ta_store_dma(data, size);
    const u64 dt = PERF_TICKS() - _t0;
    TaTicks += dt;
    TaCalls++;
    s_dma_ticks += dt;
    s_dma_calls++;
    s_dma_blocks += size;
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
