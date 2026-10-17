// CCN: SH4 Cache and TLB Controller emulation
// Reference: SH7091 Hardware Manual, section 7 (CCN)

// TODO: complete ccn_Reset; the magic PC address is fragile; CCR.ORA and MMUCR.SQMD are ignored. Not worth it:
// a table for the CCN[idx] repetition, full TLB emulation (most games run with AT=0), a new bitfield layout.

#include "types.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/mem/mmu.h"
#include "plugins/plugin_manager.h"
#include "ccn.h"
#include "sh4_registers.h"
#include "sh4_fullmmu.h"

// ---- Register storage ----
CCN_PTEH_type  CCN_PTEH;
CCN_PTEL_type  CCN_PTEL;
u32            CCN_TTB;
u32            CCN_TEA;
CCN_MMUCR_type CCN_MMUCR;
u8             CCN_BASRA;
u8             CCN_BASRB;
CCN_CCR_type   CCN_CCR;
u32            CCN_TRA;
u32            CCN_EXPEVT;
u32            CCN_INTEVT;
CCN_PTEA_type  CCN_PTEA;
CCN_QACR_type  CCN_QACR0;
CCN_QACR_type  CCN_QACR1;

// mmu_check: begin ccn
// P58 probe: the first writes, with the manual's fields and the two bits the emulator acts on.
#define MMUCR_LOG_MAX 64
static u32 s_mmucr_writes;

// SH4 MMU (bundle 20): Windows CE flushes the TLB and the I-cache dozens of times a second; the first lines print.
static u32 s_mmu_quiet;
static bool ccn_log_ok() { return !g_fullmmu_active || s_mmu_quiet++ < 16; }

// ---- MMUCR write handler: logs the write and the MMU enable/disable edges, clears the self-clearing TI bit ----
static void CCN_MMUCR_write(u32 value)
{
    const bool fixed = g_mmu_bits_preset != 0;
    CCN_MMUCR_type temp;
    temp.reg_data = fixed ? value & MMUCR_WMASK : value;

    const u32 at = fixed ? MMUCR_AT(temp.reg_data) : temp.AT;
    const u32 ti = fixed ? MMUCR_TI(temp.reg_data) : temp.TI;
    const bool at_edge = at != mmucr_at();

    if (s_mmucr_writes++ < MMUCR_LOG_MAX)
        printf("CCN: MMUCR write %08X (was %08X): AT=%u TI=%u SV=%u SQMD=%u URC=%u URB=%u LRUI=%u, acts on AT=%u TI=%u"
               " (pc=%08X)\n", value, CCN_MMUCR.reg_data, MMUCR_AT(value), MMUCR_TI(value), MMUCR_SV(value),
               MMUCR_SQMD(value), MMUCR_URC(value), MMUCR_URB(value), MMUCR_LRUI(value), at, ti, curr_pc);

    if (at_edge && ccn_log_ok())
    {
        printf("CCN: MMU address translation %s (pc=%08X)\n",
               at ? "ENABLED - only SQ remaps supported" : "disabled",
               curr_pc);
    }

    // TI = self-clearing write-only TLB invalidate; mmu_InvalidateTLB says what it drops.
    if (ti)
    {
        if (ccn_log_ok())
            printf("CCN: TLB invalidate (pc=%08X)\n", curr_pc);
        mmu_InvalidateTLB();
        if (fixed)
            temp.reg_data &= ~MMUCR_TI_BIT;
        else
            temp.TI = 0;
    }

    CCN_MMUCR = temp;

    // P58 (Flycast CCN_MMUCR_write): the JIT takes the PREF target, JIT PREFETCH and JIT GUARD REUSE from AT when it
    // compiles, so the blocks of before an AT edge must go.
    if (fixed && at_edge)
        sh4_cpu.ResetCache();
}

// P58 self-test at boot: one word through the bitfield and through the masks. On big endian the bitfield must print
// AT=0 TI=0 URC=2, the masks AT=1 TI=1 URC=42.
static void ccn_mmucr_selftest()
{
    CCN_MMUCR_type t;
    t.reg_data = 0x0000A905;
    printf("[mmu] MMUCR Bits %s | self-test %08X: bitfield AT=%u TI=%u SV=%u URC=%u, masks AT=%u TI=%u SV=%u URC=%u\n",
           g_mmu_bits_preset ? "FIXED" : "OLD", t.reg_data, (u32)t.AT, (u32)t.TI, (u32)t.SV, (u32)t.URC,
           MMUCR_AT(t.reg_data), MMUCR_TI(t.reg_data), MMUCR_SV(t.reg_data), MMUCR_URC(t.reg_data));
}
// mmu_check: end ccn

