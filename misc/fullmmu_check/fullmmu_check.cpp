// Plan section 7, bundle 20 (P38), PC check (WSL): dc/sh4/sh4_mmu_int.cpp, the SH4 MMU interpreter. Flycast's op tests
// run through it with AT 0 and 1; then hand-assembled SH4 programs raise each MMU exception, against hand-worked results.
#include "types.h"
#include "dc/sh4/sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/sh4_fullmmu.h"
#include "dc/mem/mmu.h"
#include "dc/mem/sh4_mem.h"
#include "plugins/plugin_manager.h"
#include "ps3_timebase.h"
#include <cstdarg>
#include <string>
#include <vector>

// ---- what the op bodies, sh4_registers.cpp and the engine link against ----
int g_interp_fix_preset = 1, g_fpscr_cause_preset = 1, g_mmu_bits_preset = 1, g_full_mmu_preset = 2;
extern "C" { u32 g_ccall[CC_COUNT]; int g_jit_ccalls_preset; uint64_t ps3_timebase_hz = 80000000; }
extern "C" int get_accuracy_preset() { return 0; }
extern "C" uint64_t gettime(void) { static uint64_t t; return t += 4000000; }
f32 sin_table[0x10000 + 0x4000];
CCN_PTEH_type CCN_PTEH;
CCN_PTEL_type CCN_PTEL;
CCN_MMUCR_type CCN_MMUCR;
CCN_PTEA_type CCN_PTEA;
u32 CCN_TRA, CCN_EXPEVT, CCN_INTEVT, CCN_TEA, CCN_TTB;
TLB_Entry UTLB[64], ITLB[4];
u32 sq_remap[64];
u8 sq_both[64];
sh4_if sh4_cpu;
volatile bool sh4_int_bCpuRun;
OpCallFP* OpPtr[0x10000];
sh4_opcodelistentry* OpDesc[0x10000];
u32 SB_ISTNRM, SB_ISTEXT, SB_ISTERR, SB_IML2NRM, SB_IML2EXT, SB_IML4NRM, SB_IML4EXT, SB_IML6NRM, SB_IML6EXT;
u32 interrupt_vpend, interrupt_vmask, decoded_srimask;
static u32 g_utlb_syncs, g_sqw_calls, g_bad, g_bad_addr, g_slices, g_stop_at, g_plain_slots;
void UTLB_Sync(u32) { g_utlb_syncs++; }
void ITLB_Sync(u32) {}
void ic_sqw(u32, bool) { g_sqw_calls++; }
// The plain op bodies (ic_cpu.cpp) link; the engine runs its own copies, so these must stay at 0.
void ExecuteDelayslot() { g_plain_slots++; }
void ExecuteDelayslot_RTE() { g_plain_slots++; }
int UpdateINTC() { return 0; }
bool SRdecode() { return false; }
void Sh4_ApplyAccuracyPreset() {}
s32 sh4_GetCpuTimeslice() { return 1; }
s32 sh4_GetCpuRatio() { return 1; }
// The JIT form's side (sh4_mmu_jit.cpp): what the engine calls, recorded.
int g_mmu_jit_preset, g_mmu_fast_preset, g_mmu_bcache_preset, g_mmu_sq_preset;
bool g_jmmu_bcache, g_jmmu_sq;
static bool g_can_jit;
static int g_jit_selects, g_jit_selected;
static u32 g_utlb_hooks, g_utlb_hook_last, g_fast_changes;
bool jmmu_can_run() { return g_can_jit; }
void jmmu_select(bool on) { g_jit_selects++; g_jit_selected = on; }
void jmmu_utlb_changed(u32 e) { g_utlb_hooks++; g_utlb_hook_last = e; }
void jmmu_fast_changed() { g_fast_changes++; }
// One op per slice: the run stops after g_stop_at ops.
int FASTCALL UpdateSystem()
{
	if (++g_slices >= g_stop_at)
		sh4_int_bCpuRun = false;
	return 1;
}
#include "mac_ref.inc"

// Do_Exeption as dc/sh4/intc.cpp has it, and a record of each call.
#define Do_Exeption Do_Exeption_intc
#include "do_exception.inc"
#undef Do_Exeption

static std::vector<std::string> g_log;
int fm_log(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (g_log.size() < 4096)
		g_log.push_back(line);
	return 0;
}

// ---- physical memory: area 3 RAM in every region, the store queues, the CCN registers ----
#define RAM_SIZE (16u << 20)
static u8 g_ram[RAM_SIZE + 8];
struct TaRec { u32 addr, w0; };
static std::vector<TaRec> g_ta;

static u32* p4_reg(u32 a)
{
	switch (a)
	{
	case 0xFF000000: return &CCN_PTEH.reg_data;
	case 0xFF000004: return &CCN_PTEL.reg_data;
	case 0xFF000008: return &CCN_TTB;
	case 0xFF00000C: return &CCN_TEA;
	case 0xFF000010: return &CCN_MMUCR.reg_data;
	case 0xFF000020: return &CCN_TRA;
	case 0xFF000024: return &CCN_EXPEVT;
	case 0xFF000028: return &CCN_INTEVT;
	case 0xFF000034: return &CCN_PTEA.reg_data;
	}
	return nullptr;
}

static void bad(u32 a)
{
	if (!g_bad++)
		g_bad_addr = a;
}

template<typename T> static T mem_rd(u32 a)
{
	T v = 0;
	if ((a >> 26) == 0x38)
		memcpy(&v, &sq_both[a & 0x3F], sizeof(T));
	else if ((a >> 24) == 0xFF)
	{
		u32* p = p4_reg(a);
		if (p && sizeof(T) == 4)
			v = (T)*p;
		else
			bad(a);
	}
	else if ((a & 0x1C000000) == 0x0C000000)
		memcpy(&v, &g_ram[a & (RAM_SIZE - 1)], sizeof(T));
	else
		bad(a);
	return v;
}

template<typename T> static void mem_wr(u32 a, T v)
{
	if ((a >> 26) == 0x38)
		memcpy(&sq_both[a & 0x3F], &v, sizeof(T));
	else if ((a >> 24) == 0xF6 && sizeof(T) == 4)
	{
		// The UTLB address array (the manual): VPN, D, V and ASID of the entry in bits 13-8.
		TLB_Entry& e = UTLB[(a >> 8) & 63];
		e.Address.reg_data = (u32)v & 0xFFFFFCFF;
		e.Data.D = ((u32)v >> 9) & 1;
		e.Data.V = ((u32)v >> 8) & 1;
	}
	else if ((a >> 24) == 0xF7 && sizeof(T) == 4)
	{
		if (a & 0x00800000)
			UTLB[(a >> 8) & 63].Assistance.reg_data = (u32)v;
		else
			UTLB[(a >> 8) & 63].Data.reg_data = (u32)v;
	}
	else if ((a >> 24) == 0xFF)
	{
		u32* p = p4_reg(a);
		if (!p || sizeof(T) != 4)
			bad(a);
		else if (p == &CCN_MMUCR.reg_data)
		{
			// ccn.cpp CCN_MMUCR_write with MMUCR Bits FIXED: the write mask, TI clears every V bit and reads 0.
			u32 w = (u32)v & MMUCR_WMASK;
			if (w & MMUCR_TI_BIT)
			{
				for (TLB_Entry& e : UTLB)
					e.Data.V = 0;
				for (TLB_Entry& e : ITLB)
					e.Data.V = 0;
				w &= ~MMUCR_TI_BIT;
			}
			*p = w;
		}
		else
			*p = (u32)v;
	}
	else if ((a & 0x1C000000) == 0x0C000000)
		memcpy(&g_ram[a & (RAM_SIZE - 1)], &v, sizeof(T));
	else
		bad(a);
}

u8 _vmem_ReadMem8(u32 a) { return mem_rd<u8>(a); }
u16 _vmem_ReadMem16(u32 a) { return mem_rd<u16>(a); }
u32 _vmem_ReadMem32(u32 a) { return mem_rd<u32>(a); }
u64 _vmem_ReadMem64(u32 a) { return mem_rd<u64>(a); }
void _vmem_WriteMem8(u32 a, u8 v) { mem_wr<u8>(a, v); }
void _vmem_WriteMem16(u32 a, u16 v) { mem_wr<u16>(a, v); }
void _vmem_WriteMem32(u32 a, u32 v) { mem_wr<u32>(a, v); }
void _vmem_WriteMem64(u32 a, u64 v) { mem_wr<u64>(a, v); }
void WriteMemBlock_nommu_ptr(u32 dst, u32* src, u32 size)
{
	for (u32 i = 0; i < size; i += 4)
		mem_wr<u32>(dst + i, src[i >> 2]);
}
void TAWriteSQ(u32 address, u32* data) { g_ta.push_back({ address, data[0] }); }

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

struct Exc { u32 epc, code, vect, old_sr, tea; };
static std::vector<Exc> g_exc;
bool Do_Exeption(u32 epc, u32 evn, u32 vect)
{
	const u32 old_sr_full = Sh4cntx.sr.GetFull();
	const bool ok = Do_Exeption_intc(epc, evn, vect);
	g_exc.push_back({ epc, evn, vect, old_sr_full, CCN_TEA });
	return ok;
}

// ---- the opcode table, as BuildOpcodeTables() fills OpPtr[] ----
struct OpRow { OpCallFP* fn; u32 mask; u32 rez; const char* name; };
extern sh4op(iNotImplemented);
#include "ops_table.inc"

static sh4_opcodelistentry s_desc[sizeof(k_ops) / sizeof(k_ops[0]) + 1];

