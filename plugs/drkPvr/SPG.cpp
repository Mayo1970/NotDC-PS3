// SPG emulation: scanline/raster beam registers and interrupts; H-blank is not emulated properly.

#include "spg.h"
#include "Renderer_if.h"
#include "regs.h"
#include "ps3/ps3_sndthr.h"
#include "ps3/ps3_stall.h"
#include "ps3/ps3_arm_spe.h"
#include "ps3_timebase.h"
#include "dc/sh4/sys_gate.h"
#include <unistd.h>

// Game-preset controlled toggle (ps3/ps3_presets.cpp).
extern "C" int get_speed_limiter_preset();
#define SPEED_LIMITER() (get_speed_limiter_preset() != 0)

// PVR X-scaler preset (ps3/ps3_presets.cpp).
extern "C" int get_x_scaler_preset();
#define X_SCALER() (get_x_scaler_preset() != 0)

// Hardware-like HOLLY IRQ delays (ps3/ps3_presets.cpp).
extern "C" int get_render_delay_preset();
#define RENDER_DELAY() (get_render_delay_preset() != 0)

// IFB_PROBE breakdown (ps3_driver.cpp); silent when the preset is off, shares the 1 s stats block.
extern "C" void ifb_probe_dump(double seconds);
// JIT HOTBLOCKS (ps3_driver.cpp, plan 5.18); vt_k is the stats line's vt, which marks dense windows.
extern "C" void hotblocks_dump(double seconds, double vt_k);
// C call-out census (dc/sh4/rec_v2/ccall_census.h). Takes vblanks/sec too,
// so it can report the per-FRAME rate dave actually asked for.
extern "C" void ccall_census_dump(double seconds, double vbs);
// Same probe, ARM7 JIT side (plugs/vbaARM/arm7_jit.cpp): exits, cache hits, C helpers.
void arm_jit_census_dump(double seconds);
// [arm7s] ARM7 and AICA timer activity (plugs/nullAICA/aica.cpp), always on.
void aica_arm7s_dump(double wall_s);
// [aicafix] the Looney Tunes fixes at work (plugs/nullAICA/aica.cpp, plan 6.5), on while one of them is on.
void aica_fix_dump(double wall_s);
// [ta-split] TA store path split (plugs/drkPvr/ta.cpp, plan 5.17), always on.
void ta_split_dump(double seconds);
// [gdr] disc reads per caller (ps3/ps3_gdasync.cpp, OPTIMIZATION_POTENTIAL P3), on when there were reads.
void gda_stats(double secs);

// Host-side exit-combo poll (EXIT FIX = 3): driven by the scanline counter, which runs whatever the
// guest does, so a game that stops polling Maple (or never fires vblank) can still be exited.
extern "C" int  get_exit_fix_preset();
extern "C" void ExitCombo_HostPoll();
#define EXIT_HOST_POLL() (get_exit_fix_preset() >= 3)

// Scanline ticks between host polls: ~15.7 kHz, so 512 is about one poll per frame, like Maple.
#define EXIT_HOST_POLL_LINES 512

u32 spg_InVblank = 0;
s32 spg_ScanlineSh4CycleCounter = 0;
u32 spg_ScanlineCount = 512;
u32 spg_CurrentScanline = (u32)-1;  // Explicit -1 cast, avoids signed/unsigned warning
u32 spg_VblankCount = 0;
u32 spg_VblankCountTotal = 0; // monotonic vblank count since boot, never reset
s32 spg_LineSh4Cycles = 0;
u32 spg_FrameSh4Cycles = 0;

double spg_last_vps = 0;

// ── Frame pacer state (see spg_PaceFrame() below) ───────────────────────────
// Declared in drkPvr.h.
double spg_PaceErrorSec = 0.0;  // cumulative wall-clock deviation from the real-time
                                // schedule at the last vblank (+ = late, - = early)
double spg_PaceFrameSec = 0.0;  // real-time duration of one emulated video frame,
                                // 0 until the SPG has been programmed
static double s_pace_deadline = 0.0; // os_GetSeconds() time this vblank should occur at

// 54 MHz pixel clock (register defines it as 27 MHz, doubled here)
//54 mhz pixel clock (actually, this is defined as 27 .. why ? --drk)

#define PIXEL_CLOCK (27000000) // (54 * 1000 * 1000 / 2)

