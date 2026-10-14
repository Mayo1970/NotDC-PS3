// interp_check: the FPU op bodies as dc/sh4/sh4_fpu.cpp builds them.
#include "dc/sh4/sh4_fpu_prelude.h"
#include "dc/sh4/sh4_fpu_ops.h"

void iNimp(const char* str) { printf("Unimplemented sh4 fpu instruction: %s\n", str); }