static bool build_table()
{
	const u32 rows = sizeof(k_ops) / sizeof(k_ops[0]);
	s_desc[rows].diss = "unknown";
	for (u32 i = 0; i < 0x10000; i++)
	{
		OpPtr[i] = iNotImplemented;
		OpDesc[i] = &s_desc[rows];
	}
	for (const OpRow& o : k_ops)
	{
		sh4_opcodelistentry& d = s_desc[&o - k_ops];
		d.diss = o.name;
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
		{
			OpPtr[((i << shft) & ~o.mask) + o.rez] = o.fn;
			OpDesc[((i << shft) & ~o.mask) + o.rez] = &d;
		}
	}
	return true;
}

static void set_sr(u32 v)
{
	Sh4cntx.sr.SetFull(v);
	Sh4cntx.old_sr.SetFull(v);
}

// ---- part 1: Flycast's op tests through the engine, AT 0 and AT 1 (P1/P2 addresses: no translation, SR.MD 1) ----
struct MmuIo
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
	void set_sr(u32 v) { ::set_sr(v); }
	u64 mac() { return ((u64)Sh4cntx.mach << 32) | Sh4cntx.macl; }
	void set_mac(u64 v) { Sh4cntx.mach = (u32)(v >> 32); Sh4cntx.macl = (u32)v; }
	u32& fpul() { return Sh4cntx.fpul; }
	void set_pr(u32 v) { Sh4cntx.fpscr.PR = v; Sh4cntx.old_fpscr.PR = v; }
	void set_sz(u32 v) { Sh4cntx.fpscr.SZ = v; Sh4cntx.old_fpscr.SZ = v; }
	u32 at;
	void prepare(u16 op, u16 op2, u16 op3)
	{
		mem_wr<u16>(START, op);
		if (op2)
			mem_wr<u16>(START + 2, op2);
		if (op3)
			mem_wr<u16>(START + 4, op3);
	}
	void run(int n)
	{
		Sh4cntx.pc = START;
		CCN_MMUCR.reg_data = at;
		g_slices = 0;
		g_stop_at = n;
		Sh4_int_Run_Mmu();
	}
	void write16(u32 a, u32 v) { mem_wr<u8>(a, (u8)v); mem_wr<u8>(a + 1, (u8)(v >> 8)); }
	void write32(u32 a, u32 v) { write16(a, v & 0xFFFF); write16(a + 2, v >> 16); }
	u32 read8(u32 a) { return mem_rd<u8>(a); }
	u32 read16(u32 a) { return mem_rd<u16>(a); }
	u32 read32(u32 a) { return mem_rd<u32>(a); }
	bool mac_l_s1() { return true; }
};

static bool op_tests(u32 at, u32& checks)
{
	memset(g_ram, 0, sizeof(g_ram));
	memset(&Sh4cntx, 0, sizeof(Sh4cntx));
	memset(UTLB, 0, sizeof(UTLB));
	memset(ITLB, 0, sizeof(ITLB));
	Sh4cntx.fpscr.full = 0x00040001;
	Sh4cntx.old_fpscr = Sh4cntx.fpscr;
	g_exc.clear();
	g_bad = 0;
	MmuIo io;
	io.at = at;
	Sh4tStats st;
	memset(&st, 0, sizeof(st));
	Sh4OpTest<MmuIo> t(io, st, "[fullmmu]");
	t.run_all();
	checks += st.checks;
	bool ok = st.groups == 16 && st.fails == 0 && g_bad == 0 && g_exc.empty();
	printf("%s op tests AT=%u: %u checks in %u groups, %u failed, %u stray accesses, %u exceptions\n", ok ? "ok  " : "FAIL",
	       at, st.checks, st.groups, st.fails, g_bad, (u32)g_exc.size());
	return ok;
}

// ---- part 2: SH4 programs for each MMU path ----
static int g_fails;
static const char* g_test = "";
static u32 g_tests;
#define CHECK(cond, ...) do { if (!(cond)) { g_fails++; printf("FAIL %s: ", g_test); printf(__VA_ARGS__); \
	printf("\n"); } } while (0)

enum : u32 { VBR = 0x8C000000, MISS_H = VBR + 0x400, GEN_H = VBR + 0x100, CODE_VA = 0x00010000, CODE_PA = 0x0C010000,
	P1_CODE = 0x8C010000, PTEH_A = 0xFF000000, PTEL_A = 0xFF000004, MMUCR_A = 0xFF000010, SR_USER = 0,
	SR_PRIV = 0x40000000, SR_FD = 0x00008000, AT = 1 };
enum { SZ_1K, SZ_4K, SZ_64K, SZ_1M };

static u16 NOP() { return 0x0009; }
static u16 MOVI(int n, int imm) { return 0xE000 | n << 8 | (imm & 0xFF); }
static u16 MOVL_LD(int m, int n) { return 0x6002 | n << 8 | m << 4; }
static u16 MOVL_ST(int m, int n) { return 0x2002 | n << 8 | m << 4; }
static u16 MOVL_PUSH(int m, int n) { return 0x2006 | n << 8 | m << 4; }
static u16 MOVW_ST(int m, int n) { return 0x2001 | n << 8 | m << 4; }
static u16 STSL_MACL_PUSH(int n) { return 0x4012 | n << 8; }
static u16 MACL(int m, int n) { return 0x000F | n << 8 | m << 4; }
static u16 BRA(int disp) { return 0xA000 | (disp & 0xFFF); }
static u16 JMP(int m) { return 0x402B | m << 8; }
static u16 JSR(int m) { return 0x400B | m << 8; }
static u16 RTE() { return 0x002B; }
static u16 LDTLB() { return 0x0038; }
static u16 STC_SSR(int n) { return 0x0032 | n << 8; }
static u16 LDC_SSR(int m) { return 0x403E | m << 8; }
static u16 AND(int m, int n) { return 0x2009 | n << 8 | m << 4; }
static u16 FADD(int m, int n) { return 0xF000 | n << 8 | m << 4; }
static u16 LDS_FPUL(int m) { return 0x405A | m << 8; }
static u16 PREF(int n) { return 0x0083 | n << 8; }
static u16 TRAPA(int imm) { return 0xC300 | (imm & 0xFF); }

// Code at a virtual address, written at its physical one.
struct Prog
{
	u32 va, pa;
	Prog(u32 v, u32 p) : va(v), pa(p) {}
	Prog& op(u16 o) { mem_wr<u16>(pa, o); va += 2; pa += 2; return *this; }
	Prog& spin() { return op(BRA(-2)).op(NOP()); }
};

static u32 ptel(u32 pa, int sz, u32 prot, bool d, bool sh = true, bool v = true)
{
	return (pa & 0x1FFFFC00) | (v ? 0x100 : 0) | (sz >> 1 ? 0x80 : 0) | prot << 5 | (sz & 1 ? 0x10 : 0) | (d ? 4 : 0) |
	       (sh ? 2 : 0);
}
static void map(int i, u32 va, u32 pa, int sz, u32 prot, bool d = true, bool sh = true, u32 asid = 0)
{
	UTLB[i].Address.reg_data = (va & 0xFFFFFC00) | asid;
	UTLB[i].Data.reg_data = ptel(pa, sz, prot, d, sh);
	UTLB[i].Assistance.reg_data = 0;
}

// A fresh machine: VBR 0x8C000000, both handlers spin; the user code page at the last UTLB entry.
static void fresh(const char* name, u32 mmucr)
{
	g_test = name;
	g_tests++;
	memset(g_ram, 0, sizeof(g_ram));
	memset(sq_both, 0, sizeof(sq_both));
	memset(&Sh4cntx, 0, sizeof(Sh4cntx));
	Sh4cntx.fpscr.full = 0x00040001;
	Sh4cntx.old_fpscr = Sh4cntx.fpscr;
	Sh4cntx.vbr = VBR;
	memset(UTLB, 0, sizeof(UTLB));
	memset(ITLB, 0, sizeof(ITLB));
	CCN_PTEH.reg_data = CCN_PTEL.reg_data = CCN_PTEA.reg_data = 0;
	CCN_TEA = CCN_EXPEVT = CCN_TRA = 0;
	CCN_MMUCR.reg_data = mmucr;
	g_exc.clear();
	g_log.clear();
	g_ta.clear();
	g_bad = g_sqw_calls = 0;
	Prog(MISS_H, MISS_H).spin();
	Prog(GEN_H, GEN_H).spin();
	map(63, CODE_VA, CODE_PA, SZ_4K, 2);
}

// The TLB-miss handler loads PTEL (bank 1's r3), ldtlb at URC, and returns: the op runs again.
static void handler_miss(u32 ptel_value)
{
	Prog(MISS_H, MISS_H).op(MOVL_ST(3, 2)).op(LDTLB()).op(RTE()).op(NOP());
	Sh4cntx.r_bank[2] = PTEL_A;
	Sh4cntx.r_bank[3] = ptel_value;
}

static void handler_fpu()
{
	// Clears SSR.FD and returns: the op runs again with the FPU on.
	Prog(GEN_H, GEN_H).op(STC_SSR(4)).op(AND(5, 4)).op(LDC_SSR(4)).op(RTE()).op(NOP());
	Sh4cntx.r_bank[5] = ~SR_FD;
}

static void run(u32 pc, u32 srv, u32 ops)
{
	set_sr(srv);
	Sh4cntx.pc = pc;
	g_slices = 0;
	g_stop_at = ops;
	Sh4_int_Run_Mmu();
	CHECK(g_bad == 0, "%u accesses outside the memory map, the first at %08X", g_bad, g_bad_addr);
}

static void exc_is(size_t n, u32 spc, u32 code, u32 vect)
{
	CHECK(g_exc.size() == n, "%u exceptions, want %u", (u32)g_exc.size(), (u32)n);
	if (!g_exc.empty())
	{
		const Exc& e = g_exc[0];
		CHECK(e.epc == spc && e.code == code && e.vect == vect, "exception SPC %08X code %03X vector %03X, want %08X %03X %03X",
		      e.epc, e.code, e.vect, spc, code, vect);
	}
}

