// OPTIMIZATION_PLAN 5.15: ARM7DI interpreter for the SPU with arm7_cached.cpp's results, ticks, slice exits and FIQ
// points. Portable C; the includer defines a7_fetch/rd32/rd8/wr32/wr8 (ReadMemArm rules), A7_E68K_OUT and A7_MAP_N.
#pragma once
#include <stdint.h>
#include <string.h>
#include "../../plugs/vbaARM/arm7_p49.h"

#define A7_ARAM_MASK 0x1FFFFFu
#define A7_MAX_BLOCK 128u   // instructions per block, as MAX_BLOCK_UOPS
#ifndef A7_TRACE
#define A7_TRACE(kind, pc, op, ticks)   // PC harness: 'D' dispatch, 'I' instruction, 'F' failed condition
#endif

enum
{
	A7_R13_IRQ = 18, A7_R14_IRQ = 19, A7_SPSR_IRQ = 20,
	A7_R13_USR = 26, A7_R14_USR = 27,
	A7_R13_SVC = 28, A7_R14_SVC = 29, A7_SPSR_SVC = 30,
	A7_R13_ABT = 31, A7_R14_ABT = 32, A7_SPSR_ABT = 33,
	A7_R13_UND = 34, A7_R14_UND = 35, A7_SPSR_UND = 36,
	A7_R8_FIQ = 37, A7_R9_FIQ = 38, A7_R10_FIQ = 39, A7_R11_FIQ = 40, A7_R12_FIQ = 41,
	A7_R13_FIQ = 42, A7_R14_FIQ = 43, A7_SPSR_FIQ = 44,
	A7_R_COUT = 45, A7_R_SHIFT = 46,   // arm7_cached's shift-uop scratch: kept, a stale R_COUT is read as there
};

// Block meta: the ARAM address of the block's last instruction, plus these flags in the low bits.
#define A7_META_CAP  1u   // the block ends on the instruction cap: a rehome follows its last instruction
#define A7_META_IDLE 2u   // its closing B is an idle self-loop (+100 ticks)
// Map entry, one word: (word index in ARAM + 1) << 12 | words to the block's end << 2 | flags; 0 = empty.

typedef struct
{
	uint32_t r[47];          // arm_Reg: r0-r15, 16 CPSR, 17 SPSR, banked copies, 45/46 scratch
	uint32_t next_pc;        // arm_ArmNextPC
	uint8_t n, z, c, v;
	uint8_t irq_en, fiq_en;
	uint8_t enabled;
	uint8_t idle_pref;       // ARM7 Idle Skip SAFE wanted (the integrator keeps it current)
	uint8_t idle_safe;       // latched at the first run after a flush, as s_idle_safe
	uint8_t dirty;           // block map flushed: latch idle_safe on the next run
	uint8_t running;         // a7_run is running: an AICA write comes from the ARM7
	uint8_t race_req;        // AICA Race Fix: the store just made raised the SH4 line (a7_race)
	uint8_t reset_svc;       // ARM7 Reset Fix: a reset enters SVC (0x13), not System (0x1F); the integrator sets it
	uint8_t race_done;       // AICA Race Fix: this slice was extended already (once per slice)
	uint8_t pad[2];
	int32_t mode;            // armMode
	uint32_t decodes, evicts, fiqs;   // stats
	int32_t budget;          // the running slice's ticks
	uint32_t races;          // AICA Race Fix: slices extended
	A7P49 p49;               // P49 census (log only): what a7_decode met
	uint32_t map[A7_MAP_N];
} A7Cpu;

static inline uint32_t a7_popc8(uint32_t v)
{
	v = v - ((v >> 1) & 0x55);
	v = (v & 0x33) + ((v >> 2) & 0x33);
	return (v + (v >> 4)) & 0x0F;
}

// ── arm7.cpp state helpers, verbatim semantics ──────────────────────────────

static void a7_update_cpsr(A7Cpu* a)
{
	uint32_t cpsr = a->r[16] & 0x40;
	if (a->n) cpsr |= 0x80000000u;
	if (a->z) cpsr |= 0x40000000u;
	if (a->c) cpsr |= 0x20000000u;
	if (a->v) cpsr |= 0x10000000u;
	if (!a->fiq_en) cpsr |= 0x40;
	if (!a->irq_en) cpsr |= 0x80;
	cpsr |= (uint32_t)(a->mode & 0x1F);
	a->r[16] = cpsr;
}

// CPUUpdateFlags: its break-loop test reads IF/IE/IME, which are always 0 here.
static void a7_update_flags(A7Cpu* a)
{
	const uint32_t cpsr = a->r[16];
	a->n = (cpsr & 0x80000000u) != 0;
	a->z = (cpsr & 0x40000000u) != 0;
	a->c = (cpsr & 0x20000000u) != 0;
	a->v = (cpsr & 0x10000000u) != 0;
	a->irq_en = (cpsr & 0x80) == 0;
	a->fiq_en = (cpsr & 0x40) == 0;
}

static inline void a7_swap(uint32_t* x, uint32_t* y)
{
	const uint32_t t = *y;
	*y = *x;
	*x = t;
}

