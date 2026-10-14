#pragma once

#include "types.h"

// SCI, the SH4's serial port without FIFO; the Dreamcast does not use it. Its registers exist only with the row
// On-chip Regs FLYCAST (sci.cpp).

void sci_Init();
void sci_Reset(bool Manual);
void sci_Term();
