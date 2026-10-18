#pragma once
// modem_check: Flycast stdclass.h; modem.cpp calls it only without NDEBUG.
#include "types.h"
static inline u64 getTimeMs() { return 0; }
