// SH4 cached (threaded) interpreter, as in plugs/vbaARM/arm7_cached.cpp: a block is decoded once into Sh4Uop slots,
// so an op is a cycle decrement, a pointer bump and a computed goto, not a _vmem fetch, OpPtr load and full-ABI call.

#include "types.h"
#include "dc/pvr/pvr_if.h"
#include "sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"
#include "sh4_registers.h"
#include "sh4_opcode_list.h"
#include "ccn.h"
#include "intc.h"
#include "tmu.h"
#include "dc/gdrom/gdrom_if.h"
#include "ps3/ps3_phase.h"
#include <math.h>
#include <float.h>

#if !defined(__GNUC__)
#  error "sh4_cached.cpp requires GCC computed-goto (&&label / goto *)."
#endif

// FPU prelude first: it declares the real iNimp(const char*), which the CPU prelude then shadows with the two-argument
// cpu_iNimp. GetN/GetM/GetImm* are spelled differently in the two preludes, so drop them before switching sets.
#include "sh4_fpu_prelude.h"
#undef GetN
#undef GetM
#undef GetImm4
#undef GetImm8
#undef GetImm12
#include "sh4_cpu_prelude.h"
#include "dc/mem/mmu.h"

// ---- Cache tables ----

struct Sh4Uop
{
	void* dispatch;   // computed-goto target (&&label)
	u32   op;         // raw 16-bit opcode, kept in a word for a cheap load
};

// Direct-mapped entrypoint cache with a full-PC tag (a uop per address would need 32 MB for 16 MB of RAM); a miss
// re-decodes. The index drops the region bits: all mirrors of a RAM offset share a slot, one invalidate covers them.
struct Sh4Ep
{
	u32     tag;      // instruction address, or EP_EMPTY_TAG
	Sh4Uop* uop;
};

#define SH4_POOL_SIZE   (256 * 1024)          // 2 MB of uops
#define SH4_EP_BITS     18
#define SH4_EP_SIZE     (1u << SH4_EP_BITS)   // 2 MB of entrypoints
#define SH4_EP_MASK     (SH4_EP_SIZE - 1)
#define SH4_EP_IDX(pc)  (((pc) >> 1) & SH4_EP_MASK)
#define SH4_MAX_BLOCK   128

// Every SH4 instruction address is even, so an odd tag never matches.
#define EP_EMPTY_TAG    1u

static Sh4Uop s_pool[SH4_POOL_SIZE];
static Sh4Ep  s_ep[SH4_EP_SIZE];
static u32    s_pool_used   = 1;      // slot 0 is the "decode this block" sentinel
static bool   s_tables_init = false;  // label addresses are only valid inside Run()
bool g_sh4_cached_live = false;       // the same flag, for RAM writers that skip the invalidate call (plan 5.44)
static u32    s_pool_resets = 0;

// opcode -> label, built once from OpPtr[] and the generated name list.
static void*  s_dispatch[0x10000];

static void ep_clear_all()
{
	for (u32 i = 0; i < SH4_EP_SIZE; i++)
		s_ep[i].tag = EP_EMPTY_TAG;
}

// Drop every cached block (sh4_cpu.ResetCache: CCR write, ELF load). Safe inside a running block: the pool keeps its
// contents, so the block in flight runs to its end on stale uops and only then re-homes into a decode.
void sh4_cached_flush()
{
	if (!s_tables_init)
		return;
	ep_clear_all();
}

// Drop the blocks covering [addr, addr+size) (bulk DMA writes). Over-invalidation is harmless: the index ignores
// the region bits, so clearing a window of slots also drops unrelated addresses that alias into it.
void sh4_cached_invalidate_range(u32 addr, u32 size)
{
	// Only system RAM (0x0C000000 and mirrors) can hold SH4 code; skipping the rest keeps the per-SQ-flush cost at one
	// compare, since the store queue mostly targets the TA FIFO and VRAM.
	if (!s_tables_init || (addr & 0x1C000000) != 0x0C000000)
		return;

	const u32 slots = (size >> 1) + 1;
	if (slots >= SH4_EP_SIZE)
	{
		ep_clear_all();
		return;
	}

	u32 idx = SH4_EP_IDX(addr);
	for (u32 i = 0; i < slots; i++)
	{
		s_ep[idx].tag = EP_EMPTY_TAG;
		idx = (idx + 1) & SH4_EP_MASK;
	}
}

// ---- The engine ----