static void a7_switch_mode(A7Cpu* a, int mode, int save_state)
{
	uint32_t* r = a->r;
	a7_update_cpsr(a);
	switch (a->mode)
	{
	case 0x10: case 0x1F:
		r[A7_R13_USR] = r[13]; r[A7_R14_USR] = r[14]; r[17] = r[16];
		break;
	case 0x11:
		a7_swap(&r[A7_R8_FIQ], &r[8]); a7_swap(&r[A7_R9_FIQ], &r[9]); a7_swap(&r[A7_R10_FIQ], &r[10]);
		a7_swap(&r[A7_R11_FIQ], &r[11]); a7_swap(&r[A7_R12_FIQ], &r[12]);
		r[A7_R13_FIQ] = r[13]; r[A7_R14_FIQ] = r[14]; r[A7_SPSR_FIQ] = r[17];
		break;
	case 0x12: r[A7_R13_IRQ] = r[13]; r[A7_R14_IRQ] = r[14]; r[A7_SPSR_IRQ] = r[17]; break;
	case 0x13: r[A7_R13_SVC] = r[13]; r[A7_R14_SVC] = r[14]; r[A7_SPSR_SVC] = r[17]; break;
	case 0x17: r[A7_R13_ABT] = r[13]; r[A7_R14_ABT] = r[14]; r[A7_SPSR_ABT] = r[17]; break;
	case 0x1b: r[A7_R13_UND] = r[13]; r[A7_R14_UND] = r[14]; r[A7_SPSR_UND] = r[17]; break;
	}
	const uint32_t cpsr = r[16], spsr = r[17];
	switch (mode)
	{
	case 0x10: case 0x1F:
		r[13] = r[A7_R13_USR]; r[14] = r[A7_R14_USR]; r[16] = spsr;
		break;
	case 0x11:
		a7_swap(&r[8], &r[A7_R8_FIQ]); a7_swap(&r[9], &r[A7_R9_FIQ]); a7_swap(&r[10], &r[A7_R10_FIQ]);
		a7_swap(&r[11], &r[A7_R11_FIQ]); a7_swap(&r[12], &r[A7_R12_FIQ]);
		r[13] = r[A7_R13_FIQ]; r[14] = r[A7_R14_FIQ];
		r[17] = save_state ? cpsr : r[A7_SPSR_FIQ];
		break;
	case 0x12:
		r[13] = r[A7_R13_IRQ]; r[14] = r[A7_R14_IRQ]; r[16] = spsr;
		r[17] = save_state ? cpsr : r[A7_SPSR_IRQ];
		break;
	case 0x13:
		r[13] = r[A7_R13_SVC]; r[14] = r[A7_R14_SVC]; r[16] = spsr;
		r[17] = save_state ? cpsr : r[A7_SPSR_SVC];
		break;
	case 0x17:
		r[13] = r[A7_R13_ABT]; r[14] = r[A7_R14_ABT]; r[16] = spsr;
		r[17] = save_state ? cpsr : r[A7_SPSR_ABT];
		break;
	case 0x1b:
		r[13] = r[A7_R13_UND]; r[14] = r[A7_R14_UND]; r[16] = spsr;
		r[17] = save_state ? cpsr : r[A7_SPSR_UND];
		break;
	default:
		break;   // arm7.cpp prints "Unsupported ARM mode" and goes on
	}
	a->mode = mode;
	a7_update_flags(a);
	a7_update_cpsr(a);
}

static void a7_swi(A7Cpu* a)
{
	const uint32_t pc = a->r[15];
	a7_switch_mode(a, 0x13, 1);
	a->r[14] = pc - 4;
	a->r[15] = 0x08;
	a->irq_en = 0;
	a->next_pc = 0x08;
	a->r[15] += 4;
}

static void a7_undefined(A7Cpu* a)
{
	const uint32_t pc = a->r[15];
	a7_switch_mode(a, 0x1b, 1);
	a->r[14] = pc - 4;
	a->r[15] = 0x04;
	a->irq_en = 0;
	a->next_pc = 0x04;
	a->r[15] += 4;
}

static void a7_fiq(A7Cpu* a)
{
	const uint32_t pc = a->r[15];
	a->fiqs++;
	a7_switch_mode(a, 0x11, 1);
	a->r[14] = pc;
	a->r[15] = 0x1c;
	a->irq_en = 0;
	a->fiq_en = 0;
	a->next_pc = a->r[15];
	a->r[15] += 4;
}

static void a7_flush(A7Cpu* a)
{
	memset(a->map, 0, sizeof(a->map));
	a->dirty = 1;
}

// arm_Reset() plus the cached engine's table flush.
static void a7_reset(A7Cpu* a)
{
	a->enabled = 0;
	memset(a->r, 0, sizeof(a->r));
	a->mode = a->reset_svc ? 0x13 : 0x1F;
	a->r[13] = 0x03007F00u;
	a->r[15] = 0;
	a->r[16] = 0;
	a->r[A7_R13_IRQ] = 0x03007FA0u;
	a->r[A7_R13_SVC] = 0x03007FE0u;
	a->irq_en = 1;
	a->fiq_en = 0;
	a->n = a->z = a->c = a->v = 0;
	a->r[16] |= 0x40;
	a7_update_cpsr(a);
	a->next_pc = a->r[15];
	a->r[15] += 4;
	a7_flush(a);
}