// Called when SPG registers are updated
void CalculateSync()
{
    u32 pixel_clock = FB_R_CTRL.vclk_div ? PIXEL_CLOCK : PIXEL_CLOCK / 2;

    spg_ScanlineCount = SPG_LOAD.vcount + 1;

    spg_LineSh4Cycles = (u32)(
        (u64)SH4_CLOCK_EFF * (u64)(SPG_LOAD.hcount + 1) / (u64)pixel_clock
    );

    // SCALER_CTL.hscale: the TA renders 1280 wide and the scaler halves it; with x_scaler on, widen
    // the canvas 2x so the geometry fills the screen (preset off keeps the legacy 640 canvas).
    float scale_x = (X_SCALER() && SCALER_CTL.hscale) ? 2.0f : 1.0f;

    if (SPG_CONTROL.interlace)
    {
        spg_LineSh4Cycles /= 2;
        rend_set_fb_scale(scale_x, 1.0f);
    }
    else
    {
        rend_set_fb_scale(scale_x,
            ((SPG_CONTROL.NTSC == 0 && SPG_CONTROL.PAL == 0) ||
             (SPG_CONTROL.NTSC == 1 && SPG_CONTROL.PAL == 1)) ? 1.0f : 0.5f);
    }

    spg_FrameSh4Cycles = spg_ScanlineCount * spg_LineSh4Cycles;

    // If vbstart is past the counter wrap (vcount+1), the vblank event never fires and the stats go silent while the
    // game runs on SPG_VBLANK_INT: log vcount, hcount, vbstart and vbend here to check.
}

s32 render_end_pending_cycles = 0;

// RENDER_DELAY() preset: delay list-complete IRQs by 200 cycles and render-done ISP/TSP/video by
// 800k/850k/900k after STARTRENDER (originaldave_'s holly.cpp); fixes games that pace off them (MvC2).

static const HollyInterruptID s_ListEndIrqID[5] =
{
    holly_OPAQUE,
    holly_OPAQUEMOD,
    holly_TRANS,
    holly_TRANSMOD,
    holly_PUNCHTHRU
};

#define LIST_END_IRQ_DELAY 200

static s32 s_list_irq_cycles[5] = { 0, 0, 0, 0, 0 };
static u32 s_list_irq_active = 0;   // bitmask of lists with a pending IRQ

// Staggered render-done counters, armed by StartRender() (ps3_rend.cpp) when the
// preset is on; render_end_pending_cycles stays 0 in that mode.
s32 render_isp_pending_cycles = 0;
s32 render_tsp_pending_cycles = 0;
s32 render_vd_pending_cycles  = 0;

// Called from the TA (ta.h) instead of raising the list-complete IRQ directly.
void spg_SchedListEndIrq(u32 list)
{
    if (list >= 5)
        return;     // corrupt/unknown ListType in the EOL parameter: no IRQ

    if (!RENDER_DELAY())
    {
        params.RaiseInterrupt(s_ListEndIrqID[list]);
        return;
    }

    sys_sync();
    s_list_irq_cycles[list] = LIST_END_IRQ_DELAY;
    s_list_irq_active |= 1u << list;
    sys_kick();
}

// Frame pacer, once per emulated vblank: early -> sleep the surplus (speed limiter); late -> publish
// the cumulative, clamped lateness in spg_PaceErrorSec for frameskip AUTO (absolute schedule, no drift).

#define PACE_MAX_LATE_FRAMES  4.0   // catch-up burst ceiling after a stall/load
#define PACE_MAX_EARLY_FRAMES 1.0   // credit ceiling when running unthrottled

// Temporary bisect counters for the FMV-overspeed investigation: which branch
// spg_PaceFrame() takes, once per second in the existing stats line.
u32 g_pace_calls, g_pace_zero, g_pace_reset, g_pace_slept;

