// ARM7 has no JIT on PS3. The SH4 backend lives in ps3/ps3_driver.cpp and
// provides ngen_CC_* and the ifb/hotblocks probes itself.
#include "types.h"
#include "dc/sh4/rec_v2/ngen.h"

void arm_jit_after_init() {}
void arm_jit_on_reset() {}
bool arm_jit_active() { return false; }
void arm_jit_run(u32 CycleCount) {}
void arm_jit_census_dump(double seconds) {}


