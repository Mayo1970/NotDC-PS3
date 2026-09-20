// SH4 FPU interpreter. Macros/helpers are in sh4_fpu_prelude.h, opcode bodies in
// sh4_fpu_ops.h, so the cached engine can re-include the same bodies as labels.

#include "sh4_fpu_prelude.h"
#include "sh4_fpu_ops.h"


// ============================================================================
// ERROR HANDLING
// ============================================================================

void iNimp(const char *str)
{
  printf("Unimplemented sh4 fpu instruction: %s\n", str);
  // Optional: Add more robust error handling here
  // For example: log to file, trigger breakpoint, etc.
}