static void spg_PaceFrame()
{
    stall_frame();
    g_pace_calls++;
    if (spg_FrameSh4Cycles == 0)
    {
        g_pace_zero++;
        return;     // SPG not programmed yet: no meaningful frame period
    }

    // Same effective clock spg_FrameSh4Cycles was built from, so the underclock preset keeps the real
    // video period.
    const double period = (double)spg_FrameSh4Cycles / (double)SH4_CLOCK_EFF;

    // First vblank or a video-mode change: start a fresh schedule and report "on time".
    if (s_pace_deadline == 0.0 ||
        period < spg_PaceFrameSec * 0.99 || period > spg_PaceFrameSec * 1.01)
    {
        g_pace_reset++;
        spg_PaceFrameSec = period;
        spg_PaceErrorSec = 0.0;
        s_pace_deadline  = os_GetSeconds() + period;
        return;
    }

    double now = os_GetSeconds();
    double err = now - s_pace_deadline;     // + = behind real time

    // EARLY arm: cap at 100% speed by sleeping only the surplus (the SH4 thread never waits for vsync).
    if (err < 0.0 && SPEED_LIMITER())
    {
        g_pace_slept++;
        stall_push(ST_SLEEP);
        usleep((useconds_t)(-err * 1000000.0));
        stall_pop();
        now = os_GetSeconds();
        err = now - s_pace_deadline;
    }

    // Anti-windup: late side bounds the catch-up burst after a stall, early side bounds the credit a fast
    // stretch can bank with the limiter off.
    const double max_late  =  PACE_MAX_LATE_FRAMES  * period;
    const double max_early = -PACE_MAX_EARLY_FRAMES * period;

    if (err > max_late || err < max_early)
    {
        err = (err > max_late) ? max_late : max_early;
        s_pace_deadline = now - err;   // re-anchor so the next vblank measures
                                       // from the clamp, not from the backlog
    }

    spg_PaceErrorSec = err;
    s_pace_deadline += period;         // next vblank's ideal time
}

// Plan 5.41 Frame Hold: os_GetSeconds() time the next emulated vblank is due at, 0 while the pacer sleeps for none.
double spg_NextVblankTime()
{
    return (SPEED_LIMITER() && spg_PaceFrameSec > 0.0) ? s_pace_deadline : 0.0;
}