static u32 ram32(u32 pa) { return mem_rd<u32>(pa); }
// In a handler (SR.RB 1) the code's r0-r7 are in r_bank.
static u32 bank0(int n) { return Sh4cntx.sr.RB && n < 8 ? Sh4cntx.r_bank[n] : Sh4cntx.r[n]; }
static void set32(u32 pa, u32 v) { mem_wr<u32>(pa, v); }
static int utlb_with(u32 va)
{
	int n = 0;
	for (const TLB_Entry& e : UTLB)
		n += e.Data.V && (e.Address.reg_data & 0xFFFFF000) == (va & 0xFFFFF000);
	return n;
}
static bool logged(const char* text)
{
	for (const std::string& s : g_log)
		if (s.find(text) != std::string::npos)
			return true;
	return false;
}

static void t_miss_read()
{
	fresh("TLB miss on a read, ldtlb, rte", AT);
	Prog(CODE_VA, CODE_PA).op(MOVL_LD(1, 0)).op(MOVI(7, 1)).spin();
	set32(0x0C400010, 0xCAFEBABE);
	Sh4cntx.r[1] = 0x00400010;
	handler_miss(ptel(0x0C400000, SZ_4K, 3, true));
	run(CODE_VA, SR_USER, 20);
	exc_is(1, CODE_VA, 0x040, 0x400);
	CHECK(CCN_TEA == 0x00400010 && (CCN_PTEH.reg_data & 0xFFFFFC00) == 0x00400000, "TEA %08X PTEH %08X", CCN_TEA,
	      CCN_PTEH.reg_data);
	CHECK(Sh4cntx.r[0] == 0xCAFEBABE && Sh4cntx.r[7] == 1 && Sh4cntx.r[1] == 0x00400010, "r0 %08X r1 %08X r7 %u",
	      Sh4cntx.r[0], Sh4cntx.r[1], Sh4cntx.r[7]);
	CHECK(utlb_with(0x00400000) == 1, "%d UTLB entries map the page", utlb_with(0x00400000));
	CHECK(Sh4cntx.sr.MD == 0 && Sh4cntx.pc == CODE_VA + 4, "after rte: MD %u pc %08X", (u32)Sh4cntx.sr.MD, Sh4cntx.pc);
	CHECK(logged("[fullmmu] exception 040 at 00010000, TEA 00400010"), "no exception line in the log");
}

static void t_push_retry()
{
	fresh("mov.l Rm,@-Rn: TLB miss on the write, Rn moves once", AT);
	Prog(CODE_VA, CODE_PA).op(MOVL_PUSH(1, 15)).op(MOVI(7, 1)).spin();
	Sh4cntx.r[1] = 0x12345678;
	Sh4cntx.r[15] = 0x00500000;
	handler_miss(ptel(0x0C4FF000, SZ_4K, 3, true));
	run(CODE_VA, SR_USER, 20);
	exc_is(1, CODE_VA, 0x060, 0x400);
	CHECK(CCN_TEA == 0x004FFFFC, "TEA %08X", CCN_TEA);
	CHECK(Sh4cntx.r[15] == 0x004FFFFC && ram32(0x0C4FFFFC) == 0x12345678 && Sh4cntx.r[7] == 1, "r15 %08X mem %08X r7 %u",
	      Sh4cntx.r[15], ram32(0x0C4FFFFC), Sh4cntx.r[7]);

	fresh("sts.l MACL,@-Rn: TLB miss on the write, Rn moves once", AT);
	Prog(CODE_VA, CODE_PA).op(STSL_MACL_PUSH(15)).op(MOVI(7, 1)).spin();
	Sh4cntx.macl = 0x0BADF00D;
	Sh4cntx.r[15] = 0x00500000;
	handler_miss(ptel(0x0C4FF000, SZ_4K, 3, true));
	run(CODE_VA, SR_USER, 20);
	exc_is(1, CODE_VA, 0x060, 0x400);
	CHECK(Sh4cntx.r[15] == 0x004FFFFC && ram32(0x0C4FFFFC) == 0x0BADF00D, "r15 %08X mem %08X", Sh4cntx.r[15],
	      ram32(0x0C4FFFFC));
}

static void t_mac_retry()
{
	fresh("mac.l @Rm+,@Rn+: TLB miss on the second read, both move once", AT);
	Prog(CODE_VA, CODE_PA).op(MACL(1, 2)).op(MOVI(7, 1)).spin();
	map(62, 0x00410000, 0x0C410000, SZ_4K, 3);
	set32(0x0C410000, 3);
	set32(0x0C420000, 0xFFFFFFFE);
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = 0x00420000;
	Sh4cntx.macl = 10;
	handler_miss(ptel(0x0C420000, SZ_4K, 3, true));
	run(CODE_VA, SR_USER, 20);
	exc_is(1, CODE_VA, 0x040, 0x400);
	CHECK(CCN_TEA == 0x00420000, "TEA %08X", CCN_TEA);
	CHECK(Sh4cntx.r[1] == 0x00410004 && Sh4cntx.r[2] == 0x00420004 && Sh4cntx.mach == 0 && Sh4cntx.macl == 4 &&
	      Sh4cntx.r[7] == 1, "r1 %08X r2 %08X MAC %08X:%08X r7 %u", Sh4cntx.r[1], Sh4cntx.r[2], Sh4cntx.mach, Sh4cntx.macl,
	      Sh4cntx.r[7]);
}

static void t_slot_fault()
{
	fresh("TLB miss in bra's delay slot: SPC is the branch", AT);
	// bra to +12; its slot reads an unmapped page.
	Prog p(CODE_VA, CODE_PA);
	p.op(BRA(4)).op(MOVL_LD(1, 0)).op(MOVI(7, 9));
	Prog(CODE_VA + 12, CODE_PA + 12).op(MOVI(7, 1)).spin();
	set32(0x0C400010, 0xCAFEBABE);
	Sh4cntx.r[1] = 0x00400010;
	handler_miss(ptel(0x0C400000, SZ_4K, 3, true));
	run(CODE_VA, SR_USER, 20);
	exc_is(1, CODE_VA, 0x040, 0x400);
	CHECK(Sh4cntx.r[0] == 0xCAFEBABE && Sh4cntx.r[7] == 1 && Sh4cntx.pc == CODE_VA + 14, "r0 %08X r7 %u pc %08X",
	      Sh4cntx.r[0], Sh4cntx.r[7], Sh4cntx.pc);

	fresh("TLB miss in jsr's delay slot: PR set again, same value", AT);
	Prog(CODE_VA, CODE_PA).op(JSR(4)).op(MOVL_LD(1, 0)).op(MOVI(7, 9));
	Prog(CODE_VA + 0x10, CODE_PA + 0x10).op(MOVI(7, 1)).spin();
	set32(0x0C400010, 0xCAFEBABE);
	Sh4cntx.r[1] = 0x00400010;
	Sh4cntx.r[4] = CODE_VA + 0x10;
	handler_miss(ptel(0x0C400000, SZ_4K, 3, true));
	run(CODE_VA, SR_USER, 20);
	exc_is(1, CODE_VA, 0x040, 0x400);
	CHECK(Sh4cntx.pr == CODE_VA + 4 && Sh4cntx.r[0] == 0xCAFEBABE && Sh4cntx.r[7] == 1, "pr %08X r0 %08X r7 %u",
	      Sh4cntx.pr, Sh4cntx.r[0], Sh4cntx.r[7]);
}

static void t_fpu_off()
{
	const u32 f15 = 0x3FC00000, f20 = 0x40000000;
	fresh("SR.FD: fadd raises FPU disable, the handler clears FD", AT);
	handler_fpu();
	Prog(CODE_VA, CODE_PA).op(FADD(1, 0)).op(MOVI(7, 1)).spin();
	((u32*)Sh4cntx.fr)[0] = f15;
	((u32*)Sh4cntx.fr)[1] = f20;
	run(CODE_VA, SR_USER | SR_FD, 20);
	exc_is(1, CODE_VA, 0x800, 0x100);
	CHECK(Sh4cntx.fr[0] == 3.5f && Sh4cntx.sr.FD == 0 && Sh4cntx.r[7] == 1, "fr0 %f FD %u r7 %u", Sh4cntx.fr[0],
	      (u32)Sh4cntx.sr.FD, Sh4cntx.r[7]);

	fresh("SR.FD: fadd in bra's delay slot raises the slot FPU disable", AT);
	handler_fpu();
	Prog(CODE_VA, CODE_PA).op(BRA(2)).op(FADD(1, 0)).op(MOVI(7, 9));
	Prog(CODE_VA + 8, CODE_PA + 8).op(MOVI(7, 1)).spin();
	((u32*)Sh4cntx.fr)[0] = f15;
	((u32*)Sh4cntx.fr)[1] = f20;
	run(CODE_VA, SR_USER | SR_FD, 20);
	exc_is(1, CODE_VA, 0x820, 0x100);
	CHECK(Sh4cntx.fr[0] == 3.5f && Sh4cntx.r[7] == 1, "fr0 %f r7 %u", Sh4cntx.fr[0], Sh4cntx.r[7]);

	fresh("SR.FD: lds Rm,FPUL is an FPU op", AT);
	handler_fpu();
	Prog(CODE_VA, CODE_PA).op(LDS_FPUL(1)).spin();
	Sh4cntx.r[1] = 0x1234;
	run(CODE_VA, SR_USER | SR_FD, 20);
	exc_is(1, CODE_VA, 0x800, 0x100);
	CHECK(Sh4cntx.fpul == 0x1234, "fpul %08X", Sh4cntx.fpul);

	fresh("SR.FD with AT 0: the FPU disable still comes", 0);
	handler_fpu();
	Prog(P1_CODE, P1_CODE).op(FADD(1, 0)).spin();
	((u32*)Sh4cntx.fr)[0] = f15;
	((u32*)Sh4cntx.fr)[1] = f20;
	run(P1_CODE, SR_PRIV | SR_FD, 20);
	exc_is(1, P1_CODE, 0x800, 0x100);
	CHECK(Sh4cntx.fr[0] == 3.5f, "fr0 %f", Sh4cntx.fr[0]);

	fresh("SR.FD 0: no exception", AT);
	Prog(CODE_VA, CODE_PA).op(FADD(1, 0)).spin();
	((u32*)Sh4cntx.fr)[0] = f15;
	((u32*)Sh4cntx.fr)[1] = f20;
	run(CODE_VA, SR_USER, 20);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.fr[0] == 3.5f, "fr0 %f", Sh4cntx.fr[0]);
}

