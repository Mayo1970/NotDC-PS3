// OPTIMIZATION_PLAN 5.15 B0 PC check: ps3/spu/arm7_spu.h against the real arm7.cpp + arm7_cached.cpp, slice by slice
// on random programs (state, memory writes, register accesses), plus the 45 ARM7DI conformance tests on arm7_spu.
#include "arm7.h"
#include "arm_mem.h"
#include "arm7_selftest_data.h"
#include <vector>

typedef union { u32 I; } reg_pair;
extern reg_pair arm_Reg[47];
extern u32 arm_ArmNextPC;
extern bool N_FLAG, Z_FLAG, C_FLAG, V_FLAG, armIrqEnable, armFiqEnable, Arm7Enabled;
extern int armMode;
void arm_FlushCode();

bool e68k_out;
uint64_t ps3_timebase_hz = 1;
static int g_idle_pref;
extern "C" int get_arm7_idle_skip_preset() { return g_idle_pref; }
void arm_jit_after_init() {}
void arm_jit_on_reset() {}
bool g_arm_spe;   // 5.15 hooks in arm7.cpp, off here
void arm_spe_reset() {}
void arm_spe_flush() {}
bool arm_jit_active() { return false; }
void arm_jit_run(u32) {}
void arm_jit_census_dump(double) {}
double os_GetSeconds() { return 0; }

// ---- memory and register-space model, one per core ----
struct LogRec { u32 kind, addr, val, size; };
static bool g_spu_e68k;
struct Side
{
	u8* mem;
	bool* e68k;   // reference: the global e68k_out it reads
	std::vector<LogRec> log;
	u32 devcnt;
};
static Side g_side[2];   // 0 reference, 1 arm7_spu
static bool g_immutable;  // random programs: writes are logged, not applied, so code never changes under a block

static u32 dev_read(Side& s, u32 a, int sz)
{
	a &= 0x7FFF;
	u32 v;
	if (a == 0x2D00)
		v = 3;                                   // REG_L
	else
		v = (a * 2654435761u ^ (s.devcnt++ * 40503u)) & 0xFFFF;
	s.log.push_back({ 1, a, v, (u32)sz });
	return v;
}

static void dev_write(Side& s, u32 a, u32 v, int sz)
{
	a &= 0x7FFF;
	if (a == 0x2D04 && (v & 1))
		*s.e68k = false;                         // REG_M: the FIQ is accepted
	s.log.push_back({ 2, a, v, (u32)sz });
}

static u32 mem_read(Side& s, int sz, u32 addr)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
		return dev_read(s, addr, sz);
	if (sz == 4)
	{
		if (addr & 3)
		{
			const u32 w = *(u32*)&s.mem[(addr & ~3u) & ARAM_MASK], rot = (addr & 3) * 8;
			return (w >> rot) | (w << (32 - rot));
		}
		return *(u32*)&s.mem[addr & ARAM_MASK];
	}
	if (sz == 2)
		return *(u16*)&s.mem[addr & ARAM_MASK];
	return s.mem[addr & ARAM_MASK];
}

static void mem_write(Side& s, int sz, u32 addr, u32 v)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
	{
		dev_write(s, addr, v, sz);
		return;
	}
	s.log.push_back({ 3, addr, v, (u32)sz });
	if (g_immutable)
		return;
	if (sz == 4)
	{
		if (addr & 3)
			for (int i = 0; i < 4; i++)
				s.mem[(addr + i) & ARAM_MASK] = (u8)(v >> (8 * i));
		else
			*(u32*)&s.mem[addr & ARAM_MASK] = v;
	}
	else if (sz == 2)
		*(u16*)&s.mem[addr & ARAM_MASK] = (u16)v;
	else
		s.mem[addr & ARAM_MASK] = (u8)v;
}

template<int sz, typename T> T fastcall ReadMemArm(u32 addr) { return (T)mem_read(g_side[0], sz, addr); }
template<int sz, typename T> void fastcall WriteMemArm(u32 addr, T data) { mem_write(g_side[0], sz, addr, (u32)data); }
template u8 fastcall ReadMemArm<1, u8>(u32);
template u16 fastcall ReadMemArm<2, u16>(u32);
template u32 fastcall ReadMemArm<4, u32>(u32);
template void fastcall WriteMemArm<1, u8>(u32, u8);
template void fastcall WriteMemArm<2, u16>(u32, u16);
template void fastcall WriteMemArm<4, u32>(u32, u32);