// Called from SH4 context each dispatch; updates PVR/TA state
void FASTCALL libPvr_UpdatePvr(u32 cycles)
{
    spg_ScanlineSh4CycleCounter -= (s32)cycles;

    if (spg_ScanlineSh4CycleCounter < spg_LineSh4Cycles)
    {
        // A full scanline has elapsed
        spg_CurrentScanline = (spg_CurrentScanline + 1) % spg_ScanlineCount;
        spg_ScanlineSh4CycleCounter += spg_LineSh4Cycles;

        // Host-side exit combo (EXIT FIX = 3), rate limited in scanlines.
        if (EXIT_HOST_POLL())
        {
            static u32 s_exit_poll_lines = 0;
            if (++s_exit_poll_lines >= EXIT_HOST_POLL_LINES)
            {
                s_exit_poll_lines = 0;
                ExitCombo_HostPoll();
            }
        }

        // Scanline-triggered interrupts
        if (SPG_VBLANK_INT.vblank_in_interrupt_line_number == spg_CurrentScanline)
            params.RaiseInterrupt(holly_SCANINT1);

        if (SPG_VBLANK_INT.vblank_out_interrupt_line_number == spg_CurrentScanline)
            params.RaiseInterrupt(holly_SCANINT2);

        // Vblank window
        if (SPG_VBLANK.vbstart == spg_CurrentScanline)
            spg_InVblank = 1;

        if (SPG_VBLANK.vbend == spg_CurrentScanline)
            spg_InVblank = 0;

        // Interlaced field toggle
        SPG_STATUS.fieldnum = SPG_CONTROL.interlace ? (~SPG_STATUS.fieldnum & 1) : 0;

        SPG_STATUS.vsync   = spg_InVblank;
        SPG_STATUS.scanline = spg_CurrentScanline;

        // Start of vblank
        if (SPG_VBLANK.vbstart == spg_CurrentScanline)
        {
            spg_VblankCount++;
            spg_VblankCountTotal++;

            // Note: holly_HBLank is actually VBlank on real HW — HBlank not yet emulated
            params.RaiseInterrupt(holly_HBLank);

            rend_vblank();

            // Update FPS display every 1 second
            double now = os_GetSeconds();
            double tdiff = now - spg_last_vps;

            if (tdiff > 1.0)
            {
                stall_push(ST_LOG);
                spg_last_vps = now;
                // 5.14: the sound counters and dumps below belong to the sound thread while it runs.
                snd_lock(SND_LOCK_STATS);

                double spd_fps = FrameCount    / tdiff;
                double spd_vbs = spg_VblankCount / tdiff;
                double spd_cpu = (spd_vbs * spg_FrameSh4Cycles) / 1000000.0;
                // Against the effective clock, so an underclocked run still reads 100% at full rate.
                double clk_mhz = SH4_CLOCK_EFF / 1000000.0;
                double fullvbs = (spd_vbs / spd_cpu) * clk_mhz;
                double mv      = VertexCount  / 1000.0;
                // Vertices per strip: decides whether the render loop's fixed
                // per-STRIP cost or its per-VERTEX cost is the thing to attack.
                double vps     = StripCount ? (double)VertexCount / StripCount : 0.0;

                // Wall-time share inside DoRender() and the TA decoder; ta_kps keeps the per-call TA cost visible.
                double rnd_pct = PERF_TICKS_US(RenderTicks) / 1e6 / tdiff * 100.0;
                double ta_pct  = PERF_TICKS_US(TaTicks)     / 1e6 / tdiff * 100.0;
                double ta_kps  = TaCalls / tdiff / 1000.0;

                // Sound split: ARM7 vs AICA synthesis vs pacing sleep (taken out of aica%, shown as snd%).
                u64    aica_net = AicaTicks > SndWaitTicks ? AicaTicks - SndWaitTicks : 0;
                double arm_pct  = PERF_TICKS_US(ArmTicks)     / 1e6 / tdiff * 100.0;
                double aica_pct = PERF_TICKS_US(aica_net)     / 1e6 / tdiff * 100.0;
                double snd_pct  = PERF_TICKS_US(SndWaitTicks) / 1e6 / tdiff * 100.0;

                // Part of aica%: the per-voice channel loop, and the average
                // number of voices it stepped per sample (aica_fast only).
                double vox_pct  = PERF_TICKS_US(AicaVoxTicks) / 1e6 / tdiff * 100.0;
                double voices   = AicaSampleCount ? (double)AicaVoiceSum / AicaSampleCount : 0.0;

                VertexCount     = 0;
                StripCount      = 0;
                RenderTicks     = 0;
                TaTicks         = 0;
                TaCalls         = 0;
                ArmTicks        = 0;
                AicaTicks       = 0;
                SndWaitTicks    = 0;
                AicaVoxTicks    = 0;
                AicaVoiceSum    = 0;
                AicaSampleCount = 0;
                FrameCount      = 0;
                spg_VblankCount = 0;

                // Determine video mode strings
                const char* mode;
                const char* res;

                if (SPG_CONTROL.NTSC == 0 && SPG_CONTROL.PAL == 1)
                {
                    mode = "PAL";
                    res  = SPG_CONTROL.interlace ? "480i" : "240p";
                }
                else if (SPG_CONTROL.NTSC == 1 && SPG_CONTROL.PAL == 0)
                {
                    mode = "NTSC";
                    res  = SPG_CONTROL.interlace ? "480i" : "240p";
                }
                else
                {
                    mode = "VGA";
                    res  = SPG_CONTROL.interlace ? "480i" : "480p";
                }

                char fpsStr[256];

                sprintf(
                    fpsStr,
                    "FPS: %.2f\nSPEED: %.2f%%",
                    spd_fps,
                    spd_cpu * 100.0 / clk_mhz);

                rend_set_fps_text(fpsStr);

#ifndef TARGET_PSP
                printf(
                    "%3.2f%% VPS:%3.2f(%s%s%3.2f)RPS:%3.2f vt:%4.2fK %4.2fK v/st:%.1f rnd:%.1f%% ta:%.1f%%(%.0fk/s) arm:%.1f%% aica:%.1f%% vox:%.1f%%(%.1f) snd:%.1f%%\n",
                    spd_cpu * 100.0 / clk_mhz, spd_vbs,
                    mode, res, fullvbs,
                    spd_fps,
                    (spd_fps > 0.0 ? mv / spd_fps / tdiff : 0.0),
                    mv / tdiff,
                    vps, rnd_pct, ta_pct, ta_kps,
                    arm_pct, aica_pct, vox_pct, voices, snd_pct);
                // Temporary: which branch spg_PaceFrame() took this window (FMV-overspeed bisect).
                printf("[pace] calls=%u zero=%u reset=%u slept=%u\n",
                       g_pace_calls, g_pace_zero, g_pace_reset, g_pace_slept);
                g_pace_calls = g_pace_zero = g_pace_reset = g_pace_slept = 0;
                fflush(stdout); // once per 1s: keep the log tail intact if the console is powered off

                ifb_probe_dump(tdiff);   // no-op unless the IFB PROBE preset is on
                hotblocks_dump(tdiff, spd_fps > 0.0 ? mv / spd_fps / tdiff : 0.0);   // no-op unless JIT HOTBLOCKS is on
                ccall_census_dump(tdiff, spd_vbs); // no-op unless JIT CCALLS is on
                stall_dump(tdiff);                 // plan 5.32, the same switch
                arm_jit_census_dump(tdiff);        // same, and only while the ARM7 JIT runs
                aica_arm7s_dump(tdiff);
                aica_fix_dump(tdiff);
                ta_split_dump(tdiff);
                gda_stats(tdiff);
                snd_thread_stats();
                arm_spe_stats(tdiff);
                sys_gate_stats(tdiff);
#endif
                snd_unlock();
                stall_pop();
            }

            // Frame pacing: speed limiter plus the lateness signal (spg_PaceFrame).
            spg_PaceFrame();
        }
    }

    // Deferred render completion interrupt (legacy path, RENDER_DELAY() off)
    if (render_end_pending_cycles > 0)
    {
        render_end_pending_cycles -= (s32)cycles;
        if (render_end_pending_cycles <= 0)
        {
            params.RaiseInterrupt(holly_RENDER_DONE);
            params.RaiseInterrupt(holly_RENDER_DONE_isp);
            params.RaiseInterrupt(holly_RENDER_DONE_vd);
            rend_end_render();
        }
    }

    // RENDER_DELAY() staggered render-done: ISP, then TSP, then Video.
    // rend_end_render() runs with the last (Video) IRQ = true end of render.
    if (render_isp_pending_cycles > 0)
    {
        render_isp_pending_cycles -= (s32)cycles;
        if (render_isp_pending_cycles <= 0)
            params.RaiseInterrupt(holly_RENDER_DONE_isp);
    }

    if (render_tsp_pending_cycles > 0)
    {
        render_tsp_pending_cycles -= (s32)cycles;
        if (render_tsp_pending_cycles <= 0)
            params.RaiseInterrupt(holly_RENDER_DONE);
    }

    if (render_vd_pending_cycles > 0)
    {
        render_vd_pending_cycles -= (s32)cycles;
        if (render_vd_pending_cycles <= 0)
        {
            params.RaiseInterrupt(holly_RENDER_DONE_vd);
            rend_end_render();
        }
    }

    // RENDER_DELAY() deferred list-complete IRQs
    if (s_list_irq_active)
    {
        for (u32 i = 0; i < 5; i++)
        {
            if (!(s_list_irq_active & (1u << i)))
                continue;

            s_list_irq_cycles[i] -= (s32)cycles;
            if (s_list_irq_cycles[i] <= 0)
            {
                s_list_irq_active &= ~(1u << i);
                params.RaiseInterrupt(s_ListEndIrqID[i]);
            }
        }
    }
}