// One access from a fresh machine: the exception it raises, or none (code 0).
static void one_access(const char* name, u32 mmucr, u32 srv, u16 op, u32 addr, int prot, bool d, u32 code, u32 tea)
{
	fresh(name, mmucr);
	const bool user = srv == SR_USER;
	const u32 pc = user ? CODE_VA : P1_CODE;
	Prog(pc, user ? CODE_PA : P1_CODE).op(op).op(MOVI(7, 1)).spin();
	if (prot >= 0)
		map(40, 0x00410000, 0x0C410000, SZ_4K, (u32)prot, d);
	set32(0x0C410000, 0x600DDA7A);
	Sh4cntx.r[0] = 0x55555555;
	Sh4cntx.r[1] = addr;
	Sh4cntx.r[2] = 0xA5A5A5A5;
	run(pc, srv, 20);
	if (!code)
	{
		exc_is(0, 0, 0, 0);
		CHECK(Sh4cntx.r[7] == 1, "r7 %u: the next op did not run", Sh4cntx.r[7]);
		return;
	}
	const u32 vect = code == 0x040 || code == 0x060 ? 0x400 : 0x100;
	exc_is(1, pc, code, vect);
	CHECK(CCN_TEA == tea && bank0(0) == 0x55555555 && ram32(0x0C410000) == 0x600DDA7A && ram32(0x0C410004) == 0 &&
	      bank0(7) == 0, "TEA %08X want %08X, r0 %08X mem %08X %08X r7 %u", CCN_TEA, tea, bank0(0), ram32(0x0C410000),
	      ram32(0x0C410004), bank0(7));
	CHECK(Sh4cntx.ssr == srv && Sh4cntx.sr.MD == 1 && Sh4cntx.sr.BL == 1 && Sh4cntx.sr.RB == 1,
	      "SSR %08X SR %08X", Sh4cntx.ssr, Sh4cntx.sr.GetFull());
}

static void t_protection()
{
	const u16 rd = MOVL_LD(1, 0), wr = MOVL_ST(0, 1);
	one_access("user read, PR 0: protection", AT, SR_USER, rd, 0x00410000, 0, true, 0x0A0, 0x00410000);
	one_access("user read, PR 2: allowed", AT, SR_USER, rd, 0x00410000, 2, true, 0, 0);
	one_access("user write, PR 2: write protection", AT, SR_USER, wr, 0x00410004, 2, true, 0x0C0, 0x00410004);
	one_access("user write, PR 3: allowed", AT, SR_USER, wr, 0x00410004, 3, true, 0, 0);
	one_access("privileged write, PR 0: write protection", AT, SR_PRIV, wr, 0x00410004, 0, true, 0x0C0, 0x00410004);
	one_access("privileged read, PR 0: allowed", AT, SR_PRIV, rd, 0x00410000, 0, true, 0, 0);
	one_access("privileged write, PR 1: allowed", AT, SR_PRIV, wr, 0x00410004, 1, true, 0, 0);
	one_access("write to D 0: initial page write", AT, SR_USER, wr, 0x00410004, 3, false, 0x080, 0x00410004);
	one_access("read of D 0: allowed", AT, SR_USER, rd, 0x00410000, 3, false, 0, 0);

	fresh("a read, then a write, of a D 0 page: the write still faults", AT);
	map(40, 0x00410000, 0x0C410000, SZ_4K, 3, false);
	Prog(CODE_VA, CODE_PA).op(MOVL_LD(1, 0)).op(MOVL_ST(2, 1)).spin();
	set32(0x0C410000, 0x600DDA7A);
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = 0xA5A5A5A5;
	run(CODE_VA, SR_USER, 10);
	exc_is(1, CODE_VA + 2, 0x080, 0x100);
	CHECK(bank0(0) == 0x600DDA7A && ram32(0x0C410000) == 0x600DDA7A, "r0 %08X mem %08X", bank0(0), ram32(0x0C410000));
	one_access("misaligned read: address error before the TLB", AT, SR_USER, rd, 0x00410002, 3, true, 0x0E0, 0x00410002);
	one_access("misaligned word write: write address error", AT, SR_USER, MOVW_ST(0, 1), 0x00410001, 3, true, 0x100,
	           0x00410001);
	one_access("user read of P1: address error", AT, SR_USER, rd, 0x8C410000, 3, true, 0x0E0, 0x8C410000);
	one_access("misaligned P1 read with AT 1: address error", AT, SR_PRIV, rd, 0x8C410002, -1, true, 0x0E0, 0x8C410002);
	one_access("misaligned P1 read with AT 0: none (as the plain interpreter)", 0, SR_PRIV, rd, 0x8C410002, -1, true, 0, 0);
	one_access("user store queue write, SQMD 1: address error", AT | 0x200, SR_USER, wr, 0xE0000000, -1, true, 0x100,
	           0xE0000000);
	one_access("user store queue write, SQMD 0: allowed", AT, SR_USER, wr, 0xE0000000, -1, true, 0, 0);
	CHECK(mem_rd<u32>(0xE0000000) == 0x55555555, "store queue %08X", mem_rd<u32>(0xE0000000));
	one_access("unmapped write: TLB miss write", AT, SR_USER, wr, 0x00430000, -1, true, 0x060, 0x00430000);
	one_access("unmapped read: TLB miss read", AT, SR_PRIV, rd, 0x00430008, -1, true, 0x040, 0x00430008);

	fresh("user fetch from a PR 0 page: instruction protection", AT);
	map(63, CODE_VA, CODE_PA, SZ_4K, 0);
	Prog(CODE_VA, CODE_PA).op(MOVI(7, 1)).spin();
	run(CODE_VA, SR_USER, 10);
	exc_is(1, CODE_VA, 0x0A0, 0x100);
	CHECK(CCN_TEA == CODE_VA && Sh4cntx.r[7] == 0, "TEA %08X r7 %u", CCN_TEA, Sh4cntx.r[7]);

	fresh("user jmp to P1: instruction address error at the target", AT);
	Prog(CODE_VA, CODE_PA).op(JMP(4)).op(MOVI(7, 1));
	Sh4cntx.r[4] = 0x8C001000;
	run(CODE_VA, SR_USER, 10);
	exc_is(1, 0x8C001000, 0x0E0, 0x100);
	CHECK(CCN_TEA == 0x8C001000 && bank0(7) == 1, "TEA %08X r7 %u (the slot runs first)", CCN_TEA, bank0(7));

	fresh("two UTLB entries for one page: multi-hit", AT);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).spin();
	map(3, 0x00410000, 0x0C410000, SZ_4K, 3);
	map(9, 0x00410000, 0x0C420000, SZ_4K, 3);
	Sh4cntx.r[1] = 0x00410000;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(1, P1_CODE, 0x140, 0x100);

	fresh("an exception with SR.BL 1: the SH4 stops", AT);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).spin();
	Sh4cntx.r[1] = 0x00430000;
	run(P1_CODE, SR_PRIV | 0x10000000, 10);
	exc_is(0, 0, 0, 0);
	CHECK(g_slices < 10 && logged("FATAL: exception 040 at 8C010000 with SR.BL set"), "slices %u, no FATAL line", g_slices);
}

static void t_urc()
{
	for (u32 urb = 0; urb <= 8; urb += 8)
	{
		fresh(urb ? "URC wraps at URB 8" : "URC counts each UTLB access", AT | urb << 18 | 5 << 10);
		Prog p(P1_CODE, P1_CODE);
		for (int i = 0; i < 10; i++)
			p.op(MOVL_LD(1, 0));
		p.op(MOVL_LD(6, 5)).spin();
		map(40, 0x00410000, 0x0C410000, SZ_4K, 3);
		Sh4cntx.r[1] = 0x00410000;
		Sh4cntx.r[6] = MMUCR_A;
		run(P1_CODE, SR_PRIV, 30);
		// The manual: URC + 1 per access, 0 when it reaches URB. From 5, 10 steps: 15, or 6 7 0 1 .. 7 with URB 8.
		const u32 want = urb ? 7 : 15;
		exc_is(0, 0, 0, 0);
		CHECK(((Sh4cntx.r[5] >> 10) & 63) == want, "URC %u, want %u", (Sh4cntx.r[5] >> 10) & 63, want);
	}
}