static void a7_set_enabled(A7Cpu* a, int enabled)
{
	if (!a->enabled && enabled)
		a7_reset(a);
	a->enabled = (uint8_t)(enabled != 0);
}

// reset_svc: ARM7 Reset Fix, kept by every later a7_reset.
static void a7_init(A7Cpu* a, int reset_svc)
{
	memset(a, 0, sizeof(*a));
	a->reset_svc = (uint8_t)(reset_svc != 0);
	a7_reset(a);
}

// AICA Race Fix (Flycast avoidRaceCondition): a store that left an SH4 interrupt pending keeps 50 ticks of its slice
// after it. The integrator's write path calls a7_race; one extension per slice, so a raising loop still ends it.
static inline void a7_race(A7Cpu* a)
{
	if (a->running && !a->race_done)
		a->race_req = 1;
}

static void a7_race_clamp(A7Cpu* a, int32_t* ticks)
{
	a->race_req = 0;
	const int32_t lim = a->budget - 50;
	if (*ticks > lim)
	{
		*ticks = lim;
		a->race_done = 1;
		a->races++;
	}
}
#define A7_RACE(a, ticks) do { if (__builtin_expect((a)->race_req, 0)) a7_race_clamp((a), (ticks)); } while (0)

// ── Block map: the arm7_cached block a dispatch to this address lands in ────

// Arm7IdleScan (plugs/vbaARM/arm7_idle.h) in C.
typedef struct { uint32_t reads; int ok; } A7Scan;

static inline void a7_scan_insn(A7Scan* s, uint32_t op)
{
	uint32_t src = 0, dst = 0;
	const uint32_t fam = (op >> 26) & 3;
	if (fam == 1)
	{
		src = 1u << ((op >> 16) & 0xF);
		if (op & 0x02000000)
			src |= 1u << (op & 0xF);
		if (op & 0x00100000)
		{
			dst = 1u << ((op >> 12) & 0xF);
			if (!(op & 0x01000000) || (op & 0x00200000))
				s->ok = 0;
		}
	}
	else if (fam == 0)
	{
		if ((op & 0x0FC000F0) == 0x00000090)
		{
			src = (1u << (op & 0xF)) | (1u << ((op >> 8) & 0xF));
			if (op & 0x00200000)
				src |= 1u << ((op >> 12) & 0xF);
			dst = 1u << ((op >> 16) & 0xF);
		}
		else if ((op & 0x0FB00FF0) == 0x01000090)
			s->ok = 0;
		else if (((op >> 23) & 3) == 2 && !(op & 0x00100000))
		{
			if (op & 0x00200000)
				s->ok = 0;
			else
				dst = 1u << ((op >> 12) & 0xF);
		}
		else
		{
			const uint32_t op4 = (op >> 21) & 0xF;
			if (op4 != 0xD && op4 != 0xF)
				src |= 1u << ((op >> 16) & 0xF);
			if (!(op & 0x02000000))
			{
				src |= 1u << (op & 0xF);
				if (op & 0x10)
					src |= 1u << ((op >> 8) & 0xF);
			}
			if (op4 < 0x8 || op4 > 0xB)
				dst = 1u << ((op >> 12) & 0xF);
		}
	}
	else if (fam == 2 && !(op & 0x02000000))
		s->ok = 0;
	s->reads |= src & 0x7FFF;
	if (dst & s->reads)
		s->ok = 0;
}

