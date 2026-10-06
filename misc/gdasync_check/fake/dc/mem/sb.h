#pragma once
// PC stand-in: the GD-DMA registers (harness.cpp).
#include "types.h"
#define SB_BASE 0x005F6800
#define SB_GDEN_addr 0x005F7414
#define SB_GDST_addr 0x005F7418
#define REG_32BIT_READWRITE 4
#define REG_READ_DATA 8
struct RegisterStruct { u32 flags; void (*writeFunction)(u32); };
extern RegisterStruct sb_regs[0x800];
extern u32 SB_GDSTAR, SB_GDLEN, SB_GDDIR, SB_GDEN, SB_GDST, SB_GDSTARD, SB_GDLEND;