static void t_itlb_lru()
{
	fresh("ITLB fills by MMUCR.LRUI", AT);
	// Pages A-E jump on in a ring; the ITLB holds 4. After A B C D E A: [D C A E], LRUI 0x1E (the manual's table).
	const u32 page[5] = { 0x00020000, 0x00030000, 0x00040000, 0x00050000, 0x00060000 };
	for (int i = 0; i < 5; i++)
	{
		map(10 + i, page[i], 0x0C020000 + i * 0x10000, SZ_4K, 3);
		Prog(page[i], 0x0C020000 + i * 0x10000).op(JMP(i + 1)).op(NOP());
		Sh4cntx.r[i + 1] = page[(i + 1) % 5];
	}
	run(page[0], SR_PRIV, 6);
	exc_is(0, 0, 0, 0);
	const u32 want[4] = { page[3], page[2], page[0], page[4] };
	for (int i = 0; i < 4; i++)
		CHECK(ITLB[i].Data.V && (ITLB[i].Address.reg_data & 0xFFFFF000) == want[i], "ITLB[%d] %08X V %u, want %08X", i,
		      ITLB[i].Address.reg_data, (u32)ITLB[i].Data.V, want[i]);
	CHECK(MMUCR_LRUI(CCN_MMUCR.reg_data) == 0x1E && Sh4cntx.pc == page[1], "LRUI %02X pc %08X",
	      MMUCR_LRUI(CCN_MMUCR.reg_data), Sh4cntx.pc);

	fresh("ITLB: one fetch from a page still updates LRUI", AT);
	// C jumps to A's last op, which falls into B: A gets one fetch. Fills C 3, A 2, B 1; LRUI 0x38.
	const u32 pc_ = 0x00070000, pa_ = 0x00080000, pb_ = 0x00081000;
	map(10, pc_, 0x0C070000, SZ_4K, 3);
	map(11, pa_, 0x0C080000, SZ_4K, 3);
	map(12, pb_, 0x0C081000, SZ_4K, 3);
	Prog(pc_, 0x0C070000).op(JMP(1)).op(NOP());
	Prog(pa_ + 0xFFE, 0x0C080FFE).op(NOP());
	Prog(pb_, 0x0C081000).spin();
	Sh4cntx.r[1] = pa_ + 0xFFE;
	run(pc_, SR_PRIV, 3);
	exc_is(0, 0, 0, 0);
	const u32 want2[4] = { 0, pb_, pa_, pc_ };
	for (int i = 1; i < 4; i++)
		CHECK(ITLB[i].Data.V && (ITLB[i].Address.reg_data & 0xFFFFF000) == want2[i], "ITLB[%d] %08X, want %08X", i,
		      ITLB[i].Address.reg_data, want2[i]);
	CHECK(MMUCR_LRUI(CCN_MMUCR.reg_data) == 0x38 && Sh4cntx.pc == pb_, "LRUI %02X pc %08X",
	      MMUCR_LRUI(CCN_MMUCR.reg_data), Sh4cntx.pc);

	fresh("ITLB: a fetch after a write of LRUI updates it again", AT);
	// The page fills ITLB 3; the code writes LRUI 0; the next fetch from the same page makes it 0x0B again.
	map(10, pa_, 0x0C080000, SZ_4K, 3);
	Prog(pa_, 0x0C080000).op(MOVL_ST(2, 3)).op(NOP()).op(MOVL_LD(3, 5)).spin();
	Sh4cntx.r[2] = AT;
	Sh4cntx.r[3] = MMUCR_A;
	run(pa_, SR_PRIV, 4);
	exc_is(0, 0, 0, 0);
	CHECK(MMUCR_LRUI(Sh4cntx.r[5]) == 0x0B, "LRUI %02X after the write", MMUCR_LRUI(Sh4cntx.r[5]));
}

static void t_sq_pref()
{
	fresh("pref with AT 1: the UTLB gives the store queue's target", AT);
	map(20, 0xE0000000, 0x0C600000, SZ_1M, 3);
	Prog(P1_CODE, P1_CODE).op(MOVL_ST(0, 1)).op(MOVL_ST(2, 3)).op(PREF(3)).op(MOVL_ST(2, 4)).op(PREF(4)).op(MOVI(7, 1))
		.spin();
	Sh4cntx.r[0] = 0x11111111;
	Sh4cntx.r[1] = 0xE0000000;
	Sh4cntx.r[2] = 0x22222222;
	Sh4cntx.r[3] = 0xE000001C;
	Sh4cntx.r[4] = 0xE0000020;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(0, 0, 0, 0);
	CHECK(ram32(0x0C600000) == 0x11111111 && ram32(0x0C60001C) == 0x22222222 && ram32(0x0C600020) == 0x22222222 &&
	      g_sqw_calls == 0 && Sh4cntx.r[7] == 1, "RAM %08X %08X %08X, plain flushes %u", ram32(0x0C600000),
	      ram32(0x0C60001C), ram32(0x0C600020), g_sqw_calls);

	fresh("pref with AT 1 to an unmapped store queue page: TLB miss read", AT);
	Prog(P1_CODE, P1_CODE).op(PREF(1)).spin();
	Sh4cntx.r[1] = 0xE0100000;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(1, P1_CODE, 0x040, 0x400);
	CHECK(CCN_TEA == 0xE0100000, "TEA %08X", CCN_TEA);

	fresh("pref with AT 1 to a page in area 4: the TA path", AT);
	map(21, 0xE0200000, 0x10000000, SZ_1M, 3);
	Prog(P1_CODE, P1_CODE).op(MOVL_ST(0, 1)).op(PREF(1)).spin();
	Sh4cntx.r[0] = 0x33333333;
	Sh4cntx.r[1] = 0xE0200000;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(0, 0, 0, 0);
	CHECK(g_ta.size() == 1 && g_ta[0].addr == 0x10000000 && g_ta[0].w0 == 0x33333333, "%u TA writes",
	      (u32)g_ta.size());

	fresh("pref with AT 0: the plain store queue flush", 0);
	Prog(P1_CODE, P1_CODE).op(PREF(1)).spin();
	Sh4cntx.r[1] = 0xE0000000;
	run(P1_CODE, SR_PRIV, 10);
	CHECK(g_sqw_calls == 1, "plain flushes %u", g_sqw_calls);

	fresh("ldtlb of a store queue page syncs sq_remap", AT);
	Prog(P1_CODE, P1_CODE).op(MOVL_ST(4, 5)).op(MOVL_ST(6, 8)).op(LDTLB()).spin();
	Sh4cntx.r[4] = 0xE0300000;
	Sh4cntx.r[5] = PTEH_A;
	Sh4cntx.r[6] = ptel(0x0C700000, SZ_1M, 3, true);
	Sh4cntx.r[8] = PTEL_A;
	g_utlb_syncs = 0;
	run(P1_CODE, SR_PRIV, 10);
	CHECK(g_utlb_syncs == 1 && utlb_with(0xE0300000) == 1, "UTLB_Sync calls %u", g_utlb_syncs);
}

static void t_translate()
{
	fresh("page sizes, P4 through the TLB, and the VPN/PPN masks", AT);
	map(30, 0x00700400, 0x0C700800, SZ_1K, 3);
	map(31, 0x00711000, 0x0C713000, SZ_4K, 3);
	map(32, 0x00725400, 0x0C755C00, SZ_64K, 3);   // the low VPN/PPN bits are not compared, not used
	map(33, 0x00800000, 0x0CA00000, SZ_1M, 3);
	map(34, 0x00600000, 0x1F000000, SZ_4K, 3);    // physical area 7: the P4 registers
	set32(0x0C700BFC, 0x10001000);
	set32(0x0C713FFC, 0x40004000);
	set32(0x0C75ABCC, 0x64646464);
	set32(0x0C755BCC, 0xBAD0BAD0);   // where a 4 KB mask would read
	set32(0x0CAABCD0, 0x01010101);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).op(MOVL_LD(2, 9)).op(MOVL_LD(3, 10)).op(MOVL_LD(4, 11)).op(MOVL_LD(5, 12))
		.spin();
	Sh4cntx.r[1] = 0x007007FC;
	Sh4cntx.r[2] = 0x00711FFC;
	Sh4cntx.r[3] = 0x0072ABCC;
	Sh4cntx.r[4] = 0x008ABCD0;
	Sh4cntx.r[5] = 0x00600010;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[8] == 0x10001000 && Sh4cntx.r[9] == 0x40004000 && Sh4cntx.r[10] == 0x64646464 &&
	      Sh4cntx.r[11] == 0x01010101, "1K %08X 4K %08X 64K %08X 1M %08X", Sh4cntx.r[8], Sh4cntx.r[9], Sh4cntx.r[10],
	      Sh4cntx.r[11]);
	CHECK(Sh4cntx.r[12] == CCN_MMUCR.reg_data, "P4 through the TLB: %08X, MMUCR %08X", Sh4cntx.r[12], CCN_MMUCR.reg_data);

	fresh("ASID: a private page needs PTEH.ASID; the cached translation sees the change", AT);
	map(40, 0x00410000, 0x0C410000, SZ_4K, 3, true, false, 5);
	set32(0x0C410000, 0x5A5A5A5A);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).op(MOVL_ST(2, 3)).op(MOVL_LD(1, 9)).spin();
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = 6;
	Sh4cntx.r[3] = PTEH_A;
	Sh4cntx.r[9] = 0x99;
	CCN_PTEH.reg_data = 5;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE + 4, 0x040, 0x400);
	CHECK(Sh4cntx.r[8] == 0x5A5A5A5A && Sh4cntx.r[9] == 0x99, "r8 %08X r9 %08X", Sh4cntx.r[8], Sh4cntx.r[9]);

	fresh("ASID: MMUCR.SV with SR.MD 1 matches any ASID", AT | 0x100);
	map(40, 0x00410000, 0x0C410000, SZ_4K, 3, true, false, 5);
	set32(0x0C410000, 0x5A5A5A5A);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).spin();
	Sh4cntx.r[1] = 0x00410000;
	CCN_PTEH.reg_data = 6;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[8] == 0x5A5A5A5A, "r8 %08X", Sh4cntx.r[8]);

	fresh("ASID: MMUCR.SV does not help user mode", AT | 0x100);
	map(40, 0x00410000, 0x0C410000, SZ_4K, 3, true, false, 5);
	Prog(CODE_VA, CODE_PA).op(MOVL_LD(1, 8)).spin();
	Sh4cntx.r[1] = 0x00410000;
	CCN_PTEH.reg_data = 6;
	run(CODE_VA, SR_USER, 10);
	exc_is(1, CODE_VA, 0x040, 0x400);

	fresh("ldtlb over the entry of the last access: the next access sees the new page", AT);
	map(7, 0x00410000, 0x0C410000, SZ_4K, 3);
	set32(0x0C410000, 0x11110000);
	set32(0x0C420000, 0x22220000);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).op(MOVL_ST(2, 3)).op(MOVL_ST(4, 5)).op(MOVL_ST(6, 8)).op(LDTLB())
		.op(MOVL_LD(1, 9)).spin();
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = AT | 7 << 10;
	Sh4cntx.r[3] = MMUCR_A;
	Sh4cntx.r[4] = 0x00410000;
	Sh4cntx.r[5] = PTEH_A;
	Sh4cntx.r[6] = ptel(0x0C420000, SZ_4K, 3, true);
	Sh4cntx.r[8] = PTEL_A;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[0] == 0x11110000 && Sh4cntx.r[9] == 0x22220000, "before %08X after %08X", Sh4cntx.r[0], Sh4cntx.r[9]);

	fresh("MMUCR.TI drops the cached translation", AT);
	map(7, 0x00410000, 0x0C410000, SZ_4K, 3);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).op(MOVL_ST(2, 3)).op(MOVL_LD(1, 9)).spin();
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = AT | MMUCR_TI_BIT;
	Sh4cntx.r[3] = MMUCR_A;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE + 4, 0x040, 0x400);
}

