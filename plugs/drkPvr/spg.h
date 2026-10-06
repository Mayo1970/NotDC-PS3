#pragma once
#include "drkPvr.h"

bool spg_Init();
void spg_Term();
void spg_Reset(bool Manual);
void CalculateSync();
void FASTCALL libPvr_UpdatePvr(u32 cycles);

// Raise (or, with the render_delay preset, schedule +200 cycles) the
// list-complete IRQ for TA list 0..4. Called from ta.h at END-OF-LIST.
void spg_SchedListEndIrq(u32 list);

// Plan 5.36: SPG_STATUS as a guest read sees it, with the slice ends the SYS GATE has not run yet.
u32 spg_read_status();