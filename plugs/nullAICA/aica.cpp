#include "aica.h"
#include "sgc_if.h"
#include "aica_mem.h"
#include "wii/wii_audio.h"
#include "ps3/ps3_aica_spe.h"
#include <math.h>

// config.h poisons BIG_ENDIAN/LITTLE_ENDIAN; libogc's machine/endian.h defines them, so undef first (as wii_audio.cpp).
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif
#include <ogc/lwp_watchdog.h>   // gettime(), ticks_to_millisecs()

#define SH4_IRQ_BIT (1<<(u8)holly_SPU_IRQ)
#define AICA_RATE_DIAG 0   // 1 = count real libAICA_TimeStep() calls/sec (see there)

CommonData_struct* CommonData;
DSPData_struct*    DSPData;
InterruptInfo*     MCIEB;
InterruptInfo*     MCIPD;
InterruptInfo*     MCIRE;
InterruptInfo*     SCIEB;
InterruptInfo*     SCIPD;
InterruptInfo*     SCIRE;

// ---- Interrupts ----

u32 GetL(u32 witch)
{
    if (witch > 7)
        witch = 7;

    u32 bit = 1 << witch;
    u32 rv  = 0;

    if (CommonData->SCILV0 & bit) rv  = 1;
    if (CommonData->SCILV1 & bit) rv |= 2;
    if (CommonData->SCILV2 & bit) rv |= 4;

    return rv;
}

// vbaARM/arm_mem.cpp: raises the ARM7 FIQ that drivers wait on after SCIEB + timers; without it the ARM7 never
// reaches KeyOn. C++ linkage on purpose: an extern "C" declaration breaks the link.
void FASTCALL ArmInterruptChange(u32 bits, u32 L);

extern int g_aica_fast_preset;   // wii/main.cpp

void update_arm_interrupts()
{
    u32 p_ints = SCIEB->full & SCIPD->full;

    u32 Lval = 0;
    if (p_ints && g_aica_fast_preset)
    {
        // Runs every sample. Lowest pending source among bits 0..10, same as
        // the loop below; SAMPLE_DONE is bit 10, so the loop often ran all 11.
        const u32 low = p_ints & 0x7FF;
        if (low)
            Lval = GetL(__builtin_ctz(low));
    }
    else if (p_ints)
    {
        u32 bit_value = 1;
        for (u32 i = 0; i < 11; i++)
        {
            if (p_ints & bit_value)
            {
                Lval = GetL(i);
                break;
            }
            bit_value <<= 1;
        }
    }

    ArmInterruptChange(p_ints, Lval);
}

void UpdateSh4Ints()
{
    u32 p_ints = MCIEB->full & MCIPD->full;
    if (p_ints)
    {
        if ((*aica_params.SB_ISTEXT & SH4_IRQ_BIT) == 0)
            aica_params.RaiseInterrupt(holly_SPU_IRQ);
    }
    else
    {
        if (*aica_params.SB_ISTEXT & SH4_IRQ_BIT)
            aica_params.CancelInterrupt(holly_SPU_IRQ);
    }
}

// ---- Timers ----

static void timer_overflow_diag(u32 id);

struct AicaTimerData
{
    union
    {
        struct
        {
#ifdef WII
            u32 pad:16;
            u32 nil:5;
            u32 md:3;
            u32 count:8;
#else
            u32 count:8;
            u32 md:3;
            u32 nil:5;
            u32 pad:16;
#endif
        };
        u32 data;
    };
};

class AicaTimer
{
public:
    AicaTimerData* data;
    s32  c_step;
    u32  m_step;
    u32  id;

    void Init(u8* regbase, u32 timer)
    {
        data   = (AicaTimerData*)&regbase[0x2890 + timer * 4];
        id     = timer;
        m_step = 1 << (data->md);
        c_step = m_step;
    }

