#pragma once
// PC stand-in: the GD-DMA writes main RAM through this (harness.cpp).
#include "types.h"
void WriteMem32_nommu(u32 addr, u32 data);
