#pragma once
#include "types.h"
enum HollyInterruptID { holly_SPU_DMA = 15, holly_EXT_DMA1 = 16, holly_EXT_DMA2 = 17 };
void asic_RaiseInterrupt(HollyInterruptID inter);
