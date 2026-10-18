#pragma once
// modem_check: Flycast's interrupt calls, for the reference build.
enum HollyInterruptID { holly_EXP_8BIT = 0x102 };
void asic_RaiseInterrupt(HollyInterruptID inter);
void asic_CancelInterrupt(HollyInterruptID inter);