    void StepTimer()
    {
        c_step--;
        if (c_step == 0)
        {
            c_step = m_step;
            data->count++;
            if (data->count == 0)
            {
                timer_overflow_diag(id);
                if      (id == 0) { SCIPD->TimerA = 1; MCIPD->TimerA = 1; }
                else if (id == 1) { SCIPD->TimerB = 1; MCIPD->TimerB = 1; }
                else              { SCIPD->TimerC = 1; MCIPD->TimerC = 1; }
            }
        }
    }

    void RegisterWrite()
    {
        u32 n_step = 1 << (data->md);
        if (n_step != m_step)
        {
            m_step = n_step;
            c_step = m_step;
        }
    }
};

AicaTimer timers[3];

// ---- [arm7s] (OPTIMIZATION_PLAN 6.1): once per wall second, counts per emulated second ----

extern u32  g_arm7s_runs, g_arm7s_fiqs;   // vbaARM
extern u64  g_arm7s_ticks;
u32 g_arm7s_mon_rd[2], g_arm7s_mslc_wr[2];   // [0] SH4, [1] ARM7

u64 g_aica_smp;   // AICA samples since boot
AicaRw g_aicarw[2];

// The last AICA internal DMA start (0x288C DEXE), which neither nullAICA nor caique carries out (Flycast does).
static struct { u32 n, gate, dir, dlg, dmea, drga; } s_dexe;

struct TimerDiag
{
    u32  tim, md, ovf, merge, lat_n, lat_max;
    u64  lat_sum, ovf_at;
    bool wait;   // overflowed, TIMx not written since
};
static TimerDiag s_td[3];

// Called before the SCIPD bit is set, so a merge sees the bit of the previous overflow still pending.
static void timer_overflow_diag(u32 id)
{
    TimerDiag& t = s_td[id];
    t.ovf++;
    if (SCIPD->full & (0x40u << id))
        t.merge++;
    t.ovf_at = g_aica_smp;
    t.wait = true;
}

// Reload latency: samples from an overflow to the next TIMx write.
static void timer_write_diag(u32 id)
{
    TimerDiag& t = s_td[id];
    t.tim = timers[id].data->count;
    t.md  = timers[id].data->md;
    if (t.wait)
    {
        const u32 lat = (u32)(g_aica_smp - t.ovf_at);
        t.lat_sum += lat;
        t.lat_n++;
        if (lat > t.lat_max)
            t.lat_max = lat;
        t.wait = false;
    }
}