void Sh4_int_Run_Cached()
{
	// Latch the timing parameters for this run, as the plain interpreter does.
	Sh4_ApplyAccuracyPreset();

	const s32 timeslice = sh4_GetCpuTimeslice();
	const s32 ratio     = sh4_GetCpuRatio();

	Sh4Uop* pc_op = &s_pool[0];
	u32     op    = 0;
	s32     l     = timeslice;

	if (!s_tables_init)
	{
		// Pair every handler function with its label, then fan that out over the
		// 0x10000 opcode table the same way BuildOpcodeTables() fills OpPtr[].
		static OpCallFP* map_fp[256];
		static void*     map_lbl[256];
		u32 k = 0;

		#define SH4_CACHED_OP(name) \
			do { extern sh4op(name); map_fp[k] = name; map_lbl[k] = &&lbl_##name; k++; } while (0);
		#include "sh4_cached_ops.inc"
		#undef SH4_CACHED_OP

		// Linear search with a last-hit cache: OpPtr[] runs in long stretches of
		// the same handler, so this is a few hundred thousand compares in total.
		u32 last = 0;
		u32 unlabelled = 0;
		for (u32 i = 0; i < 0x10000; i++)
		{
			OpCallFP* fp = OpPtr[i];
			if (map_fp[last] != fp)
			{
				u32 j = 0;
				for (; j < k; j++)
					if (map_fp[j] == fp)
						break;
				// An opcode with no label (a handler added without updating the
				// generated list) falls back to a plain indirect call.
				if (j == k)
				{
					s_dispatch[i] = &&lbl_generic;
					unlabelled++;
					continue;
				}
				last = j;
			}
			s_dispatch[i] = map_lbl[last];
		}

		s_pool[0].dispatch = &&lbl_decode;
		s_pool[0].op       = 0;
		s_pool_used        = 1;
		ep_clear_all();
		s_tables_init      = true;
		g_sh4_cached_live  = true;
		printf("Sh4: cached interpreter ready, %u opcode labels, %u opcodes on the generic fallback\n",
		       (unsigned)k, (unsigned)unlabelled);
	}

	sh4_int_bCpuRun = true;

	// PC model of Sh4_int_Run: next_pc == A+2 while the op at A runs; branch ops set it and run their own delay slot.
	// ADVANCE() charges the op, checks the budget, steps next_pc/pc_op (never past a block: each ends in lbl_rehome).
	#define ADVANCE() do {                        \
			l -= ratio;                           \
			if (l <= 0) goto lbl_slice_end;       \
			next_pc += 2;                         \
			pc_op++;                              \
			op = pc_op->op;                       \
			goto *pc_op->dispatch;                \
		} while (0)

	goto lbl_home;

	// Opcode bodies shared with sh4_cpu.cpp / sh4_fpu.cpp, re-included as labels. Each sh4op() emits the ADVANCE() that
	// the previous body falls into, then its own label; the first ADVANCE() is unreachable, the last body closes below.
	#undef sh4op
	#define sh4op(name) ADVANCE(); lbl_##name:

	#include "sh4_cpu_movs.h"
	#include "sh4_cpu_branch.h"
	#include "sh4_cpu_arith.h"
	#include "sh4_cpu_logic.h"
	#include "sh4_cpu_misc.h"

	// Switch to the FPU macro set for the FPU bodies: iNimp goes back to the
	// one-argument FPU reporter and GetN/GetM/GetImm* to their FPU spelling.
	#undef iNimp
	#undef iWarn
	#undef GetN
	#undef GetM
	#undef GetImm4
	#undef GetImm8
	#undef GetImm12
	#define GetN(str) ((str >> 8) & 0xf)
	#define GetM(str) ((str >> 4) & 0xf)
	#define GetImm4(str) ((str >> 0) & 0xf)
	#define GetImm8(str) ((str >> 0) & 0xff)
	#define GetImm12(str) ((str >> 0) & 0xfff)

	#include "sh4_fpu_ops.h"

	ADVANCE();   // closes the last opcode body

	// Fallback for an opcode whose handler is not in the generated name list.
lbl_generic:
	OpPtr[op](op);
	ADVANCE();

	// Block terminator. ADVANCE() already bumped next_pc past the instruction
	// the previous uop left pending, so undo that and re-home.
lbl_rehome:
	next_pc -= 2;

	// Entry point for "next_pc holds the address of the next instruction".
lbl_home:
	{
		Sh4Ep* e = &s_ep[SH4_EP_IDX(next_pc)];
		pc_op = (e->tag == next_pc) ? e->uop : &s_pool[0];
	}
	next_pc += 2;
	op = pc_op->op;
	goto *pc_op->dispatch;

	// Decode the block starting at next_pc-2, then run it.
lbl_decode:
	{
		SH4_PHASE(PH_DECODE);
		const u32 start = next_pc - 2;

		// Pool exhausted: drop everything. This only ever runs here, at a block
		// boundary, so no block is executing out of the slots being reused.
		if (s_pool_used + SH4_MAX_BLOCK + 2 > SH4_POOL_SIZE)
		{
			s_pool_used = 1;
			ep_clear_all();
			s_pool_resets++;
			printf("Sh4: cached pool reset #%u\n", (unsigned)s_pool_resets);
		}

		Sh4Uop* first = &s_pool[s_pool_used];
		u32     a     = start;
		u32     n     = 0;

		for (;;)
		{
			const u32 o = IReadMem16(a);

			Sh4Uop* u   = &s_pool[s_pool_used++];
			u->op       = o;
			u->dispatch = s_dispatch[o];

			Sh4Ep* e = &s_ep[SH4_EP_IDX(a)];
			e->tag   = a;
			e->uop   = u;

			a += 2;
			n++;

			// End the block on anything that can move the PC behind our back: branches and delay-slot ops (WritesPC),
			// and the two ldc-to-SR ops, which can dispatch an interrupt through UpdateINTC().
			const sh4_opcodelistentry* d = OpDesc[o];
			if (d->SetPC() || d->SetSR() || n >= SH4_MAX_BLOCK)
				break;
		}

		// Terminator for the trailing ADVANCE()'s pc_op++.
		s_pool[s_pool_used++].dispatch = &&lbl_rehome;

		pc_op = first;
	}
	SH4_PHASE(PH_GUEST);
	op = pc_op->op;
	goto *pc_op->dispatch;

	// Timeslice boundary. next_pc already points at the next instruction.
lbl_slice_end:
	l += timeslice;
	UpdateSystem();
	if (!sh4_int_bCpuRun)
		goto lbl_exit;
	goto lbl_home;

lbl_exit:
	sh4_int_bCpuRun = false;
	return;

	#undef ADVANCE
}
