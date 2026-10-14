#pragma once
// Row "SH4 MMU" (plan section 7, bundle 20, P38): the interpreter with the full SH4 MMU, sh4_mmu_int.cpp; row "SH4 MMU
// Engine" JIT: the same MMU on the JIT, sh4_mmu_jit.cpp.
#include "types.h"

extern bool g_disc_wince;       // dc.cpp: the disc's IP.BIN names Windows CE
extern bool g_fullmmu_active;   // this boot runs the full MMU (on the interpreter, or on the JIT with g_jit_mmu)
extern bool g_jit_mmu;          // this boot runs it on the JIT

// After each reset, before the CPU runs: the row and the disc pick the engine of this boot.
void sh4_fullmmu_select();
void Sh4_int_Run_Mmu();

// sh4_mmu_jit.cpp: ldtlb wrote a UTLB entry (a no-op without g_jit_mmu). The JIT's slow paths see the other writes
// that change a translation: PTEH, MMUCR and the TLB arrays.
void jmmu_utlb_changed(u32 entry);
// The JIT is the menu's engine, so it can run this boot; the choice of this boot.
bool jmmu_can_run();
void jmmu_select(bool on);

// sh4_mmu_int.cpp's side of the JIT form. The translations return 0 or the exception code (TEA and PTEH set); ent is the
// UTLB entry used, -1 for an untranslated address. jx_ifb and jx_take: 1 = the exception is taken.
u32 jx_data_xlat(u32 va, u32 size, u32 write, u32* pa, int* ent);
u32 jx_fetch(u32 va, u32* pa);
u32 jx_ifb(u32 op, u32 epc, u32 slot);
u32 jx_take(u32 epc, u32 code);
bool jx_fpu_op(u32 op);
void jx_stats_print();
