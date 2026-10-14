// Bundle 20 (P38), host only: sh4_mmu_jit.cpp and blockmanager.cpp's MMU blocks on scripted translations: JTC fills and
// drops, URC, code page generations, the lookup's exceptions and AT edges, the engine choice. Not in the PS3 build.
#include "types.h"
#include <string>
#include <cstdarg>
#include <sys/mman.h>

static std::vector<std::string> g_log;
static int jr_log(const char* fmt, ...)
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

#define printf jr_log
#ifndef JMMU_SRC
#define JMMU_SRC "dc/sh4/sh4_mmu_jit.cpp"
#endif
#include JMMU_SRC
#undef printf
// The register macros of sh4_registers.h would rewrite the names below.
#undef r
#undef sr
#undef fpscr
#undef next_pc
#undef spc
#undef pr

// ---- blockmanager.cpp's MMU functions (run.sh cuts them out), on a bucket table as that file's ----
#define CCALL(x)
#define bm_AddrHash(addr) (((addr)>>BM_BLOCKLIST_SHIFT)&BM_BLOCKLIST_MASK)
typedef vector<DynarecBlock> bm_List;
static bm_List blocks[BM_BLOCKLIST_COUNT];
DynarecBlock* cache[BM_BLOCKLIST_COUNT];
static DynarecBlock empty_block = {0, 0xFFFFFFFF, 0, 0, 0, 0};
BlockCacheFlatEntry bm_bcache[BM_BLOCKLIST_COUNT];
static void bcache_mirror(u32 idx, DynarecBlock* b)
{
	bm_bcache[idx].addr = b->addr;
	bm_bcache[idx].code = (u32)(uintptr_t)b->code;
}
#include "sec_bm.inc"
static void bm_reset_all()
{
	for (u32 i = 0; i < BM_BLOCKLIST_COUNT; i++)
	{
		cache[i] = &empty_block;
		blocks[i].clear();
	}
}

// ---- what sh4_mmu_jit.cpp links against, scripted and recorded ----
__settings settings;
sh4_if sh4_cpu;
ALIGN(64) Sh4Context Sh4cntx;
CCN_PTEH_type CCN_PTEH;
CCN_MMUCR_type CCN_MMUCR;
int g_mmu_bits_preset = 1;
bool g_dec_fpu_disabled;
bool g_mmu_fast;
uint64_t ps3_timebase_hz = 79800000, g_fake_tb, g_fake_step = 100;
// MMU Model FAST's write hook (sh4_mmu_int.cpp): recorded; TI or a TLB array flushes, as there.
struct Faw { u32 pa, v; };
static std::vector<Faw> g_faw;
static int g_fast_flushes;
void jx_fast_after_write(u32 pa, u32 v)
{
	g_faw.push_back({ pa, v });
	if (((pa >> 24) & 0xFA) == 0xF2 || ((pa & 0x1FFFFFFF) == 0x1F000010 && (v & MMUCR_TI_BIT)))
	{
		g_fast_flushes++;
		jmmu_fast_changed();
	}
}
VArray2 mem_b;
void* _vmem_MemInfo_ptr[0x100];
static u8* g_ram;   // main RAM below 4 GB, as the JTC keeps 32-bit host addresses
static u8* g_sq;

struct Access { u32 pa, sz; u64 v; bool write; };
static std::vector<Access> g_acc;
static u64 g_rd_value;
// The CCN registers a write reaches, at P4 or through the area 7 mirror.
static void reg_write(u32 a, u32 v)
{
	if ((a & 0x1FFFFFFF) == 0x1F000000)
		CCN_PTEH.reg_data = v;
	else if ((a & 0x1FFFFFFF) == 0x1F000010)
		CCN_MMUCR.reg_data = v;
}
u8 _vmem_ReadMem8(u32 a) { g_acc.push_back({ a, 1, 0, false }); return (u8)g_rd_value; }
u16 _vmem_ReadMem16(u32 a) { g_acc.push_back({ a, 2, 0, false }); return (u16)g_rd_value; }
u32 _vmem_ReadMem32(u32 a) { g_acc.push_back({ a, 4, 0, false }); return (u32)g_rd_value; }
u64 _vmem_ReadMem64(u32 a) { g_acc.push_back({ a, 8, 0, false }); return g_rd_value; }
void _vmem_WriteMem8(u32 a, u8 v) { g_acc.push_back({ a, 1, v, true }); reg_write(a, v); }
void _vmem_WriteMem16(u32 a, u16 v) { g_acc.push_back({ a, 2, v, true }); reg_write(a, v); }
void _vmem_WriteMem32(u32 a, u32 v) { g_acc.push_back({ a, 4, v, true }); reg_write(a, v); }
void _vmem_WriteMem64(u32 a, u64 v) { g_acc.push_back({ a, 8, v, true }); }