// Plan 5.36: lines with an effect beyond SPG_STATUS (interrupts, the vblank edges and the work at vbstart).
static inline bool spg_event_line(u32 line)
{
    return line == SPG_VBLANK_INT.vblank_in_interrupt_line_number ||
           line == SPG_VBLANK_INT.vblank_out_interrupt_line_number ||
           line == SPG_VBLANK.vbstart || line == SPG_VBLANK.vbend;
}

static inline s32 spg_slices_to_count(s32 cycles, s32 ts, s32 max)
{
    if (cycles <= 0)
        return max;
    const s32 n = (cycles + ts - 1) / ts;
    return n < max ? n : max;
}

// Plan 5.36: slice ends up to the next one where libPvr_UpdatePvr does more than move SPG_STATUS, at most max.
s32 spg_slices_to_event(s32 ts, s32 max)
{
    max = spg_slices_to_count(render_end_pending_cycles, ts, max);
    max = spg_slices_to_count(render_isp_pending_cycles, ts, max);
    max = spg_slices_to_count(render_tsp_pending_cycles, ts, max);
    max = spg_slices_to_count(render_vd_pending_cycles, ts, max);
    for (u32 i = 0; i < 5; i++)
        if (s_list_irq_active & (1u << i))
            max = s_list_irq_cycles[i] <= 0 ? 1 : spg_slices_to_count(s_list_irq_cycles[i], ts, max);

    const bool every = EXIT_HOST_POLL();
    s32 c = spg_ScanlineSh4CycleCounter;
    u32 line = spg_CurrentScanline;
    for (s32 n = 1; n < max; n++)
    {
        c -= ts;
        if (c < spg_LineSh4Cycles)
        {
            line = (line + 1) % spg_ScanlineCount;
            c += spg_LineSh4Cycles;
            if (every || spg_event_line(line))
                return n;
        }
    }
    return max;
}