static void t_illegal_trapa()
{
	u32 bad_op = 0;
	for (u32 op = 0xFFFF; op >= 0xF000 && !bad_op; op--)
		if (OpPtr[op] == iNotImplemented)
			bad_op = op;
	fresh("an undefined op: illegal instruction", AT);
	Prog(P1_CODE, P1_CODE).op((u16)bad_op).spin();
	run(P1_CODE, SR_PRIV, 10);
	exc_is(1, P1_CODE, 0x180, 0x100);

	fresh("an undefined op in a delay slot: slot illegal instruction", AT);
	Prog(P1_CODE, P1_CODE).op(BRA(2)).op((u16)bad_op).spin();
	run(P1_CODE, SR_PRIV, 10);
	exc_is(1, P1_CODE, 0x1A0, 0x100);
	CHECK(bad_op != 0 && logged("[fullmmu] illegal op"), "op %04X, no illegal op line", bad_op);

	fresh("trapa in user mode with AT 1", AT);
	Prog(CODE_VA, CODE_PA).op(TRAPA(0x20)).spin();
	run(CODE_VA, SR_USER, 10);
	exc_is(1, CODE_VA + 2, 0x160, 0x100);
	CHECK(CCN_TRA == 0x80, "TRA %08X", CCN_TRA);
}

// The diagnostic lines (HW run 3): a polling loop is the top op, with what it reads, and its code.
static void t_diag()
{
	fresh("the diagnostic lines name a polling loop", AT);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).op(BRA(-3)).op(NOP());
	set32(0x0C400000, 0x12345678);
	Sh4cntx.r[1] = 0x8C400000;
	run(P1_CODE, SR_PRIV, 30000);
	exc_is(0, 0, 0, 0);
	char want[96];
	snprintf(want, sizeof(want), "%08X", P1_CODE + 2);
	CHECK(logged("[fullmmu] top pc of") && logged("(rd 8C400000=12345678)") && logged(want), "no top pc line");
	snprintf(want, sizeof(want), "[fullmmu] code at %08X:", P1_CODE + 2);
	CHECK(logged(want) && logged("   <-") && logged("[fullmmu] irq: SR"), "no code or irq lines");
}

static u32 g_jit_clears;
static void jit_clear() { g_jit_clears++; }

static void t_select()
{
	g_test = "the row and the disc pick the engine";
	g_tests++;
	const int rows[3] = { 0, 1, 2 };
	// SH4 MMU Engine JIT keeps the JIT (jmmu_select(true)) when it is the menu's engine; else the MMU interpreter.
	for (int jit = 0; jit < 2; jit++)
		for (int can = 0; can < 2; can++)
			for (int row : rows)
				for (int wince = 0; wince < 2; wince++)
				{
					g_full_mmu_preset = row;
					g_mmu_jit_preset = jit;
					g_can_jit = can;
					g_disc_wince = wince;
					sh4_cpu.Run = nullptr;
					sh4_cpu.ResetCache = jit_clear;
					g_jit_selects = 0;
					g_jit_selected = -1;
					sh4_fullmmu_select();
					const bool want = row == 2 || (row == 1 && wince), on_jit = want && jit && can;
					CHECK(g_fullmmu_active == want && (sh4_cpu.Run == Sh4_int_Run_Mmu) == (want && !on_jit),
					      "engine %d JIT %d row %d WinCE %d: active %d", jit, can, row, wince, (int)g_fullmmu_active);
					CHECK(g_jit_selects == 1 && g_jit_selected == (int)on_jit, "engine %d JIT %d row %d WinCE %d: select %d x%d",
					      jit, can, row, wince, g_jit_selected, g_jit_selects);
					// The MMU engine has no JIT code: its cache reset must not run the JIT's clear.
					g_jit_clears = 0;
					sh4_cpu.ResetCache();
					CHECK(g_jit_clears == (want && !on_jit ? 0u : 1u), "row %d WinCE %d: %u JIT clears", row, wince,
					      g_jit_clears);
					if (on_jit)
						CHECK(logged("the MMU JIT runs this boot"), "no JIT line");
				}
	g_full_mmu_preset = 2;
	g_mmu_jit_preset = 0;
}

// ---- the JIT form's entries (sh4_mmu_jit.cpp calls them): translations, ops and exceptions with no longjmp out ----
static void t_jit_data()
{
	u32 pa;
	int ent;
	fresh("JIT data translation: a hit, its UTLB entry", AT);
	map(5, 0x00400000, 0x0C400000, SZ_4K, 3);
	set_sr(SR_USER);
	CHECK(jx_data_xlat(0x00400010, 4, 0, &pa, &ent) == 0 && pa == 0x0C400010 && ent == 5, "pa %08X entry %d", pa, ent);
	CHECK(jx_data_xlat(0x00400FFE, 2, 1, &pa, &ent) == 0 && pa == 0x0C400FFE && ent == 5, "write: pa %08X entry %d", pa,
	      ent);
	// A write after a read of another page: its entry, not the read's.
	map(6, 0x00410000, 0x0C410000, SZ_4K, 3);
	CHECK(jx_data_xlat(0x00400010, 4, 0, &pa, &ent) == 0 && ent == 5, "read entry %d", ent);
	CHECK(jx_data_xlat(0x00410020, 4, 1, &pa, &ent) == 0 && pa == 0x0C410020 && ent == 6, "write: pa %08X entry %d", pa,
	      ent);
	CHECK(g_exc.empty(), "an exception was taken");

	fresh("JIT data translation: miss, protection, address error, no exception taken", AT);
	map(5, 0x00400000, 0x0C400000, SZ_4K, 2);
	set_sr(SR_USER);
	CHECK(jx_data_xlat(0x00500004, 4, 0, &pa, &ent) == 0x040 && CCN_TEA == 0x00500004 &&
	      (CCN_PTEH.reg_data & 0xFFFFFC00) == 0x00500000, "read miss: TEA %08X PTEH %08X", CCN_TEA, CCN_PTEH.reg_data);
	CHECK(jx_data_xlat(0x00500004, 1, 1, &pa, &ent) == 0x060, "write miss");
	CHECK(jx_data_xlat(0x00400004, 4, 1, &pa, &ent) == 0x0C0, "write to a read-only page");
	CHECK(jx_data_xlat(0x00400002, 4, 0, &pa, &ent) == 0x0E0, "a misaligned read");
	CHECK(jx_data_xlat(0x00400001, 2, 1, &pa, &ent) == 0x100, "a misaligned write");
	CHECK(jx_data_xlat(0x8C001000, 4, 0, &pa, &ent) == 0x0E0, "P1 in user mode");
	CHECK(g_exc.empty() && Sh4cntx.sr.MD == 0, "an exception was taken");

	fresh("JIT data translation: P1 and AT 0 untranslated", AT);
	set_sr(SR_PRIV);
	CHECK(jx_data_xlat(0x8C001000, 4, 1, &pa, &ent) == 0 && pa == 0x8C001000 && ent == -1, "P1: pa %08X entry %d", pa, ent);
	CCN_MMUCR.reg_data = 0;
	set_sr(SR_USER);
	CHECK(jx_data_xlat(0x00500006, 2, 0, &pa, &ent) == 0 && pa == 0x00500006 && ent == -1, "AT 0: pa %08X entry %d", pa,
	      ent);
}

static void t_jit_fetch()
{
	u32 pa;
	fresh("JIT fetch: the ITLB, its cache checked against the entry", AT);
	set_sr(SR_USER);
	CHECK(jx_fetch(CODE_VA + 4, &pa) == 0 && pa == CODE_PA + 4, "first fetch pa %08X", pa);
	CHECK(jx_fetch(CODE_VA + 0x3FE, &pa) == 0 && pa == CODE_PA + 0x3FE, "second fetch pa %08X", pa);
	// A new ITLB copy of the page at another frame: the cached translation must not stay.
	for (TLB_Entry& e : ITLB)
		if (e.Data.V)
			e.Data.reg_data = ptel(0x0C020000, SZ_4K, 2, true);
	CHECK(jx_fetch(CODE_VA + 8, &pa) == 0 && pa == 0x0C020008, "after the ITLB change pa %08X", pa);
	CHECK(jx_fetch(CODE_VA + 1, &pa) == 0x0E0, "an odd pc");
	CHECK(jx_fetch(P1_CODE, &pa) == 0x0E0, "P1 in user mode");
	CHECK(jx_fetch(0x00600000, &pa) == 0x040 && CCN_TEA == 0x00600000, "a miss: TEA %08X", CCN_TEA);
	set_sr(SR_PRIV);
	CHECK(jx_fetch(P1_CODE, &pa) == 0 && pa == P1_CODE, "P1: pa %08X", pa);
	CHECK(g_exc.empty(), "an exception was taken");

	fresh("JIT fetch: a new ASID misses a page that is not shared", AT);
	map(63, CODE_VA, CODE_PA, SZ_4K, 2, true, false, 7);
	CCN_PTEH.reg_data = 7;
	set_sr(SR_USER);
	CHECK(jx_fetch(CODE_VA, &pa) == 0 && pa == CODE_PA, "ASID 7: pa %08X", pa);
	CCN_PTEH.reg_data = 8;
	CHECK(jx_fetch(CODE_VA + 2, &pa) == 0x040, "ASID 8 fetched the page");
}

