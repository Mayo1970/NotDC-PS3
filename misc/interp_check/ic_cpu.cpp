// interp_check: the CPU op bodies as dc/sh4/sh4_cpu.cpp builds them, on the harness's memory and stubs.
#include "types.h"
#include "dc/sh4/sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/intc.h"
#include "dc/mem/sh4_mem.h"
#include "dc/sh4/sh4_cpu_prelude.h"

bool sh4_sleeping;
void cpu_iNimp(u32 op, const char* info) { printf("Unimplemented opcode: 0x%04X : %s\n", op, info); }
void cpu_iWarn(u32 op, const char* info) { printf("Warning opcode: 0x%04X : %s\n", op, info); }

#include "dc/sh4/sh4_cpu_movs.h"
#include "dc/sh4/sh4_cpu_branch.h"
#include "dc/sh4/sh4_cpu_arith.h"
#include "dc/sh4/sh4_cpu_logic.h"
#include "dc/mem/mmu.h"
#include "dc/sh4/sh4_cpu_misc.h"