// Plan 5.42: the line state that n event-free slices leave, from spg_slices_quiet for spg_slices_run.
static s32 s_quiet_ctr;
static u32 s_quiet_line, s_quiet_lines;

static inline bool spg_count_quiet(s32 cycles, s32 run)
{
    return cycles <= 0 || cycles > run;
}

// Plan 5.42: true when the next n slices end no countdown and reach no event line. Changes nothing the update reads.
bool spg_slices_quiet(s32 ts, s32 n)
{
    const s32 run = ts * n;
    if (s_list_irq_active || !spg_count_quiet(render_end_pending_cycles, run) ||
        !spg_count_quiet(render_isp_pending_cycles, run) || !spg_count_quiet(render_tsp_pending_cycles, run) ||
        !spg_count_quiet(render_vd_pending_cycles, run))
        return false;

    s32 c = spg_ScanlineSh4CycleCounter;
    u32 line = spg_CurrentScanline, lines = 0;
    if (c - run >= spg_LineSh4Cycles)
        c -= run;   // no line ends in the run
    else
    {
        const bool every = EXIT_HOST_POLL();
        for (s32 i = 0; i < n; i++)
        {
            c -= ts;
            if (c < spg_LineSh4Cycles)
            {
                line = (line + 1) % spg_ScanlineCount;
                c += spg_LineSh4Cycles;
                if (every || spg_event_line(line))
                    return false;
                lines++;
            }
        }
    }
    s_quiet_ctr = c;
    s_quiet_line = line;
    s_quiet_lines = lines;
    return true;
}

// Plan 5.42: those n slices as one step, right after a true spg_slices_quiet(ts, n).
void spg_slices_run(s32 ts, s32 n)
{
    const s32 run = ts * n;
    spg_ScanlineSh4CycleCounter = s_quiet_ctr;
    if (s_quiet_lines)
    {
        spg_CurrentScanline = s_quiet_line;
        // One word store: the field flips once per line, as in libPvr_UpdatePvr.
        SPG_STATUS_type st;
        st.full = SPG_STATUS.full;
        st.fieldnum = SPG_CONTROL.interlace ? ((st.fieldnum ^ s_quiet_lines) & 1) : 0;
        st.vsync    = spg_InVblank;
        st.scanline = s_quiet_line;
        SPG_STATUS.full = st.full;
    }
    if (render_end_pending_cycles > 0)
        render_end_pending_cycles -= run;
    if (render_isp_pending_cycles > 0)
        render_isp_pending_cycles -= run;
    if (render_tsp_pending_cycles > 0)
        render_tsp_pending_cycles -= run;
    if (render_vd_pending_cycles > 0)
        render_vd_pending_cycles -= run;
}

// The skipped slices hold no event line (spg_slices_to_event), so they move only the line and the field.
u32 spg_read_status()
{
    s32 k = sys_pending();
    if (k <= 0)
        return SPG_STATUS.full;
    const s32 ts = sh4_GetTimeslice();
    s32 c = spg_ScanlineSh4CycleCounter;
    u32 line = spg_CurrentScanline;
    SPG_STATUS_type st;
    st.full = SPG_STATUS.full;
    while (k-- > 0)
    {
        c -= ts;
        if (c < spg_LineSh4Cycles)
        {
            line = (line + 1) % spg_ScanlineCount;
            c += spg_LineSh4Cycles;
            st.fieldnum = SPG_CONTROL.interlace ? (~st.fieldnum & 1) : 0;
            st.vsync    = spg_InVblank;
            st.scanline = line;
        }
    }
    return st.full;
}

bool spg_Init()
{
    return true;
}

void spg_Term()
{
}

void spg_Reset(bool Manual)
{
    // Drop any in-flight delayed IRQs — a reset mid-render must not fire
    // stale render-done / list-complete interrupts into the fresh state.
    render_end_pending_cycles = 0;
    render_isp_pending_cycles = 0;
    render_tsp_pending_cycles = 0;
    render_vd_pending_cycles  = 0;
    s_list_irq_active = 0;

    // Drop the pacing schedule: a deadline from before the reset would give the skipper a phantom debt.
    s_pace_deadline  = 0.0;
    spg_PaceErrorSec = 0.0;
    spg_PaceFrameSec = 0.0;

    CalculateSync();
    sys_kick();
}