// The translations of sh4_mmu_int.cpp: the next data result; a fetch maps U0 by g_fetch_delta, P1/P2 as they are.
struct Xlat { u32 code, pa; int ent; };
static Xlat g_xl;
static int g_xl_calls;
u32 jx_data_xlat(u32 va, u32 size, u32 write, u32* pa, int* ent)
{
	g_xl_calls++;
	if (g_xl.code)
		return g_xl.code;
	*pa = g_xl.pa;
	*ent = g_xl.ent;
	return 0;
}
static u32 g_fetch_code, g_fetch_delta;
static int g_fetch_calls;
u32 jx_fetch(u32 va, u32* pa)
{
	g_fetch_calls++;
	if (g_fetch_code)
		return g_fetch_code;
	*pa = (va >> 31) ? va : va + g_fetch_delta;
	return 0;
}
static u32 g_ifb_ret, g_ifb_pteh, g_ifb_args[3];
static int g_ifb_calls;
u32 jx_ifb(u32 op, u32 epc, u32 slot)
{
	g_ifb_calls++;
	g_ifb_args[0] = op;
	g_ifb_args[1] = epc;
	g_ifb_args[2] = slot;
	if (g_ifb_pteh)
		CCN_PTEH.reg_data = g_ifb_pteh;
	return g_ifb_ret;
}
struct Take { u32 epc, code; };
static std::vector<Take> g_takes;
u32 jx_take(u32 epc, u32 code)
{
	g_takes.push_back({ epc, code });
	Sh4cntx.pc = 0x8C000400;
	return 1;
}
static int g_stats_prints;
void jx_stats_print() { g_stats_prints++; }

struct Comp { u32 pc, pa, cfg, pgen; };
static std::vector<Comp> g_comp;
static bool g_comp_fd, g_comp_fail;
static u8 g_codes[4096];
DynarecCodeEntry* rdv_CompilePCMmu(u32 pc, u32 pa, u32 cfg, u32 pgen)
{
	g_comp.push_back({ pc, pa, cfg, pgen });
	g_dec_fpu_disabled = g_comp_fd;
	if (g_comp_fd || g_comp_fail)
		return 0;
	DynarecCodeEntry* code = (DynarecCodeEntry*)(void*)&g_codes[g_comp.size() & 4095];
	bm_AddCodeMmu(pc, pa, cfg, pgen, code);
	return code;
}
static int g_clears;
void recSh4_Run() {}
void recSh4_ClearCache() { g_clears++; bm_reset_all(); }
static std::vector<int> g_dispatch;
void ngen_mmu_dispatch(bool on) { g_dispatch.push_back(on); }
static void other_reset_cache() {}

// ---- the checks ----
static int g_fails, g_checks;
static const char* g_test = "";
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fails++; if (g_fails < 40) { fprintf(stdout, "FAIL %s: ", g_test); \
	fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } } } while (0)

static bool logged(const char* text)
{
	for (const std::string& s : g_log)
		if (s.find(text) != std::string::npos)
			return true;
	return false;
}

static const u32 SR_MD = 0x40000000;
static void set_md(bool md) { Sh4cntx.sr.status = md ? SR_MD : 0; }

// A boot with the MMU JIT, AT on, user mode, RAM and the store queues mapped as _vmem maps them.
static void world(const char* name)
{
	g_test = name;
	memset(&Sh4cntx, 0, sizeof(Sh4cntx));
	CCN_PTEH.reg_data = 0;
	CCN_MMUCR.reg_data = 1;
	memset(_vmem_MemInfo_ptr, 0, sizeof(_vmem_MemInfo_ptr));
	for (u32 i = 0x0C; i < 0x100; i += 0x20)
		_vmem_MemInfo_ptr[i] = g_ram + 8;	// 16 MB mirrors: shift 8
	_vmem_MemInfo_ptr[0xE0] = g_sq + 26;		// the store queues: a 64-byte mirror
	settings.dynarec.Enable = true;
	sh4_cpu.ResetCache = other_reset_cache;
	bm_reset_all();
	g_log.clear();
	g_mmu_fast = false;
	g_jmmu_bcache = g_jmmu_sq = false;
	g_faw.clear();
	g_fast_flushes = 0;
	jmmu_select(true);
	g_acc.clear();
	g_takes.clear();
	g_comp.clear();
	g_xl = Xlat();
	g_xl_calls = g_fetch_calls = g_ifb_calls = g_clears = 0;
	g_fetch_code = 0;
	g_fetch_delta = 0;
	g_comp_fd = g_comp_fail = false;
	g_ifb_ret = g_ifb_pteh = 0;
	g_rd_value = 0;
	set_md(false);
}

static JtcEntry& slot(u32 t, u32 va) { return g_jtc[t][(va >> 10) & (JTC_N - 1)]; }
static u32 host_of(u32 pa) { return (u32)(uintptr_t)(g_ram + (pa & 0x00FFFC00)); }

static void read_from(u32 va, u32 pa, int ent)
{
	g_xl = { 0, pa, ent };
	jmmu_rd32(va);
}