// ---- the core under test ----
static inline uint32_t a7_fetch(uint32_t a) { return *(u32*)&g_side[1].mem[a]; }
static inline uint32_t a7_rd32(uint32_t a) { return mem_read(g_side[1], 4, a); }
static inline uint32_t a7_rd8(uint32_t a) { return mem_read(g_side[1], 1, a) & 0xFF; }
static inline void a7_wr32(uint32_t a, uint32_t v) { mem_write(g_side[1], 4, a, v); }
static inline void a7_wr8(uint32_t a, uint32_t v) { mem_write(g_side[1], 1, a, v & 0xFF); }
#define A7_E68K_OUT (g_spu_e68k)
#ifndef A7_MAP_N
#define A7_MAP_N (1u << 19)
#endif
static bool g_trace;
static u64 g_cov[8];   // coverage: instructions, failed conditions, dispatches, PC writes, idle branches, caps, shifts by reg
static inline void a7_cov(int kind, u32 pc, u32 x, int ticks)
{
	if (g_trace)
		printf("    %c %08X %08X t=%d\n", kind, pc, x, ticks);
	if (kind == 'I')
	{
		g_cov[0]++;
		if (((x >> 26) & 3) == 0 && !(x & 0x02000000) && (x & 0x10) && (x & 0x0FC000F0) != 0x90) g_cov[6]++;
		if (((x >> 12) & 0xF) == 15) g_cov[3]++;
	}
	else if (kind == 'F')
		g_cov[1]++;
	else
	{
		g_cov[2]++;
		if (x & 2) g_cov[4]++;
		if (x & 1) g_cov[5]++;
	}
}
#define A7_TRACE(kind, pc, op, ticks) a7_cov(kind, pc, op, ticks)
#include "../../ps3/spu/arm7_spu.h"

static A7Cpu g_cpu, g_snap;

// ---- random programs ----
static u64 g_rng;
static u32 rnd()
{
	g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
	return (u32)g_rng;
}
static u32 pick(u32 n) { return rnd() % n; }

static u32 cond_field()
{
	const u32 r = pick(100);
	if (r < 60) return 0xE;
	if (r < 98) return pick(15);
	return 0xF;
}

static u32 reg_field() { return pick(100) < 6 ? 15 : pick(15); }

static const u32 k_modes[7] = { 0x10, 0x11, 0x12, 0x13, 0x17, 0x1b, 0x1f };

// One instruction; nocontrol: no branch, PC write, SWI or undefined (for straight-line runs).
static u32 gen_insn(bool nocontrol)
{
	const u32 c = cond_field() << 28;
	for (;;)
	{
		const u32 k = pick(100);
		if (k < 30)   // data processing
		{
			u32 op = c | (pick(16) << 21) | (pick(2) << 20) | (reg_field() << 16) | (reg_field() << 12);
			if (pick(2))
				op |= 0x02000000 | (pick(16) << 8) | pick(256);
			else
			{
				op |= reg_field();
				const u32 f = pick(4);
				if (f == 0) {}                                            // plain Rm
				else if (f < 3) op |= (pick(32) << 7) | (pick(4) << 5);   // shift by immediate
				else op |= (reg_field() << 8) | (pick(4) << 5) | 0x10;    // shift by register
			}
			if (nocontrol && ((op >> 12) & 0xF) == 15)
				continue;
			return op;
		}
		if (k < 52)   // LDR / STR
		{
			u32 op = c | 0x04000000 | (pick(32) << 20) | (reg_field() << 16) | (reg_field() << 12);
			if (pick(2))
				op |= 0x02000000 | (pick(32) << 7) | (pick(4) << 5) | reg_field();
			else
				op |= pick(4096);
			if (nocontrol && (op & 0x00100000) && ((op >> 12) & 0xF) == 15)
				continue;
			return op;
		}
		if (k < 58)   // LDM / STM
		{
			u32 list = rnd() & 0xFFFF;
			if (!list) list = 1;
			u32 op = c | 0x08000000 | (pick(32) << 20) | (reg_field() << 16) | list;
			if (nocontrol && (op & 0x00100000) && (op & 0x8000))
				continue;
			return op;
		}
		if (k < 63)   // MUL / MLA
			return c | 0x00000090 | (pick(4) << 20) | (reg_field() << 16) | (reg_field() << 12) | (reg_field() << 8) | reg_field();
		if (k < 65)   // SWP / SWPB
			return c | 0x01000090 | (pick(2) << 22) | (reg_field() << 16) | (reg_field() << 12) | reg_field();
		if (k < 70)   // MRS / MSR
		{
			const u32 spsr = pick(2) << 22;
			if (pick(3) == 0)
				return c | 0x010F0000 | spsr | (reg_field() << 12);
			const u32 fields = pick(16) << 16;
			if (pick(2))
			{
				const u32 v = (rnd() & 0xC0) | k_modes[pick(7)];
				return c | 0x0320F000 | spsr | fields | v;
			}
			return c | 0x0120F000 | spsr | fields | reg_field();
		}
		if (nocontrol)
			continue;
		if (k < 90)   // B / BL, mostly short
		{
			int32_t off = pick(100) < 20 ? -2 : (int32_t)pick(40) - 24;
			if (pick(10) == 0) off = (int32_t)(rnd() & 0xFFFF) - 0x8000;
			return c | 0x0A000000 | (pick(4) == 0 ? 0x01000000 : 0) | ((u32)off & 0x00FFFFFF);
		}
		if (k < 93) return c | 0x0F000000 | (rnd() & 0xFFFFFF);   // SWI
		if (k < 95) return c | 0x0C000000 | (rnd() & 0x03FFFFFF);  // coprocessor
		return rnd();
	}
}

