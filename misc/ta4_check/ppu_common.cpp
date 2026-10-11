// Bundle 4 PC check: what ta.h calls outside the splitter, once for both PPU builds.
#include "drkPvr.h"
#include "spg.h"

int msgboxf(const char*, unsigned int, ...) { return 0; }
void spg_SchedListEndIrq(u32) {}
