#pragma once
#include "types.h"

// G21 log (Plasma Sword): last BIOS calls and GD-ROM events, printed at SB_SFRES, the first Boot ROM write or bad opcode.
// Weak: the PC checks build gdromv3.cpp and sb.cpp without ps3_sfres.cpp.
void sfres_gd(u32 kind, u32 a, u32 b, u32 c, u32 d) __attribute__((weak));
void sfres_dump(u32 why) __attribute__((weak));
bool sfres_bios_entry(u32 pc);
void sfres_bios_call(u32 pc, u32 r4, u32 r5, u32 r6, u32 r7, u32 r15);
