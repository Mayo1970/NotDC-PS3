#pragma once
// Row "SH4 MMU" (plan section 7, bundle 20, P38): the interpreter with the full SH4 MMU, sh4_mmu_int.cpp.
#include "types.h"

extern bool g_disc_wince;       // dc.cpp: the disc's IP.BIN names Windows CE
extern bool g_fullmmu_active;   // this boot runs Sh4_int_Run_Mmu

// After each reset, before the CPU runs: the row and the disc pick the engine of this boot.
void sh4_fullmmu_select();
void Sh4_int_Run_Mmu();
