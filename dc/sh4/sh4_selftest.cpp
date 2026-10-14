// Row "SH4 Op Tests" (plan section 7, bundle 20, P46): sh4_optest.h on the real interpreter at boot, so GetDR/SetDR's
// big-endian branch, the 64-bit word order and the XOR byte lanes run. Before the first reset, which clears its RAM.
#include "types.h"
#include "sh4_interpreter.h"
#include "sh4_registers.h"
#include "sh4_opcode_list.h"
#include "dc/mem/sh4_mem.h"
#include "ps3/ps3_presets.h"

// The register macros of sh4_registers.h would rewrite the names below.
#undef r
#undef r_bank
#undef gbr
#undef ssr
#undef spc
#undef sgr
#undef dbr
#undef vbr
#undef mach
#undef macl
#undef pr
#undef fpul
#undef next_pc
#undef curr_pc
#undef sr
#undef fpscr
#undef old_sr
#undef old_fpscr
#undef fr
#undef xf
#undef fr_hex
#undef xf_hex
#undef dr_hex
#undef xd_hex
#include "sh4_optest.h"

namespace
{
struct Ps3Io
{
	typedef sr_type SrType;
	static const u32 START = 0xAC000000;
	u32& r(int n) { return Sh4cntx.r[n]; }
	u32& gbr() { return Sh4cntx.gbr; }
	u32& fr_hex(int n) { return ((u32*)Sh4cntx.fr)[n]; }
	u32& xf_hex(int n) { return ((u32*)Sh4cntx.xf)[n]; }
	double get_dr(int n) { return GetDR(n); }
	void set_dr(int n, double d) { SetDR(n, d); }
	sr_type& sr() { return Sh4cntx.sr; }
	void set_sr(u32 v) { Sh4cntx.sr.SetFull(v); Sh4cntx.old_sr.SetFull(v); }
	u64 mac() { return ((u64)Sh4cntx.mach << 32) | Sh4cntx.macl; }
	void set_mac(u64 v) { Sh4cntx.mach = (u32)(v >> 32); Sh4cntx.macl = (u32)v; }
	u32& fpul() { return Sh4cntx.fpul; }
	void set_pr(u32 v) { Sh4cntx.fpscr.PR = v; Sh4cntx.old_fpscr.PR = v; }
	void set_sz(u32 v) { Sh4cntx.fpscr.SZ = v; Sh4cntx.old_fpscr.SZ = v; }
	void prepare(u16 op, u16 op2, u16 op3)
	{
		WriteMem16(START, op);
		if (op2)
			WriteMem16(START + 2, op2);
		if (op3)
			WriteMem16(START + 4, op3);
	}
	void run(int n)
	{
		Sh4cntx.pc = START;
		for (int i = 0; i < n; i++)
		{
			const u32 op = IReadMem16(Sh4cntx.pc);
			Sh4cntx.pc += 2;
			OpPtr[op](op);
		}
	}
	// Flycast writes words at any address (gbr + 0x17): an unaligned one goes byte by byte, little-endian.
	void write16(u32 a, u32 v)
	{
		if (a & 1)
			for (u32 i = 0; i < 2; i++)
				WriteMem8(a + i, (u8)(v >> (8 * i)));
		else
			WriteMem16(a, (u16)v);
	}
	void write32(u32 a, u32 v)
	{
		if (a & 3)
			for (u32 i = 0; i < 4; i++)
				WriteMem8(a + i, (u8)(v >> (8 * i)));
		else
			WriteMem32(a, v);
	}
	u32 read8(u32 a) { return ReadMem8(a); }
	u32 read16(u32 a) { return ReadMem16(a); }
	u32 read32(u32 a) { return ReadMem32(a); }
	// The OLD mac.l with S=1 hangs in its verify.
	bool mac_l_s1() { return g_interp_fix_preset != 0; }
};
}

// Returns the failed checks. The SH4 context is put back as it was.
int sh4_RunSelfTests()
{
	static Sh4Context saved;
	saved = Sh4cntx;
	Sh4cntx.fpscr.full = 0x00040001;
	Sh4cntx.old_fpscr = Sh4cntx.fpscr;

	Ps3Io io;
	Sh4tStats st;
	memset(&st, 0, sizeof(st));
	Sh4OpTest<Ps3Io> t(io, st, "[sh4test]");
	t.run_all();

	Sh4cntx = saved;
	printf("[sh4test] Flycast SH4 op tests (P46) and bundle 20's cases: %u checks in %u groups, %u failed | Interp Fixes "
	       "%s, FPSCR Cause %s\n", st.checks, st.groups, st.fails, g_interp_fix_preset ? "FIXED" : "OLD",
	       g_fpscr_cause_preset ? "CLEAR" : "OLD");
	return (int)st.fails;
}
