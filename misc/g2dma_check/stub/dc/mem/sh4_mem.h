#pragma once
#include "types.h"
u32 ReadMem32_nommu(u32 addr);
void WriteMem32_nommu(u32 addr, u32 data);
