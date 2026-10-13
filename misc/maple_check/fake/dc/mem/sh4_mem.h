#pragma once
#include "types.h"
// maple_check: the model RAM of maple_check.cpp.
u32 ReadMem32_nommu(u32 addr);
u8* GetMemPtr(u32 addr, u32 size);
