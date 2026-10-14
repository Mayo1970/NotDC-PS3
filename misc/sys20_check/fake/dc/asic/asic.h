#pragma once
// sys20_check: the ASIC calls of sb.cpp; the harness has empty ones.
#include "types.h"
void asic_reg_Init();
void asic_reg_Reset(bool Manual);
void asic_reg_Term();
