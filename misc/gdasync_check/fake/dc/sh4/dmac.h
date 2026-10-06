#pragma once
// PC stand-in: DMAOR, read by UpdateGDRom.
#include "types.h"
union DMAC_DMAOR_type { u32 full; };
extern DMAC_DMAOR_type DMAC_DMAOR;
#define DMAOR_MASK 0xFFFF8201u