static void t_cfg()
{
	world("the key bits: MD, PR, SZ, RM 1");
	set_md(true);
	Sh4cntx.fpscr.full = 0;
	CHECK(jmmu_cfg() == 1, "MD: %u", jmmu_cfg());
	set_md(false);
	Sh4cntx.fpscr.full = 1 << 19;
	CHECK(jmmu_cfg() == 4, "PR: %u", jmmu_cfg());
	Sh4cntx.fpscr.full = 1 << 20;
	CHECK(jmmu_cfg() == 8, "SZ: %u", jmmu_cfg());
	Sh4cntx.fpscr.full = 1;
	CHECK(jmmu_cfg() == 16, "RM 1: %u", jmmu_cfg());
	Sh4cntx.fpscr.full = 3 | 1 << 21 | 1 << 18;
	CHECK(jmmu_cfg() == 0, "RM 3, FR and DN: %u", jmmu_cfg());
}

static void t_fill()
{
	world("a translated read fills its mode's read table");
	g_rd_value = 0x11223344;
	g_xl = { 0, 0x0C123410, 5 };
	CHECK(jmmu_rd32(0x00401010) == 0x11223344, "value");
	CHECK(slot(0, 0x00401010).tag == 0x00401000 && slot(0, 0x00401010).base == host_of(0x0C123410), "slot %08X %08X",
	      slot(0, 0x00401010).tag, slot(0, 0x00401010).base);
	CHECK(g_acc.size() == 1 && g_acc[0].pa == 0x0C123410 && !g_acc[0].write, "the access at the physical address");
	CHECK(slot(1, 0x00401010).tag == 0xFFFFFFFF && slot(2, 0x00401010).tag == 0xFFFFFFFF, "another table filled");

	world("a privileged write fills table 3; sub-word values zero-extended");
	set_md(true);
	g_xl = { 0, 0x0C200000, 9 };
	jmmu_wr16(0x00500002, 0xFFFFABCD);
	CHECK(slot(3, 0x00500002).tag == 0x00500000 && slot(3, 0x00500002).base == host_of(0x0C200000), "slot %08X",
	      slot(3, 0x00500002).tag);
	CHECK(g_acc.size() == 1 && g_acc[0].write && g_acc[0].sz == 2 && g_acc[0].v == 0xABCD, "the write");
	g_rd_value = 0xFFFFFF80;
	CHECK(jmmu_rd8(0x00500003) == 0x80 && jmmu_rd16(0x00500002) == 0xFF80, "rd8 %X rd16 %X", jmmu_rd8(0x00500003),
	      jmmu_rd16(0x00500002));
	g_rd_value = 0x0123456789ABCDEFull;
	CHECK(jmmu_rd64(0x00500008) == 0x0123456789ABCDEFull, "rd64");
	jmmu_wr64(0x00500010, 0xFEDCBA9876543210ull);
	CHECK(g_acc.back().write && g_acc.back().sz == 8 && g_acc.back().v == 0xFEDCBA9876543210ull, "wr64");

	world("no fill: a handler, the store queues, AT off");
	g_xl = { 0, 0xFF000008, -1 };
	jmmu_rd32(0xFF000008);
	CHECK(slot(0, 0xFF000008).tag == 0xFFFFFFFF, "a handler page");
	g_xl = { 0, 0xE0000010, -1 };
	jmmu_wr32(0xE0000010, 5);
	CHECK(slot(1, 0xE0000010).tag == 0xFFFFFFFF, "the store queues");
	CCN_MMUCR.reg_data = 0;
	g_xl = { 0, 0x0C300000, -1 };
	jmmu_rd32(0x0C300000);
	CHECK(slot(0, 0x0C300000).tag == 0xFFFFFFFF, "AT off");

	world("privileged P1, P2, P4 aligned: no translation call, P1 filled as it is");
	set_md(true);
	CCN_MMUCR.reg_data = 1;
	jmmu_rd32(0xFF000008);
	jmmu_wr16(0xA05F6902, 7);
	jmmu_rd8(0x8C001003);
	CHECK(g_xl_calls == 0 && g_acc.size() == 3 && g_acc[0].pa == 0xFF000008 && g_acc[1].pa == 0xA05F6902 &&
	      g_acc[2].pa == 0x8C001003, "%d calls", g_xl_calls);
	CHECK(slot(2, 0x8C001003).tag == 0x8C001000 && slot(2, 0x8C001003).base == host_of(0x8C001000) &&
	      s_own_n[0] == 0, "the P1 slot");
	g_xl = { 0x0E0, 0, 0 };
	jmmu_rd32(0xFF00000A);
	CHECK(g_xl_calls == 1 && g_jmmu_fault == 0x0E0, "a misaligned P4 read did not translate");
	g_jmmu_fault = 0;
	set_md(false);
	jmmu_rd32(0x8C001000);
	CHECK(g_xl_calls == 2 && g_jmmu_fault == 0x0E0, "user mode P1 did not translate");
	g_jmmu_fault = 0;

	world("a fault: the code, no access, no fill");
	g_xl = { 0x040, 0, 0 };
	CHECK(jmmu_rd32(0x00600000) == 0 && g_jmmu_fault == 0x040 && g_acc.empty() && slot(0, 0x00600000).tag == 0xFFFFFFFF,
	      "fault %03X, %u accesses", g_jmmu_fault, (u32)g_acc.size());
	g_jmmu_fault = 0;
	g_xl = { 0x0C0, 0, 0 };
	jmmu_wr8(0x00600000, 1);
	CHECK(g_jmmu_fault == 0x0C0 && g_acc.empty(), "write fault %03X", g_jmmu_fault);
	g_jmmu_fault = 0;
}

