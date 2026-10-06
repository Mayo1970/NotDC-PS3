// Plan 5.38 (WSL): the real shil_canonical.h div32 ops for div32_check.cpp, in a unit of their own, as the core
// headers make r, sr, mach and macl macros. The stubs only let SHIL_MODE 1 compile; nothing calls them.
#include "dc/sh4/rec_v2/decoder.h"
#include "dc/sh4/rec_v2/ngen.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/sh4_registers.h"

int msgboxf(const char*, unsigned int, ...) { abort(); }
ALIGN(64) Sh4Context Sh4cntx;
CCN_MMUCR_type CCN_MMUCR;
void ngen_CC_Start(shil_opcode*) {}
void ngen_CC_Param(shil_opcode*, shil_param*, CanonicalParamType) {}
void ngen_CC_Call(shil_opcode*, void*) {}
void ngen_CC_Finish(shil_opcode*) {}
void FASTCALL do_sqw_mmu(u32) {}
void FASTCALL do_sqw_nommu(u32) {}
bool UpdateSR() { return false; }
void UpdateFPSCR() {}
extern "C" void sh4_mac_l(u32, u32, u32, u32, u32) {}
extern "C" void sh4_mac_w(u32, u32, u32, u32, u32) {}

#define SHIL_MODE 1
#include "dc/sh4/rec_v2/shil_canonical.h"

// The u64 is packed as on the PPU: quo in word[0], rem in word[1].
void canon_div32u(u32 r1, u32 r2, u32 r3, u32& quo, u32& rem)
{
	const u64 v = shil_opcl_div32u::f1(r1, r2, r3);
	quo = ((const u32*)&v)[0];
	rem = ((const u32*)&v)[1];
}

void canon_div32s(u32 r1, u32 r2, u32 r3, u32& quo, u32& rem)
{
	const u64 v = shil_opcl_div32s::f1(r1, r2, r3);
	quo = ((const u32*)&v)[0];
	rem = ((const u32*)&v)[1];
}

u32 canon_div32p2(u32 a, u32 b, u32 T)
{
	return shil_opcl_div32p2::f1(a, b, T);
}
