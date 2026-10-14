#pragma once
// fullmmu_check: the engine's printf lines go to the harness's log, so the checks can read them.
#include <cstdio>
int fm_log(const char* fmt, ...);
#define printf fm_log
