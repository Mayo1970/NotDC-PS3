#pragma once
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// UDP log to the dev PC (nc -ul 18194). printf/puts/putchar are wrapped onto it.
int ps3_log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int ps3_log_vprintf(const char* fmt, va_list ap);

#ifdef __cplusplus
}
#endif
