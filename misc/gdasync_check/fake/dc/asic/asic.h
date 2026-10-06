#pragma once
// PC stand-in: harness.cpp logs the GD-ROM interrupts.
enum HollyInterruptID { holly_GDROM_CMD = 1, holly_GDROM_DMA = 2 };
void asic_RaiseInterrupt(HollyInterruptID id);
void asic_CancelInterrupt(HollyInterruptID id);
