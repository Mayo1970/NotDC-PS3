// Plan section 7, bundle 20, PC check (WSL): the SH4 interpreter's op bodies against Flycast's op tests (P46) and
// bundle 20's cases, at each value of Interp Fixes and FPSCR Cause. A little-endian PC runs GetDR's LE branch only.
#include "types.h"
#include "dc/sh4/sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/sh4/ccn.h"
#include "dc/mem/mmu.h"
#include "dc/mem/sh4_mem.h"

// ---- what the op bodies and sh4_registers.cpp link against ----
int g_interp_fix_preset, g_fpscr_cause_preset, g_mmu_bits_preset = 1;
extern "C" { u32 g_ccall[CC_COUNT]; int g_jit_ccalls_preset; }
extern "C" int get_accuracy_preset() { return 0; }
f32 sin_table[0x10000 + 0x4000];
CCN_PTEH_type CCN_PTEH;
CCN_PTEL_type CCN_PTEL;
CCN_MMUCR_type CCN_MMUCR;
u32 CCN_TRA;
TLB_Entry UTLB[64], ITLB[4];
u32 sq_remap[64];
void UTLB_Sync(u32) {}
static u32 g_stub_calls, g_verify;
void ic_sqw(u32, bool) { g_stub_calls++; }
void ExecuteDelayslot() { g_stub_calls++; }
void ExecuteDelayslot_RTE() { g_stub_calls++; }
bool Do_Exeption(u32, u32, u32) { g_stub_calls++; return true; }
int UpdateINTC() { g_stub_calls++; return 0; }
int FASTCALL UpdateSystem() { g_stub_calls++; return 1; }
bool SRdecode() { return false; }
OpCallFP* OpPtr[0x10000];
int sh4_RunSelfTests();
#include "mac_ref.inc"

// ---- flat RAM: area 3 and its mirrors, as the tests use 0x8C and 0xAC ----
#define RAM_SIZE (16u << 20)
static u8 g_ram[RAM_SIZE + 8];
static u32 g_bad;
static u8* ram_at(u32 a, u32 n)
{
	if ((a & 0x1C000000) != 0x0C000000 || (a & (RAM_SIZE - 1)) + n > RAM_SIZE)
	{
		g_bad++;
		return nullptr;
	}
	return &g_ram[a & (RAM_SIZE - 1)];
}
template<typename T> static T rd(u32 a) { T v = 0; if (u8* p = ram_at(a, sizeof(T))) memcpy(&v, p, sizeof(T)); return v; }
template<typename T> static void wr(u32 a, T v) { if (u8* p = ram_at(a, sizeof(T))) memcpy(p, &v, sizeof(T)); }
u8 ic_read8(u32 a) { return rd<u8>(a); }
u16 ic_read16(u32 a) { return rd<u16>(a); }
u32 ic_read32(u32 a) { return rd<u32>(a); }
u64 ic_read64(u32 a) { return rd<u64>(a); }
void ic_write8(u32 a, u8 v) { wr<u8>(a, v); }
void ic_write16(u32 a, u16 v) { wr<u16>(a, v); }
void ic_write32(u32 a, u32 v) { wr<u32>(a, v); }
void ic_write64(u32 a, u64 v) { wr<u64>(a, v); }

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
#include "dc/sh4/sh4_optest.h"

// ---- the opcode table, as BuildOpcodeTables() fills OpPtr[] ----
struct OpRow { OpCallFP* fn; u32 mask; u32 rez; const char* name; };
extern sh4op(iNotImplemented);
#include "ops_table.inc"

static bool build_table()
{
	for (u32 i = 0; i < 0x10000; i++)
		OpPtr[i] = iNotImplemented;
	for (const OpRow& o : k_ops)
	{
		u32 count, shft;
		switch (o.mask)
		{
		case 0xFFFF: count = 1; shft = 0; break;
		case 0xF0FF: count = 16; shft = 8; break;
		case 0xF00F: count = 256; shft = 4; break;
		case 0xF000: count = 4096; shft = 0; break;
		case 0xFF00: count = 256; shft = 0; break;
		case 0xF08F: count = 256; shft = 4; break;
		case 0xF1FF: count = 8; shft = 9; break;
		case 0xF3FF: count = 4; shft = 10; break;
		default: printf("FAIL table: %s has mask %04X\n", o.name, o.mask); return false;
		}
		for (u32 i = 0; i < count; i++)
			OpPtr[((i << shft) & ~o.mask) + o.rez] = o.fn;
	}
	return true;
}

struct PcIo
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
		ic_write16(START, op);
		if (op2)
			ic_write16(START + 2, op2);
		if (op3)
			ic_write16(START + 4, op3);
	}
	void run(int n)
	{
		Sh4cntx.pc = START;
		for (int i = 0; i < n; i++)
		{
			const u32 op = ic_read16(Sh4cntx.pc);
			Sh4cntx.pc += 2;
			try { OpPtr[op](op); }
			catch (const VerifyFail& v) { g_verify++; printf("verify fired in %04X: %s (the PS3 hangs here)\n", op, v.what); }
		}
	}
	void write16(u32 a, u32 v) { ic_write16(a, (u16)v); }
	void write32(u32 a, u32 v) { ic_write32(a, v); }
	u32 read8(u32 a) { return ic_read8(a); }
	u32 read16(u32 a) { return ic_read16(a); }
	u32 read32(u32 a) { return ic_read32(a); }
	bool mac_l_s1() { return true; }
};