static void t_drop()
{
	world("ldtlb drops the slots of its entry, steps URC");
	read_from(0x00401000, 0x0C401000, 5);
	read_from(0x00402000, 0x0C402000, 5);
	read_from(0x00403400, 0x0C403400, 5);
	read_from(0x00404000, 0x0C404000, 6);
	read_from(0x8C005000, 0x8C005000, -1);
	CCN_MMUCR.reg_data = 1 | 10 << 10;
	jmmu_utlb_changed(5);
	CHECK(slot(0, 0x00401000).tag == 0xFFFFFFFF && slot(0, 0x00402000).tag == 0xFFFFFFFF &&
	      slot(0, 0x00403400).tag == 0xFFFFFFFF, "entry 5's slots stay");
	CHECK(slot(0, 0x00404000).tag == 0x00404000 && slot(0, 0x8C005000).tag == 0x8C005000, "other slots dropped");
	CHECK(MMUCR_URC(CCN_MMUCR.reg_data) == 11 && MMUCR_AT(CCN_MMUCR.reg_data), "URC %u", MMUCR_URC(CCN_MMUCR.reg_data));
	CCN_MMUCR.reg_data = 1 | 63 << 10;
	jmmu_utlb_changed(1);
	CHECK(MMUCR_URC(CCN_MMUCR.reg_data) == 0, "URC wraps at 64: %u", MMUCR_URC(CCN_MMUCR.reg_data));
	CCN_MMUCR.reg_data = 1 | 11 << 10 | 12 << 18;
	jmmu_utlb_changed(1);
	CHECK(MMUCR_URC(CCN_MMUCR.reg_data) == 0 && MMUCR_URB(CCN_MMUCR.reg_data) == 12, "URC wraps at URB: %u",
	      MMUCR_URC(CCN_MMUCR.reg_data));

	world("a slot filled again from its entry is listed once");
	read_from(0x8C005000, 0x8C005000, -1);
	for (int i = 0; i < 60; i++)
		read_from(0x00401000, 0x0C401000, 7);
	jmmu_utlb_changed(7);
	CHECK(slot(0, 0x00401000).tag == 0xFFFFFFFF && slot(0, 0x8C005000).tag == 0x8C005000, "a repeat overflowed the list");

	world("a list that overflows: its drop flushes every table");
	read_from(0x8C005000, 0x8C005000, -1);
	for (u32 i = 0; i < 49; i++)
		read_from(0x00400000 + (i << 10), 0x0C400000 + (i << 10), 8);
	jmmu_utlb_changed(8);
	CHECK(slot(0, 0x8C005000).tag == 0xFFFFFFFF && slot(0, 0x00400000).tag == 0xFFFFFFFF, "no full flush");

	world("the hook does nothing without the MMU JIT");
	read_from(0x00401000, 0x0C401000, 5);
	jmmu_select(false);
	g_jit_mmu = false;
	CCN_MMUCR.reg_data = 1 | 3 << 10;
	jmmu_utlb_changed(5);
	CHECK(MMUCR_URC(CCN_MMUCR.reg_data) == 3, "URC stepped: %u", MMUCR_URC(CCN_MMUCR.reg_data));
}

static void filled_world(const char* name)
{
	world(name);
	read_from(0x00401000, 0x0C401000, 5);
	read_from(0x8C005000, 0x8C005000, -1);
}
static bool owned_kept() { return slot(0, 0x00401000).tag == 0x00401000; }
static bool p1_kept() { return slot(0, 0x8C005000).tag == 0x8C005000; }