// The decoder's block rules (lbl_unknown_block): which instruction ends the block, which make an idle loop unsafe.
static uint32_t a7_decode(A7Cpu* a, uint32_t start)
{
	A7Scan scan = { 0, 1 };
	int safe = 1;
	uint32_t pc = start, n = 0, meta;
	a->decodes++;
	for (;;)
	{
		const uint32_t op = a7_fetch(pc & A7_ARAM_MASK);
		const uint32_t fam = (op >> 26) & 3;
		int last = 0;
		a7_scan_insn(&scan, op);
		a7_p49_scan(&a->p49, pc, op);
		if (fam == 1)
		{
			if (!(op & 0x00100000))
				safe = 0;
			else if (((op >> 12) & 0xF) == 15)
				last = 1;
		}
		else if (fam == 0)
		{
			if ((op & 0x0FC000F0) == 0x00000090)
				;   // MUL / MLA
			else if ((op & 0x0FB00FF0) == 0x01000090)
				safe = 0;   // SWP
			else if (((op >> 23) & 3) == 2 && !(op & 0x00100000))
			{
				if (op & 0x00200000)
					safe = 0;   // MSR
			}
			else
			{
				const uint32_t op4 = (op >> 21) & 0xF;
				if (((op >> 12) & 0xF) == 15 && !(op4 >= 0x8 && op4 <= 0xB))
					last = 1;
			}
		}
		else if (fam == 2)
		{
			if (!(op & 0x02000000))
			{
				safe = 0;   // LDM / STM
				if ((op & 0x00100000) && (op & 0x8000))
					last = 1;
			}
			else
			{
				meta = pc;
				if (!(op & 0x01000000))
				{
					int32_t off = (int32_t)(op << 8) >> 6;
					const uint32_t target = pc + 8 + (uint32_t)off;
					if (safe && (!a->idle_safe || scan.ok) && target >= start && target <= pc)
						meta |= A7_META_IDLE;
				}
				break;
			}
		}
		else
			last = 1;   // SWI, coprocessor
		if (last)
		{
			meta = pc;
			break;
		}
		if (++n >= A7_MAX_BLOCK)
		{
			meta = pc | A7_META_CAP;
			break;
		}
		pc += 4;
	}
	// Keyed by the word within ARAM, as ARM7_ENTRYPOINTS: a dispatch to an alias lands in the same block.
	meta = (meta & ~3u & A7_ARAM_MASK) | (meta & 3u);
	uint32_t p = start & A7_ARAM_MASK & ~3u;
	for (uint32_t left = ((meta - p) & A7_ARAM_MASK) >> 2;; left--)
	{
		const uint32_t w = p >> 2, i = w & (A7_MAP_N - 1);
		if (a->map[i] && (a->map[i] >> 12) != w + 1)
			a->evicts++;
		a->map[i] = ((w + 1) << 12) | (left << 2) | (meta & 3u);
		if (!left)
			break;
		p = (p + 4) & A7_ARAM_MASK;
	}
	return meta;
}

static inline uint32_t a7_lookup(A7Cpu* a, uint32_t pc)
{
	const uint32_t m = pc & A7_ARAM_MASK & ~3u, w = m >> 2, e = a->map[w & (A7_MAP_N - 1)];
	if ((e >> 12) == w + 1)
		return ((m + (((e >> 2) & 0x7Fu) << 2)) & A7_ARAM_MASK) | (e & 3u);
	return a7_decode(a, pc);
}

// ── Instruction helpers (arm7_cached.cpp / arm7_cached_gen.inc semantics) ──

#define A7_NEG(i) ((i) >> 31)
#define A7_POS(i) ((~(i)) >> 31)

static inline void a7_add_flags(A7Cpu* a, uint32_t lhs, uint32_t rhs, uint32_t res)
{
	a->z = res == 0;
	a->n = (uint8_t)A7_NEG(res);
	a->c = ((A7_NEG(lhs) & A7_NEG(rhs)) | (A7_NEG(lhs) & A7_POS(res)) | (A7_NEG(rhs) & A7_POS(res))) != 0;
	a->v = ((A7_NEG(lhs) & A7_NEG(rhs) & A7_POS(res)) | (A7_POS(lhs) & A7_POS(rhs) & A7_NEG(res))) != 0;
}

static inline void a7_sub_flags(A7Cpu* a, uint32_t lhs, uint32_t rhs, uint32_t res)
{
	a->z = res == 0;
	a->n = (uint8_t)A7_NEG(res);
	a->c = ((A7_NEG(lhs) & A7_POS(rhs)) | (A7_NEG(lhs) & A7_POS(res)) | (A7_POS(rhs) & A7_POS(res))) != 0;
	a->v = ((A7_NEG(lhs) & A7_POS(rhs) & A7_POS(res)) | (A7_POS(lhs) & A7_NEG(rhs) & A7_NEG(res))) != 0;
}

static inline int a7_cond(const A7Cpu* a, uint32_t cond)
{
	switch (cond)
	{
	case 0x0: return a->z;
	case 0x1: return !a->z;
	case 0x2: return a->c;
	case 0x3: return !a->c;
	case 0x4: return a->n;
	case 0x5: return !a->n;
	case 0x6: return a->v;
	case 0x7: return !a->v;
	case 0x8: return a->c && !a->z;
	case 0x9: return !a->c || a->z;
	case 0xA: return a->n == a->v;
	case 0xB: return a->n != a->v;
	case 0xC: return !a->z && a->n == a->v;
	case 0xD: return a->z || a->n != a->v;
	case 0xE: return 1;
	default: return 0;   // NV
	}
}