static void gen_program(u8* mem)
{
	// Code: the first 64 KB, with straight-line runs (block cap) and store-then-spin loops (idle skip block rules).
	u32* w = (u32*)mem;
	u32 i = 0;
	while (i < 16384)
	{
		const u32 r = pick(1000);
		if (r < 3 && i + 160 < 16384)
		{
			for (u32 n = 140 + pick(20); n; n--)
				w[i++] = gen_insn(true);
		}
		else if (r < 30 && i + 8 < 16384)
		{
			w[i++] = 0xE5810000 | (pick(8) << 12) | (pick(8) << 16) | pick(64);   // STR before the loop head
			const u32 head = i;
			w[i++] = 0xE5900000 | (pick(4) << 12) | (pick(8) << 16) | (pick(16) << 2);  // LDR
			if (pick(2)) w[i++] = 0xE2800001 | (pick(4) << 12) | (pick(4) << 16);      // ADD (maybe loop-carried)
			w[i++] = 0xE3500000 | (pick(4) << 16) | pick(4);                           // CMP
			w[i] = 0x1A000000 | ((u32)((int32_t)head - (int32_t)i - 2) & 0x00FFFFFF);  // BNE head
			i++;
		}
		else
			w[i++] = gen_insn(false);
	}
	// Vectors return now and then (undefined/SWI: MOVS pc, lr; FIQ: SUBS pc, lr, #4), so runs leave them.
	if (pick(4)) w[1] = 0xE1B0F00E;
	if (pick(4)) w[2] = 0xE1B0F00E;
	if (pick(4)) w[7] = 0xE25EF004;
	// Data: code-like words, small numbers and noise.
	for (u32 j = 16384; j < ARAM_SIZE / 4; j++)
	{
		const u32 r = pick(4);
		w[j] = r == 0 ? gen_insn(false) : r == 1 ? pick(0x10000) : r == 2 ? (pick(0x4000) << 2) : rnd();
	}
}