void aica_arm7s_dump(double wall_s)
{
    static u64 last_smp;
    const double es = (double)(g_aica_smp - last_smp) / 44100.0;
    last_smp = g_aica_smp;
    const double k = es > 0.0 ? 1.0 / es : 0.0;

    char buf[640];
    int n = snprintf(buf, sizeof(buf), "[arm7s] per emulated s (emu %.2f s / wall %.2f s): runs %.0f ticks %.0f fiq %.0f",
                     es, wall_s, g_arm7s_runs * k, (double)g_arm7s_ticks * k, g_arm7s_fiqs * k);
    for (int i = 0; i < 3 && n < (int)sizeof(buf); i++)
    {
        TimerDiag& t = s_td[i];
        n += snprintf(buf + n, sizeof(buf) - n, " | %c tim %02X md %u P %u ovf %.1f merge %.1f lat ", 'A' + i,
                      t.tim, t.md, (256 - t.tim) << t.md, t.ovf * k, t.merge * k);
        if (n < (int)sizeof(buf))
            n += t.lat_n ? snprintf(buf + n, sizeof(buf) - n, "%.1f/%u", (double)t.lat_sum / t.lat_n, t.lat_max)
                         : snprintf(buf + n, sizeof(buf) - n, "-");
        t.ovf = t.merge = t.lat_n = t.lat_max = 0;
        t.lat_sum = 0;
    }
    if (n < (int)sizeof(buf))
        snprintf(buf + n, sizeof(buf) - n,
                 " | SCIEB %03X SCIPD %03X SCILV %02X/%02X/%02X MCIEB %03X"
                 " | mon rd sh4 %.0f arm %.0f | mslc wr sh4 %.0f arm %.0f",
                 SCIEB->full & 0x7FF, SCIPD->full & 0x7FF, (u32)CommonData->SCILV0, (u32)CommonData->SCILV1,
                 (u32)CommonData->SCILV2, MCIEB->full & 0x7FF, g_arm7s_mon_rd[0] * k, g_arm7s_mon_rd[1] * k,
                 g_arm7s_mslc_wr[0] * k, g_arm7s_mslc_wr[1] * k);
    printf("%s\n", buf);

    g_arm7s_runs = g_arm7s_fiqs = 0;
    g_arm7s_ticks = 0;
    g_arm7s_mon_rd[0] = g_arm7s_mon_rd[1] = g_arm7s_mslc_wr[0] = g_arm7s_mslc_wr[1] = 0;

    // [aicarw]: what the sound driver touches, by CPU. Timer counts, SCIRE, SCILV0-2 and MCIRE read 0 on hardware
    // ("write-only rd"; caique NOTES "Timers and interrupts"); nullAICA returns stored or live values.
    const AicaRw& s = g_aicarw[0];
    const AicaRw& a = g_aicarw[1];
    n = snprintf(buf, sizeof(buf), "[aicarw] per emulated s: kyonex sh4 %.0f arm %.0f (key lines dropped %u) | dexe sh4 %.0f"
                 " arm %.0f | slot rd sh4 %.0f arm %.0f (reg 0x00 sh4 %.0f arm %.0f) | timer rd sh4 %.0f arm %.0f"
                 " | write-only rd sh4 %.0f arm %.0f | irq rd sh4 %.0f arm %.0f",
                 s.kyonex * k, a.kyonex * k, aica_key_log_reset(), s.dexe * k, a.dexe * k, s.slot_rd * k, a.slot_rd * k,
                 s.reg0_rd * k, a.reg0_rd * k, s.tim_rd * k, a.tim_rd * k, s.wo_rd * k, a.wo_rd * k, s.irq_rd * k,
                 a.irq_rd * k);
    if (s_dexe.n && n < (int)sizeof(buf))
        snprintf(buf + n, sizeof(buf) - n, " | dexe total %u, last: gate %u dir %u dlg %u dmea %06X drga %04X", s_dexe.n,
                 s_dexe.gate, s_dexe.dir, s_dexe.dlg, s_dexe.dmea, s_dexe.drga);
    printf("%s\n", buf);
    memset(g_aicarw, 0, sizeof(g_aicarw));

    static u32 calls;
    if (++calls % 5 == 0)
        aica_montrace_dump();
}

// ---- Main loop ----

void FASTCALL UpdateAICA(u32 Samples)
{
    while (Samples > 0)
    {
        Samples--;

        AICA_Sample();
        SCIPD->SAMPLE_DONE = 1;

        for (int i = 0; i < 3; i++)
            timers[i].StepTimer();
    }

    update_arm_interrupts();
    UpdateSh4Ints();
}

// ---- Register writes ----

template<u32 sz>
void WriteAicaReg(u32 reg, u32 data)
{
    switch (reg)
    {
    case SCIPD_addr:
        verify(sz != 1);
        if (data & (1 << 5))
        {
            SCIPD->SCPU = 1;
            update_arm_interrupts();
        }
        return; // read-only

    case SCIRE_addr:
        verify(sz != 1);
        SCIPD->full &= ~data;
        data = 0;
        update_arm_interrupts();
        break;

    case MCIPD_addr:
        if (data & (1 << 5))
        {
            verify(sz != 1);
            MCIPD->SCPU = 1;
            UpdateSh4Ints();
        }
        return; // read-only

    case MCIRE_addr:
        verify(sz != 1);
        MCIPD->full &= ~data;
        UpdateSh4Ints();
        break;

    case TIMER_A:
        WriteMemArr(aica_reg, reg, data, sz);
        timers[0].RegisterWrite();
        timer_write_diag(0);
        break;

    case TIMER_B:
        WriteMemArr(aica_reg, reg, data, sz);
        timers[1].RegisterWrite();
        timer_write_diag(1);
        break;

    case TIMER_C:
        WriteMemArr(aica_reg, reg, data, sz);
        timers[2].RegisterWrite();
        timer_write_diag(2);
        break;

    default:
        WriteMemArr(aica_reg, reg, data, sz);
        if (reg == 0x288C && (data & 1))   // DEXE: counted only, the transfer is not carried out
        {
            g_aicarw[g_arm7_in_run].dexe++;
            s_dexe.n++;
            s_dexe.gate = CommonData->DGATE;
            s_dexe.dir  = CommonData->DDIR;
            s_dexe.dlg  = CommonData->DLG;
            s_dexe.dmea = (CommonData->DMEA_hi << 16) | (CommonData->DMEA_lo << 2);
            s_dexe.drga = CommonData->DRGA << 2;
        }
        break;
    }
}