// The shift uop: operand2 into r[R_SHIFT], the carry into r[R_COUT] only where lbl_shc_<form> writes it.
static inline void a7_shift(A7Cpu* a, uint32_t op, uint32_t pc, int carry)
{
	uint32_t* r = a->r;
	const uint32_t rm = op & 0xF, byreg = (op >> 4) & 1, type = (op >> 5) & 3;
	if (rm == 15)
		r[15] = pc + (byreg ? 12 : 8);   // a register shift amount sees PC one instruction further on
	const uint32_t v = r[rm];
	if (!byreg)
	{
		const uint32_t s = (op >> 7) & 0x1F;
		switch (type)
		{
		case 0:   // LSL #s, s != 0 (LSL #0 is the plain form)
			if (s) { if (carry) r[A7_R_COUT] = (v >> (32 - s)) & 1; r[A7_R_SHIFT] = v << s; }
			else r[A7_R_SHIFT] = v;
			break;
		case 1:
			if (s) { if (carry) r[A7_R_COUT] = (v >> (s - 1)) & 1; r[A7_R_SHIFT] = v >> s; }
			else { if (carry) r[A7_R_COUT] = (v & 0x80000000u) ? 1 : 0; r[A7_R_SHIFT] = 0; }
			break;
		case 2:
			if (s) { if (carry) r[A7_R_COUT] = ((int32_t)v >> (int)(s - 1)) & 1; r[A7_R_SHIFT] = (uint32_t)((int32_t)v >> (int)s); }
			else if (v & 0x80000000u) { if (carry) r[A7_R_COUT] = 1; r[A7_R_SHIFT] = 0xFFFFFFFFu; }
			else { if (carry) r[A7_R_COUT] = 0; r[A7_R_SHIFT] = 0; }
			break;
		default:
			if (s) { if (carry) r[A7_R_COUT] = (v >> (s - 1)) & 1; r[A7_R_SHIFT] = (v << (32 - s)) | (v >> s); }
			else { const uint32_t cc = a->c; if (carry) r[A7_R_COUT] = v & 1; r[A7_R_SHIFT] = (v >> 1) | (cc << 31); }
			break;
		}
		return;
	}
	uint32_t s = r[(op >> 8) & 0xF] & 0xFF;
	switch (type)
	{
	case 0:
		if (s)
		{
			if (s == 32) { if (carry) r[A7_R_COUT] = v & 1; r[A7_R_SHIFT] = 0; }
			else if (s < 32) { if (carry) r[A7_R_COUT] = (v >> (32 - s)) & 1; r[A7_R_SHIFT] = v << s; }
			else { if (carry) r[A7_R_COUT] = 0; r[A7_R_SHIFT] = 0; }
		}
		else r[A7_R_SHIFT] = v;
		break;
	case 1:
		if (s)
		{
			if (s == 32) { if (carry) r[A7_R_COUT] = (v & 0x80000000u) ? 1 : 0; r[A7_R_SHIFT] = 0; }
			else if (s < 32) { if (carry) r[A7_R_COUT] = (v >> (s - 1)) & 1; r[A7_R_SHIFT] = v >> s; }
			else { if (carry) r[A7_R_COUT] = 0; r[A7_R_SHIFT] = 0; }
		}
		else r[A7_R_SHIFT] = v;
		break;
	case 2:
		if (s < 32)
		{
			if (s) { if (carry) r[A7_R_COUT] = ((int32_t)v >> (int)(s - 1)) & 1; r[A7_R_SHIFT] = (uint32_t)((int32_t)v >> (int)s); }
			else r[A7_R_SHIFT] = v;
		}
		else if (v & 0x80000000u) { if (carry) r[A7_R_COUT] = 1; r[A7_R_SHIFT] = 0xFFFFFFFFu; }
		else { if (carry) r[A7_R_COUT] = 0; r[A7_R_SHIFT] = 0; }
		break;
	default:
		if (s)
		{
			s &= 0x1F;
			if (s) { if (carry) r[A7_R_COUT] = (v >> (s - 1)) & 1; r[A7_R_SHIFT] = (v << (32 - s)) | (v >> s); }
			else { if (carry) r[A7_R_COUT] = (v & 0x80000000u) ? 1 : 0; r[A7_R_SHIFT] = v; }
		}
		else { if (carry) r[A7_R_COUT] = (v & 0x80000000u) ? 1 : 0; r[A7_R_SHIFT] = v; }
		break;
	}
}