static void t_regs()
{
	filled_world("PTEH: the same ASID keeps the slots, a new one drops them");
	g_xl = { 0, 0xFF000000, -1 };
	jmmu_wr32(0xFF000000, 0x00401000);
	CHECK(owned_kept() && p1_kept(), "the same ASID dropped");
	jmmu_wr32(0xFF000000, 0x00401000 | 0x23);
	CHECK(!owned_kept() && p1_kept(), "a new ASID: owned %d P1 %d", owned_kept(), p1_kept());

	filled_world("a new ASID drops the slots of UTLB entry 0 too");
	read_from(0x00406000, 0x0C406000, 0);	// JTC index 24: not the P1 slot's 20
	g_xl = { 0, 0xFF000000, -1 };
	jmmu_wr32(0xFF000000, 0x11);
	CHECK(slot(0, 0x00406000).tag == 0xFFFFFFFF && p1_kept(), "entry 0's slot kept");

	filled_world("PTEH through the area 7 mirror");
	g_xl = { 0, 0xBF000000, -1 };
	jmmu_wr32(0xBF000000, 0x42);
	CHECK(!owned_kept() && p1_kept(), "kept");

	filled_world("MMUCR, the UTLB and ITLB arrays drop; the operand cache arrays do not");
	g_xl = { 0, 0xF4000010, -1 };
	jmmu_wr32(0xF4000010, 0);
	CHECK(owned_kept(), "an operand cache write dropped");
	g_xl = { 0, 0xFF000010, -1 };
	jmmu_wr32(0xFF000010, 1);
	CHECK(!owned_kept() && p1_kept(), "MMUCR");
	read_from(0x00401000, 0x0C401000, 5);
	g_xl = { 0, 0xF6000300, -1 };
	jmmu_wr32(0xF6000300, 0);
	CHECK(!owned_kept(), "the UTLB address array");
	read_from(0x00401000, 0x0C401000, 5);
	g_xl = { 0, 0xF7800300, -1 };
	jmmu_wr16(0xF7800300, 0);
	CHECK(!owned_kept(), "the UTLB data array 2");
	read_from(0x00401000, 0x0C401000, 5);
	g_xl = { 0, 0xF3000000, -1 };
	jmmu_wr8(0xF3000000, 0);
	CHECK(!owned_kept(), "the ITLB data array");

	filled_world("an I-cache array write moves the epoch");
	const u32 e0 = s_jepoch;
	g_xl = { 0, 0xF0001000, -1 };
	jmmu_wr32(0xF0001000, 0);
	g_xl = { 0, 0xF1000000, -1 };
	jmmu_wr32(0xF1000000, 0);
	CHECK(s_jepoch == e0 + 2 && owned_kept(), "epoch %u -> %u", e0, s_jepoch);

	filled_world("an ifb that writes PTEH drops the slots, and says what it took");
	g_ifb_pteh = 0x00000055;
	g_ifb_ret = 1;
	CHECK(jmmu_ifb(0x2002, 0x8C001234, 1) == 1 && g_ifb_calls == 1 && g_ifb_args[0] == 0x2002 &&
	      g_ifb_args[1] == 0x8C001234 && g_ifb_args[2] == 1, "the ifb call");
	CHECK(!owned_kept() && p1_kept(), "the new ASID kept the slots");
}

static void code_page(u32 pa, u8 seed)
{
	for (u32 i = 0; i < 1024; i++)
		g_ram[(pa & 0xFFFC00) + i] = (u8)(seed + i * 7);
}

static void t_lookup()
{
	world("the lookup compiles once, then finds the block");
	g_fetch_delta = 0x0C3F0000;
	code_page(0x0C400000, 1);
	void* a = jmmu_lookup(0x00010020);
	CHECK(a && g_comp.size() == 1 && g_comp[0].pc == 0x00010020 && g_comp[0].pa == 0x0C400020 && g_comp[0].cfg == 0 &&
	      g_comp[0].pgen == 0 && Sh4cntx.pc == 0x00010020, "%u compiles", (u32)g_comp.size());
	CHECK(jmmu_lookup(0x00010020) == a && g_comp.size() == 1, "compiled again");

	world("a code page that changed is seen after an I-cache flush");
	g_fetch_delta = 0x0C3F0000;
	code_page(0x0C400000, 1);
	a = jmmu_lookup(0x00010020);
	code_page(0x0C400000, 2);
	CHECK(jmmu_lookup(0x00010020) == a && g_comp.size() == 1, "a change seen before the flush");
	sh4_cpu.ResetCache();
	void* b = jmmu_lookup(0x00010020);
	CHECK(b && b != a && g_comp.size() == 2 && g_comp[1].pgen == 1, "the changed page: %u compiles, gen %u",
	      (u32)g_comp.size(), g_comp.size() > 1 ? g_comp[1].pgen : 99);
	sh4_cpu.ResetCache();
	CHECK(jmmu_lookup(0x00010020) == b && g_comp.size() == 2, "an unchanged page compiled again");
	// The same physical page at another virtual address: a block of its own, the page's generation.
	CHECK(jmmu_lookup(0x8C400020) != b && g_comp.size() == 3 && g_comp[2].pa == 0x8C400020 && g_comp[2].pgen == 1,
	      "the P1 view");

	world("another mode or FPSCR is another block");
	a = jmmu_lookup(0x8C010000);
	set_md(true);
	void* m = jmmu_lookup(0x8C010000);
	Sh4cntx.fpscr.full = 1 << 20;
	void* z = jmmu_lookup(0x8C010000);
	CHECK(a && m && z && a != m && m != z && g_comp.size() == 3 && g_comp[1].cfg == 1 && g_comp[2].cfg == 9,
	      "%u compiles", (u32)g_comp.size());

	world("the BIOS is not a RAM page: no hash, generation 0");
	a = jmmu_lookup(0xA0000100);
	code_page(0x0C000100, 9);	// the RAM page that the BIOS address would alias
	sh4_cpu.ResetCache();
	CHECK(jmmu_lookup(0xA0000100) == a && g_comp.size() == 1 && g_comp[0].pgen == 0, "the BIOS compiled again");

	world("a fetch fault: 0 and its code, then taken at the pc");
	g_fetch_code = 0x0E0;
	CHECK(jmmu_lookup(0x00010001) == 0 && g_jmmu_fault == 0x0E0 && Sh4cntx.pc == 0x00010001 && g_comp.empty(),
	      "fault %03X", g_jmmu_fault);
	CHECK(jmmu_raise(0x00010001) == 0x8C000400 && g_takes.size() == 1 && g_takes[0].epc == 0x00010001 &&
	      g_takes[0].code == 0x0E0 && g_jmmu_fault == 0, "the raise");

	world("FPU disabled at a compile: 0x800 at the block's start");
	g_comp_fd = true;
	CHECK(jmmu_lookup(0x8C010000) == 0 && g_jmmu_fault == 0x800, "fault %03X", g_jmmu_fault);
	CHECK(jmmu_raise(0x8C010000) == 0x8C000400 && g_takes.size() == 1 && g_takes[0].code == 0x800, "the raise");

	world("a block that does not compile: the SH4 spins on it");
	g_comp_fail = true;
	CHECK(jmmu_lookup(0x8C010000) == 0 && g_jmmu_fault == 0, "fault %03X", g_jmmu_fault);
	CHECK(jmmu_raise(0x8C010000) == 0x8C010000 && g_takes.empty() && Sh4cntx.pc == 0x8C010000, "no spin");

	world("an AT edge clears the code cache and the JTC");
	read_from(0x8C005000, 0x8C005000, -1);
	a = jmmu_lookup(0x8C010000);
	CHECK(g_clears == 0, "a clear without an edge");
	CCN_MMUCR.reg_data = 0;
	b = jmmu_lookup(0x8C010000);
	CHECK(g_clears == 1 && g_comp.size() == 2 && slot(0, 0x8C005000).tag == 0xFFFFFFFF, "%d clears, %u compiles", g_clears,
	      (u32)g_comp.size());
	CHECK(jmmu_lookup(0x8C010000) == b && g_clears == 1, "a second clear");
}

