// PC stand-in for dc/mem/sb.h: the G2 DMA registers and the register table.
#pragma once
#include "types.h"
#define SB_BASE 0x005F6800
#define SB_ADST_addr 0x005F7818
#define SB_E1ST_addr 0x005F7838
#define SB_E2ST_addr 0x005F7858
extern RegisterStruct sb_regs[0x540];
extern u32 SB_ISTNRM;
extern u32 SB_ADSTAG, SB_ADSTAR, SB_ADLEN, SB_ADDIR, SB_ADEN, SB_ADST, SB_ADSUSP;
extern u32 SB_E1STAG, SB_E1STAR, SB_E1LEN, SB_E1DIR, SB_E1EN, SB_E1ST;
extern u32 SB_E2STAG, SB_E2STAR, SB_E2LEN, SB_E2DIR, SB_E2EN, SB_E2ST;