// Data processing. Returns 1 when it wrote the PC (the caller re-dispatches at r[15] & ~3).
static inline int a7_dp(A7Cpu* a, uint32_t op, uint32_t pc, int32_t* ticks)
{
	uint32_t* r = a->r;
	const uint32_t op4 = (op >> 21) & 0xF, s = (op >> 20) & 1;
	const uint32_t rd = (op >> 12) & 0xF, rn = (op >> 16) & 0xF;
	const int logical = !(op4 >= 2 && op4 <= 7) && !(op4 == 0xA || op4 == 0xB);   // AND EOR TST TEQ ORR MOV BIC MVN
	const int writes = op4 < 0x8 || op4 > 0xB;
	const int pcwrite = writes && rd == 15;
	uint32_t value;
	int cout = a->c;
	if (op & 0x02000000)
	{
		const uint32_t imm8 = op & 0xFF, rot = ((op >> 8) & 0xF) * 2;
		value = rot ? ((imm8 >> rot) | (imm8 << (32 - rot))) : imm8;
		if (rot)
			cout = (value & 0x80000000u) != 0;
		if (pcwrite || rn == 15)
			r[15] = pc + 8;
	}
	else if (!(op & 0x10) && ((op >> 5) & 3) == 0 && ((op >> 7) & 0x1F) == 0)
	{
		if (pcwrite || rn == 15 || (op & 0xF) == 15)
			r[15] = pc + 8;
		value = r[op & 0xF];
	}
	else
	{
		a7_shift(a, op, pc, s && logical);   // shc_ for every logical-S op, the _pc ones too
		if (pcwrite || rn == 15)
			r[15] = pc + 8;
		value = r[A7_R_SHIFT];
		if (!pcwrite)
			cout = r[A7_R_COUT] != 0;
	}
	if (pcwrite)
		cout = a->c;   // the _pc labels take C_OUT from C_FLAG
	const uint32_t lhs = r[rn];
	uint32_t res;
	switch (op4)
	{
	case 0x0: res = lhs & value; break;
	case 0x1: res = lhs ^ value; break;
	case 0x2: res = lhs - value; if (s) a7_sub_flags(a, lhs, value, res); break;
	case 0x3: res = value - lhs; if (s) a7_sub_flags(a, value, lhs, res); break;
	case 0x4: res = lhs + value; if (s) a7_add_flags(a, lhs, value, res); break;
	case 0x5: res = lhs + value + a->c; if (s) a7_add_flags(a, lhs, value, res); break;
	case 0x6: res = lhs - value - !a->c; if (s) a7_sub_flags(a, lhs, value, res); break;
	case 0x7: res = value - lhs - !a->c; if (s) a7_sub_flags(a, value, lhs, res); break;
	case 0x8: res = lhs & value; break;
	case 0x9: res = lhs ^ value; break;
	case 0xA: res = lhs - value; a7_sub_flags(a, lhs, value, res); break;
	case 0xB: res = lhs + value; a7_add_flags(a, lhs, value, res); break;
	case 0xC: res = lhs | value; break;
	case 0xD: res = value; break;
	case 0xE: res = lhs & ~value; break;
	default: res = ~value; break;
	}
	if (logical && s)
	{
		a->n = (res & 0x80000000u) != 0;
		a->z = res == 0;
		a->c = (uint8_t)cout;
	}
	if (!writes)
		return 0;
	r[rd] = res;
	if (!pcwrite)
		return 0;
	if (s)
	{
		(*ticks)++;
		a7_switch_mode(a, (int)(a->r[17] & 0x1F), 0);
	}
	return 1;
}

static void a7_stm(A7Cpu* a, uint32_t op, int32_t* ticks)
{
	uint32_t* r = a->r;
	const uint32_t base = (op >> 16) & 0xF, rn = r[base] & 0xFFFFFFFCu;
	const uint32_t n = a7_popc8(op & 0xFF) + a7_popc8((op >> 8) & 0xFF);
	const int P = (op >> 24) & 1, U = (op >> 23) & 1, W = (op >> 21) & 1, S = (op >> 22) & 1;
	uint32_t first, wb;
	if (U) { first = P ? rn + 4 : rn; wb = rn + 4 * n; }
	else { first = P ? rn - 4 * n : rn - 4 * n + 4; wb = rn - 4 * n; }
	uint32_t addr = first & 0xFFFFFFFCu;
	*ticks += 2;
	const int user = S && a->mode != 0x10 && a->mode != 0x1f;
	int wrote_base = 0;
	for (uint32_t i = 0; i <= 15; i++)
	{
		if (!(op & (1u << i)))
			continue;
		uint32_t v;
		if (i == 15)
			v = r[15] + 4;
		else if (i == base && W && !wrote_base)
			v = r[base];
		else if (user)
			v = (i >= 8 && i <= 12 && a->mode == 0x11) ? r[(A7_R8_FIQ - 8) + i]
			  : (i == 13 ? r[A7_R13_USR] : (i == 14 ? r[A7_R14_USR] : r[i]));
		else
			v = r[i];
		a7_wr32(addr, v);
		A7_RACE(a, ticks);
		*ticks += 2;
		addr += 4;
		if (W && !wrote_base)
		{
			r[base] = wb;
			wrote_base = 1;
		}
	}
}

// Returns 1 when it loaded the PC (next_pc set).
static int a7_ldm(A7Cpu* a, uint32_t op, int32_t* ticks)
{
	uint32_t* r = a->r;
	const uint32_t base = (op >> 16) & 0xF, rn = r[base] & 0xFFFFFFFCu;
	const uint32_t n = a7_popc8(op & 0xFF) + a7_popc8((op >> 8) & 0xFF);
	const int P = (op >> 24) & 1, U = (op >> 23) & 1, W = (op >> 21) & 1, S = (op >> 22) & 1;
	const int haspc = (op & 0x8000) != 0;
	uint32_t first, wb;
	if (U) { first = P ? rn + 4 : rn; wb = rn + 4 * n; }
	else { first = P ? rn - 4 * n : rn - 4 * n + 4; wb = rn - 4 * n; }
	uint32_t addr = first & 0xFFFFFFFCu;
	*ticks += 2;
	const int user = S && !haspc && a->mode != 0x10 && a->mode != 0x1f;
	for (uint32_t i = 0; i <= 14; i++)
	{
		if (!(op & (1u << i)))
			continue;
		const uint32_t v = a7_rd32(addr);
		*ticks += 2;
		addr += 4;
		if (user)
		{
			if (i >= 8 && i <= 12 && a->mode == 0x11) r[(A7_R8_FIQ - 8) + i] = v;
			else if (i == 13) r[A7_R13_USR] = v;
			else if (i == 14) r[A7_R14_USR] = v;
			else r[i] = v;
		}
		else
			r[i] = v;
	}
	if (haspc)
	{
		r[15] = a7_rd32(addr);
		*ticks += 3;
		if (W && !(op & (1u << base)))
			r[base] = wb;
		if (S)
			a7_switch_mode(a, (int)(r[17] & 0x1f), 0);
		a->next_pc = r[15] & 0xFFFFFFFCu;
		r[15] = a->next_pc + 4;
		return 1;
	}
	if (W && !(op & (1u << base)))
		r[base] = wb;
	return 0;
}

