#pragma once
#include "types.h"

// The DMAs that write guest RAM report it; the JIT drops the code it compiled from there (driver.cpp). An enum, not a
// name: a string per call site would take a TOC entry each, and the PS3 build has few left.
enum JitDma { JDMA_GDROM, JDMA_PVR, JDMA_G2, JDMA_G2_EXT1, JDMA_G2_EXT2, JDMA_MAPLE };
void rdv_DmaWrite(u32 addr, u32 size, JitDma who);