// ---- compare ----
static u64 g_slices, g_fails;
static bool compare(const char* what, u32 prog, u32 slice)
{
	char buf[256];
	buf[0] = 0;
	for (int i = 0; i < 47 && !buf[0]; i++)
		if (arm_Reg[i].I != g_cpu.r[i])
			snprintf(buf, sizeof(buf), "r[%d] ref %08X spu %08X", i, arm_Reg[i].I, g_cpu.r[i]);
	if (!buf[0] && arm_ArmNextPC != g_cpu.next_pc)
		snprintf(buf, sizeof(buf), "next_pc ref %08X spu %08X", arm_ArmNextPC, g_cpu.next_pc);
	if (!buf[0] && (N_FLAG != g_cpu.n || Z_FLAG != g_cpu.z || C_FLAG != g_cpu.c || V_FLAG != g_cpu.v))
		snprintf(buf, sizeof(buf), "flags ref %d%d%d%d spu %d%d%d%d", N_FLAG, Z_FLAG, C_FLAG, V_FLAG, g_cpu.n, g_cpu.z,
		         g_cpu.c, g_cpu.v);
	if (!buf[0] && (armIrqEnable != g_cpu.irq_en || armFiqEnable != g_cpu.fiq_en))
		snprintf(buf, sizeof(buf), "irq/fiq ref %d%d spu %d%d", armIrqEnable, armFiqEnable, g_cpu.irq_en, g_cpu.fiq_en);
	if (!buf[0] && armMode != g_cpu.mode)
		snprintf(buf, sizeof(buf), "mode ref %X spu %X", armMode, g_cpu.mode);
	if (!buf[0] && Arm7Enabled != (bool)g_cpu.enabled)
		snprintf(buf, sizeof(buf), "enabled ref %d spu %d", Arm7Enabled, g_cpu.enabled);
	if (!buf[0] && *g_side[0].e68k != *g_side[1].e68k)
		snprintf(buf, sizeof(buf), "e68k ref %d spu %d", *g_side[0].e68k, *g_side[1].e68k);
	if (!buf[0])
	{
		const std::vector<LogRec>& a = g_side[0].log;
		const std::vector<LogRec>& b = g_side[1].log;
		const size_t n = a.size() < b.size() ? a.size() : b.size();
		for (size_t i = 0; i < n && !buf[0]; i++)
			if (a[i].kind != b[i].kind || a[i].addr != b[i].addr || a[i].val != b[i].val || a[i].size != b[i].size)
				snprintf(buf, sizeof(buf), "access #%zu ref k%u %06X=%08X/%u spu k%u %06X=%08X/%u", i, a[i].kind, a[i].addr,
				         a[i].val, a[i].size, b[i].kind, b[i].addr, b[i].val, b[i].size);
		if (!buf[0] && a.size() != b.size())
			snprintf(buf, sizeof(buf), "accesses ref %zu spu %zu", a.size(), b.size());
	}
	g_side[0].log.clear();
	g_side[1].log.clear();
	if (!buf[0])
		return true;
	if (g_fails++ < 10)
		printf("MISMATCH %s prog %u slice %u: %s (pc ref %08X)\n", what, prog, slice, buf, arm_ArmNextPC);
	return false;
}

static void set_both_reg(int i, u32 v)
{
	arm_Reg[i].I = v;
	g_cpu.r[i] = v;
}

static bool g_step;   // rerun of a failing program: one instruction per slice, first mismatch printed with its opcode

static bool run_program(u32 prog)
{
	gen_program(g_side[0].mem);
	memcpy(g_side[1].mem, g_side[0].mem, ARAM_SIZE);
	for (int i = 0; i < 2; i++)
	{
		g_side[i].devcnt = 0;
		g_side[i].log.clear();
	}
	g_idle_pref = pick(2);
	g_cpu.idle_pref = (uint8_t)g_idle_pref;
	arm_SetEnabled(false);
	arm_SetEnabled(true);
	a7_set_enabled(&g_cpu, 0);
	a7_set_enabled(&g_cpu, 1);
	for (int i = 0; i < 15; i++)
		set_both_reg(i, pick(100) < 5 ? rnd() : (pick(100) < 70 ? pick(0x20000) : pick(ARAM_SIZE)));
	const u32 start = pick(16384) * 4;
	arm_ArmNextPC = start;
	g_cpu.next_pc = start;
	set_both_reg(15, start + 4);
	if (pick(2))
	{
		arm_Reg[16].I &= ~0x40u;
		armFiqEnable = true;
		g_cpu.r[16] &= ~0x40u;
		g_cpu.fiq_en = 1;
	}
	*g_side[0].e68k = *g_side[1].e68k = false;
	for (u32 s = 0; s < (g_step ? 20000u : 400u); s++)
	{
		if (g_step)
		{
			const u32 pc = arm_ArmNextPC, op = *(u32*)&g_side[0].mem[pc & ARAM_MASK];
			arm_Run(1);
			a7_run(&g_cpu, 1);
			if (!compare("step", prog, s))
			{
				printf("  at pc %08X op %08X (mode %X, flags ref %d%d%d%d)\n", pc, op, armMode, N_FLAG, Z_FLAG, C_FLAG,
				       V_FLAG);
				return false;
			}
			continue;
		}
		const u32 r = pick(1000);
		if (r < 40)
			*g_side[0].e68k = *g_side[1].e68k = true;
		else if (r < 70)
		{
			// A jump elsewhere, identical on both: next instruction and its prefetch PC.
			const u32 pc = pick(16384) * 4;
			arm_ArmNextPC = pc;
			g_cpu.next_pc = pc;
			set_both_reg(15, pc + 4);
		}
		else if (r < 150)
			*g_side[0].e68k = *g_side[1].e68k = false;
		else if (r < 155)
		{
			g_idle_pref = pick(2);
			g_cpu.idle_pref = (uint8_t)g_idle_pref;
			arm_FlushCode();
			a7_flush(&g_cpu);
		}
		else if (r < 157)
		{
			const bool en = pick(2);
			arm_SetEnabled(en);
			a7_set_enabled(&g_cpu, en);
		}
		const u32 budget = pick(10) == 0 ? pick(6) : pick(400);
		memcpy(&g_snap, &g_cpu, sizeof(g_cpu));
		const bool e68k0 = g_spu_e68k;
		arm_Run(budget);
		a7_run(&g_cpu, budget);
		g_slices++;
		if (!compare("random", prog, s))
		{
			if (g_fails <= 3)
			{
				// Replay the slice on arm7_spu from its start state, traced; the reference only shows where it ended.
				printf("  replay: budget %u, start pc %08X mode %X fiq_en %d e68k %d idle_safe %d\n", budget, g_snap.next_pc,
				       g_snap.mode, g_snap.fiq_en, e68k0, g_snap.idle_safe);
				const bool e_now = g_spu_e68k;
				memcpy(&g_cpu, &g_snap, sizeof(g_cpu));
				g_spu_e68k = e68k0;
				g_trace = true;
				a7_run(&g_cpu, budget);
				g_trace = false;
				g_spu_e68k = e_now;
				printf("  ref end: next_pc %08X mode %X | spu end: next_pc %08X mode %X\n", arm_ArmNextPC, armMode,
				       g_cpu.next_pc, g_cpu.mode);
			}
			return false;
		}
	}
	return true;
}