static void a7_msr(A7Cpu* a, uint32_t op)
{
	uint32_t value;
	if (op & 0x02000000)
	{
		value = op & 0xFF;
		const int shift = (op & 0xF00) >> 7;
		if (shift)
			value = (value << (32 - shift)) | (value >> shift);
	}
	else
		value = a->r[op & 15];
	if (!(op & 0x00400000))
	{
		a7_update_cpsr(a);
		uint32_t nv = a->r[16];
		if (a->mode > 0x10)
		{
			if (op & 0x00010000) nv = (nv & 0xFFFFFF00u) | (value & 0x000000FFu);
			if (op & 0x00020000) nv = (nv & 0xFFFF00FFu) | (value & 0x0000FF00u);
			if (op & 0x00040000) nv = (nv & 0xFF00FFFFu) | (value & 0x00FF0000u);
		}
		if (op & 0x00080000) nv = (nv & 0x00FFFFFFu) | (value & 0xFF000000u);
		nv |= 0x10;
		a7_switch_mode(a, (int)(nv & 0x1f), 0);
		a->r[16] = nv;
		a7_update_flags(a);
	}
	else if (a->mode > 0x10 && a->mode < 0x1f)
	{
		uint32_t* sp = &a->r[17];
		if (op & 0x00010000) *sp = (*sp & 0xFFFFFF00u) | (value & 0x000000FFu);
		if (op & 0x00020000) *sp = (*sp & 0xFFFF00FFu) | (value & 0x0000FF00u);
		if (op & 0x00040000) *sp = (*sp & 0xFF00FFFFu) | (value & 0x00FF0000u);
		if (op & 0x00080000) *sp = (*sp & 0x00FFFFFFu) | (value & 0xFF000000u);
	}
}

// LDR / STR / LDRB / STRB. Returns 1 when an LDR wrote the PC.
static inline int a7_mem(A7Cpu* a, uint32_t op, uint32_t pc, int32_t* ticks)
{
	uint32_t* r = a->r;
	const uint32_t dest = (op >> 12) & 0xF, base = (op >> 16) & 0xF;
	const int L = (op >> 20) & 1, W = (op >> 21) & 1, B = (op >> 22) & 1, U = (op >> 23) & 1, P = (op >> 24) & 1;
	const int I = (op >> 25) & 1;
	if (base == 15 || (I && (op & 0xF) == 15))
		r[15] = pc + 8;
	uint32_t offset;
	if (!I)
		offset = op & 0xFFF;
	else
	{
		const uint32_t rm = r[op & 0xF], sa = (op >> 7) & 0x1F;
		switch ((op >> 5) & 3)
		{
		case 0: offset = rm << sa; break;
		case 1: offset = sa ? (rm >> sa) : 0; break;
		case 2: offset = sa ? (uint32_t)((int32_t)rm >> (int)sa) : ((rm & 0x80000000u) ? 0xFFFFFFFFu : 0); break;
		default: offset = sa ? ((rm >> sa) | (rm << (32 - sa))) : (((uint32_t)a->c << 31) | (rm >> 1)); break;
		}
	}
	const uint32_t rn = r[base];
	const uint32_t moved = U ? rn + offset : rn - offset;
	const uint32_t addr = P ? moved : rn;
	if (L)
	{
		const uint32_t data = B ? a7_rd8(addr) : a7_rd32(addr);
		if (!P)
		{
			if (dest != base)
				r[base] = moved;
		}
		else if (W && dest != base)
			r[base] = addr;
		r[dest] = data;
		*ticks += 4;
		if (dest == 15)
		{
			*ticks += 2;
			return 1;
		}
		return 0;
	}
	if (P && W)
		r[base] = addr;
	if (B)
		a7_wr8(addr, r[dest] & 0xFF);
	else
		a7_wr32(addr, r[dest]);
	A7_RACE(a, ticks);
	if (!P)
		r[base] = moved;
	*ticks += 3;
	return 0;
}

