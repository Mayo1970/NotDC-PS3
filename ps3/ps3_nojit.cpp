// No JIT on PS3: stand-ins for the SH4/ARM7 JIT backends and their probes.
// rec_v2 decoder/shil still link because sh4_opcode_list uses their tables.
#include "types.h"
#include "dc/sh4/rec_v2/ngen.h"

void ngen_CC_Start(shil_opcode* op) {}
void ngen_CC_Param(shil_opcode* op, shil_param* par, CanonicalParamType tp) {}
void ngen_CC_Call(shil_opcode* op, void* function) {}
void ngen_CC_Finish(shil_opcode* op) {}

void arm_jit_after_init() {}
void arm_jit_on_reset() {}
bool arm_jit_active() { return false; }
void arm_jit_run(u32 CycleCount) {}
void arm_jit_census_dump(double seconds) {}

extern "C" void ifb_probe_dump(double seconds) {}
extern "C" void hotblocks_dump(double seconds) {}
