#pragma once
#include "types.h"
// maple_check: the maple registers as plain variables, and the two entries that maple_Init() sets.
extern u32 SB_MDSTAR, SB_MDTSEL, SB_MDEN, SB_MDST, SB_MSYS, SB_MSHTCL, SB_MDAPRO, SB_MMSEL;
struct RegisterStruct { u32 flags; void (*writeFunction)(u32); };
extern RegisterStruct sb_regs[4];
enum { REG_32BIT_READWRITE = 4, REG_READ_DATA = 8 };
#define SB_BASE 0
#define SB_MDST_addr 4
#define SB_MSHTCL_addr 8