static void t_select()
{
	world("the choice installs the JIT's lookup and its cache flush, and takes them back");
	CHECK(g_jit_mmu && sh4_cpu.Run == recSh4_Run && sh4_cpu.ResetCache == jmmu_reset_cache && !g_dispatch.empty() &&
	      g_dispatch.back() == 1, "on");
	CHECK(logged("[fullmmu] MMU JIT:"), "no line");
	jmmu_select(false);
	CHECK(!g_jit_mmu && sh4_cpu.ResetCache == recSh4_ClearCache && g_dispatch.back() == 0, "off");
	sh4_cpu.ResetCache = other_reset_cache;
	jmmu_select(false);
	CHECK(sh4_cpu.ResetCache == other_reset_cache, "off took another engine's flush");
	settings.dynarec.Enable = false;
	CHECK(!jmmu_can_run(), "the interpreter as the menu's engine");
	settings.dynarec.Enable = true;
	CHECK(jmmu_can_run(), "the JIT as the menu's engine");
}

static void t_stats()
{
	world("the [jmmu] line: once a change, the counts of the second");
	jmmu_stats(1.0);
	g_log.clear();
	jmmu_stats(1.0);
	CHECK(!logged("[jmmu]"), "a line with no change");
	jmmu_lookup(0x8C010000);
	jmmu_lookup(0x8C010000);
	jmmu_stats(1.0);
	CHECK(logged("[jmmu] lookups 2, compiles 1"), "the line: %s", g_log.empty() ? "none" : g_log.back().c_str());
	jmmu_select(false);
	g_log.clear();
	jmmu_lookup(0x8C010000);
	jmmu_stats(1.0);
	CHECK(!logged("[jmmu]"), "a line without the MMU JIT");
}

static void t_bm()
{
	g_test = "the MMU blocks of blockmanager.cpp";
	bm_reset_all();
	DynarecCodeEntry* c1 = (DynarecCodeEntry*)(void*)&g_codes[1];
	DynarecCodeEntry* c2 = (DynarecCodeEntry*)(void*)&g_codes[2];
	DynarecCodeEntry* c3 = (DynarecCodeEntry*)(void*)&g_codes[3];
	bm_AddCodeMmu(0x00010000, 0x0C400000, 0, 4, c1);
	bm_AddCodeMmu(0x00010000, 0x0C500000, 0, 0, c2);
	DynarecBlock* b = bm_GetBlockMmu(0x00010000, 0x0C400000, 0);
	CHECK(b && b->code == c1 && b->pgen == 4, "the first");
	b = bm_GetBlockMmu(0x00010000, 0x0C500000, 0);
	CHECK(b && b->code == c2, "the second frame");
	CHECK(!bm_GetBlockMmu(0x00010000, 0x0C400000, 1) && !bm_GetBlockMmu(0x00010004, 0x0C400000, 0), "a wrong key found");
	bm_AddCodeMmu(0x00010000, 0x0C400000, 0, 5, c3);
	b = bm_GetBlockMmu(0x00010000, 0x0C400000, 0);
	CHECK(b && b->code == c3 && b->pgen == 5 && blocks[bm_AddrHash(0x00010000)].size() == 2, "the replacement");
	// A full bucket: the least looked-up block goes.
	bm_reset_all();
	for (u32 i = 0; i < BM_MAX_BLOCKS_PER_BUCKET; i++)
	{
		bm_AddCodeMmu(0x00020000, 0x0C000000 + i * 2, 0, 0, c1);
		bm_GetBlockMmu(0x00020000, 0x0C000000 + i * 2, 0)->lookups = 10 + i;
	}
	bm_GetBlockMmu(0x00020000, 0x0C000000 + 6, 0)->lookups = 1;
	bm_AddCodeMmu(0x00020000, 0x0D000000, 0, 0, c2);
	CHECK(blocks[bm_AddrHash(0x00020000)].size() == BM_MAX_BLOCKS_PER_BUCKET && !bm_GetBlockMmu(0x00020000, 0x0C000006, 0) &&
	      bm_GetBlockMmu(0x00020000, 0x0D000000, 0) && bm_GetBlockMmu(0x00020000, 0x0C000008, 0), "the eviction");
	bm_reset_all();
}

