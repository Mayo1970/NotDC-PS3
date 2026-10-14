#pragma once
// Row "SH4 MMU Engine" JIT (plan section 7, bundle 20, P38): what ps3_driver.cpp's MMU blocks call and read.
#include "types.h"

// JTC: one table per mode and access (index md * 2 + write), an entry per 1 KB virtual page that hit a direct host
// page; the JIT's inline lookup reads tag and base. A tag that is not a page (low bits set) never matches.
#define JTC_N 1024
struct JtcEntry { u32 tag, base; };
extern JtcEntry g_jtc[4][JTC_N];
// The exception code a slow path left for the block's exception exit; 0 = none.
extern volatile u32 g_jmmu_fault;

// MMUCR.AT as the MMU engine reads it (the manual's bit): the emitter compiles MMU accesses only with AT on.
bool jmmu_at();
// A block's key besides its pcs: SR.MD, FPSCR.PR and SZ, RM 1.
u32 jmmu_cfg();

// The slow paths of a JTC miss: the access through the TLB, a JTC fill, or g_jmmu_fault. Sizes 1 and 2 return the
// value zero-extended.
u32 jmmu_rd8(u32 va);
u32 jmmu_rd16(u32 va);
u32 jmmu_rd32(u32 va);
u64 jmmu_rd64(u32 va);
void jmmu_wr8(u32 va, u32 v);
void jmmu_wr16(u32 va, u32 v);
void jmmu_wr32(u32 va, u32 v);
void jmmu_wr64(u32 va, u64 v);
// An op on the MMU interpreter (jx_ifb), with the JTC checks after it: 1 = it raised an exception, now taken.
u32 jmmu_ifb(u32 op, u32 epc, u32 slot);

// The main loop's lookup at each block end: the code for next_pc, or 0 with g_jmmu_fault set.
void* jmmu_lookup(u32 pc);
// The pending exception at epc, after the main loop flushed the GPRs; returns the pc to run.
u32 jmmu_raise(u32 epc);

// The [jmmu] line, once a second (ps3_driver.cpp hotblocks_dump).
void jmmu_stats(double seconds);

// ps3_driver.cpp: the main loop's lookup goes to the MMU one (the first word of loop_no_update), or back.
void ngen_mmu_dispatch(bool on);
// driver.cpp: compile pc read at pa, as the block of key (pc, pa, cfg) with the page's generation; 0 = no block
// (with g_dec_fpu_disabled: SR.FD and an FPU op, so the FPU-disabled exception comes first).
typedef void DynarecCodeEntry();
DynarecCodeEntry* rdv_CompilePCMmu(u32 pc, u32 pa, u32 cfg, u32 pgen);
extern bool g_dec_fpu_disabled;