// One slice of `budget` ticks, as arm_Run_Cached(CycleCount). Returns the ticks charged (less after an a7_race_clamp).
// Signed tests: a7_race_clamp can leave ticks below 0 in a slice under 50 ticks.
static int32_t a7_run(A7Cpu* a, uint32_t budget)
{
	if (!a->enabled)
		return 0;
	if (a->dirty)
	{
		a->idle_safe = a->idle_pref;
		a->dirty = 0;
	}
	uint32_t* r = a->r;
	int32_t ticks = 0;
	uint32_t pc = a->next_pc, meta = 0;
	a->budget = (int32_t)budget;
	a->running = 1;
	a->race_done = 0;

dispatch:
	if (a->fiq_en && (A7_E68K_OUT))
	{
		r[15] = pc + 4;
		a7_fiq(a);
		pc = a->next_pc;
	}
	if (ticks >= (int32_t)budget)
		goto out;
	ticks += 6;
	meta = a7_lookup(a, pc);
	A7_TRACE('D', pc, meta, ticks);

	for (;;)
	{
		const uint32_t op = a7_fetch(pc & A7_ARAM_MASK);
		const uint32_t cond = op >> 28;
		if (cond != 0xE && !a7_cond(a, cond))
		{
			A7_TRACE('F', pc, op, ticks);
			pc += 4;   // COND_FAIL re-homes: FIQ sample and lookup at the next instruction
			goto dispatch;
		}
		A7_TRACE('I', pc, op, ticks);
		const uint32_t fam = (op >> 26) & 3;
		if (fam == 0)
		{
			if ((op & 0x0FC000F0) == 0x00000090)
			{
				const uint32_t rd = (op >> 16) & 0xF;
				uint32_t rs = r[(op >> 8) & 0xF];
				const int mla = (op & 0x00200000) != 0;
				r[rd] = r[op & 0xF] * rs + (mla ? r[(op >> 12) & 0xF] : 0);
				if (op & 0x00100000)
				{
					a->n = (r[rd] & 0x80000000u) != 0;
					a->z = r[rd] == 0;
				}
				if ((int32_t)rs < 0)
					rs = ~rs;
				ticks += mla ? 1 : 0;
				if ((rs & 0xFFFFFF00u) == 0) ticks += 2;
				else if ((rs & 0xFFFF0000u) == 0) ticks += 3;
				else if ((rs & 0xFF000000u) == 0) ticks += 4;
				else ticks += 5;
			}
			else if ((op & 0x0FB00FF0) == 0x01000090)
			{
				const uint32_t addr = r[(op >> 16) & 0xF];
				if (op & 0x00400000)
				{
					const uint32_t t = a7_rd8(addr);
					a7_wr8(addr, r[op & 0xF] & 0xFF);
					r[(op >> 12) & 0xF] = t & 0xFF;
				}
				else
				{
					const uint32_t t = a7_rd32(addr);
					a7_wr32(addr, r[op & 0xF]);
					r[(op >> 12) & 0xF] = t;
				}
				A7_RACE(a, &ticks);
				ticks += 4;
			}
			else if (((op >> 23) & 3) == 2 && !(op & 0x00100000))
			{
				if (!(op & 0x00200000))
				{
					if (op & 0x00400000)
						r[(op >> 12) & 0xF] = r[17];
					else
					{
						a7_update_cpsr(a);
						r[(op >> 12) & 0xF] = r[16];
					}
				}
				else
					a7_msr(a, op);
				ticks += 1;
			}
			else if (a7_dp(a, op, pc, &ticks))
			{
				pc = r[15] & 0xFFFFFFFCu;
				goto dispatch;
			}
		}
		else if (fam == 1)
		{
			if (a7_mem(a, op, pc, &ticks))
			{
				pc = r[15] & 0xFFFFFFFCu;
				goto dispatch;
			}
		}
		else if (fam == 2)
		{
			if (!(op & 0x02000000))
			{
				r[15] = pc + 8;
				if (!(op & 0x00100000))
					a7_stm(a, op, &ticks);
				else if (a7_ldm(a, op, &ticks))
				{
					pc = a->next_pc;
					goto dispatch;
				}
			}
			else
			{
				const int32_t off = (int32_t)(op << 8) >> 6;
				if (op & 0x01000000)
					r[14] = pc + 4;
				else if (meta & A7_META_IDLE)
					ticks += 100;
				pc = pc + 8 + (uint32_t)off;
				ticks += 3;
				goto dispatch;
			}
		}
		else if (((op >> 24) & 0xF) == 0xF)
		{
			ticks += 3;
			r[15] = pc + 8;
			a7_swi(a);
			pc = a->next_pc;
			goto dispatch;
		}
		else
		{
			r[15] = pc + 8;
			a7_undefined(a);
			pc = a->next_pc;
			goto dispatch;
		}

		// ADVANCE: next instruction of the block; after a capped block's last one, the rehome dispatch follows.
		const int rehome = (meta & A7_META_CAP) && (pc & A7_ARAM_MASK & ~3u) == (meta & ~3u);
		pc += 4;
		if (ticks >= (int32_t)budget)
			goto out;
		ticks += 6;
		if (rehome)
			goto dispatch;
	}

out:
	a->next_pc = pc;
	r[15] = pc + 4;
	a->running = 0;
	return ticks;
}