// ---- CCR write handler: handles the self-clearing ICI/OCI invalidate bits, logs cache enable/disable changes ----
static void CCN_CCR_write(u32 value)
{
    CCN_CCR_type temp;
    temp.reg_data = value;

    // Instruction cache invalidate (ICI is self-clearing)
    // Magic address suppresses a noisy but harmless BIOS invalidation.
    if (temp.ICI)
    {
        if (curr_pc != 0xAC13DBF8 && ccn_log_ok())
            printf("CCN: I-cache invalidation requested (pc=%08X)\n", curr_pc);
        sh4_cpu.ResetCache();
        temp.ICI = 0;
    }
    // The I-cache turned on again: drop the JIT code, as ICI does. Shenmue II turns it off and on, with no ICI, before it
    // calls code it just wrote; the hardware's 8 KB cache has turned over by then, the JIT's blocks have not.
    else if (temp.ICE && !CCN_CCR.ICE)
        sh4_cpu.ResetCache();

    // Operand cache invalidate (OCI is self-clearing)
    // No OC emulation, but the bit must still be cleared per spec.
    if (temp.OCI)
    {
        temp.OCI = 0;
    }

    // Log cache enable state changes
    if (temp.ICE != CCN_CCR.ICE)
        printf("CCN: I-cache %s (pc=%08X)\n", temp.ICE ? "enabled" : "disabled", curr_pc);
    if (temp.OCE != CCN_CCR.OCE)
        printf("CCN: O-cache %s (pc=%08X)\n", temp.OCE ? "enabled" : "disabled", curr_pc);

    CCN_CCR = temp;
}

// ---- ccn_Init: wire all CCN registers into the SH4 register dispatch table ----
// Original direct indexing on purpose: macros broke the build, as the _addr tokens are struct members, not integers.
void ccn_Init()
{
    // CCN PTEH 0xFF000000
    CCN[(CCN_PTEH_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_PTEH_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_PTEH_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_PTEH_addr&0xFF)>>2].data32        = &CCN_PTEH.reg_data;

    // CCN PTEL 0xFF000004
    CCN[(CCN_PTEL_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_PTEL_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_PTEL_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_PTEL_addr&0xFF)>>2].data32        = &CCN_PTEL.reg_data;

    // CCN TTB 0xFF000008
    CCN[(CCN_TTB_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_TTB_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_TTB_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_TTB_addr&0xFF)>>2].data32        = &CCN_TTB;

    // CCN TEA 0xFF00000C
    CCN[(CCN_TEA_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_TEA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_TEA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_TEA_addr&0xFF)>>2].data32        = &CCN_TEA;

    // CCN MMUCR 0xFF000010 -- read via data32, writes go through handler
    CCN[(CCN_MMUCR_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    CCN[(CCN_MMUCR_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_MMUCR_addr&0xFF)>>2].writeFunction = CCN_MMUCR_write;
    CCN[(CCN_MMUCR_addr&0xFF)>>2].data32        = &CCN_MMUCR.reg_data;

    // CCN BASRA 0xFF000014 (8-bit)
    CCN[(CCN_BASRA_addr&0xFF)>>2].flags         = REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_BASRA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_BASRA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_BASRA_addr&0xFF)>>2].data8         = &CCN_BASRA;

    // CCN BASRB 0xFF000018 (8-bit)
    CCN[(CCN_BASRB_addr&0xFF)>>2].flags         = REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_BASRB_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_BASRB_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_BASRB_addr&0xFF)>>2].data8         = &CCN_BASRB;

    // CCN CCR 0xFF00001C -- read via data32, writes go through handler
    CCN[(CCN_CCR_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA;
    CCN[(CCN_CCR_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_CCR_addr&0xFF)>>2].writeFunction = CCN_CCR_write;
    CCN[(CCN_CCR_addr&0xFF)>>2].data32        = &CCN_CCR.reg_data;

    // CCN TRA 0xFF000020
    CCN[(CCN_TRA_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_TRA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_TRA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_TRA_addr&0xFF)>>2].data32        = &CCN_TRA;

    // CCN EXPEVT 0xFF000024
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_EXPEVT_addr&0xFF)>>2].data32        = &CCN_EXPEVT;

    // CCN INTEVT 0xFF000028
    CCN[(CCN_INTEVT_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_INTEVT_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_INTEVT_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_INTEVT_addr&0xFF)>>2].data32        = &CCN_INTEVT;

    // CCN PTEA 0xFF000034
    CCN[(CCN_PTEA_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_PTEA_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_PTEA_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_PTEA_addr&0xFF)>>2].data32        = &CCN_PTEA.reg_data;

    // CCN QACR0 0xFF000038
    CCN[(CCN_QACR0_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_QACR0_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_QACR0_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_QACR0_addr&0xFF)>>2].data32        = &CCN_QACR0.reg_data;

    // CCN QACR1 0xFF00003C
    CCN[(CCN_QACR1_addr&0xFF)>>2].flags         = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
    CCN[(CCN_QACR1_addr&0xFF)>>2].readFunction  = 0;
    CCN[(CCN_QACR1_addr&0xFF)>>2].writeFunction = 0;
    CCN[(CCN_QACR1_addr&0xFF)>>2].data32        = &CCN_QACR1.reg_data;

    ccn_mmucr_selftest();
}

// ---- ccn_Reset: restore power-on / reset values per the SH7091 hardware manual ----
// EXPEVT = 0x000 on power-on, 0x020 on manual reset (the original always wrote 0).
void ccn_Reset(bool Manual)
{
    CCN_TRA            = 0x00000000;
    CCN_EXPEVT         = Manual ? 0x00000020 : 0x00000000;
    CCN_MMUCR.reg_data = 0x00000000;
    s_mmucr_writes     = 0;
    CCN_CCR.reg_data   = 0x00000000;
}

void ccn_Term()
{
    // Nothing to release -- all storage is statically allocated
}