static void t_jit_ifb()
{
	fresh("JIT ifb: an op that faults, from a slot and not", AT);
	Prog(CODE_VA, CODE_PA).op(MOVL_LD(1, 0));
	Sh4cntx.r[1] = 0x00500000;
	set_sr(SR_USER);
	CHECK(jx_ifb(MOVL_LD(1, 0), CODE_VA + 0x20, 0) == 1, "no exception reported");
	exc_is(1, CODE_VA + 0x20, 0x040, 0x400);
	CHECK(Sh4cntx.pc == MISS_H && Sh4cntx.sr.MD == 1, "pc %08X", Sh4cntx.pc);

	fresh("JIT ifb: an op that runs", AT);
	set_sr(SR_USER);
	CHECK(jx_ifb(MOVI(4, 0x55), CODE_VA, 0) == 0 && Sh4cntx.r[4] == 0x55 && g_exc.empty(), "r4 %08X", Sh4cntx.r[4]);

	fresh("JIT ifb: FPU disabled, then in a slot; an illegal op in a slot", AT);
	set_sr(SR_PRIV | SR_FD);
	CHECK(jx_ifb(FADD(1, 2), P1_CODE, 0) == 1, "FADD with FD");
	exc_is(1, P1_CODE, 0x800, 0x100);
	g_exc.clear();
	set_sr(SR_PRIV | SR_FD);
	CHECK(jx_ifb(LDS_FPUL(3), P1_CODE + 4, 1) == 1, "lds FPUL in a slot with FD");
	exc_is(1, P1_CODE + 4, 0x820, 0x100);
	g_exc.clear();
	set_sr(SR_PRIV);
	u32 bad_op = 0;
	for (u32 op = 0xFFFF; op && !bad_op; op--)
		if (OpPtr[op] == iNotImplemented)
			bad_op = op;
	CHECK(jx_ifb(bad_op, P1_CODE + 8, 1) == 1, "op %04X", bad_op);
	exc_is(1, P1_CODE + 8, 0x1A0, 0x100);

	fresh("JIT ifb: ldtlb calls the JIT's hook with the entry", AT);
	set_sr(SR_PRIV);
	CCN_MMUCR.reg_data = AT | 17 << 10;
	g_utlb_hooks = 0;
	CHECK(jx_ifb(LDTLB(), P1_CODE, 0) == 0 && g_utlb_hooks == 1 && g_utlb_hook_last == 17, "%u hooks, entry %u",
	      g_utlb_hooks, g_utlb_hook_last);

	fresh("JIT ifb: a fault with SR.BL set stops at the op", AT);
	Sh4cntx.r[1] = 0x00500000;
	set_sr(SR_PRIV | 0x10000000);
	CHECK(jx_ifb(MOVL_LD(1, 0), CODE_VA + 6, 0) == 1 && g_exc.empty() && Sh4cntx.pc == CODE_VA + 6, "pc %08X, %u exceptions",
	      Sh4cntx.pc, (u32)g_exc.size());
	CHECK(logged("[fullmmu] FATAL"), "no FATAL line");
}

static void t_jit_take()
{
	fresh("JIT take: a write miss, FPU disabled, SR.BL", AT);
	set_sr(SR_USER);
	CHECK(jx_take(CODE_VA + 2, 0x060) == 1, "not taken");
	exc_is(1, CODE_VA + 2, 0x060, 0x400);
	CHECK(Sh4cntx.pc == MISS_H, "pc %08X", Sh4cntx.pc);
	g_exc.clear();
	set_sr(SR_USER | SR_FD);
	CHECK(jx_take(CODE_VA + 4, 0x800) == 1, "not taken");
	exc_is(1, CODE_VA + 4, 0x800, 0x100);
	g_exc.clear();
	set_sr(SR_PRIV | 0x10000000);
	CHECK(jx_take(P1_CODE, 0x040) == 0 && g_exc.empty() && Sh4cntx.pc == P1_CODE, "BL: pc %08X", Sh4cntx.pc);
	CHECK(jx_fpu_op(FADD(1, 2)) && jx_fpu_op(LDS_FPUL(1)) && !jx_fpu_op(MOVI(1, 1)) && !jx_fpu_op(MOVL_LD(1, 2)),
	      "the FPU op table");
}

// ---- row MMU Model FAST: Flycast's FAST_MMU table in place of the 64-entry UTLB ----
// A fresh machine with MMU Model FAST on the interpreter: the choice empties the table; entries come by ldtlb only.
static void fast_fresh(const char* name, u32 mmucr)
{
	fresh(name, mmucr);
	g_full_mmu_preset = 2;
	g_mmu_fast_preset = 1;
	g_mmu_jit_preset = 0;
	sh4_fullmmu_select();
	g_fast_changes = 0;
}

// ldtlb at UTLB slot i, as a TLB miss handler loads an entry (jx_ifb runs the engine's op).
static void load(int i, u32 va, u32 pa, int sz, u32 prot, bool d = true, bool sh = true, u32 asid = 0)
{
	const u32 old = Sh4cntx.sr.GetFull();
	set_sr(SR_PRIV);
	CCN_PTEH.reg_data = (va & 0xFFFFFC00) | asid;
	CCN_PTEL.reg_data = ptel(pa, sz, prot, d, sh);
	CCN_PTEA.reg_data = 0;
	CCN_MMUCR.reg_data = (CCN_MMUCR.reg_data & ~(63u << 10)) | (u32)i << 10;
	jx_ifb(LDTLB(), P1_CODE, 0);
	set_sr(old);
}

