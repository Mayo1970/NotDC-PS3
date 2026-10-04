// SH4 CPU interpreter: core operations (tuned for PowerPC).

#include "types.h"
#include "dc/pvr/pvr_if.h"
#include "sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"
#include "sh4_registers.h"
#include "ccn.h"
#include "intc.h"
#include "tmu.h"
#include "dc/gdrom/gdrom_if.h"

#include "sh4_cpu_prelude.h"

// GLOBAL STATE
bool sh4_sleeping;

// DEBUG/ERROR REPORTING
void cpu_iNimp(u32 op, const char* info)
{
	printf("Unimplemented opcode: 0x%04X : %s\n", op, info);
}

void cpu_iWarn(u32 op, const char* info)
{
	printf("Warning opcode: 0x%04X : %s @ PC=0x%08X\n", op, info, curr_pc);
}

// INCLUDE INSTRUCTION IMPLEMENTATIONS
#include "sh4_cpu_movs.h"
#include "sh4_cpu_branch.h"
#include "sh4_cpu_arith.h"
#include "sh4_cpu_logic.h"
#include "dc/mem/mmu.h"
#include "sh4_cpu_misc.h"

// The recompiler's PREF target with the MMU on; the one without is in ps3/ps3_rend.cpp (plan 5.17).
void FASTCALL do_sqw_mmu(u32 dst)   { do_sqw<true>(dst); }

