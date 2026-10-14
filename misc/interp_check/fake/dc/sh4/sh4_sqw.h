#pragma once
// interp_check: pref's store-queue flush is not in the tests; the harness counts calls.
#include "types.h"
#include "dc/mem/mmu.h"
void ic_sqw(u32 dst, bool mmu);
template<bool mmu_on> inline void do_sqw(u32 dst) { ic_sqw(dst, mmu_on); }