// ---- conformance tests on arm7_spu (flat memory, as nativetest) ----
static int run_selftests()
{
	int failed = 0;
	g_immutable = false;
	for (int t = 0; t < arm7_selftest_count; t++)
	{
		const Arm7SelfTest& st = arm7_selftests[t];
		memset(g_side[1].mem, 0, ARAM_SIZE);
		memcpy(g_side[1].mem, st.data, st.size);
		g_spu_e68k = false;
		a7_set_enabled(&g_cpu, 0);
		a7_set_enabled(&g_cpu, 1);
		g_cpu.next_pc = 0;
		g_cpu.r[15] = 4;
		u32 n = 0;
		while (*(u32*)&g_side[1].mem[g_cpu.next_pc & ARAM_MASK] != 0xDEADBEEFu && n < 100000)
		{
			a7_run(&g_cpu, 1);
			n++;
		}
		g_side[1].log.clear();
		if (n >= 100000 || (g_cpu.r[1] & 0x3FFFFFFFu))
		{
			failed++;
			printf("SELFTEST FAIL %-10s r1=%08X%s\n", st.name, g_cpu.r[1], n >= 100000 ? " (timeout)" : "");
		}
	}
	printf("selftest: %d / %d passed\n", arm7_selftest_count - failed, arm7_selftest_count);
	return failed;
}

int main(int argc, char** argv)
{
	const u32 progs = argc > 1 ? (u32)strtoul(argv[1], 0, 10) : 200;
	g_rng = argc > 2 ? strtoull(argv[2], 0, 10) * 2654435761ull + 7 : 88172645463325252ull;
	g_side[0].e68k = &e68k_out;
	g_side[1].e68k = &g_spu_e68k;
	g_side[0].mem = (u8*)calloc(1, ARAM_SIZE);
	g_side[1].mem = (u8*)calloc(1, ARAM_SIZE);
	arm_Init();
	a7_init(&g_cpu);
	int bad = run_selftests();
	g_immutable = true;
	u32 okp = 0;
	for (u32 p = 0; p < progs; p++)
	{
		const u64 seed = g_rng;
		if (run_program(p))
		{
			okp++;
			continue;
		}
		if (g_fails <= 10)
		{
			const u64 after = g_rng;
			g_rng = seed;
			g_step = true;
			run_program(p);
			g_step = false;
			g_rng = after;
		}
	}
	printf("random: %u / %u programs, %llu slices, decodes %u, evicts %u, fiqs %u\n", okp, progs,
	       (unsigned long long)g_slices, g_cpu.decodes, g_cpu.evicts, g_cpu.fiqs);
	printf("coverage: %llu instructions, %llu failed conditions, %llu dispatches, %llu Rd=15, %llu idle branches,"
	       " %llu cap rehomes, %llu shifts by register\n", (unsigned long long)g_cov[0], (unsigned long long)g_cov[1],
	       (unsigned long long)g_cov[2], (unsigned long long)g_cov[3], (unsigned long long)g_cov[4],
	       (unsigned long long)g_cov[5], (unsigned long long)g_cov[6]);
	bad += (int)(progs - okp);
	printf("%s\n", bad ? "FAIL" : "PASS");
	return bad ? 1 : 0;
}
