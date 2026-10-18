#pragma once
// modem_check: the checked file's printf goes to the harness (lockstep.cpp modem_log). <cstdio> first: it
// undefines printf, and later includes of it do nothing.
#include <cstdio>
extern "C" int modem_log(const char* fmt, ...);
#define printf modem_log