// ---- row MMU Model FAST, the JIT side: owner 64, its table's changes, MMUCR and the TLB arrays ----
static void t_fast()
{
	world("FAST: a slot of the table is owner 64's; ldtlb keeps it, a change of the table drops it");
	g_mmu_fast = true;
	read_from(0x00401000, 0x0C401000, 64);
	read_from(0x8C005000, 0x8C005000, -1);
	const u32 drops = s_js.drops;
	jmmu_utlb_changed(5);
	// No UTLB entry owns a slot: the [jmmu] line's drops would count a drop of nothing.
	CHECK(owned_kept() && p1_kept() && s_js.drops == drops, "ldtlb dropped a FAST slot, or counted a drop");
	u32 g = g_jdgen;
	jmmu_fast_changed();
	CHECK(!owned_kept() && p1_kept() && g_jdgen == g + 1, "the change: owned %d P1 %d gen +%u", owned_kept(), p1_kept(),
	      g_jdgen - g);

	world("FAST: a new ASID drops owner 64's slots");
	g_mmu_fast = true;
	read_from(0x00401000, 0x0C401000, 64);
	g_xl = { 0, 0xFF000000, -1 };
	jmmu_wr32(0xFF000000, 0x77);
	CHECK(!owned_kept(), "the slot stayed");

	world("FAST: MMUCR keeps the slots but for TI (a flush) and a new SV");
	g_mmu_fast = true;
	read_from(0x00401000, 0x0C401000, 64);
	g_xl = { 0, 0xFF000010, -1 };
	g = g_jdgen;
	jmmu_wr32(0xFF000010, 1 | 5 << 10);
	CHECK(owned_kept() && g_jdgen == g && g_faw.size() == 1 && g_faw[0].pa == 0x1F000010 && g_faw[0].v == (1 | 5 << 10),
	      "URC only: owned %d gen +%u, %u hook calls", owned_kept(), g_jdgen - g, (u32)g_faw.size());
	jmmu_wr32(0xFF000010, 1 | MMUCR_TI_BIT);
	CHECK(!owned_kept() && g_fast_flushes == 1 && g_jdgen == g + 1, "TI: owned %d, %d flushes, gen +%u", owned_kept(),
	      g_fast_flushes, g_jdgen - g);
	read_from(0x00401000, 0x0C401000, 64);
	g = g_jdgen;
	g_xl = { 0, 0xFF000010, -1 };
	jmmu_wr32(0xFF000010, 1 | 0x100);
	CHECK(!owned_kept() && g_jdgen == g + 1 && g_fast_flushes == 1, "SV: owned %d gen +%u", owned_kept(), g_jdgen - g);
	read_from(0x00401000, 0x0C401000, 64);
	g_xl = { 0, 0xFF000010, -1 };
	jmmu_wr32(0xFF000010, 1 | 0x100 | 9 << 10);
	CHECK(owned_kept(), "the same SV dropped");

	world("FAST: a TLB array write goes to the table's hook with the value 0");
	g_mmu_fast = true;
	read_from(0x00401000, 0x0C401000, 64);
	g_xl = { 0, 0xF7800300, -1 };
	jmmu_wr16(0xF7800300, 0x1234);
	CHECK(!owned_kept() && g_fast_flushes == 1 && g_faw.size() == 1 && g_faw[0].pa == 0xF7800300 && g_faw[0].v == 0,
	      "owned %d, %d flushes, %u hook calls", owned_kept(), g_fast_flushes, (u32)g_faw.size());

	world("FULL: no table hook; MMUCR drops every translated slot");
	read_from(0x00401000, 0x0C401000, 5);
	g_xl = { 0, 0xFF000010, -1 };
	jmmu_wr32(0xFF000010, 1 | 5 << 10);
	CHECK(!owned_kept() && g_faw.empty(), "owned %d, %u hook calls", owned_kept(), (u32)g_faw.size());
}

// ---- row MMU Block Cache: the fills, their key and the generation's moves ----
static const JDisp& jd(u32 pc) { return g_jdisp[jdisp_index(pc)]; }