static void t_fast()
{
	fast_fresh("FAST: a page stays after its UTLB slot is loaded again", AT);
	CHECK(g_mmu_fast && logged("[fullmmu] MMU Model FAST:"), "FAST off, or no line");
	load(5, 0x00400000, 0x0C400000, SZ_4K, 3);
	load(5, 0x00500000, 0x0C500000, SZ_4K, 3);
	set32(0x0C400010, 0x11111111);
	set32(0x0C500020, 0x22222222);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).op(MOVL_LD(2, 9)).spin();
	Sh4cntx.r[1] = 0x00400010;
	Sh4cntx.r[2] = 0x00500020;
	CCN_PTEH.reg_data = 0;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[8] == 0x11111111 && Sh4cntx.r[9] == 0x22222222 && utlb_with(0x00400000) == 0, "r8 %08X r9 %08X",
	      Sh4cntx.r[8], Sh4cntx.r[9]);

	fast_fresh("FAST: a miss runs the handler; its ldtlb loads the page", AT);
	handler_miss(ptel(0x0C410000, SZ_4K, 3, true));
	set32(0x0C410010, 0xCAFEBABE);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).op(MOVL_LD(1, 7)).spin();
	Sh4cntx.r[1] = 0x00410010;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE, 0x040, 0x400);
	CHECK(Sh4cntx.r[0] == 0xCAFEBABE && Sh4cntx.r[7] == 0xCAFEBABE, "r0 %08X r7 %08X", Sh4cntx.r[0], Sh4cntx.r[7]);

	fast_fresh("FAST: MMUCR.TI empties the table", AT);
	load(7, 0x00410000, 0x0C410000, SZ_4K, 3);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).op(MOVL_ST(2, 3)).op(MOVL_LD(1, 9)).spin();
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = AT | MMUCR_TI_BIT;
	Sh4cntx.r[3] = MMUCR_A;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE + 4, 0x040, 0x400);
	CHECK(g_fast_changes == 1, "%u table changes", g_fast_changes);

	fast_fresh("FAST: a UTLB array write leaves what the UTLB holds", AT);
	load(5, 0x00410000, 0x0C410000, SZ_4K, 3);
	load(6, 0x00420000, 0x0C420000, SZ_4K, 3);
	set32(0x0C410000, 0x41414141);
	Prog(P1_CODE, P1_CODE).op(MOVL_ST(2, 3)).op(MOVL_LD(1, 8)).op(MOVL_LD(4, 9)).spin();
	Sh4cntx.r[2] = 0x00420000;	// entry 6, V 0
	Sh4cntx.r[3] = 0xF6000600;
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[4] = 0x00420000;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE + 4, 0x040, 0x400);
	CHECK(Sh4cntx.r[8] == 0x41414141 && !UTLB[6].Data.V, "r8 %08X, entry 6 V %u", Sh4cntx.r[8], (u32)UTLB[6].Data.V);

	fast_fresh("FAST: no protection or first-write checks", AT);
	load(63, CODE_VA, CODE_PA, SZ_4K, 2);
	load(5, 0x00400000, 0x0C400000, SZ_4K, 0, false);
	Prog(CODE_VA, CODE_PA).op(MOVL_ST(1, 2)).op(MOVI(7, 1)).spin();
	Sh4cntx.r[1] = 0x12345678;
	Sh4cntx.r[2] = 0x00400040;
	run(CODE_VA, SR_USER, 10);
	exc_is(0, 0, 0, 0);
	CHECK(ram32(0x0C400040) == 0x12345678 && Sh4cntx.r[7] == 1, "RAM %08X r7 %u", ram32(0x0C400040), Sh4cntx.r[7]);

	fast_fresh("FAST: a private page needs PTEH.ASID", AT);
	load(40, 0x00410000, 0x0C410000, SZ_4K, 3, true, false, 5);
	set32(0x0C410000, 0x5A5A5A5A);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).op(MOVL_ST(2, 3)).op(MOVL_LD(1, 9)).spin();
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = 6;
	Sh4cntx.r[3] = PTEH_A;
	Sh4cntx.r[9] = 0x99;
	CCN_PTEH.reg_data = 5;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE + 4, 0x040, 0x400);
	CHECK(Sh4cntx.r[8] == 0x5A5A5A5A && Sh4cntx.r[9] == 0x99, "r8 %08X r9 %08X", Sh4cntx.r[8], Sh4cntx.r[9]);

	fast_fresh("FAST: MMUCR.SV with SR.MD 1 matches any ASID, not in user mode", AT | 0x100);
	load(40, 0x00410000, 0x0C410000, SZ_4K, 3, true, false, 5);
	load(63, CODE_VA, CODE_PA, SZ_4K, 2);
	set32(0x0C410000, 0x5A5A5A5A);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).spin();
	Sh4cntx.r[1] = 0x00410000;
	CCN_PTEH.reg_data = 6;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[8] == 0x5A5A5A5A, "r8 %08X", Sh4cntx.r[8]);
	g_exc.clear();
	Prog(CODE_VA, CODE_PA).op(MOVL_LD(1, 8)).spin();
	run(CODE_VA, SR_USER, 10);
	exc_is(1, CODE_VA, 0x040, 0x400);

	fast_fresh("FAST: ldtlb of the same page and ASID with a new frame replaces it", AT);
	load(5, 0x00410000, 0x0C410000, SZ_4K, 3);
	set32(0x0C410000, 0x11110000);
	set32(0x0C420000, 0x22220000);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 0)).op(MOVL_ST(2, 3)).op(MOVL_ST(4, 5)).op(MOVL_ST(6, 8)).op(LDTLB())
		.op(MOVL_LD(1, 9)).spin();
	Sh4cntx.r[1] = 0x00410000;
	Sh4cntx.r[2] = AT | 9 << 10;
	Sh4cntx.r[3] = MMUCR_A;
	Sh4cntx.r[4] = 0x00410000;
	Sh4cntx.r[5] = PTEH_A;
	Sh4cntx.r[6] = ptel(0x0C420000, SZ_4K, 3, true);
	Sh4cntx.r[8] = PTEL_A;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[0] == 0x11110000 && Sh4cntx.r[9] == 0x22220000 && g_fast_changes == 1, "before %08X after %08X, %u"
	      " changes", Sh4cntx.r[0], Sh4cntx.r[9], g_fast_changes);

	fast_fresh("FAST: page sizes, P4 through the table, the VPN/PPN masks", AT);
	load(30, 0x00700400, 0x0C700800, SZ_1K, 3);
	load(31, 0x00711000, 0x0C713000, SZ_4K, 3);
	load(32, 0x00725400, 0x0C755C00, SZ_64K, 3);
	load(33, 0x00800000, 0x0CA00000, SZ_1M, 3);
	load(34, 0x00600000, 0x1F000000, SZ_4K, 3);
	set32(0x0C700BFC, 0x10001000);
	set32(0x0C713FFC, 0x40004000);
	set32(0x0C75ABCC, 0x64646464);
	set32(0x0C755BCC, 0xBAD0BAD0);
	set32(0x0CAABCD0, 0x01010101);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).op(MOVL_LD(2, 9)).op(MOVL_LD(3, 10)).op(MOVL_LD(4, 11)).op(MOVL_LD(5, 12))
		.spin();
	Sh4cntx.r[1] = 0x007007FC;
	Sh4cntx.r[2] = 0x00711FFC;
	Sh4cntx.r[3] = 0x0072ABCC;
	Sh4cntx.r[4] = 0x008ABCD0;
	Sh4cntx.r[5] = 0x00600010;
	CCN_PTEH.reg_data = 0;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[8] == 0x10001000 && Sh4cntx.r[9] == 0x40004000 && Sh4cntx.r[10] == 0x64646464 &&
	      Sh4cntx.r[11] == 0x01010101 && Sh4cntx.r[12] == CCN_MMUCR.reg_data, "1K %08X 4K %08X 64K %08X 1M %08X P4 %08X",
	      Sh4cntx.r[8], Sh4cntx.r[9], Sh4cntx.r[10], Sh4cntx.r[11], Sh4cntx.r[12]);

	fast_fresh("FAST: a shared entry and a private one of the same page both stay", AT);
	load(5, 0x00410000, 0x0C410000, SZ_4K, 3, true, false, 5);
	load(6, 0x00410000, 0x0C420000, SZ_4K, 3, true, true, 0);
	set32(0x0C420000, 0x2A2A2A2A);
	Prog(P1_CODE, P1_CODE).op(MOVL_LD(1, 8)).spin();
	Sh4cntx.r[1] = 0x00410000;
	CCN_PTEH.reg_data = 6;
	run(P1_CODE, SR_PRIV, 10);
	exc_is(0, 0, 0, 0);
	CHECK(Sh4cntx.r[8] == 0x2A2A2A2A, "ASID 6: r8 %08X", Sh4cntx.r[8]);

	fast_fresh("FAST: pref through the table, not the UTLB; a page it lacks misses", AT);
	load(20, 0xE0000000, 0x0C600000, SZ_1M, 3);
	load(20, 0x00900000, 0x0C900000, SZ_4K, 3);	// the UTLB slot holds another page now
	Prog(P1_CODE, P1_CODE).op(MOVL_ST(0, 1)).op(PREF(1)).op(PREF(2)).spin();
	Sh4cntx.r[0] = 0x11111111;
	Sh4cntx.r[1] = 0xE000000C;
	Sh4cntx.r[2] = 0xE0100000;
	CCN_PTEH.reg_data = 0;
	run(P1_CODE, SR_PRIV, 20);
	exc_is(1, P1_CODE + 4, 0x040, 0x400);
	CHECK(ram32(0x0C60000C) == 0x11111111 && CCN_TEA == 0xE0100000, "RAM %08X TEA %08X", ram32(0x0C60000C), CCN_TEA);

	u32 pa;
	int ent;
	fast_fresh("FAST: the JIT's entries: data as entry 64, fetches through the table", AT);
	load(5, 0x00400000, 0x0C400000, SZ_4K, 3);
	load(63, CODE_VA, CODE_PA, SZ_4K, 2);
	CCN_PTEH.reg_data = 0;
	set_sr(SR_USER);
	CHECK(jx_data_xlat(0x00400010, 4, 1, &pa, &ent) == 0 && pa == 0x0C400010 && ent == 64, "pa %08X entry %d", pa, ent);
	CHECK(jx_data_xlat(0x00500000, 4, 0, &pa, &ent) == 0x040, "an absent page");
	CHECK(jx_fetch(CODE_VA + 4, &pa) == 0 && pa == CODE_PA + 4, "fetch pa %08X", pa);
	CHECK(jx_fetch(CODE_VA + 8, &pa) == 0 && pa == CODE_PA + 8, "cached fetch pa %08X", pa);
	jx_fast_after_write(0xFF000010, MMUCR_TI_BIT);
	CHECK(jx_fetch(CODE_VA + 8, &pa) == 0x040, "a fetch after TI");
	CHECK(g_exc.empty(), "an exception was taken");

	fast_fresh("FAST: a full table starts again", AT);
	for (u32 i = 0; i <= 32768; i++)
		load((int)(i & 63), i << 12, 0x0C000000 + ((i << 12) & 0xFFF000), SZ_4K, 3);
	CCN_PTEH.reg_data = 0;
	set_sr(SR_PRIV);
	CHECK(jx_data_xlat(32768u << 12, 4, 0, &pa, &ent) == 0 && pa == 0x0C000000, "the last: pa %08X", pa);
	CHECK(jx_data_xlat(0x00000000, 4, 0, &pa, &ent) == 0x040, "the first page stayed");
	jx_stats_print();
	CHECK(logged("[fullmmu] FAST table: entries 1 ") && logged(" full 1"), "the stats line");

	fast_fresh("FAST: the choice empties the table; no FAST without the full MMU", AT);
	load(5, 0x00400000, 0x0C400000, SZ_4K, 3);
	sh4_fullmmu_select();
	set_sr(SR_PRIV);
	CHECK(jx_data_xlat(0x00400000, 4, 0, &pa, &ent) == 0x040, "the table kept a page");
	g_full_mmu_preset = 0;
	sh4_fullmmu_select();
	CHECK(!g_mmu_fast, "FAST with the MMU off");
	g_full_mmu_preset = 2;
	g_mmu_fast_preset = 0;
	sh4_fullmmu_select();
	CHECK(!g_mmu_fast, "FAST with the row FULL");
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
	sin_table[0x8000] = 0.0f;
	for (int i = 0x8001; i < 0x10000; i++)
		sin_table[i] = -sin_table[i - 0x8000];
	for (int i = 0x10000; i < 0x14000; i++)
		sin_table[i] = sin_table[i & 0xFFFF];
	if (!build_table())
		return 2;
	u32 checks = 0;
	bool ok = op_tests(0, checks);
	ok = op_tests(AT, checks) && ok;
	g_test = "every opcode's handler has its MMU copy";
	CHECK(logged(" 0 opcodes without an MMU copy"), "the tables line: %s", g_log.empty() ? "none" : g_log[0].c_str());
	t_miss_read();
	t_push_retry();
	t_mac_retry();
	t_slot_fault();
	t_fpu_off();
	t_protection();
	t_urc();
	t_itlb_lru();
	t_sq_pref();
	t_translate();
	t_illegal_trapa();
	t_diag();
	t_jit_data();
	t_jit_fetch();
	t_jit_ifb();
	t_jit_take();
	t_select();
	t_fast();
	CHECK(g_plain_slots == 0, "the plain delay slot ran %u times", g_plain_slots);
	ok = ok && g_fails == 0;
	printf("%s: %u op test checks with AT 0 and 1, %u MMU programs, %d failed\n", ok ? "PASS" : "FAIL", checks, g_tests,
	       g_fails);
	return ok ? 0 : 1;
}