static const char* const k_names[16] = {
	"MovRmRnTest", "MovImmRnTest", "MovMiscTest", "LoadTest", "LoadTest2", "StoreTest", "StoreTest2", "ArithmeticTest",
	"MulDivTest", "CmpTest", "StatusRegTest", "FloatingPointTest", "DoubleFloatingPointTest", "MacSaturationTest",
	"FpscrCauseTest", "Bundle20OpTest"
};

// The tests' failures by group: only the rows' own groups may fail, each by the count of its fault.
static bool run_combo(int fix, int cause, u32& checks)
{
	memset(g_ram, 0, sizeof(g_ram));
	memset(&Sh4cntx, 0, sizeof(Sh4cntx));
	Sh4cntx.fpscr.full = 0x00040001;
	Sh4cntx.old_fpscr = Sh4cntx.fpscr;
	g_interp_fix_preset = fix;
	g_fpscr_cause_preset = cause;
	g_verify = g_bad = 0;
	PcIo io;
	Sh4tStats st;
	memset(&st, 0, sizeof(st));
	Sh4OpTest<PcIo> t(io, st, "[interp]");
	t.run_all();
	checks += st.checks;
	bool ok = st.groups == 16 && g_bad == 0;
	if (st.groups != 16)
		printf("FAIL fix=%d cause=%d: %u groups ran\n", fix, cause, st.groups);
	if (g_bad)
		printf("FAIL fix=%d cause=%d: %u accesses outside RAM\n", fix, cause, g_bad);
	for (u32 g = 0; g < 16; g++)
	{
		// OLD addv and subv overflow an int: an optimizing compiler may drop their T (4 checks); g++ -O2 does.
		if (!fix && g == 7)
		{
			if (st.group_fails[g] != 0 && st.group_fails[g] != 4)
			{
				printf("FAIL fix=%d cause=%d: ArithmeticTest has %u failures, want 0 or 4\n", fix, cause, st.group_fails[g]);
				ok = false;
			}
			else
				printf("note fix=0: the OLD addv/subv lost T in %u checks with this compiler\n", st.group_fails[g]);
			continue;
		}
		// OLD fneg: fr9 not negated and fr8 flipped; the mac and cause groups fail as a whole.
		const bool want_fail = (!fix && (g == 12 || g == 13)) || (!cause && g == 14);
		const bool bad = want_fail ? (g == 12 ? st.group_fails[g] != 2 : st.group_fails[g] == 0) : st.group_fails[g] != 0;
		if (bad)
		{
			printf("FAIL fix=%d cause=%d: %s has %u failures, want %s\n", fix, cause, k_names[g], st.group_fails[g],
			       want_fail ? (g == 12 ? "2" : "some") : "0");
			ok = false;
		}
	}
	// Only the OLD mac.l with S=1 may reach a verify: once per run of it.
	if (g_verify != (fix ? 0u : 4u))
	{
		printf("FAIL fix=%d cause=%d: %u verify hits\n", fix, cause, g_verify);
		ok = false;
	}
	printf("%s fix=%d cause=%d: %u checks, %u failed\n", ok ? "ok  " : "FAIL", fix, cause, st.checks, st.fails);
	return ok;
}

int main(int argc, char** argv)
{
	FILE* f = fopen(argc > 1 ? argv[1] : "misc/fsca-table.bin", "rb");
	if (!f || fread(sin_table, sizeof(f32), 0x8000, f) != 0x8000)
	{
		printf("FAIL: no fsca table\n");
		return 2;
	}
	fclose(f);
	// GenerateSinCos(): the second half is the negated first, then the cos overlap.
	sin_table[0x8000] = 0.0f;
	for (int i = 0x8001; i < 0x10000; i++)
		sin_table[i] = -sin_table[i - 0x8000];
	for (int i = 0x10000; i < 0x14000; i++)
		sin_table[i] = sin_table[i & 0xFFFF];
	if (!build_table())
		return 2;
	// "fixed": both rows at the new path only, for the build with every UBSan check (the OLD addv overflows an int).
	const bool fixed_only = argc > 2 && !strcmp(argv[2], "fixed");
	u32 checks = 0;
	bool ok = true;
	for (int fix = fixed_only; fix < 2; fix++)
		for (int cause = fixed_only; cause < 2; cause++)
			ok = run_combo(fix, cause, checks) && ok;
	// The boot self-test with its own Io (sh4_selftest.cpp), both rows new: no failure, and the context comes back.
	g_interp_fix_preset = g_fpscr_cause_preset = 1;
	memset(&Sh4cntx, 0, sizeof(Sh4cntx));
	Sh4cntx.r[3] = 0x13579BDF;
	Sh4cntx.pc = 0x8C010000;
	const Sh4Context before = Sh4cntx;
	const int self_fails = sh4_RunSelfTests();
	const bool kept = !memcmp(&before, &Sh4cntx, sizeof(Sh4cntx));
	if (self_fails || !kept)
	{
		printf("FAIL boot self-test: %d failed, the context %s\n", self_fails, kept ? "came back" : "changed");
		ok = false;
	}
	printf("%s: %u checks in %d row combination(s)\n", ok ? "PASS" : "FAIL", checks, fixed_only ? 1 : 4);
	return ok ? 0 : 1;
}