static void t_bcache()
{
	world("block cache: a lookup with AT off fills pc, key (mode, ASID), generation and code");
	g_jmmu_bcache = true;
	CCN_MMUCR.reg_data = 0;
	jmmu_select(true);
	set_md(true);
	Sh4cntx.fpscr.full = 1 << 19 | 1;
	CCN_PTEH.reg_data = 0x00400042;
	void* a = jmmu_lookup(0x8C010020);
	CHECK(a && jd(0x8C010020).pc == 0x8C010020 && jd(0x8C010020).key == (1u | 4 | 16 | 0x42 << 8) &&
	      jd(0x8C010020).gen == g_jdgen && jd(0x8C010020).code == (u32)(uintptr_t)a, "entry %08X %08X %u %08X",
	      jd(0x8C010020).pc, jd(0x8C010020).key, jd(0x8C010020).gen, jd(0x8C010020).code);
	CHECK(jmmu_dkey() == jd(0x8C010020).key, "jmmu_dkey %08X", jmmu_dkey());
	CHECK(jmmu_lookup(0x8C010020) == a && jd(0x8C010020).code == (u32)(uintptr_t)a, "the found block's fill");
	CHECK(jdisp_index(0x8C010020) == ((0x8C010020u ^ (0x8C010020u >> 12)) >> 1) % JDISP_N, "the index");

	world("block cache: AT on fills only with MMU Model FAST");
	g_jmmu_bcache = true;
	jmmu_lookup(0x8C010020);
	CHECK(jd(0x8C010020).pc == 0, "FULL with AT on filled");
	g_mmu_fast = true;
	a = jmmu_lookup(0x8C010020);
	CHECK(jd(0x8C010020).pc == 0x8C010020 && jd(0x8C010020).code == (u32)(uintptr_t)a, "FAST did not fill");

	world("block cache: the generation moves at each change of a translation or of the code");
	g_jmmu_bcache = true;
	g_mmu_fast = true;
	u32 g = g_jdgen;
	sh4_cpu.ResetCache();
	CHECK(g_jdgen == g + 1, "an I-cache flush: +%u", g_jdgen - g);
	g_xl = { 0, 0xF0001000, -1 };
	jmmu_wr32(0xF0001000, 0);
	CHECK(g_jdgen == g + 2, "an I-cache array write: +%u", g_jdgen - g);
	jmmu_code_cleared();
	jmmu_fast_changed();
	CHECK(g_jdgen == g + 4, "a code clear and a table change: +%u", g_jdgen - g);
	CCN_MMUCR.reg_data = 0;
	jmmu_lookup(0x8C010000);
	CHECK(g_jdgen == g + 5, "an AT edge: +%u", g_jdgen - g);
	g_xl = { 0, 0xFF000010, -1 };
	jmmu_wr32(0xFF000010, 0x100);
	CHECK(g_jdgen == g + 6, "a new SV: +%u", g_jdgen - g);
	g_xl = { 0, 0xFF000000, -1 };
	jmmu_wr32(0xFF000000, 0x55);
	CHECK(g_jdgen == g + 6, "a new ASID moved it (the key has the ASID): +%u", g_jdgen - g);

	world("block cache: OFF fills nothing; the choice empties the table");
	CCN_MMUCR.reg_data = 0;
	jmmu_select(true);
	jmmu_lookup(0x8C010020);
	CHECK(jd(0x8C010020).pc == 0, "OFF filled");
	g_jmmu_bcache = true;
	jmmu_lookup(0x8C010020);
	jmmu_select(true);
	CHECK(jd(0x8C010020).pc == 0 && jd(0x8C010020).gen == 0 && g_jdgen == 1, "the table after the choice");
}

static void t_time()
{
	world("the ms line: a sampled call counts 16 times; a compile is not in its lookup");
	g_fake_step = 79800;	// 1 ms a read
	s_tick = 0;
	for (int i = 0; i < 16; i++)
		jmmu_lookup(0x8C010000);
	s_tick = 15;
	jmmu_rd32(0x8C005000);
	jmmu_stats(1.0);
	CHECK(logged("[jmmu] ms/s: lookup 16.0, compile 1.0, slow r 16.0 w 0.0, ifb 0.0, exc 0.0 | block cache OFF"),
	      "the line: %s", g_log.empty() ? "none" : g_log.back().c_str());
	g_log.clear();
	s_tick = 15;
	jmmu_wr32(0x8C005000, 1);
	s_tick = 15;
	jmmu_raise(0x8C010000);
	jmmu_stats(1.0);
	CHECK(logged("slow r 0.0 w 16.0, ifb 0.0, exc 16.0"), "the line: %s", g_log.empty() ? "none" : g_log.back().c_str());
	// A sampled lookup that compiles: four reads, the compile's two take one step out.
	g_log.clear();
	s_tick = 15;
	jmmu_lookup(0x8C020000);
	jmmu_stats(1.0);
	CHECK(logged("[jmmu] ms/s: lookup 32.0, compile 1.0,"), "the line: %s", g_log.empty() ? "none" : g_log.back().c_str());
	g_fake_step = 100;
}

int main()
{
	g_ram = (u8*)mmap(0, RAM_SIZE + 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	g_sq = (u8*)mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (g_ram == MAP_FAILED || g_sq == MAP_FAILED || ((uintptr_t)g_ram >> 32))
	{
		printf("FAIL: no RAM below 4 GB\n");
		return 2;
	}
	mem_b.data = g_ram;
	mem_b.size = RAM_SIZE;
	t_cfg();
	t_fill();
	t_drop();
	t_regs();
	t_lookup();
	t_select();
	t_stats();
	t_bm();
	t_fast();
	t_bcache();
	t_time();
	printf("%s: %d runtime checks, %d failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