template void WriteAicaReg<1>(u32 reg, u32 data);
template void WriteAicaReg<2>(u32 reg, u32 data);

// ---- Init / Term ----

void AICA_Init()
{
    verify(sizeof(*CommonData) == 0x508);
    verify(sizeof(*DSPData)    == 0x15C8);
    //printf("[AICA] AICA_Init: struct sizes OK\n");

    //printf("[AICA] AICA_Init: aica_reg ptr = %p\n", (void*)aica_reg);
    CommonData = (CommonData_struct*)&aica_reg[0x2800];
    DSPData    = (DSPData_struct*)   &aica_reg[0x3000];
    SCIEB = (InterruptInfo*)&aica_reg[0x289C];
    SCIPD = (InterruptInfo*)&aica_reg[0x289C + 4];
    SCIRE = (InterruptInfo*)&aica_reg[0x289C + 8];
    MCIEB = (InterruptInfo*)&aica_reg[0x28B4];
    MCIPD = (InterruptInfo*)&aica_reg[0x28B4 + 4];
    MCIRE = (InterruptInfo*)&aica_reg[0x28B4 + 8];
    //printf("[AICA] AICA_Init: pointers set\n");

    //printf("[AICA] AICA_Init: sgc_Init()...\n");
    sgc_Init();
    //printf("[AICA] AICA_Init: sgc_Init() done\n");

    //printf("[AICA] AICA_Init: timers init...\n");
    for (int i = 0; i < 3; i++)
        timers[i].Init(aica_reg, i);
    //printf("[AICA] AICA_Init: timers done\n");

    //printf("[AICA] AICA_Init: wii_audio_aica_ready()...\n");
    wii_audio_aica_ready();
    //printf("[AICA] AICA_Init: done\n");
}

void AICA_Term()
{
    sgc_Term();
}

// One AICA sample: timers, SAMPLE_DONE, ARM7 / SH4 interrupt lines. Driven per sample from armUpdateARM
// (SH4 timeslice), see [[aica-arm-timing]].
void libAICA_TimeStep()
{
    g_aica_smp++;
    AICA_Sample();
    SCIPD->SAMPLE_DONE = 1;

    for (int i = 0; i < 3; i++)
        timers[i].StepTimer();

    update_arm_interrupts();
    UpdateSh4Ints();

    if (g_aica_spe_on)
        ps3aica_step();

#if AICA_RATE_DIAG
    // Off by default (a gettime() and a 64-bit divide per sample): real TimeStep calls per wall second.
    // ~4400/s instead of 44100 would mean the AICA runs at 1/10 rate (audio low and stretched).
    static u32 sample_count = 0;
    static u64 last_check_ms = 0;
    sample_count++;
    u64 now_ms = ticks_to_millisecs(gettime());
    if (now_ms - last_check_ms >= 1000)
    {
        //printf("[AICA] real-time sample rate: %u samples/%llums (target 44100/1000ms)\n",
        //       sample_count, (unsigned long long)(now_ms - last_check_ms));
        (void)sample_count;
        sample_count = 0;
        last_check_ms = now_ms;
    }
#endif
}
