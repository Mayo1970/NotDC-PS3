// SH4 -> PPC dynarec (nullDCe's PPC one, ported to the PS3's 64-bit ELFv1 ABI). r0, r3-r12 and f0-f13
// are volatile; r13-r31 and f14-f31 are saved, which is why pinned SH4 registers live there.
#include "types.h"
#include <stddef.h>	// offsetof — used for jit_scratch context slot addressing
#include <math.h>	// sqrtf — fsqrt/fsrra native call targets
#include <stdlib.h>	// malloc — JIT_HOTBLOCKS lazily allocates its block table
#include <stdarg.h>	// va_list — JIT_HOTBLOCKS batches its log lines
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/sh4/sh4_interpreter.h"	// sh4_GetTimeslice — preset-latched timeslice

#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/rec_v2/ngen.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"	// sq_both — fastmem SQ trampoline fast path
#include "dc/sh4/rec_v2/ccall_census.h"	// C call-out census (JIT_CCALLS preset)
#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jitmem.h"
#include "ps3/ps3_jit_sq.h"
#include "ps3/ps3_jit_fv.h"
#include "ps3/ps3_jit_hot.h"
#include "ps3/ps3_jit_idle.h"
#include "ps3/ps3_jit_tcopy.h"
#include "ps3/ps3_jit_sqla.h"
#include "ps3/ps3_jit_ppe.h"
#include "ps3/ps3_jit_dynic.h"
#include "ps3/ps3_jit_frchg.h"
#include "ps3/ps3_jit_pref.h"
#include "ps3/ps3_jit_greuse.h"
#include "ps3/ps3_jit_carry.h"

// ppc_emitter.h macros that clash with this file's own wrapper functions of the same names.
#ifdef ppc_li
#  undef ppc_li
#endif
#ifdef ppc_lis
#  undef ppc_lis
#endif
#ifdef ppc_li32
#  undef ppc_li32
#endif
#ifdef ppc_mr
#  undef ppc_mr
#endif
#ifdef ppc_mov
#  undef ppc_mov
#endif
#ifdef ppc_nop
#  undef ppc_nop
#endif
#ifdef ppc_blr
#  undef ppc_blr
#endif
#ifdef ppc_bctr
#  undef ppc_bctr
#endif
#ifdef ppc_bctrl
#  undef ppc_bctrl
#endif
#ifdef ppc_b
#  undef ppc_b
#endif
#ifdef ppc_bl
#  undef ppc_bl
#endif
// These are defined as real functions below; ppc_bx is also a real function
// (overload of the auto-generated one) so guard it too.
#ifdef ppc_bx
#  undef ppc_bx
#endif

// Define "invalid" value for non-mapped-registery
const ppc_freg ppc_finvalid = static_cast<ppc_freg>(-1);  // sentinel: out-of-range (valid regs are 0..31)
const ppc_ireg ppc_rinvalid = static_cast<ppc_ireg>(-1);  // sentinel: out-of-range (valid regs are 0..31)

// Defined in ps3_presets.cpp
extern "C" int get_debug_loop();
extern "C" int get_bcache_preset();	// ps3_presets.cpp: 0=off (default), 1=flat dispatch cache
extern "C" int get_dyn_ic_preset();	// ps3_presets.cpp: 0=off (default), 1=call+jump, 2=+rts
extern "C" int get_fpu_pin_preset();	// ps3_presets.cpp: 0=off (default), 1=pin fr[0..15] to f14..f29
extern "C" int get_jit_align_preset();	// ps3_presets.cpp: 0=off (default), 1=32-byte-align block entries
extern "C" int get_ifb_flush_preset();	// ps3_presets.cpp: 0=off (default, full bracket), 1=selective
extern "C" int get_ifb_probe_preset();	// ps3_presets.cpp: 0=off (default), 1=count ifb sites per opcode

// clrldi rA,rS,32 - drop the upper 32 bits. rldicl with SH=0, MB=32.
static void ppc_clrldi32(u32 A,u32 S)
{
	ppc_emit(0x78000000 | (S<<21) | (A<<16) | 0x20);
}

// ppc_li: Loads a 32-bit immediate value into a PowerPC register.
void ppc_li(u32 D,u32 imm)
{
	if (is_s16(imm))
	{
		ppc_addi(D,0,imm);
		return;
	}
	else
	{
		ppc_addis(D,0,imm>>16);
		if ((u16)imm != 0) {
			ppc_ori(D,D,(u16)imm);
		}

		// addis sign-extends on PPC64: a constant with bit 31 set gets 0xFFFFFFFF above it. Harmless for w-form
		// arithmetic, poison at a C call (ELFv1: the caller extends u32 args, and RAM addresses start 0x8C).
		if (imm & 0x80000000)
			ppc_clrldi32(D,D);
	}
}

// ELFv1: a C function pointer is a descriptor, and generated code has none, so entering the cache from C
// needs one built by hand, with our own TOC (Dynarec.MD 3.2).
static struct { u64 entry; u64 toc; u64 env; } s_enter_opd;

static void ppc_enter_code(void* code)
{
	u64 r2;
	__asm__ volatile("mr %0,2" : "=r"(r2));
	s_enter_opd.entry = (u64)(uintptr_t)code;
	s_enter_opd.toc   = r2;
	s_enter_opd.env   = 0;
	((void (*)())(void*)&s_enter_opd)();
}

// DS-form 64-bit load/store. The emitter header is PPC32-generated, so the
// ELFv1 frame ops below have to be encoded by hand.
static void ppc_std(u32 S,s32 d,u32 A)  { ppc_emit(0xF8000000 | (S<<21) | (A<<16) | (u32)(d & 0xFFFC)); }
static void ppc_stdu(u32 S,s32 d,u32 A) { ppc_emit(0xF8000001 | (S<<21) | (A<<16) | (u32)(d & 0xFFFC)); }
static void ppc_ld(u32 D,s32 d,u32 A)   { ppc_emit(0xE8000000 | (D<<21) | (A<<16) | (u32)(d & 0xFFFC)); }
static void ppc_ldx(u32 D,u32 A,u32 B)  { ppc_emit(0x7C00002A | (D<<21) | (A<<16) | (B<<11)); }

// ---- Jump offset calculation ----

snat ppc_jdiff_raw(void* dst)
{
	return (u8*)dst-(u8*)emit_GetCCPtr();
}
snat ppc_jdiff(void* dst)
{
	return ppc_jdiff_raw(dst)>>2;
}

void ppc_bx(void* dst,u32 LK)
{
	snat offs = ppc_jdiff_raw(dst);
	//offs must fit in 24 bits
	verify(offs<33554432 && offs>-33554432);
	offs>>=2;
	// does this work ? //
	ppc_bx(offs,0,LK);
}

// Raw code addresses: cache stubs and compiled block entry points.
void ppc_call(void* code)
{
	ppc_bx(code,1);
}
template<typename T> void ppc_call(T* dst) { return ppc_call((void*)dst); }

void ppc_jump(void* code)
{
	ppc_bx(code,0);
}
template<typename T> void ppc_jump(T* dst) { return ppc_jump((void*)dst); }

void ppc_call_and_jump(void* code)
{
	ppc_call(code);
	ppc_mtctr(ppc_r3);
	ppc_bcctrx(BO_ALWAYS,BI_CR0_EQ,0);  // bctr
}

// ELFv1: a C function pointer is an OPD descriptor; branching to it lands in .opd data. DynarecCodeEntry*
// points at generated code, and the type cannot tell the two apart, hence the _c names.
static void* c_entry(void* fn)
{
	return (void*)(uintptr_t)ps3_opd_entry(fn);
}

void ppc_call_c(void* fn) { ppc_call(c_entry(fn)); }
template<typename T> void ppc_call_c(T* fn) { return ppc_call_c((void*)fn); }

void ppc_call_and_jump_c(void* fn) { ppc_call_and_jump(c_entry(fn)); }
template<typename T> void ppc_call_and_jump_c(T* fn) { return ppc_call_and_jump_c((void*)fn); }

void make_address_range_executable(void* addr, u32 size)
{
	ps3_jit_icache_sync(addr, size);
}

void ppc_lip(u32 D,void* ptr)
{
	ppc_li(D,(u8*)ptr-(u8*)0);
}
template<typename T> void ppc_lip(u32 D, T* ptr) { return ppc_lip(D, (void*)ptr); }

ppc_ireg ppc_cycles = ppc_r29;
ppc_ireg ppc_contex = ppc_r30;
ppc_ireg ppc_djump = ppc_r31;
ppc_ireg ppc_next_pc = ppc_rarg0;

void ppc_emit(u32 insn)
{
	emit_Write32(insn);
}

void* loop_no_update;
void* ngen_LinkBlock_Static_stub;
void* ngen_LinkBlock_Dynamic_1st_stub;
void* ngen_LinkBlock_Dynamic_2nd_stub;
void* ngen_LinkBlock_Dynamic_IC_stub;	// DYN_IC preset: inline-cache fill
void* ngen_BlockCheckFail_stub;
void* loop_do_update_write;
void (*loop_code)() ;
void (*ngen_FailedToFindBlock)();

// Emits the addis half of an absolute address into rD and returns the low 16-bit displacement.
u32 ppc_addr_high(u32 rD,void* ptr);
void ppc_sh_store(u32 D,u32 sh4_reg);	// defined below; ngen_Begin's pc trace needs it

// Bring-up: block entries, bumped by generated code when DebugLoop is on.
extern "C" volatile u32 g_jit_blocks_entered = 0;

struct
{
	bool has_jcond;

	// T-FORWARDING: a T producer (cmp/tst/dt) leaves T in rarg0; set by binop_end, cleared before every op, so
	// the next op can skip reloading the T just stored (a load-hit-store). The store stays: later blocks read T.
	bool t_in_rarg0;

	// JIT_CR0: true only for the block's last op, when the block exits on sr_T and this op writes it: CR0 keeps
	// the condition for ngen_End's branch. Last op only, so nothing in between can clobber CR0.
	bool t_exit_cr0;     // this op may hand its condition to ngen_End
	bool cr0_live;       // it did; cr0_bit/cr0_invert describe it
	u32  cr0_bit;        // BI_CR0_xx index holding the condition
	bool cr0_invert;     // sr_T is the INVERSE of that bit (e.g. >= is !<)

	TCopy tc;            // JIT T COPY: which op's sr_T store also fills djump for the jcond

	void Reset()
	{
		has_jcond=false;
		t_in_rarg0=false;
		t_exit_cr0=false;
		cr0_live=false;
		cr0_bit=0;
		cr0_invert=false;
		tcopy_begin(tc,0);
	}
} compile_state;
u32 last_block;

// JIT_TFWD preset: forward sr_T from rarg0 to the branch that consumes it (see compile_state.t_in_rarg0).
extern "C" int get_jit_tfwd_preset();

// JIT_FSQRT preset (ps3_presets.cpp): inline fsqrts for fsqrt/fsrra instead of a C call.
extern "C" int get_jit_fsqrt_preset();

// JIT_CR0 preset: a block-ending compare leaves its answer in CR0 for the exit branch (see emit_t_to_cr0).
extern "C" int get_jit_cr0_preset();

// JIT_FSCHG preset (ps3_presets.cpp): fschg only flips FPSCR.SZ, so shop_sync_fpscr just
// keeps old_fpscr in step and skips the UpdateFPSCR() call (and its mtfsfi).
extern "C" int get_jit_fschg_preset();

// JIT_FMOV preset: FPU_PIN's direct lfs/stfs on the fastmem shapes. A separate switch from fpu_pin, since
// rec_fastmem_patch() must decode those float shapes too.
extern "C" int get_jit_fmov_preset();

// JIT SQ STORES (plan 5.17 Step 3): store-queue shape for stores before a PREF; sites compiled since boot.
extern "C" int get_jit_sq_store_preset();
u32 g_jit_sq_sites;

// JIT SQ MISS (plan 5.27): 1 store-queue test first in a store's cold fragment, 2 also the look-ahead guess.
// Sites compiled since boot: look-ahead shapes (part of g_jit_sq_sites) and cold fragments with the test.
extern "C" int get_jit_sq_miss_preset();
u32 g_jit_sq_la_sites, g_jit_sq_cold_sites;
static u32 sqla_read16(u32 addr) { return ReadMem16(addr); }

// JIT FTRV (plan 5.20): ftrv/fipr without FPR copies, ftrv's sums interleaved, fldi from a table.
extern "C" int get_jit_ftrv_preset();

// Plan 5.21's SHIL passes (dc/sh4/rec_v2/shil_opt.h), for the [jitcfg] line.
extern "C" int get_jit_constprop_preset();
extern "C" int get_jit_dve_preset();

// JIT CYCLE CHECK (plan 5.22): 0 old (bge over the exit), 1 exit out of line, 2 also no poll on forward-only ends.
extern "C" int get_jit_cycle_check_preset();
static ppc_label* s_cycle_exit;	// this block's blt to its out-of-line exit, patched after ngen_End
static u32 s_cycle_poll, s_cycle_nopoll;	// blocks compiled with and without the poll, for the [cyc] line

// JIT IDLE SKIP (plan 5.25): one entry per idle-loop pc (a cache clear reuses it), the last one shared on
// overflow. The [idle] line reads the counters; the loop range is for its code dumps.
extern "C" int get_jit_idle_skip_preset();
#define IDLE_MAX 1024
static IdleEnt s_idle_ent[IDLE_MAX] __attribute__((aligned(16)));
static IdleEnt s_idle_last[IDLE_MAX];	// counters at the last [idle] line
struct IdleMeta { u32 pc, loop_lo, loop_hi; bool dumped; };
static IdleMeta s_idle_meta[IDLE_MAX];
static u32 s_idle_n;

// JIT T COPY (plan 5.26). jcond sites compiled since boot, for the [tcopy] line: T already in djump, moved from
// rarg0 (T FORWARD), loaded from sr_T, and loaded although a writer was armed (its store was not inline).
extern "C" int get_jit_tcopy_preset();
static u32 s_tc_copy, s_tc_fwd, s_tc_load, s_tc_miss;

// Plan 5.24: JIT MFOCRF reads CR0 with mfocrf; JIT FPUL MOVES takes a memory trip out of flds, fsts, ftrc and
// float. Sites compiled since boot with the new forms, for the [ppe] line.
extern "C" int get_jit_mfocrf_preset();
extern "C" int get_jit_fpul_moves_preset();
static u32 s_ppe_focrf, s_ppe_flds, s_ppe_fsts, s_ppe_ftrc, s_ppe_float;

// Plan 5.29 JIT FRCHG and 5.30 JIT PREFETCH; sites compiled since boot, also on the [ppe] line.
extern "C" int get_jit_frchg_preset();
extern "C" int get_jit_prefetch_preset();
static u32 s_frchg_sites, s_pref_sites;

// Plan 5.31 JIT GUARD REUSE: this block's plan (each memory op's check, then the roles); sites since boot.
extern "C" int get_jit_guard_reuse_preset();
static GrPlan s_gr;
static u8 s_gr_kind[GR_MAX_OPS];
static u32 s_gr_validates, s_gr_reuses;

// DYN_IC (plan 5.3), since boot: sites compiled and misses of filled sites (counted at site+16), each by kind
// (call, jump, ret), and sites filled. Generated code reaches s_ic_miss with addis from 0: it must lie below 2 GB.
static u32 s_ic_sites[3], s_ic_fills;
static u32 s_ic_miss[3];

// A block polls if its end can close a loop: a static target at or below its start, or a dynamic or interrupt
// end. Ends without the poll only reach higher starts, so every loop still polls once per turn.
static bool cycle_poll_needed(const DecodedBlock* b)
{
	switch (b->BlockType)
	{
	case BET_StaticJump: case BET_StaticCall: case BET_Cond_0: case BET_Cond_1:
		return b->BranchBlock <= b->start;
	default:
		return true;
	}
}

// Forward decls: GPR/FPR allocation maps + flush/reload (defined later in this file).
ppc_ireg GetIntReg(u32 reg);
ppc_freg GetFloatReg(u32 reg);
void reg_flush_all();
void reg_reload_all();
void reg_flush_all_fpu();
void reg_reload_all_fpu();

// ---- Block begin/end ----

void ngen_Begin(DecodedBlock* block,bool force_checks)
{
	compile_state.Reset();

	// Bring-up: store the guest pc at every block entry, so the heartbeat names the last block run (next_pc
	// moves per timeslice); the counter tells a dead SH4 thread (frozen) from a spin. rarg0/rarg1 are free.
	if (get_debug_loop())
	{
		u32 lo=ppc_addr_high(ppc_rarg1,(void*)&g_jit_blocks_entered);
		ppc_lwz(ppc_rarg0,ppc_rarg1,lo);
		ppc_addi(ppc_rarg0,ppc_rarg0,1);
		ppc_stw(ppc_rarg0,ppc_rarg1,lo);

		ppc_li(ppc_rarg0,block->start);
		ppc_sh_store(ppc_rarg0,reg_nextpc);
	}

	// Self-modifying code guard (force_checks, JIT_SBP): compare the block's first and last SH4 instructions
	// with compile time (MvC2 overlays reuse addresses); a mismatch bails to rdv_BlockCheckFail before billing.
	if (force_checks && ngen_BlockCheckFail_stub)
	{
		const u32 code_sz = block->sh4_code_size ? block->sh4_code_size : 2;
		u8* ptr = GetMemPtr(block->start, code_sz);

		// DoCheck already probed this, but the mapping is the thing that makes
		// the compare safe — never emit a load we cannot justify.
		if (ptr)
		{
			// Only worth a second compare when the block is longer than one
			// instruction; otherwise the tail IS the head.
			const bool check_tail = (code_sz >= 4);
			ppc_label* fail = 0;

			// --- first instruction --------------------------------------------
			{
				const u16 first = *(u16*)ptr;
				u32 lo = ppc_addr_high(ppc_rarg1,(void*)ptr);
				ppc_lhz(ppc_rarg1,ppc_rarg1,lo);
				ppc_cmpli(ppc_cr0,ppc_rarg1,first,0);
			}

			if (check_tail)
			{
				fail = ppc_CreateLabel();
				ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);	// bne -> fail

				// --- last instruction ------------------------------------------
				u8* tail = ptr + code_sz - 2;
				const u16 last = *(u16*)tail;
				u32 lo = ppc_addr_high(ppc_rarg1,(void*)tail);
				ppc_lhz(ppc_rarg1,ppc_rarg1,lo);
				ppc_cmpli(ppc_cr0,ppc_rarg1,last,0);
			}

			ppc_label* check_ok=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq -> check_ok

			if (fail)
				fail->MarkLabel();					// the bne above lands here
			ppc_li(ppc_rarg0,block->start);			// rdv_BlockCheckFail(pc)
			ppc_jump(ngen_BlockCheckFail_stub);

			check_ok->MarkLabel();
		}
	}

	const int cc=get_jit_cycle_check_preset();
	s_cycle_exit=0;
	if (cc>=2 && !cycle_poll_needed(block))
	{
		ppc_addi(ppc_cycles,ppc_cycles,-block->cycles);
		s_cycle_nopoll++;
	}
	else if (cc>=1)
	{
		// The exit goes out of line (ngen_Compile, after ngen_End): the common path falls through a not-taken blt.
		ppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);
		s_cycle_exit=ppc_CreateLabel();
		ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);
		s_cycle_poll++;
	}
	else
	{
		ppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);

		ppc_label* jdst=ppc_CreateLabel();
		ppc_bcx(BO_FALSE,BI_CR0_LT,0,0,0);

		ppc_li(ppc_next_pc,block->start);
		ppc_jump(loop_do_update_write);

		jdst->MarkLabel();
	}

	// JIT IDLE SKIP: a block that may store marks the turn as work, so the next idle check runs on.
	if (get_jit_idle_skip_preset() && block->has_store)
		emit_idle_mark(ppc_contex,offsetof(Sh4Context,jit_idle_work));

	// No GPR reload: pinned r14..r28 hold the SH4 GPRs across blocks, loaded once in the mainloop prologue and
	// resynced only around shop_ifb and the canonical fallback.
}

// ---- Memory access helpers ----

// Static GPR allocation master switch. Full rationale at reg_flush_all().
// Must be defined before the first use below; set to 0 for all-memory mode.
#define STATIC_GPR_ALLOC 1

//1 opcode (2 if bouncing a pinned FR through memory — see FPU_PIN below)
void ppc_sh_load(u32 D,u32 sh4_reg)
{
#if STATIC_GPR_ALLOC
	ppc_ireg ri=GetIntReg(sh4_reg);
	if (ri!=ppc_rinvalid)
	{
		// Value already lives in a pinned PPC register; just move it.
		if ((u32)ri!=D)
			ppc_ori(D,ri,0);	// mr D, ri
		return;
	}
#endif
	// FPU_PIN: readm/writem/mov64 move FR bit patterns through here; a pinned FR's bits are in its FPR, not
	// Sh4cntx.fr[], so flush the FPR to its slot before the integer load.
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			u32 ofs=Sh4cntx.offset(sh4_reg);
			ppc_stfs(rf,ppc_contex,ofs);
			ppc_lwz(D,ppc_contex,ofs);
			return;
		}
	}
	ppc_lwz(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load(D,prm._reg);
}
// The PPC reg holding an SH4 source: its pinned reg, or scratch D after a load. Only for ops that do not
// write the pinned reg they read (single-instruction ops with a scratch dest).
static u32 src_or_load(u32 sh4_reg,u32 D)
{
#if STATIC_GPR_ALLOC
	ppc_ireg ri=GetIntReg(sh4_reg);
	if (ri!=ppc_rinvalid)
		return (u32)ri;
#endif
	ppc_sh_load(D,sh4_reg);
	return D;
}
static u32 src_or_load(shil_param prm,u32 D)
{
	verify(prm.is_reg());
	return src_or_load(prm._reg,D);
}
void ppc_sh_load_f32(u32 D,u32 sh4_reg)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			// Value already lives in a pinned PPC FPR; just move it.
			if ((u32)rf!=D)
				ppc_fmrx(D,rf,0);
			return;
		}
	}
	ppc_lfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load_f32(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load_f32(D,prm._reg);
}
void ppc_sh_load_u16(u32 D,u32 sh4_reg)
{
	ppc_lhz(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_load_u16(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_load_u16(D,prm._reg);
}
//1 opcode
void ppc_sh_addr(u32 D,u32 sh4_reg)
{
	ppc_addi(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
void ppc_sh_addr(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_addr(D,prm._reg);
}
//1 opcode (2 if bouncing a pinned FR through memory — see FPU_PIN, ppc_sh_load)
void ppc_sh_store(u32 D,u32 sh4_reg)
{
#if STATIC_GPR_ALLOC
	ppc_ireg ri=GetIntReg(sh4_reg);
	if (ri!=ppc_rinvalid)
	{
		// Destination is a pinned PPC register; update it in place.
		if ((u32)ri!=D)
			ppc_ori(ri,D,0);	// mr ri, D
		return;
	}
#endif
	// FPU_PIN: after the raw store, reload the pinned FPR from that slot so it does not go stale.
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			u32 ofs=Sh4cntx.offset(sh4_reg);
			ppc_stw(D,ppc_contex,ofs);
			ppc_lfs(rf,ppc_contex,ofs);
			return;
		}
	}
	ppc_stw(D,ppc_contex,Sh4cntx.offset(sh4_reg));
	if (sh4_reg==reg_sr_T && tcopy_store(compile_state.tc))
		ppc_ori(ppc_djump,D,0);		// JIT T COPY: mr djump,D for the block's jcond
}
void ppc_sh_store(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_store(D,prm._reg);
}
void ppc_sh_store_f32(u32 D,u32 sh4_reg)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
		{
			// Destination is a pinned PPC FPR; update it in place.
			if ((u32)rf!=D)
				ppc_fmrx(rf,D,0);
			return;
		}
	}
	ppc_stfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
}
u32 ppc_addr_high(u32 rD,void* ptr)
{
	unat diff=(u8*)ptr-(u8*)0;
	u32 rv=(s32)(s16)diff;
	diff-=rv;
	ppc_addis(rD,0,diff>>16);

	return rv;
}

void ppc_sh_store_f32(u32 D,shil_param prm)
{
	verify(prm.is_reg());
	ppc_sh_store_f32(D,prm._reg);
}

// Element i of a float param is at offset(base)+i*4, as fr/xf registers are contiguous. Only fr[] is
// pinned (no FPRs left for xf), so an xf base (ftrv's XMTRX) always uses memory here.
u32 ppc_fvec_ofs(shil_param prm,u32 i)
{
	return Sh4cntx.offset(prm._reg) + i*4;
}
void ppc_fvec_load(u32 fd,shil_param prm,u32 i)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(prm._reg+i);
		if (rf!=ppc_finvalid)
		{
			if ((u32)rf!=fd)
				ppc_fmrx(fd,rf,0);
			return;
		}
	}
	ppc_lfs(fd,ppc_contex,ppc_fvec_ofs(prm,i));
}
void ppc_fvec_store(u32 fs,shil_param prm,u32 i)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(prm._reg+i);
		if (rf!=ppc_finvalid)
		{
			if ((u32)rf!=fs)
				ppc_fmrx(rf,fs,0);
			return;
		}
	}
	ppc_stfs(fs,ppc_contex,ppc_fvec_ofs(prm,i));
}

// ---- Calling convention adapter ----

struct CC_PS
{
	CanonicalParamType type;
	shil_param* par;
};
vector<CC_PS> CC_pars;
void ngen_CC_Start(shil_opcode* op) 
{ 
	CC_pars.clear();
}
void ngen_CC_Param(shil_opcode* op,shil_param* par,CanonicalParamType tp) 
{
	switch(tp)
	{
		case CPT_f32rv:
			ppc_sh_store_f32(ppc_frv0, *par);
			break;

		case CPT_u32rv:
		case CPT_u64rvL:
			ppc_sh_store(ppc_rrv0, *par);
			break;

		case CPT_u64rvH:
			ppc_sh_store(ppc_rrv1, *par);
			break;

		case CPT_u32:
		case CPT_ptr:
		case CPT_f32:
			{
				CC_PS t={tp,par};
				CC_pars.push_back(t);
			}
			break;

		default:
			die("invalid tp");
	}
}
void ngen_CC_Call(shil_opcode*op,void* function) 
{
	u32 rd_fp=ppc_farg0;
	u32 rd_gpr=ppc_rarg0;
	for (int i=CC_pars.size();i-->0;)
	{
		if (CC_pars[i].type==CPT_ptr)
		{
			ppc_sh_addr(rd_gpr,*CC_pars[i].par);
		}
		else
		{
			if (CC_pars[i].par->is_reg())
			{
				if (CC_pars[i].type==CPT_f32)
				{
					ppc_sh_load_f32(rd_fp,*CC_pars[i].par);
					rd_fp++;
				}
				else
				{
					ppc_sh_load(rd_gpr,*CC_pars[i].par);
				}
			}
			else
				ppc_li(rd_gpr,CC_pars[i].par->_imm);
		}
		rd_gpr++;
	}
	//printf("used reg r0 to r%d, %d params, calling %08X\n",rd-1,CC_pars.size(),function);
	ppc_call_c(function);
}

// ---- Binary operations ----

void binop_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());

	verify(op->rs1.is_reg());
	//verify(!op->rs2.is_imm() || op->rs2.is_imm_s16());
	
	ppc_sh_load(ppc_rarg0,op->rs1);

	if (op->rs2.is_imm())
	{
		ppc_li(ppc_rarg1,op->rs2._imm);
	}
	else if (op->rs2.is_reg())
	{
		ppc_sh_load(ppc_rarg1,op->rs2);
	}
}

void binop_end(shil_opcode* op)
{
	ppc_sh_store(ppc_rarg0,op->rd);

	// ppc_sh_store leaves rarg0 intact, so an unpinned dest like sr_T is still in rarg0 for the next op. With
	// JIT_TFWD off the flag is never set, and both consumers reload as before.
	compile_state.t_in_rarg0 = get_jit_tfwd_preset() &&
		op->rd.is_reg() && op->rd._reg==reg_sr_T;
}

// binop3_start: rs1/rs2/rd as pinned regs or rarg0/rarg1 scratch (bop_a, bop_b, bop_d). Only for
// single-instruction ops, which read both sources before writing, so bop_d may alias a source.
u32 bop_d, bop_a, bop_b;

// Resolve rs1 -> bop_a (source) and rd -> bop_d (dest). Shared by the plain
// register form and the immediate-folded form.
void bop_resolve_a_d(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());

	ppc_ireg a=GetIntReg(op->rs1._reg);
	if (a!=ppc_rinvalid)
		bop_a=a;
	else
	{
		ppc_sh_load(ppc_rarg0,op->rs1);
		bop_a=ppc_rarg0;
	}

	ppc_ireg d=GetIntReg(op->rd._reg);
	bop_d = (d!=ppc_rinvalid) ? (u32)d : (u32)ppc_rarg0;
}

// Resolve rs2 -> bop_b (source). Materialises an immediate into rarg1.
void bop_resolve_b(shil_opcode* op)
{
	if (op->rs2.is_imm())
	{
		ppc_li(ppc_rarg1,op->rs2._imm);
		bop_b=ppc_rarg1;
	}
	else
	{
		ppc_ireg b=GetIntReg(op->rs2._reg);
		if (b!=ppc_rinvalid)
			bop_b=b;
		else
		{
			ppc_sh_load(ppc_rarg1,op->rs2);
			bop_b=ppc_rarg1;
		}
	}
}

void binop3_start(shil_opcode* op)
{
	verify(!op->rs2.is_null());
	bop_resolve_a_d(op);
	bop_resolve_b(op);
}

void binop3_end(shil_opcode* op)
{
	// If rd is pinned the op already wrote it in place; only a scratch dest
	// needs storing back to the context.
	if (bop_d==ppc_rarg0 && GetIntReg(op->rd._reg)==ppc_rinvalid)
		ppc_sh_store(ppc_rarg0,op->rd);
}

void binop_start_fpu(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());

	verify(op->rs1.is_reg());
	verify(op->rs2.is_reg());
	
	ppc_sh_load_f32(ppc_farg0,op->rs1);
	ppc_sh_load_f32(ppc_farg1,op->rs2);
}

void binop_end_fpu(shil_opcode* op)
{
	ppc_sh_store_f32(ppc_farg0,op->rd);
}

// ---- Unary operations ----

// Unary integer: loads rs1 -> rarg0, operation writes rarg0, stores rarg0 -> rd
void unop_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rd.is_reg());

	if (op->rs1.is_imm())
		ppc_li(ppc_rarg0,op->rs1._imm);
	else
	{
		verify(op->rs1.is_reg());
		ppc_sh_load(ppc_rarg0,op->rs1);
	}
}

void unop_end(shil_opcode* op)
{
	ppc_sh_store(ppc_rarg0,op->rd);
}

// As binop3_*, for single-instruction unary ops (neg, not, ext_s8, ext_s16): bop_d may alias bop_a.
void unop3_start(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rd.is_reg());

	if (op->rs1.is_imm())
	{
		ppc_li(ppc_rarg0,op->rs1._imm);
		bop_a=ppc_rarg0;
	}
	else
	{
		verify(op->rs1.is_reg());
		ppc_ireg a=GetIntReg(op->rs1._reg);
		if (a!=ppc_rinvalid)
			bop_a=a;
		else
		{
			ppc_sh_load(ppc_rarg0,op->rs1);
			bop_a=ppc_rarg0;
		}
	}

	ppc_ireg d=GetIntReg(op->rd._reg);
	bop_d = (d!=ppc_rinvalid) ? (u32)d : (u32)ppc_rarg0;
}

void unop3_end(shil_opcode* op)
{
	if (bop_d==ppc_rarg0 && GetIntReg(op->rd._reg)==ppc_rinvalid)
		ppc_sh_store(ppc_rarg0,op->rd);
}

// Unary fpu: loads rs1 -> farg0, operation writes farg0, stores farg0 -> rd
void unop_start_fpu(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	verify(op->rs1.is_reg());
	verify(op->rd.is_reg());

	ppc_sh_load_f32(ppc_farg0,op->rs1);
}

void unop_end_fpu(shil_opcode* op)
{
	ppc_sh_store_f32(ppc_farg0,op->rd);
}

// FPU_PIN float operands, as binop3_*: single-instruction FP ops read pinned fr in place, so fop_d may
// alias a source; fpul/xf use scratch loads. With the preset off this is the old lfs/lfs/op/stfs.
u32 fop_d, fop_a, fop_b;

// Resolve a float source: its pinned FPR (read in place, no copy), or load
// into scratch D. Never emits the fmr that ppc_sh_load_f32 would.
static u32 fsrc_or_load_i(u32 sh4_reg,u32 D)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(sh4_reg);
		if (rf!=ppc_finvalid)
			return (u32)rf;
	}
	ppc_lfs(D,ppc_contex,Sh4cntx.offset(sh4_reg));
	return D;
}
static u32 fsrc_or_load(shil_param prm,u32 D)
{
	verify(prm.is_reg());
	return fsrc_or_load_i(prm._reg,D);
}

static void fbop_resolve(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
	fop_a=fsrc_or_load(op->rs1,ppc_farg0);
	fop_b=fsrc_or_load(op->rs2,ppc_farg1);
	ppc_freg d=get_fpu_pin_preset()?GetFloatReg(op->rd._reg):ppc_finvalid;
	fop_d=(d!=ppc_finvalid)?(u32)d:(u32)ppc_farg0;
}
static void fuop_resolve(shil_opcode* op)
{
	verify(!op->rs1.is_null() && !op->rd.is_null());
	fop_a=fsrc_or_load(op->rs1,ppc_farg0);
	ppc_freg d=get_fpu_pin_preset()?GetFloatReg(op->rd._reg):ppc_finvalid;
	fop_d=(d!=ppc_finvalid)?(u32)d:(u32)ppc_farg0;
}
static void fbop_end(shil_opcode* op)
{
	// A pinned rd was written in place; only the scratch dest needs a store.
	// (Pinned regs are f14+, so fop_d==farg0 <=> rd is not pinned.)
	if (fop_d==ppc_farg0)
		ppc_sh_store_f32(ppc_farg0,op->rd);
}

// A float dest's FPR: the pinned one (written in place) or scratch; fdst_store() stores only from scratch.
static u32 fdst_reg(shil_param rd,u32 scratch)
{
	if (get_fpu_pin_preset())
	{
		ppc_freg rf=GetFloatReg(rd._reg);
		if (rf!=ppc_finvalid)
			return (u32)rf;
	}
	return scratch;
}
static void fdst_store(shil_param rd,u32 reg,u32 scratch)
{
	if (reg==scratch)
		ppc_sh_store_f32(scratch,rd);
}

// Float reg-to-reg move in the fewest instructions: fmr if both pinned, stfs/lfs if one is, else lfs f0 +
// stfs f0 (also the preset-off path).
static void fmove_reg(u32 dst_reg,u32 src_reg)
{
	ppc_freg sd=get_fpu_pin_preset()?GetFloatReg(src_reg):ppc_finvalid;
	ppc_freg dd=get_fpu_pin_preset()?GetFloatReg(dst_reg):ppc_finvalid;

	if (sd!=ppc_finvalid && dd!=ppc_finvalid)
	{
		if (sd!=dd)
			ppc_fmrx(dd,sd,0);
	}
	else if (sd!=ppc_finvalid)
		ppc_stfs(sd,ppc_contex,Sh4cntx.offset(dst_reg));
	else if (dd!=ppc_finvalid)
		ppc_lfs(dd,ppc_contex,Sh4cntx.offset(src_reg));
	else
	{
		ppc_lfs(ppc_f0,ppc_contex,Sh4cntx.offset(src_reg));
		ppc_stfs(ppc_f0,ppc_contex,Sh4cntx.offset(dst_reg));
	}
}

// ---- Compare -> boolean ----
// SH4 set* ops want 0/1: mfcr (JIT MFOCRF: mfocrf), then rlwinm rotates CR0 bit b (IBM: LT 0, GT 1, EQ 2) to 31.
static void emit_cr0_bit_to_rarg0(u32 cr0_bit_index)
{
	const bool focrf=get_jit_mfocrf_preset()!=0;
	s_ppe_focrf+=focrf;
	emit_cr0_bit(ppc_rarg0,cr0_bit_index,focrf);
}

// JIT_CR0: a block-ending compare leaves T in CR0; true means the caller emits nothing more, and ngen_End
// branches on CR0 and writes sr_T as each path's constant (T == (BlockType&1) on the branch path).
static bool emit_t_to_cr0(u32 cr0_bit_index,bool invert)
{
	if (!compile_state.t_exit_cr0)
		return false;

	compile_state.cr0_live   = true;
	compile_state.cr0_bit    = cr0_bit_index;
	compile_state.cr0_invert = invert;
	return true;
}

// Dest of a carry op: pinned unless r11. The ps3_jit_carry.h forms read the sources before writing it, so it
// may alias a source.
static u32 carry_dest(shil_opcode* op)
{
	ppc_ireg d=GetIntReg(op->rd._reg);
	return (d!=ppc_rinvalid) ? (u32)d : (u32)ppc_rarg3;
}

static void carry_dest_store(shil_opcode* op,u32 d)
{
	if (d==(u32)ppc_rarg3)
		ppc_sh_store(ppc_rarg3,op->rd);
}

// Compare rs1 with rs2 into CR0, pinned operands in place, small immediates folded (cmpi s16, cmpli u16).
// The following mfcr / mfocrf writes only rarg0, so comparing pinned regs in place is safe.
static void emit_cmp_into_cr0(shil_opcode* op,bool is_signed)
{
	// rs1 in a pinned reg -> use it directly; else load into rarg0 scratch.
	u32 a=src_or_load(op->rs1,ppc_rarg0);

	if (op->rs2.is_imm() && (is_signed ? op->rs2.is_imm_s16() : op->rs2.is_imm_u16()))
	{
		if (is_signed)
			ppc_cmpi(ppc_cr0,a,op->rs2._imm,0);
		else
			ppc_cmpli(ppc_cr0,a,op->rs2._imm,0);
		return;
	}

	// rs2 in a pinned reg -> use directly; immediate -> li into rarg1; else load.
	u32 b;
	if (op->rs2.is_imm())
		{ ppc_li(ppc_rarg1,op->rs2._imm); b=ppc_rarg1; }
	else
		b=src_or_load(op->rs2,ppc_rarg1);

	if (is_signed)
		ppc_cmp(ppc_cr0,a,b,0);
	else
		ppc_cmpl(ppc_cr0,a,b,0);
}

// rd = (signed) rs1 <cond> rs2  -> 0/1
static void emit_setcc_signed(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	emit_cmp_into_cr0(op,true);
	if (emit_t_to_cr0(cr0_bit_index,invert))
		return;		// JIT_CR0: ngen_End branches on CR0 and stores T itself
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
}

// rd = (unsigned) rs1 <cond> rs2 -> 0/1
static void emit_setcc_unsigned(shil_opcode* op,u32 cr0_bit_index,bool invert)
{
	emit_cmp_into_cr0(op,false);
	if (emit_t_to_cr0(cr0_bit_index,invert))
		return;		// JIT_CR0: ngen_End branches on CR0 and stores T itself
	emit_cr0_bit_to_rarg0(cr0_bit_index);
	if (invert)
		ppc_xori(ppc_rarg0,ppc_rarg0,1);
	binop_end(op);
}


void ngen_CC_Finish(shil_opcode* op) 
{ 
	CC_pars.clear(); 
}
void DoStatic(u32 pc)
{
	// Exactly 12 bytes: ngen_LinkBlock_Static_stub patches LR-12 with `b target`. ppc_li can be 1 instruction
	// (e.g. a block at 0x8C010000), which would patch the other exit's branch, so always addis + ori.
	ppc_addis(ppc_rarg0,0,pc>>16);
	ppc_ori(ppc_rarg0,ppc_rarg0,(u16)pc);
	ppc_call(ngen_LinkBlock_Static_stub);
}

// ---- ngen_End: block exit code ----

// JIT IDLE SKIP, before an idle loop's backward link. Cycles are >= 0 here (the block's own poll passed), so the
// check only drops the rest of the timeslice. rarg1-rarg3 and CR0 are free once the exit is chosen.
static void idle_check_before_link(const DecodedBlock* block)
{
	if (!get_jit_idle_skip_preset() || !block->idle_loop)
		return;
	u32 i=0;
	while (i<s_idle_n && s_idle_meta[i].pc!=block->start)
		i++;
	if (i==s_idle_n)
	{
		if (s_idle_n<IDLE_MAX)
			s_idle_n++;
		else
			i=IDLE_MAX-1;
		IdleMeta& m=s_idle_meta[i];
		m.pc=block->start;
		m.loop_lo=block->BranchBlock;
		m.loop_hi=block->start+block->sh4_code_size;
		m.dumped=false;
	}
	// The pinned SH4 GPRs: r0-r10 and r12-r15 in r14..r28 (GetIntReg); r11 lives in the context. The entries
	// are reached with addis from 0, so they must lie below 2 GB.
	verify(GetIntReg(reg_r0)==ppc_r14 && GetIntReg(reg_r15)==ppc_r28 && GetIntReg(reg_r11)==ppc_rinvalid);
	verify((uintptr_t)&s_idle_ent[IDLE_MAX] < 0x7FFF0000u);
	const IdleRegs g={ (u32)ppc_contex, (u32)ppc_cycles, ppc_rarg1, ppc_rarg2, ppc_rarg3, ppc_r14, 15 };
	emit_idle_check(g,Sh4cntx.offset(reg_r11),offsetof(Sh4Context,jit_idle_work),&s_idle_ent[i]);
}

void ngen_End(DecodedBlock* block)
{
	// No GPR flush: pinned r14..r28 stay live across block transitions. The BET_*Intr path (UpdateINTC reads
	// r[15], swaps banks) brackets its call itself, as do shop_ifb and the canonical fallback.
	switch(block->BlockType)
	{
	case BET_Cond_0:
	case BET_Cond_1:
		{
			// JIT_CR0: the last op left its condition in CR0. BranchBlock is taken when bit == (cr0_invert ? !want :
			// want), want = BlockType&1; sr_T gets want on that path and !want on the other.
			if (compile_state.cr0_live && !compile_state.has_jcond)
			{
				const u32  want      = block->BlockType & 1;
				const bool bit_taken = compile_state.cr0_invert ? (want==0) : (want!=0);

				ppc_label* jtrue=ppc_CreateLabel();
				ppc_bcx(bit_taken?BO_TRUE:BO_FALSE,compile_state.cr0_bit,0,0,0);

				// fall-through: BranchBlock not taken, so T is the complement.
				ppc_li(ppc_rarg0,want^1);
				ppc_sh_store(ppc_rarg0,reg_sr_T);
				DoStatic(block->NextBlock);

				jtrue->MarkLabel();
				ppc_li(ppc_rarg0,want);
				ppc_sh_store(ppc_rarg0,reg_sr_T);
				idle_check_before_link(block);
				DoStatic(block->BranchBlock);
				break;
			}

			u32 reg;
			if (compile_state.has_jcond)
			{
				reg=ppc_djump;
			}
			else if (compile_state.t_in_rarg0)
			{
				// The compare that produced T was the block's last op, so T is still in rarg0 (see t_in_rarg0).
				reg=ppc_rarg0;
			}
			else
			{
				// cmpi reads any reg: if T is pinned, compare it in place.
				reg=src_or_load(reg_sr_T,ppc_rarg0);
			}

			ppc_cmpi(ppc_cr0,reg,block->BlockType&1,0);
	
			ppc_label* jtrue=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);

			DoStatic(block->NextBlock);
			jtrue->MarkLabel();
			idle_check_before_link(block);
			DoStatic(block->BranchBlock);
		}
		break;

	case BET_DynamicCall:
	case BET_DynamicJump:
	case BET_DynamicRet:
		//printf("Dynamic !\n");
		//mov reg,djump
		ppc_ori(ppc_rarg0,ppc_djump,0);  // mr rarg0, djump

		// DYN_IC: a monomorphic inline cache per site (ps3_jit_dynic.h), filled with the first target; unfilled sites
		// and misses take the lookup below. Every invalidation is a full clear, which drops guards and targets together.
		{
			const int ic = get_dyn_ic_preset();
			const u32 kind = block->BlockType==BET_DynamicCall ? 0 : block->BlockType==BET_DynamicJump ? 1 : 2;
			if (ngen_LinkBlock_Dynamic_IC_stub && (ic>=2 || (ic==1 && kind!=2)))
			{
				verify((uintptr_t)&s_ic_miss[3] < 0x7FFF0000u);
				emit_dynic_site(ngen_LinkBlock_Dynamic_IC_stub);
				emit_dynic_miss_count(ppc_rarg2,ppc_rarg3,&s_ic_miss[kind]);
				s_ic_sites[kind]++;
			}
		}
		// generic path: rarg0 = target pc; rarg1-rarg3 and CR0 are free

		if (get_bcache_preset())
		{
			// BCACHE: bm_bcache[] holds {addr, code} pairs mirrored from cache[], so a hit touches one 8-byte entry at
			// (addr<<1) & 0x1FFF8; addr != 0xFFFFFFFF means code is valid. addr stays in rarg0 for a miss.
			verify(sizeof(BlockCacheFlatEntry)==8 && BM_BLOCKLIST_COUNT==16384);

			ppc_rlwinmx(ppc_rarg1,ppc_rarg0,1,15,28,0);	// rarg1 = (addr<<1) & 0x1FFF8
			u32 lo=ppc_addr_high(ppc_rarg2,(void*)&bm_bcache[0]);
			ppc_addx(ppc_rarg2,ppc_rarg2,ppc_rarg1,0,0);	// rarg2 = &bm_bcache[idx] - lo
			ppc_lwz(ppc_rarg3,ppc_rarg2,lo);		// entry.addr
			ppc_cmp(ppc_cr0,ppc_rarg3,ppc_rarg0,0);		// == addr ?
			ppc_label* miss=ppc_CreateLabel();
			ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne miss
			ppc_lwz(ppc_rarg3,ppc_rarg2,lo+4);		// entry.code
			ppc_mtctr(ppc_rarg3);
			ppc_bcctrx(BO_ALWAYS,BI_CR0_EQ,0);		// bctr -> cached code
			miss->MarkLabel();
		}
		else
		{
			// Inline bm_CheckCache: cache[(addr>>2)&16383] holds 8-byte pointers, so the offset is (addr<<1)&0x1FFF8;
			// a hit needs addr and code, else loop_no_update. addr stays in rarg0 for bm_GetCode.
			verify(sizeof(cache[0])==8);
			ppc_rlwinmx(ppc_rarg1,ppc_rarg0,1,15,28,0);	// rarg1 = (addr<<1) & 0x1FFF8
			u32 lo=ppc_addr_high(ppc_rarg2,(void*)&cache[0]);
			ppc_addi(ppc_rarg2,ppc_rarg2,lo);		// rarg2 = &cache
			ppc_ldx(ppc_rarg2,ppc_rarg2,ppc_rarg1);		// rarg2 = cache[idx]

			ppc_lwz(ppc_rarg3,ppc_rarg2,offsetof(DynarecBlock,addr));
			ppc_cmp(ppc_cr0,ppc_rarg3,ppc_rarg0,0);	// cached->addr == addr ?
			ppc_label* miss1=ppc_CreateLabel();
			ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne miss

			// code is a pointer too: ld, not lwz.
			ppc_ld(ppc_rarg3,offsetof(DynarecBlock,code),ppc_rarg2);
			ppc_cmpi(ppc_cr0,ppc_rarg3,0,0);		// code == 0 ? (empty_block)
			ppc_label* miss2=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq miss

			// Hit: lookups++ feeds the cache-replacement policy. Not in r0: `addi rD,r0,imm` reads r0 as literal 0.
			ppc_lwz(ppc_rarg1,ppc_rarg2,offsetof(DynarecBlock,lookups));
			ppc_addi(ppc_rarg1,ppc_rarg1,1);
			ppc_stw(ppc_rarg1,ppc_rarg2,offsetof(DynarecBlock,lookups));

			ppc_mtctr(ppc_rarg3);
			ppc_bcctrx(BO_ALWAYS,BI_CR0_EQ,0);		// bctr -> cached code

			miss1->MarkLabel();
			miss2->MarkLabel();
		}
		//jmp no update
		ppc_jump(loop_no_update);
		break;

	case BET_StaticIntr:
	case BET_DynamicIntr:
		// Gated like the BET_StaticJump trace: with JIT_NEWOPS every ldc/ldc.l to SR ends a block here.
		if (get_debug_loop() == 1)
		{
			printf("BET: Interrupt !\n");
		}
		{
			u32 reg;
			if (block->BlockType==BET_StaticIntr)
			{
				ppc_li(ppc_rarg0,block->BranchBlock);
				reg=ppc_rarg0;
			}
			else
			{
				reg=ppc_djump;
			}
			ppc_sh_store(reg,reg_nextpc);
			// UpdateINTC -> Do_Exception reads r[15] and swaps the register bank: flush pinned GPRs before, reload after.
			reg_flush_all();
			ppc_call_c(&UpdateINTC);
			reg_reload_all();

			ppc_sh_load(ppc_next_pc,reg_nextpc);
			ppc_jump(loop_no_update);
		}
		break;

	case BET_StaticCall:
	case BET_StaticJump:
		if (get_debug_loop() == 1)
		{
			printf("Static 0x%08X!\n", block->BranchBlock);
		}
		DoStatic(block->BranchBlock);
		break;

	default:
		printf("END TYPE: %d\n",block->BlockType);
		die("wtfh end type\n");
	}
}

// f14..f29 = fr[0..15] only: those 16 use up the callee-saved FPRs after f0 (scratch) and f1-f13 (args);
// f30/f31 cannot hold the xf bank.
ppc_freg GetFloatReg(u32 reg)
{
	if (reg>=reg_fr_0 && reg<=reg_fr_15)
	{
		return (ppc_freg)((reg-reg_fr_0)+ppc_f14);
	}

	return ppc_finvalid;
}

//r14:r28 (shr11 missing)
ppc_ireg GetIntReg(u32 reg)
{
	if (reg>=reg_r0 && reg<=reg_r15 && reg!=reg_r11)
	{
		if (reg>=reg_r11) reg--;

		return (ppc_ireg)((reg-reg_r0 )+ppc_r14);
	}

	return ppc_rinvalid;
}
// Static GPR allocation: SH4 r0..r15 but r11 live in callee-saved r14..r28 all run, so C calls keep them.
// Sh4cntx.r[] is synced only around shop_ifb, the canonical fallback, BET_*Intr and a pending interrupt.

void reg_flush_all()
{
#if STATIC_GPR_ALLOC
	for (u32 i=reg_r0;i<=reg_r15;i++)
	{
		ppc_ireg ri=GetIntReg(i);
		if (ri!=ppc_rinvalid)
			ppc_stw(ri,ppc_contex,Sh4cntx.offset(i));
	}
#endif
}
void reg_reload_all()
{
#if STATIC_GPR_ALLOC
	for (u32 i=reg_r0;i<=reg_r15;i++)
	{
		ppc_ireg ri=GetIntReg(i);
		if (ri!=ppc_rinvalid)
			ppc_lwz(ri,ppc_contex,Sh4cntx.offset(i));
	}
#endif
}

// FPU_PIN (runtime preset): fr[0..15] live in f14..f29, xf[] in memory. Flushed around shop_ifb, the
// canonical fallback and shop_sync_fpscr (ChangeFP swaps fr/xf in memory); not BET_*Intr (no FP bank).

void reg_flush_all_fpu()
{
	if (!get_fpu_pin_preset())
		return;
	for (u32 i=reg_fr_0;i<=reg_fr_15;i++)
	{
		ppc_freg rf=GetFloatReg(i);
		ppc_stfs(rf,ppc_contex,Sh4cntx.offset(i));
	}
}
void reg_reload_all_fpu()
{
	if (!get_fpu_pin_preset())
		return;
	for (u32 i=reg_fr_0;i<=reg_fr_15;i++)
	{
		ppc_freg rf=GetFloatReg(i);
		ppc_lfs(rf,ppc_contex,Sh4cntx.offset(i));
	}
}

// IFB_FLUSH: shop_ifb spills only the GPRs its handler touches, and the float file only if it can change
// it. Closed allow-list on (mask, rez); anything unlisted, or able to raise an exception, spills all.

// Opcode-table mask constants (private copies of the #defines in
// sh4_opcode_list.cpp, which are not exported in the header).
static const u32 IFB_M_NONE = 0xFFFF;
static const u32 IFB_M_N    = 0xF0FF;
static const u32 IFB_M_NM   = 0xF00F;
static const u32 IFB_M_IMM8 = 0xFF00;

// Every SH4 GPR — the mask a bank-swapping handler needs. r11 is included and
// simply drops out in reg_flush_mask(), since GetIntReg() never pins it.
static const u32 IFB_GPR_ALL = 0xFFFFu;

// What one ifb call-out has to spill, on the two independent axes.
struct IfbBracket
{
	u32  gprs;	// bitmask over SH4 r0..r15 (IFB_GPR_ALL = all of them)
	bool fpu;	// also spill fr[0..15] (only costs anything when FPU_PIN is on)
};

// One GPR mask both ways: the union of the read and write sets (an extra reload only re-reads the store).
static IfbBracket ifb_bracket(u16 sh4op)
{
	const sh4_opcodelistentry* d = OpDesc[sh4op];
	const u32 Rn = 1u << ((sh4op >> 8) & 0xF);
	const u32 Rm = 1u << ((sh4op >> 4) & 0xF);
	const u32 R0 = 1u << 0;

	#define IFB_IS(mm,rr) (d->mask==(mm) && d->rez==(rr))
	#define IFB_B(g,f)    { IfbBracket b={(g),(f)}; return b; }

	// --- touches no GPR and no float -------------------------------------
	if (IFB_IS(IFB_M_NONE,0x0028)) IFB_B(0,false)	// clrmac      -> MACH/MACL
	if (IFB_IS(IFB_M_NONE,0x0038)) IFB_B(0,false)	// ldtlb       -> PTEH/PTEL

	// --- Rn only ----------------------------------------------------------
	if (IFB_IS(IFB_M_N,0x0002)) IFB_B(Rn,false)	// stc SR,Rn      (reads sr only)
	if (IFB_IS(IFB_M_N,0x4003)) IFB_B(Rn,false)	// stc.l SR,@-Rn  (reads sr only)
	if (IFB_IS(IFB_M_N,0x4024)) IFB_B(Rn,false)	// rotcl Rn
	if (IFB_IS(IFB_M_N,0x4025)) IFB_B(Rn,false)	// rotcr Rn
	if (IFB_IS(IFB_M_N,0x401B)) IFB_B(Rn,false)	// tas.b @Rn

	// --- Rn and Rm --------------------------------------------------------
	if (IFB_IS(IFB_M_NM,0x000F)) IFB_B(Rn|Rm,false)	// mac.l @Rm+,@Rn+
	if (IFB_IS(IFB_M_NM,0x400F)) IFB_B(Rn|Rm,false)	// mac.w @Rm+,@Rn+
	if (IFB_IS(IFB_M_NM,0x200C)) IFB_B(Rn|Rm,false)	// cmp/str Rm,Rn
	if (IFB_IS(IFB_M_NM,0x200D)) IFB_B(Rn|Rm,false)	// xtrct Rm,Rn
	if (IFB_IS(IFB_M_NM,0x3004)) IFB_B(Rn|Rm,false)	// div1 Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300A)) IFB_B(Rn|Rm,false)	// subc Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300B)) IFB_B(Rn|Rm,false)	// subv Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300E)) IFB_B(Rn|Rm,false)	// addc Rm,Rn
	if (IFB_IS(IFB_M_NM,0x300F)) IFB_B(Rn|Rm,false)	// addv Rm,Rn
	if (IFB_IS(IFB_M_NM,0x6008)) IFB_B(Rn|Rm,false)	// swap.b Rm,Rn
	if (IFB_IS(IFB_M_NM,0x600A)) IFB_B(Rn|Rm,false)	// negc Rm,Rn

	// --- implicit R0 (GBR-relative byte ops) ------------------------------
	if (IFB_IS(IFB_M_IMM8,0xCC00)) IFB_B(R0,false)	// tst.b #imm,@(R0,GBR)
	if (IFB_IS(IFB_M_IMM8,0xCD00)) IFB_B(R0,false)	// and.b #imm,@(R0,GBR)
	if (IFB_IS(IFB_M_IMM8,0xCE00)) IFB_B(R0,false)	// xor.b #imm,@(R0,GBR)
	if (IFB_IS(IFB_M_IMM8,0xCF00)) IFB_B(R0,false)	// or.b  #imm,@(R0,GBR)

	// FPSCR writers: Rn only, but ChangeFP() swaps fr/xf in memory on an FR toggle, so the float file spills.
	if (IFB_IS(IFB_M_N,0x406A)) IFB_B(Rn,true)	// lds Rn,FPSCR
	if (IFB_IS(IFB_M_N,0x4066)) IFB_B(Rn,true)	// lds.l @Rn+,FPSCR

	// SR writers: UpdateSR() swaps the GPR bank in memory on SR.RB, so all GPRs; SR has no FP bank bit.
	if (IFB_IS(IFB_M_N,0x400E)) IFB_B(IFB_GPR_ALL,false)	// ldc Rn,SR
	if (IFB_IS(IFB_M_N,0x4007)) IFB_B(IFB_GPR_ALL,false)	// ldc.l @Rn+,SR

	// --- double-precision converts: float only, no GPR --------------------
	if (IFB_IS(IFB_M_N,0xF0BD)) IFB_B(0,true)	// fcnvds DRn,FPUL
	if (IFB_IS(IFB_M_N,0xF0AD)) IFB_B(0,true)	// fcnvsd FPUL,DRn

	#undef IFB_B
	#undef IFB_IS

	// Not on the list (trapa, sleep, illegal, double-precision FPU fallbacks):
	// the legacy everything-spill.
	IfbBracket full={IFB_GPR_ALL,true};
	return full;
}

// Memory ops one bracket costs, for the probe's reporting only.
static u32 ifb_bracket_memops(const IfbBracket& b)
{
	u32 n=0;
	for (u32 i=0;i<16;i++)
		if ((b.gprs&(1u<<i)) && GetIntReg(reg_r0+i)!=ppc_rinvalid)
			n+=2;					// one stw + one lwz
	if (b.fpu && get_fpu_pin_preset())
		n+=32;						// 16 stfs + 16 lfs
	return n;
}

// Flush/reload only the GPRs in `gprs`. r11 is never pinned, so it is always memory-coherent.
static void reg_flush_mask(u32 gprs)
{
#if STATIC_GPR_ALLOC
	for (u32 i=0;i<16;i++)
	{
		if (!(gprs&(1u<<i)))
			continue;
		ppc_ireg ri=GetIntReg(reg_r0+i);
		if (ri!=ppc_rinvalid)
			ppc_stw(ri,ppc_contex,Sh4cntx.offset(reg_r0+i));
	}
#endif
}
static void reg_reload_mask(u32 gprs)
{
#if STATIC_GPR_ALLOC
	for (u32 i=0;i<16;i++)
	{
		if (!(gprs&(1u<<i)))
			continue;
		ppc_ireg ri=GetIntReg(reg_r0+i);
		if (ri!=ppc_rinvalid)
			ppc_lwz(ri,ppc_contex,Sh4cntx.offset(reg_r0+i));
	}
#endif
}

// IFB_PROBE: per-opcode shop_ifb counters, keyed on the OpDesc[] entry (all n/m variants in one row);
// ifb_probe_dump() prints and resets them once a second, so they are per-second rates.
#define IFB_SITE_MAX 64
struct IfbSite
{
	const sh4_opcodelistentry* desc;
	u32 hits;
	IfbBracket br;	// cached ifb_bracket() result, for the dump's cost columns
};
static IfbSite s_ifb_sites[IFB_SITE_MAX];
static u32 s_ifb_site_n = 0;
static u32 s_ifb_overflow = 0;

// Compile-time (not run-time) lookup: returns the counter this site should
// bump, or 0 if the registry is full.
static u32* ifb_probe_slot(u16 sh4op)
{
	const sh4_opcodelistentry* d = OpDesc[sh4op];

	for (u32 i=0;i<s_ifb_site_n;i++)
		if (s_ifb_sites[i].desc==d)
			return &s_ifb_sites[i].hits;

	if (s_ifb_site_n>=IFB_SITE_MAX)
	{
		s_ifb_overflow++;
		return 0;
	}

	IfbSite& s=s_ifb_sites[s_ifb_site_n];
	s.desc=d;
	s.hits=0;
	s.br=ifb_bracket(sh4op);
	return &s_ifb_sites[s_ifb_site_n++].hits;
}

extern "C" void ifb_probe_dump(double seconds)
{
	if (!get_ifb_probe_preset() || s_ifb_site_n==0 || seconds<=0.0)
		return;

	// Order by hit count, descending. Selection sort over <=64 entries once a
	// second is free, and it avoids dragging qsort into the build.
	u32 idx[IFB_SITE_MAX];
	u32 n=0;
	for (u32 i=0;i<s_ifb_site_n;i++)
		if (s_ifb_sites[i].hits)
			idx[n++]=i;

	if (n==0)
		return;

	for (u32 a=0;a<n;a++)
	{
		u32 best=a;
		for (u32 b=a+1;b<n;b++)
			if (s_ifb_sites[idx[b]].hits>s_ifb_sites[idx[best]].hits)
				best=b;
		u32 t=idx[a]; idx[a]=idx[best]; idx[best]=t;
	}

	// Cost of the legacy everything-spill, for the comparison column.
	IfbBracket full_br={IFB_GPR_ALL,true};
	const u32 full_cost = ifb_bracket_memops(full_br);

	u32 total=0, narrowed=0;
	double cost_full=0.0, cost_sel=0.0;
	for (u32 a=0;a<n;a++)
	{
		const IfbSite& s=s_ifb_sites[idx[a]];
		const u32 cost=ifb_bracket_memops(s.br);
		total += s.hits;
		cost_full += (double)s.hits*full_cost;
		cost_sel  += (double)s.hits*cost;
		if (cost<full_cost)
			narrowed += s.hits;
	}

	// Name the presets in force, so an A/B log is verifiable. Read at codegen, so it matches the blocks only
	// if set before the game launched.
	printf("[IFB] flush=%s fpu_pin=%s  %.2fs  total %u (%.0f/s)  narrowable %u (%.1f%%)  bracket memops/s: full %.0f -> sel %.0f (-%.1f%%)\n",
		get_ifb_flush_preset()?"ON":"OFF",
		get_fpu_pin_preset()?"ON":"OFF",
		seconds, total, total/seconds,
		narrowed, total?(narrowed*100.0/total):0.0,
		cost_full/seconds, cost_sel/seconds,
		cost_full>0.0?((cost_full-cost_sel)*100.0/cost_full):0.0);

	for (u32 a=0;a<n && a<12;a++)
	{
		const IfbSite& s=s_ifb_sites[idx[a]];
		u32 pop=0;
		for (u32 b=0;b<16;b++)
			if ((s.br.gprs&(1u<<b)) && GetIntReg(reg_r0+b)!=ppc_rinvalid)
				pop++;
		printf("[IFB]   %-34s %8u %8.0f/s  %2u gpr%s %3u ops (full %u)\n",
			s.desc->diss, s.hits, s.hits/seconds,
			pop, s.br.fpu?"+fpu":"    ",
			ifb_bracket_memops(s.br), full_cost);
	}

	if (s_ifb_overflow)
		printf("[IFB]   (%u distinct opcodes did not fit the %u-entry registry)\n",
			s_ifb_overflow, (u32)IFB_SITE_MAX);

	for (u32 i=0;i<s_ifb_site_n;i++)
		s_ifb_sites[i].hits=0;

	fflush(stdout);
}

// JIT_HOTBLOCKS (plan 5.18): entry and cold-entry counts per block, summed over dense windows (vt > 3K);
// the [HOTC] census ranks blocks by entries x hot instructions and weighs ps3_jit_hot.h's classes by entries.
extern "C" int get_hotblocks_preset();

struct HotBlock
{
	u32 sh4_pc;
	u32 sh4_size;	// bytes of SH4 source the block consumed
	u32 sh4_ops;
	u8* ppc_start;
	u32 ppc_size;	// hot body bytes, without the probe's 4-instruction counter
	u32 ppc_cold;	// out-of-line bytes after the hot body
	u32 probe_ofs;	// the counter's byte offset in the hot body
	u32 cold_frags;	// cold fragments that count their entries (0 when the block is too large)
	u64 dense_hits;
	u64 dense_cold;
	u16 cls[HC_COUNT];
};

// ~15k blocks compile after Crazy Taxi's last cache clear. ~3.3 MB, allocated only with the preset on.
#define HOT_MAX 32768
static HotBlock* s_hb = 0;
static u32* s_hb_hits = 0;	// bumped by each block's entry counter
static u32* s_hb_cold = 0;	// bumped at each counted cold-fragment entry
static bool s_hb_failed = false;
static u32 s_hb_n = 0;
static u32 s_hb_overflow = 0;
static u32 s_hb_dense_n = 0;	// dense windows summed since boot or the last cache clear
static double s_hb_dense_s = 0.0;
static u32 s_hb_census_n = 0;

// Dumps: the heaviest blocks not dumped yet, a few per dense window once the ranking has settled. By pc,
// so the same code recompiled after a cache clear is not dumped twice.
#define HOT_DUMP_MAX        128
#define HOT_DUMP_AFTER      5
#define HOT_DUMP_PER_WINDOW 4
#define HOT_CENSUS_EVERY    10
#define HOT_CENSUS_TOP      64
static u32 s_hb_dumped[HOT_DUMP_MAX];
static u32 s_hb_dumped_n = 0;

static double hot_weight(u32 i)
{
	return (double)s_hb[i].dense_hits * s_hb[i].cls[HC_INSN];
}

static bool hot_dumped(u32 pc)
{
	for (u32 i=0;i<s_hb_dumped_n;i++)
		if (s_hb_dumped[i]==pc)
			return true;
	return false;
}

// The k heaviest blocks, heaviest first; with undumped, only pcs not dumped yet.
static u32 hot_top(u32* out,u32 k,bool undumped)
{
	u32 n=0;
	for (u32 i=0;i<s_hb_n;i++)
	{
		const double w=hot_weight(i);
		if (w<=0.0 || (n==k && w<=hot_weight(out[k-1])))
			continue;
		if (undumped && hot_dumped(s_hb[i].sh4_pc))
			continue;
		u32 j=(n<k) ? n++ : k-1;
		while (j>0 && hot_weight(out[j-1])<w)
		{
			out[j]=out[j-1];
			j--;
		}
		out[j]=i;
	}
	return n;
}

// The log sends one UDP datagram per printf, so dump and census lines go out in batches of up to 1400 bytes.
struct HotOut
{
	char buf[1400];
	int n;
	HotOut() : n(0) {}
	~HotOut() { flush(); }
	void flush()
	{
		if (n)
			printf("%s",buf);
		n=0;
	}
	void line(const char* fmt,...) __attribute__((format(printf,2,3)))
	{
		char l[512];
		va_list ap;
		va_start(ap,fmt);
		int k=vsnprintf(l,sizeof(l),fmt,ap);
		va_end(ap);
		if (k<0)
			return;
		if (k>(int)sizeof(l)-1)
			k=sizeof(l)-1;
		if (n+k>=(int)sizeof(buf))
			flush();
		memcpy(buf+n,l,k);
		n+=k;
		buf[n]=0;
	}
};

// Totals over the dense windows, then the top blocks: entries/s, share and running share of the weight,
// sizes, counted cold entries/s and the block's classes (per entry).
static void hot_census(const char* why)
{
	if (!s_hb || !s_hb_n || s_hb_dense_s<=0.0)
		return;

	double tot[HC_COUNT]={0}, hits=0.0, cold=0.0, dumped_w=0.0;
	for (u32 i=0;i<s_hb_n;i++)
	{
		const HotBlock& h=s_hb[i];
		if (!h.dense_hits && !h.dense_cold)
			continue;
		hits+=(double)h.dense_hits;
		cold+=(double)h.dense_cold;
		for (u32 c=0;c<HC_COUNT;c++)
			tot[c]+=(double)h.dense_hits*h.cls[c];
		if (hot_dumped(h.sh4_pc))
			dumped_w+=hot_weight(i);
	}
	if (hits<=0.0)
		return;

	HotOut o;
	const double s=s_hb_dense_s, all_w=tot[HC_INSN];
	o.line("[HOTC] census %u (%s): %u dense windows %.1f s, %u blocks, entries/s %.0f, hot insn/entry %.2f,"
	       " cold entries/s %.0f, %u dumped = %.1f%% of the weight\n",
	       ++s_hb_census_n, why, s_hb_dense_n, s, s_hb_n, hits/s, all_w/hits, cold/s,
	       s_hb_dumped_n, all_w>0.0 ? dumped_w*100.0/all_w : 0.0);

	char buf[512];
	int m=snprintf(buf,sizeof(buf),"[HOTC] per entry:");
	for (u32 c=0;c<HC_COUNT && m<(int)sizeof(buf);c++)
		m+=snprintf(buf+m,sizeof(buf)-m," %s %.3f",k_hc_name[c],tot[c]/hits);
	o.line("%s\n",buf);

	u32 top[HOT_CENSUS_TOP];
	const u32 tn=hot_top(top,HOT_CENSUS_TOP,false);
	m=snprintf(buf,sizeof(buf),"[HOTC] # pc host ent/s w%% cum%% ops hot cold frags coldin/s |");
	for (u32 c=HC_LD;c<HC_COUNT && m<(int)sizeof(buf);c++)
		m+=snprintf(buf+m,sizeof(buf)-m," %s",k_hc_name[c]);
	o.line("%s\n",buf);

	double cum=0.0;
	for (u32 a=0;a<tn;a++)
	{
		const HotBlock& h=s_hb[top[a]];
		const double w=hot_weight(top[a]);
		cum+=w;
		m=snprintf(buf,sizeof(buf),"[HOTC] %u %08X %08X %.0f %.2f %.1f %u %u %u %u %.0f |",
		           a+1, h.sh4_pc, (u32)(uintptr_t)h.ppc_start, (double)h.dense_hits/s,
		           w*100.0/all_w, cum*100.0/all_w, h.sh4_ops, h.ppc_size, h.ppc_cold,
		           h.cold_frags, (double)h.dense_cold/s);
		for (u32 c=HC_LD;c<HC_COUNT && m<(int)sizeof(buf);c++)
			m+=snprintf(buf+m,sizeof(buf)-m," %u",h.cls[c]);
		o.line("%s%s\n",buf,hot_dumped(h.sh4_pc) ? " D" : "");
	}
	o.flush();
	fflush(stdout);
}

// Called from recSh4_ClearCache (dc/sh4/rec_v2/driver.cpp): the code these entries describe is gone.
extern "C" void hotblocks_reset()
{
	if (s_hb_dense_n)
		hot_census("cache clear");
	s_hb_n = 0;
	s_hb_overflow = 0;
	s_hb_dense_n = 0;
	s_hb_dense_s = 0.0;
}

// Compile time: claim a slot for this block, or -1. The index reaches the descriptor and both counters.
static s32 hotblocks_slot(DecodedBlock* block)
{
	if (!get_hotblocks_preset() || s_hb_failed)
		return -1;

	if (!s_hb)
	{
		s_hb = (HotBlock*)malloc(sizeof(HotBlock)*HOT_MAX);
		s_hb_hits = (u32*)malloc(sizeof(u32)*HOT_MAX);
		s_hb_cold = (u32*)malloc(sizeof(u32)*HOT_MAX);
		// Generated code reaches the counters with lis + a 16-bit offset, so they must lie below 2 GB.
		const uintptr_t hi = (uintptr_t)(s_hb_hits>s_hb_cold ? s_hb_hits : s_hb_cold) + sizeof(u32)*HOT_MAX;
		if (!s_hb || !s_hb_hits || !s_hb_cold || hi>=0x7FFF0000u)
		{
			printf("[HOT] disabled: table %p, counters %p %p\n",(void*)s_hb,(void*)s_hb_hits,(void*)s_hb_cold);
			free(s_hb);      s_hb = 0;
			free(s_hb_hits); s_hb_hits = 0;
			free(s_hb_cold); s_hb_cold = 0;
			s_hb_failed = true;
			return -1;
		}
		printf("[HOT] table of %u blocks, %u KB\n",(u32)HOT_MAX,(u32)((sizeof(HotBlock)+8)*HOT_MAX/1024));
	}

	if (s_hb_n >= HOT_MAX)
	{
		s_hb_overflow++;
		return -1;
	}

	const s32 idx = (s32)s_hb_n++;
	HotBlock* h = &s_hb[idx];
	memset(h,0,sizeof(*h));
	h->sh4_pc   = block->start;
	h->sh4_size = block->sh4_code_size;
	h->sh4_ops  = block->opcodes;
	s_hb_hits[idx] = 0;
	s_hb_cold[idx] = 0;
	return idx;
}

// SH4 source and every PPC word (hot body with the counter, then the cold area), 8 words per line.
static void hotblocks_disasm(const HotBlock& h)
{
	HotOut o;
	const u32 raw = h.ppc_size+16;
	const double s = s_hb_dense_s>0.0 ? s_hb_dense_s : 1.0;
	o.line("[HOT] --- dump pc=%08X host=%08X: %u SH4 ops / %u B -> hot %u B (counter at +%X) cold %u B,"
	       " %u counted frags | ent/s %.0f coldin/s %.0f\n",
	       h.sh4_pc, (u32)(uintptr_t)h.ppc_start, h.sh4_ops, h.sh4_size, raw, h.probe_ofs, h.ppc_cold,
	       h.cold_frags, (double)h.dense_hits/s, (double)h.dense_cold/s);

	u32 n = h.sh4_size/2;
	if (n > 256) n = 256;
	for (u32 i=0;i<n;i++)
	{
		const u32 a = h.sh4_pc + i*2;
		const u16 op = ReadMem16(a);
		o.line("[HOT] sh4 %08X %04X %s\n", a, op, OpDesc[op]->diss);
	}

	const u32* w = (const u32*)h.ppc_start;
	u32 wn = (raw+h.ppc_cold)/4;
	if (wn > 2048) wn = 2048;
	for (u32 i=0;i<wn;i+=8)
	{
		char buf[128];
		int m=snprintf(buf,sizeof(buf),"[HOT] ppc +%04X",i*4);
		for (u32 j=0;j<8 && i+j<wn;j++)
			m+=snprintf(buf+m,sizeof(buf)-m," %08X",w[i+j]);
		o.line("%s\n",buf);
	}
}

// Plan 5.25's [idle] lines, while idle loops run: checks and skips per second, the busiest idle blocks
// (checks/skips per second), then once per pc the SH4 code from its branch target to its end.
#define IDLE_TOP 6
#define IDLE_DUMP_MAX 64
static void idle_dump(double seconds)
{
	static u32 dumped_n;
	double chk=0.0, skp=0.0, tc[IDLE_TOP], ts[IDLE_TOP];
	u32 top[IDLE_TOP], tn=0;
	for (u32 i=0;i<s_idle_n;i++)
	{
		const double c=(double)(u32)(s_idle_ent[i].checks-s_idle_last[i].checks);
		const double s=(double)(u32)(s_idle_ent[i].skips-s_idle_last[i].skips);
		s_idle_last[i]=s_idle_ent[i];
		chk+=c;
		skp+=s;
		if (c<=0.0 || (tn==IDLE_TOP && c<=tc[IDLE_TOP-1]))
			continue;
		u32 j=(tn<IDLE_TOP) ? tn++ : IDLE_TOP-1;
		while (j>0 && tc[j-1]<c)
		{
			top[j]=top[j-1]; tc[j]=tc[j-1]; ts[j]=ts[j-1];
			j--;
		}
		top[j]=i; tc[j]=c; ts[j]=s;
	}
	if (chk<=0.0)
		return;

	HotOut o;
	char buf[400];
	int m=snprintf(buf,sizeof(buf),"[idle] blocks=%u checks/s %.0f skips/s %.0f | top",s_idle_n,chk/seconds,skp/seconds);
	for (u32 a=0;a<tn && m<(int)sizeof(buf);a++)
		m+=snprintf(buf+m,sizeof(buf)-m," %08X %.0f/%.0f",s_idle_meta[top[a]].pc,tc[a]/seconds,ts[a]/seconds);
	o.line("%s\n",buf);

	for (u32 a=0;a<tn && dumped_n<IDLE_DUMP_MAX;a++)
	{
		IdleMeta& mt=s_idle_meta[top[a]];
		if (mt.dumped)
			continue;
		mt.dumped=true;
		dumped_n++;
		// A plain loop is one range; otherwise its head and the idle block.
		const bool one=mt.loop_hi>mt.loop_lo && mt.loop_hi-mt.loop_lo<=96;
		o.line("[idle] code %08X, loop %08X-%08X:\n",mt.pc,mt.loop_lo,mt.loop_hi);
		// Not `r`: sh4_registers.h defines it as a macro.
		const u32 rng[2][2]={ { mt.loop_lo, one ? mt.loop_hi : mt.loop_lo+24 }, { mt.pc, one ? mt.pc : mt.loop_hi } };
		for (u32 k=0;k<2;k++)
			for (u32 pc=rng[k][0];pc<rng[k][1];pc+=2)
			{
				const u16 op=ReadMem16(pc);
				o.line("[idle]  %08X %04X %s\n",pc,op,OpDesc[op]->diss);
			}
	}
}

// Stats window (SPG.cpp, once a second, SH4 thread): one line per window; dense windows add to the census.
extern "C" void hotblocks_dump(double seconds,double vt_k)
{
	recSh4_CacheDump(seconds);

	// The stats window calls this every second, so plan 5.22's [cyc] line rides on it (after new compiles only).
	static u32 cyc_last;
	if (get_jit_cycle_check_preset() && s_cycle_poll+s_cycle_nopoll!=cyc_last)
	{
		cyc_last=s_cycle_poll+s_cycle_nopoll;
		printf("[cyc] mode=%d blocks: poll=%u nopoll=%u\n", get_jit_cycle_check_preset(), s_cycle_poll, s_cycle_nopoll);
	}

	static u32 tc_last;
	if (get_jit_tcopy_preset() && s_tc_copy+s_tc_fwd+s_tc_load!=tc_last)
	{
		tc_last=s_tc_copy+s_tc_fwd+s_tc_load;
		printf("[tcopy] jcond sites: copy=%u fwd=%u load=%u miss=%u\n", s_tc_copy, s_tc_fwd, s_tc_load, s_tc_miss);
	}

	static u32 ppe_last;
	const u32 ppe_n=s_ppe_focrf+s_ppe_flds+s_ppe_fsts+s_ppe_ftrc+s_ppe_float+s_frchg_sites+s_pref_sites+s_gr_reuses;
	if (ppe_n!=ppe_last)
	{
		ppe_last=ppe_n;
		printf("[ppe] sites: mfocrf=%u flds=%u fsts=%u ftrc=%u float=%u frchg=%u pref=%u greuse=%u/%u\n",
		       s_ppe_focrf, s_ppe_flds, s_ppe_fsts, s_ppe_ftrc, s_ppe_float, s_frchg_sites, s_pref_sites,
		       s_gr_validates, s_gr_reuses);
	}

	if (get_jit_idle_skip_preset() && seconds>0.0)
		idle_dump(seconds);

	// Plan 5.3's [dynic] line: sites compiled by kind and filled since boot, misses of filled sites per second.
	static u32 ic_last[5];
	if (get_dyn_ic_preset() && seconds>0.0)
	{
		const u32 sites=s_ic_sites[0]+s_ic_sites[1]+s_ic_sites[2];
		const u32 m0=s_ic_miss[0]-ic_last[2], m1=s_ic_miss[1]-ic_last[3], m2=s_ic_miss[2]-ic_last[4];
		if (sites!=ic_last[0] || s_ic_fills!=ic_last[1] || (m0|m1|m2))
			printf("[dynic] sites call=%u jump=%u ret=%u filled=%u | miss/s call %.0f jump %.0f ret %.0f\n",
			       s_ic_sites[0], s_ic_sites[1], s_ic_sites[2], s_ic_fills, m0/seconds, m1/seconds, m2/seconds);
		ic_last[0]=sites; ic_last[1]=s_ic_fills;
		ic_last[2]=s_ic_miss[0]; ic_last[3]=s_ic_miss[1]; ic_last[4]=s_ic_miss[2];
	}

	if (!get_hotblocks_preset() || !s_hb || s_hb_n==0 || seconds<=0.0)
		return;

	const bool dense = vt_k > 3.0;
	double hits=0.0, insn=0.0, cold=0.0;
	for (u32 i=0;i<s_hb_n;i++)
	{
		const u32 e=s_hb_hits[i], c=s_hb_cold[i];
		if (!e && !c)
			continue;
		hits+=e;
		insn+=(double)e*s_hb[i].cls[HC_INSN];
		cold+=c;
		if (dense)
		{
			s_hb[i].dense_hits+=e;
			s_hb[i].dense_cold+=c;
		}
		s_hb_hits[i]=0;
		s_hb_cold[i]=0;
	}
	printf("[HOT] %.2fs dense=%d vt %.1fK: %u blocks, entries/s %.0f, hot insn/entry %.2f, cold entries/s %.0f\n",
	       seconds, dense?1:0, vt_k, s_hb_n, hits/seconds, hits>0.0 ? insn/hits : 0.0, cold/seconds);
	if (s_hb_overflow)
		printf("[HOT] (%u blocks did not fit the %u-entry table)\n", s_hb_overflow, (u32)HOT_MAX);

	if (dense)
	{
		s_hb_dense_n++;
		s_hb_dense_s+=seconds;
		if (s_hb_dense_n>=HOT_DUMP_AFTER && s_hb_dumped_n<HOT_DUMP_MAX)
		{
			u32 pick[HOT_DUMP_PER_WINDOW];
			const u32 pn=hot_top(pick,HOT_DUMP_PER_WINDOW,true);
			for (u32 a=0;a<pn && s_hb_dumped_n<HOT_DUMP_MAX;a++)
			{
				s_hb_dumped[s_hb_dumped_n++]=s_hb[pick[a]].sh4_pc;
				hotblocks_disasm(s_hb[pick[a]]);
			}
		}
		if (s_hb_dense_n%HOT_CENSUS_EVERY==0)
			hot_census("periodic");
	}

	fflush(stdout);
}

void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);

// Native call targets for fsqrt/fsrra, same semantics as the canonical sqrtf / 1.0f/sqrtf.
static f32 rec_fsqrt(f32 x)  { CCALL(CC_FSQRT); return sqrtf(x); }

// JIT_FSQRT: GCC already compiles sqrtf to the PPU's fsqrts (-mcpu=cell), so inlining it is
// bit-identical to rec_fsqrt and only drops the call. Hand-encoded: the emitter has no fsqrts.
static void ppc_fsqrts(u32 D,u32 B) { ppc_emit(0xEC00002C | (D<<21) | (B<<11)); }
static const f32 s_fsrra_one = 1.0f;
static f32 rec_fsrra(f32 x)  { CCALL(CC_FSRRA); return 1.0f / sqrtf(x); }

// JIT FTRV (plan 5.20): fldi0 / fldi1 constants, and FV elements as ps3_jit_fv.h takes them.
static const u32 s_fldi_tab[2] = { 0x00000000, 0x3F800000 };

// JIT FPUL MOVES (plan 5.24): float's subtrahend 2^52 + 2^31, loaded with lfd, not built in jit_scratch.
static const u64 s_i2f_bias = 0x4330000080000000ull;

// JIT FPUL MOVES: an integer register an FPR store may write in place (unpinned; not sr_T, whose store also feeds
// JIT T COPY), or an FPR load may read in place (unpinned).
static bool fpul_slot_dst(const shil_param& p)
{
	return p.is_r32i() && GetIntReg(p._reg)==ppc_rinvalid && p._reg!=reg_sr_T;
}
static bool fpul_slot_src(const shil_param& p)
{
	return p.is_r32i() && GetIntReg(p._reg)==ppc_rinvalid;
}
static FvElem fv_elem(u32 sh4_reg)
{
	FvElem e;
	const ppc_freg rf=get_fpu_pin_preset() ? GetFloatReg(sh4_reg) : ppc_finvalid;
	e.reg=rf!=ppc_finvalid ? (u32)rf : FV_MEM;
	e.ofs=Sh4cntx.offset(sh4_reg);
	return e;
}

// emit_ftrv wants the vector updated in place and XMTRX as the unpinned, contiguous xf bank.
static bool ftrv_v2_ok(shil_opcode* op)
{
	if (op->rd._reg!=op->rs1._reg)
		return false;
	const u32 x=Sh4cntx.offset(op->rs2._reg);
	for (u32 k=0;k<16;k++)
		if (GetFloatReg(op->rs2._reg+k)!=ppc_finvalid || Sh4cntx.offset(op->rs2._reg+k)!=x+4*k)
			return false;
	return true;
}

// ---- Operation compilation ----

// FASTMEM (Dynarec.MD 3.4), preset `JitFastmem`. Fault-free variant: main RAM
// is inlined, every other region goes to the cold fragment. No DSI handler.

extern "C" int get_jit_fastmem_preset();

static u32  s_fm_ram_hi;	// mem_b.data >> 16, for the single addis
static bool s_fm_ok;		// RAM block fits the one-addis shape below

// Idempotent. Needs _vmem_reserve() done and must precede the first compile.
static void ps3_fastmem_init()
{
	static bool announced;
	s_fm_ok=false;
	s_fm_ram_hi=0;

	const uintptr_t p=(uintptr_t)mem_b.data;
	// One addis needs the low 16 bits clear, and sub-2 GB because it sign-extends.
	if (!p || (p & 0xFFFF) || (p + RAM_SIZE) > 0x7FFF0000u)
	{
		if (!announced)
			printf("[fastmem] disabled: RAM at %p is not a 64 KB-aligned"
			       " sub-2 GB block\n", mem_b.data);
		announced=true;
		return;
	}
	s_fm_ram_hi=(u32)(p>>16);
	s_fm_ok=true;
	if (!announced)
		printf("[fastmem] RAM window %p, preset %s\n",
		       mem_b.data, get_jit_fastmem_preset()?"ON":"OFF");
	announced=true;
}

static inline bool fm_on()
{
	return s_fm_ok && get_jit_fastmem_preset()!=0;
}

// CR0 lt <=> main RAM in any P0-P3 mirror (region and area bits rotated together); region 7 (P4, store
// queue) takes the generic path. Scratch is r0 only: the cold path needs the original address.
static void fm_emit_guard(u32 areg)
{
	ppc_rlwinmx(ppc_r0,areg,3,29,2,0);	// r0 = (area<<29) | region
	ppc_xoris(ppc_r0,ppc_r0,0x6000);	// area 3 -> 0
	ppc_cmpli(ppc_cr0,ppc_r0,7,0);		// < 7: area 3 and region != 7
}

// rT = mem_b.data + (addr & 0xFFFFFF), sub-word swizzle folded in; rlwinm zero-extends, so a poisoned
// upper half (6.2 bug 5) cannot reach the host access.
static void fm_emit_addr(u32 rT,u32 areg,u32 sz)
{
	ppc_rlwinmx(rT,areg,0,8,31,0);		// 16 MB mirror mask
	ppc_addis(rT,rT,s_fm_ram_hi);
	if (sz==1 || sz==2)
		ppc_xori(rT,rT,4-sz);
}

// Cold paths: the fast path falls through past one not-taken branch; FlushCold() emits each ColdFrag after
// ngen_End, patches the forward branch to it, and ends it with `b done` back into the block.
struct ColdFrag
{
	ppc_label* beq;     // the forward beq site to patch to the cold entry
	void*      done;    // join point in the main stream (backward target)
	u8         kind;    // 0=read scalar, 1=read pair(64), 2=write scalar, 3=write pair
	u8         sz;      // 1/2/4 (scalar)
	u8         rdreg;   // read: destination reg (rrv0 or pinned)
	u8         datareg; // write: data reg (rarg1 or pinned)
	u8         areg;    // address source reg (rarg0 or pinned)
	// FPU_PIN: a pinned-FR fast path uses lfs/stfs; the C path bounces through the FR's slot (fofs; pairs
	// fdreg+1 at fofs+4). 0xFF = int path; set on every registration, as s_cold is reused.
	u8         fdreg;   // pinned FPR for the float bounce, or 0xFF = int path
	u16        fofs;    // context byte offset of the FR's memory slot
	void*      fuct;    // slow C function (ReadMem*/WriteMem*); 0 -> default by sz
	// FASTMEM: the inline path covers main RAM only, so the fragment runs the generic _vmem table tier (1)
	// before the C call; 2 (plan 5.27) tests for the store queue first.
	u8         vmem_tier;
	u8         gr_bit;  // plan 5.31: the CR flag its entry clears (the check it validates failed), 0xFF = none
};
// Cannot overflow: a block emits < 4096 instructions (ngen_Compile needs 16 KB free) and a mem op >= 4.
static ColdFrag s_cold[1024];
static u32      s_cold_n;

static void ColdReset() { s_cold_n = 0; }

static ColdFrag& cold_new()
{
	ColdFrag& c = s_cold[s_cold_n++];
	c.gr_bit = 0xFF;
	return c;
}

// _vmem table tier: iirf = MemInfo[addr>>24], ptr = iirf & ~0x1F, access at ptr + ((addr<<sh)>>sh); 8-byte
// entries, BE low word at +4. Returns the MMIO branch's label; areg/datareg/rarg0 survive to it.
static ppc_label* emit_vmem_tier(const ColdFrag& c)
{
	const bool fdirect = c.fdreg!=0xFF;
	ppc_label* mmio;

	if (c.kind==0)			// --- read scalar -> rdreg ---
	{
		ppc_rlwinmx(ppc_r0,c.areg,11,21,28,0);		// (addr>>24)*8
		u32 lo=ppc_addr_high(ppc_rarg1,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
		if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
		ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
		ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);	// rarg2 = ptr
		ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);		// iirf < 0x20 ? (MMIO)

		mmio=ppc_CreateLabel();
		ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt -> MMIO

		// shift count = iirf directly: ptr is >=0x40-aligned, so iirf's low 6
		// bits are the shift and slw/srw only look at those.
		ppc_slwx(ppc_r0,c.areg,ppc_rarg1,0);
		ppc_srwx(ppc_r0,ppc_r0,ppc_rarg1,0);		// mirror mask
		if (c.sz<4)
			ppc_xori(ppc_r0,ppc_r0,4-c.sz);
		if (fdirect)
			ppc_lfsx(c.fdreg,ppc_rarg2,ppc_r0);
		else if (c.sz==1)
		{
			ppc_lbzx(c.rdreg,ppc_rarg2,ppc_r0);
			ppc_extsbx(c.rdreg,c.rdreg,0);
		}
		else if (c.sz==2)
		{
			ppc_lhzx(c.rdreg,ppc_rarg2,ppc_r0);
			ppc_extshx(c.rdreg,c.rdreg,0);
		}
		else
			ppc_lwzx(c.rdreg,ppc_rarg2,ppc_r0);
	}
	else if (c.kind==1)		// --- read pair (64) -> rrv0:rrv1 ---
	{
		ppc_rlwinmx(ppc_r0,ppc_rarg0,11,21,28,0);
		u32 lo=ppc_addr_high(ppc_rarg1,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
		if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
		ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
		ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);	// rarg2 = ptr (!= r0)
		ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);

		mmio=ppc_CreateLabel();
		ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt -> MMIO

		// Replicates the BE *(u64*)p load: word[addr] -> rrv0, word[addr+4] -> rrv1,
		// which is the ReadMem64 register pair.
		ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);
		ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);
		if (fdirect)
		{
			ppc_lfsx(c.fdreg,ppc_rarg2,ppc_rarg0);
			ppc_addi(ppc_rarg0,ppc_rarg0,4);
			ppc_lfsx(c.fdreg+1,ppc_rarg2,ppc_rarg0);
		}
		else
		{
			ppc_lwzx(ppc_rarg3,ppc_rarg2,ppc_rarg0);
			ppc_addi(ppc_rarg0,ppc_rarg0,4);
			ppc_lwzx(ppc_rrv1,ppc_rarg2,ppc_rarg0);
			ppc_ori(ppc_rrv0,ppc_rarg3,0);
		}
	}
	else if (c.kind==2)		// --- write scalar ---
	{
		ppc_rlwinmx(ppc_r0,c.areg,11,21,28,0);
		u32 lo=ppc_addr_high(ppc_rarg2,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
		if (lo) ppc_addi(ppc_rarg2,ppc_rarg2,lo);
		ppc_lwzx(ppc_rarg2,ppc_rarg2,ppc_r0);		// rarg2 = iirf
		// ptr must not be r0: an indexed store reads base r0 as literal zero.
		ppc_rlwinmx(ppc_rarg3,ppc_rarg2,0,0,26,0);	// rarg3 = ptr
		ppc_cmpli(ppc_cr0,ppc_rarg2,0x20,0);		// keeps iirf alive as the shift

		mmio=ppc_CreateLabel();
		ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt -> MMIO

		ppc_slwx(ppc_r0,c.areg,ppc_rarg2,0);
		ppc_srwx(ppc_r0,ppc_r0,ppc_rarg2,0);
		if (c.sz<4)
			ppc_xori(ppc_r0,ppc_r0,4-c.sz);
		if (fdirect)
			ppc_stfsx(c.fdreg,ppc_rarg3,ppc_r0);
		else if (c.sz==1)
			ppc_stbx(c.datareg,ppc_rarg3,ppc_r0);
		else if (c.sz==2)
			ppc_sthx(c.datareg,ppc_rarg3,ppc_r0);
		else
			ppc_stwx(c.datareg,ppc_rarg3,ppc_r0);
	}
	else				// --- write pair (64), data in rarg2:rarg3 ---
	{
		ppc_rlwinmx(ppc_r0,ppc_rarg0,11,21,28,0);
		u32 lo=ppc_addr_high(ppc_rarg1,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
		if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
		ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
		// ppc_andi is the RECORD form and clobbers CR0, so the MMIO compare must
		// come after it and before the rlwinm overwrites iirf.
		ppc_andi(ppc_r0,ppc_rarg1,0x1F);		// r0 = shift
		ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);
		ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,0,26,0);	// rarg1 = ptr

		mmio=ppc_CreateLabel();
		ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt -> MMIO

		// BE *(u64*)p store: word[addr] = high, word[addr+4] = low.
		ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
		ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
		if (fdirect)
		{
			ppc_stfsx(c.fdreg,ppc_rarg1,ppc_rarg0);
			ppc_addi(ppc_rarg0,ppc_rarg0,4);
			ppc_stfsx(c.fdreg+1,ppc_rarg1,ppc_rarg0);
		}
		else
		{
			ppc_stwx(ppc_rarg2,ppc_rarg1,ppc_rarg0);
			ppc_addi(ppc_rarg0,ppc_rarg0,4);
			ppc_stwx(ppc_rarg3,ppc_rarg1,ppc_rarg0);
		}
	}

	return mmio;
}


// cold_ctr (JIT HOTBLOCKS): count each fragment's entries, 20 B more per fragment, only while hot body + cold
// area stay far inside the 32 KB branch reach. Returns the fragments counted.
static u32 FlushCold(u32* cold_ctr)
{
	const u32 frags = s_cold_n;
	const bool count = cold_ctr && frags && frags<=128 && (u8*)emit_GetCCPtr()-(u8*)s_cold[0].beq < 16384;
	for (u32 i = 0; i < s_cold_n; i++)
	{
		ColdFrag& c = s_cold[i];
		// The forward branch has a 16-bit displacement; ngen_Compile's 16 KB limit keeps cold code in range.
		snat beq_disp = (u8*)emit_GetCCPtr() - (u8*)c.beq;
		verify(beq_disp >= 0 && beq_disp < 32768);
		c.beq->MarkLabel();              // patch the forward beq -> here (cold entry)
		if (count)
			emit_hot_cold_count(cold_ctr,c.kind);
		if (c.gr_bit!=0xFF)
			emit_gr_clear(c.gr_bit);     // plan 5.31: its check failed, so the ops that reuse it go cold too

		if (c.vmem_tier==2)
		{
			// Plan 5.27 SLOW PATH: a store-queue address skips the table's load and its two microcoded shifts.
			verify((c.kind==2 && c.sz==4) || (c.kind==3 && c.areg==ppc_rarg0));
			ppc_label* other=emit_sq_first(c.kind==3,c.areg,c.datareg,c.fdreg,c.done,(u32)(uintptr_t)sq_both);
			other->MarkLabel();
		}

		// FASTMEM: try the generic table first, so only true MMIO pays a C call.
		if (c.vmem_tier)
		{
			ppc_label* mmio=emit_vmem_tier(c);
			ppc_bx(c.done,0);        // table hit -> join
			mmio->MarkLabel();       // table miss falls into the call below
		}

		if (c.kind==0)                   // --- read scalar: ReadMem(addr)->rrv0 ---
		{
			if (c.areg!=ppc_rarg0) ppc_ori(ppc_rarg0,c.areg,0);   // mr rarg0,areg
			ppc_clrldi32(ppc_rarg0,ppc_rarg0);                    // ELFv1: caller extends u32
			void* f=c.fuct;
			if (!f) f=(c.sz==1)?(void*)ReadMem8:(c.sz==2)?(void*)ReadMem16:(void*)ReadMem32;
			ppc_call_c(f);
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float dest (sz==4). Bounce rrv0 -> pinned FPR via its
				// context slot (no direct GPR->FPR move on PPC750).
				ppc_stw(ppc_rrv0,ppc_contex,c.fofs);
				ppc_lfs(c.fdreg,ppc_contex,c.fofs);
			}
			else if (c.sz==1)      ppc_extsbx(c.rdreg,ppc_rrv0,0);
			else if (c.sz==2) ppc_extshx(c.rdreg,ppc_rrv0,0);
			else if (c.rdreg!=ppc_rrv0) ppc_ori(c.rdreg,ppc_rrv0,0);
		}
		else if (c.kind==1)              // --- read pair (64): ReadMem64 -> rrv0:rrv1 ---
		{
			ppc_clrldi32(ppc_rarg0,ppc_rarg0);                    // ELFv1: caller extends u32
			void* f=c.fuct? c.fuct : (void*)ReadMem64;
			ppc_call_c(f);
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float pair. rrv0=word[addr]->fdreg, rrv1=word[addr+4]
				// ->fdreg+1, each bounced through its own context slot.
				ppc_stw(ppc_rrv0,ppc_contex,c.fofs);
				ppc_lfs(c.fdreg,ppc_contex,c.fofs);
				ppc_stw(ppc_rrv1,ppc_contex,c.fofs+4);
				ppc_lfs(c.fdreg+1,ppc_contex,c.fofs+4);
			}
		}
		else if (c.kind==2)              // --- write scalar: WriteMem(addr,data) ---
		{
			if (c.areg!=ppc_rarg0)    ppc_ori(ppc_rarg0,c.areg,0);
			ppc_clrldi32(ppc_rarg0,ppc_rarg0);                    // ELFv1: caller extends u32
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float source (sz==4). Bounce pinned FPR -> rarg1 via
				// its context slot, then the normal WriteMem32.
				ppc_stfs(c.fdreg,ppc_contex,c.fofs);
				ppc_lwz(ppc_rarg1,ppc_contex,c.fofs);
				ppc_call_c(&WriteMem32);
			}
			else
			{
				if (c.datareg!=ppc_rarg1) ppc_ori(ppc_rarg1,c.datareg,0);
				// andi clears the upper half for sz 1/2; sz 4 needs the explicit
				// clrldi32, since ori copies a pinned reg's poison straight through.
				if (c.sz==1)      { ppc_andi(ppc_rarg1,ppc_rarg1,0xFF);   ppc_call_c(&WriteMem8); }
				else if (c.sz==2) { ppc_andi(ppc_rarg1,ppc_rarg1,0xFFFF); ppc_call_c(&WriteMem16); }
				else              { ppc_clrldi32(ppc_rarg1,ppc_rarg1);    ppc_call_c(&WriteMem32); }
			}
		}
		else                             // --- write pair (64): WriteMem64 ---
		{
			if (c.fdreg!=0xFF)
			{
				// FPU_PIN: float pair. fdreg=high->rarg2, fdreg+1=low->rarg3
				// (the WriteMem64(u32,u64) EABI regs), each via its context slot.
				ppc_stfs(c.fdreg,ppc_contex,c.fofs);
				ppc_lwz(ppc_rarg2,ppc_contex,c.fofs);
				ppc_stfs(c.fdreg+1,ppc_contex,c.fofs+4);
				ppc_lwz(ppc_rarg3,ppc_contex,c.fofs+4);
			}
			ppc_clrldi32(ppc_rarg0,ppc_rarg0);                    // ELFv1: caller extends u32
			ppc_call_c(&WriteMem64);
		}

		ppc_bx(c.done,0);                // backward unconditional b -> done (join)
	}
	s_cold_n = 0;
	return count ? frags : 0;
}

// FASTMEM (PPC-MMU design, kept for reference; the PS3 has no resumable DSI): branchless rlwinm-EA accesses
// whose faults rec_fastmem_patch() turns into trampolines. Fixed shapes: the EA register tells the decoder.

extern u8 CodeCache[];
extern "C" int get_fastmem_preset();	// ps3_presets.cpp: 0=off (default), 1=on

static inline bool fastmem_on()
{
	// PS3: no resumable DSI handler, so fastmem can never engage (Dynarec.MD 3.4).
	return false;
}

// Trampoline pool in .bss like CodeCache, so `b` reaches it; reset with the block cache, as sites die.
#define FM_POOL_SIZE (128*1024)
static u8  s_fm_pool[FM_POOL_SIZE] __attribute__((aligned(32)));
static u32 s_fm_pool_used   = 0;
static u32 s_fm_patch_count = 0;

// DSI-path failure records: no printf in exception context (newlib's lock can deadlock with EE=0);
// rec_fastmem_reset_pool prints the counts from normal context.
static volatile u32 s_fm_fail_pc, s_fm_fail_insn, s_fm_fail_dar;
static volatile u32 s_fm_fail_reason;	// 1=pc outside cache 2=pool full 3=undecodable 4=branch range
#define FM_RING 32
static u32 s_fm_ring[FM_RING];		// last FM_RING patched site PCs
static u32 s_fm_ring_n;

// JIT_FMOV tripwire: counts the float shapes emitted and announces the first, so a log shows whether the
// switch engaged (it needs both fpu_pin and fastmem).
static u32 s_fm_float_sites = 0;
static void fm_note_float_site()
{
	// Compile time, normal context: printf is OK here (never in the DSI path).
	if (s_fm_float_sites++ == 0)
		printf("[fmov] JIT_FMOV active: emitting lfs/stfs fastmem shapes\n");
}

// Float sites that faulted and got a trampoline: only these prove the DSI decoder's float path ran.
static u32 s_fm_float_patches = 0;

static void rec_fastmem_reset_pool()
{
	// Runs in normal context (bm_Reset / recSh4_ClearCache) — printf is OK.
	if (s_fm_patch_count)
		printf("[fastmem] cache clear: %u sites were patched (%u/%u B pool),"
		       " %u float shapes emitted / %u float sites patched\n",
		       s_fm_patch_count, s_fm_pool_used, FM_POOL_SIZE,
		       s_fm_float_sites, s_fm_float_patches);
	// Tell the [CC] census where a call-out's return address has to land to
	// count as "came from a back-patched trampoline".
	g_ccall_tramp_lo = s_fm_pool;
	g_ccall_tramp_hi = s_fm_pool + FM_POOL_SIZE;
	g_ccall_cc_lo    = CodeCache;
	g_ccall_cc_hi    = CodeCache + CODE_SIZE;
	s_fm_pool_used     = 0;
	s_fm_patch_count   = 0;
	s_fm_ring_n        = 0;
	s_fm_float_sites   = 0;	// the sites die with the cache, like the patches
	s_fm_float_patches = 0;
}

// Raw-word emitter for the DSI handler: independent of emit_ptr (a compile may be in progress), integer
// only (MSR.FP is off there).
static u32* fm_p;
static void fm_w(u32 i)                            { *fm_p++ = i; }
static u32  fm_dform(u32 op6,u32 rt,u32 ra,u32 dd) { return (op6<<26)|(rt<<21)|(ra<<16)|(dd&0xFFFF); }
static void fm_mr(u32 dst,u32 src)                 { if (dst!=src) fm_w(0x7C000378|(src<<21)|(dst<<16)|(src<<11)); }
static bool fm_branch(void* target,u32 lk)
{
	snat disp=(u8*)target-(u8*)fm_p;
	if (disp>=33554432 || disp<-33554432)
		return false;
	fm_w(0x48000000|((u32)disp&0x03FFFFFC)|lk);
	return true;
}

// reg = &sq_both[(areg & 0x3F) ^ swz], the _vmem block layout. The parameter must not be named `r`
// (sh4_registers.h #defines it).
static void fm_sq_addr(u32 reg,u32 areg,u32 swz)
{
	fm_w(0x54000000|(areg<<21)|(reg<<16)|(0<<11)|(26<<6)|(31<<1));	// rlwinm reg,areg,0,26,31
	if (swz)
		fm_w(0x68000000|(reg<<21)|(reg<<16)|swz);		// xori reg,reg,swz
	u32 sq=(u32)(uintptr_t)sq_both;
	s32 lo=(s32)(s16)sq;
	u32 hi=((u32)(sq-(u32)lo))>>16;
	fm_w(fm_dform(15,reg,reg,hi));					// addis reg,reg,hi
	fm_w(fm_dform(14,reg,reg,(u32)lo));				// addi  reg,reg,lo
}

// Context offset of the pinned FR in a float shape (f14+n = fr[n]); usable in exception context, unlike
// Sh4Context::offset(). -1 outside the bank, so a mis-decode cannot write past fr[15].
static int fm_fr_ofs(u32 fd,bool pair)
{
	const u32 lastf = pair ? (u32)(ppc_f14+14) : (u32)(ppc_f14+15);
	if (fd < (u32)ppc_f14 || fd > lastf)
		return -1;
	return (int)(SH4CTX_OFS_FR + (fd-(u32)ppc_f14)*4);
}

// The rlwinm rEA,rADDR,0,3,31 within 3 instructions before the fault: rADDR (still the original SH4
// address), or -1 if absent.
static int fm_find_areg(u32* site,u32 ea_reg)
{
	for (int back=1;back<=3;back++)
	{
		u32 i=site[-back];
		if ((i&0xFC1FFFFF)==(0x54000000|(ea_reg<<16)|0x00FE))
			return (int)((i>>21)&31);
	}
	return -1;
}

// Decode the faulting site, build its trampoline, patch the site; nonzero chains to the panic screen.
// Exception context: integer only, no printf except on failure paths.
#if PS3_FASTMEM_DSI  // no GameOS equivalent; kept for reference only
int rec_fastmem_patch(unsigned int pc)
{
	u32 dar;
	asm volatile("mfspr %0,19" : "=r"(dar));	// DAR = faulting (masked) EA

	// Only faults inside the code cache can be fastmem sites.
	if (pc < (u32)(uintptr_t)&CodeCache[0] || pc >= (u32)(uintptr_t)&CodeCache[0]+CODE_SIZE)
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=0; s_fm_fail_dar=dar; s_fm_fail_reason=1;
		return 1;
	}

	u32* site=(u32*)pc;
	const u32 insn=*site;
	const u32 op6=insn>>26;
	const u32 rt =(insn>>21)&31;	// rD (loads) / rS (stores)
	const u32 ra =(insn>>16)&31;	// base = EA scratch = shape discriminator
	const u32 dd =insn&0xFFFF;

	if (s_fm_pool_used + 32*sizeof(u32) > sizeof(s_fm_pool))
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=insn; s_fm_fail_dar=dar; s_fm_fail_reason=2;
		return 1;
	}

	fm_p=(u32*)&s_fm_pool[s_fm_pool_used];
	u32* const tramp=fm_p;
	bool ok=false;

	if ((op6==32||op6==42||op6==34) && ra==ppc_rarg1 && dd==0)
	{
		// --- scalar read: lbz/lha/lwz rD,0(r4). Replicates ColdFrag kind 0.
		int areg=fm_find_areg(site,ra);
		if (areg>=0)
		{
			ok=true;
			fm_mr(ppc_rarg0,(u32)areg);
			if (op6==34)		// lbz — ReadMem8, then extsb rD,r3
			{
				ok&=fm_branch((void*)ReadMem8,1);
				fm_w(0x7C000774|(ppc_rrv0<<21)|(rt<<16));
			}
			else if (op6==42)	// lha — ReadMem16, then extsh rD,r3
			{
				ok&=fm_branch((void*)ReadMem16,1);
				fm_w(0x7C000734|(ppc_rrv0<<21)|(rt<<16));
			}
			else			// lwz — ReadMem32
			{
				ok&=fm_branch((void*)ReadMem32,1);
				fm_mr(rt,ppc_rrv0);
			}
			// Resume after the load; a trailing in-line extsb (sz==1
			// shape) re-runs on the already-extended value — idempotent.
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==32 && ra==ppc_rarg2 && rt==ppc_rrv0 && dd==0)
	{
		// Pair read, first word (lwz r3,0(r5)): the address is still in r3, so ReadMem64 needs no setup.
		if (fm_find_areg(site,ra)==ppc_rarg0)
		{
			ok=fm_branch((void*)ReadMem64,1);
			ok&=fm_branch(site+2,0);	// skip the second lwz too
		}
	}
	else if (op6==32 && ra==ppc_rarg2 && rt==ppc_rrv1 && dd==4)
	{
		// --- pair read, second word alone (straddling pair; see LIMITATION).
		if (fm_find_areg(site,ra)>=0)
		{
			ok=true;
			fm_mr(ppc_rarg3,ppc_rrv0);			// save word[addr]
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg2,4));	// addi r3,r5,4 (masked EA+4)
			ok&=fm_branch((void*)ReadMem32,1);
			fm_mr(ppc_rrv1,ppc_rrv0);
			fm_mr(ppc_rrv0,ppc_rarg3);
			ok&=fm_branch(site+1,0);
		}
	}
	else if ((op6==36||op6==44||op6==38) && ra==ppc_rarg2 && dd==0)
	{
		// --- scalar write: stb/sth/stw rS,0(r5). Replicates ColdFrag kind 2,
		// with an inlined SQ fast path for area-0-image faults.
		int areg=fm_find_areg(site,ra);
		if (areg>=0)
		{
			const u32 sz=(op6==38)?1:(op6==44)?2:4;
			ok=true;
			if (dar < 0x04000000)
			{
				fm_w(0x54000000|((u32)areg<<21)|(ppc_rarg2<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r5,areg,6,26,31 (addr>>26)
				fm_w(0x2C000000|(ppc_rarg2<<16)|0x38);		// cmpwi r5,0x38 — SQ region?
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic (patched below)
				fm_sq_addr(ppc_rarg2,(u32)areg,(sz<4)?(4-sz):0);
				fm_w(fm_dform(op6,rt,ppc_rarg2,0));		// st{b,h,w} rS,0(r5)
				ok&=fm_branch(site+1,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;	// resolve bne -> generic
			}
			fm_mr(ppc_rarg0,(u32)areg);
			fm_mr(ppc_rarg1,rt);
			if (sz==1)      fm_w(0x70000000|(ppc_rarg1<<21)|(ppc_rarg1<<16)|0xFF);	// andi. r4,r4,0xFF
			else if (sz==2) fm_w(0x70000000|(ppc_rarg1<<21)|(ppc_rarg1<<16)|0xFFFF);
			ok&=fm_branch((sz==1)?(void*)WriteMem8:(sz==2)?(void*)WriteMem16:(void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==36 && ra==ppc_rarg1 && rt==ppc_rarg2 && dd==0)
	{
		// --- pair write, first word: stw r5,0(r4). Address still in r3,
		// data in r5:r6 — the exact WriteMem64(u32,u64) EABI registers.
		if (fm_find_areg(site,ra)==ppc_rarg0)
		{
			ok=true;
			if (dar < 0x04000000)
			{
				// fmov.d bursts into the SQ are THE geometry path.
				fm_w(0x54000000|(ppc_rarg0<<21)|(ppc_rarg1<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r4,r3,6,26,31
				fm_w(0x2C000000|(ppc_rarg1<<16)|0x38);		// cmpwi r4,0x38
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic
				fm_sq_addr(ppc_rarg1,ppc_rarg0,0);		// 8-aligned: no swizzle
				fm_w(fm_dform(36,ppc_rarg2,ppc_rarg1,0));	// stw r5,0(r4)
				fm_w(fm_dform(36,ppc_rarg3,ppc_rarg1,4));	// stw r6,4(r4)
				ok&=fm_branch(site+2,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;
			}
			ok&=fm_branch((void*)WriteMem64,1);
			ok&=fm_branch(site+2,0);
		}
	}
	else if (op6==36 && ra==ppc_rarg1 && rt==ppc_rarg3 && dd==4)
	{
		// --- pair write, second word alone (straddling pair; see LIMITATION).
		if (fm_find_areg(site,ra)>=0)
		{
			ok=true;
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg1,4));	// addi r3,r4,4 (masked EA+4)
			fm_mr(ppc_rarg1,ppc_rarg3);
			ok&=fm_branch((void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}
	// JIT_FMOV float shapes (lfs/stfs): same EA registers as the integer ones. Not gated on the preset, as a
	// site outlives the switch; trampolines bounce through the FR's own slot (f14-f31 survive the C call).
	else if (op6==48 && ra==ppc_rarg1 && dd==0)
	{
		// --- scalar float read: lfs fD,0(r4)  (fmov.s, always 4 bytes).
		int areg=fm_find_areg(site,ra);
		int fofs=fm_fr_ofs(rt,false);
		if (areg>=0 && fofs>=0)
		{
			ok=true;
			fm_mr(ppc_rarg0,(u32)areg);
			ok&=fm_branch((void*)ReadMem32,1);
			fm_w(fm_dform(36,ppc_rrv0,ppc_contex,(u32)fofs));	// stw r3,fofs(r30)
			fm_w(fm_dform(48,rt,ppc_contex,(u32)fofs));		// lfs fD,fofs(r30)
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==48 && ra==ppc_rarg2 && dd==0)
	{
		// Pair float read, first word: ReadMem64's r3:r4 = word[addr]:word[addr+4] go to fD:fD+1.
		int fofs=fm_fr_ofs(rt,true);
		if (fm_find_areg(site,ra)==ppc_rarg0 && fofs>=0)
		{
			ok=fm_branch((void*)ReadMem64,1);
			fm_w(fm_dform(36,ppc_rrv0,ppc_contex,(u32)fofs));	// stw r3,fofs(r30)
			fm_w(fm_dform(36,ppc_rrv1,ppc_contex,(u32)fofs+4));	// stw r4,fofs+4(r30)
			fm_w(fm_dform(48,rt,ppc_contex,(u32)fofs));		// lfs fD,  fofs(r30)
			fm_w(fm_dform(48,rt+1,ppc_contex,(u32)fofs+4));		// lfs fD+1,fofs+4(r30)
			ok&=fm_branch(site+2,0);				// skip the second lfs
		}
	}
	else if (op6==48 && ra==ppc_rarg2 && dd==4)
	{
		// --- pair float read, second word alone (straddling; see LIMITATION).
		// The first word is already in fD-1, which the call cannot disturb.
		int fofs=fm_fr_ofs(rt,false);
		if (fm_find_areg(site,ra)>=0 && fofs>=0)
		{
			ok=true;
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg2,4));		// addi r3,r5,4 (masked EA+4)
			ok&=fm_branch((void*)ReadMem32,1);
			fm_w(fm_dform(36,ppc_rrv0,ppc_contex,(u32)fofs));	// stw r3,fofs(r30)
			fm_w(fm_dform(48,rt,ppc_contex,(u32)fofs));		// lfs fD,fofs(r30)
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==52 && ra==ppc_rarg2 && dd==0)
	{
		// Scalar float write (stfs fS,0(r5)) with the integer write's inline SQ path; sz is 4: no swizzle, no mask.
		int areg=fm_find_areg(site,ra);
		int fofs=fm_fr_ofs(rt,false);
		if (areg>=0 && fofs>=0)
		{
			ok=true;
			if (dar < 0x04000000)
			{
				fm_w(0x54000000|((u32)areg<<21)|(ppc_rarg2<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r5,areg,6,26,31
				fm_w(0x2C000000|(ppc_rarg2<<16)|0x38);		// cmpwi r5,0x38 — SQ region?
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic (patched below)
				fm_sq_addr(ppc_rarg2,(u32)areg,0);
				fm_w(fm_dform(52,rt,ppc_rarg2,0));		// stfs fS,0(r5)
				ok&=fm_branch(site+1,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;	// resolve bne -> generic
			}
			fm_mr(ppc_rarg0,(u32)areg);				// before r4 is clobbered below
			fm_w(fm_dform(52,rt,ppc_contex,(u32)fofs));		// stfs fS,fofs(r30)
			fm_w(fm_dform(32,ppc_rarg1,ppc_contex,(u32)fofs));	// lwz  r4,fofs(r30)
			ok&=fm_branch((void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}
	else if (op6==52 && ra==ppc_rarg1 && dd==0)
	{
		// Pair float write, first word: WriteMem64 wants r3 + r5:r6, so only the data moves; fmov.d SQ bursts are
		// the geometry path, hence the inline.
		int fofs=fm_fr_ofs(rt,true);
		if (fm_find_areg(site,ra)==ppc_rarg0 && fofs>=0)
		{
			ok=true;
			if (dar < 0x04000000)
			{
				fm_w(0x54000000|(ppc_rarg0<<21)|(ppc_rarg1<<16)|(6<<11)|(26<<6)|(31<<1));	// rlwinm r4,r3,6,26,31
				fm_w(0x2C000000|(ppc_rarg1<<16)|0x38);		// cmpwi r4,0x38
				u32* bne=fm_p;
				fm_w(0x40820000);				// bne -> generic
				fm_sq_addr(ppc_rarg1,ppc_rarg0,0);		// 8-aligned: no swizzle
				fm_w(fm_dform(52,rt,ppc_rarg1,0));		// stfs fS,  0(r4)
				fm_w(fm_dform(52,rt+1,ppc_rarg1,4));		// stfs fS+1,4(r4)
				ok&=fm_branch(site+2,0);
				*bne |= ((u32)((u8*)fm_p-(u8*)bne))&0xFFFC;
			}
			fm_w(fm_dform(52,rt,ppc_contex,(u32)fofs));		// stfs fS,  fofs(r30)
			fm_w(fm_dform(52,rt+1,ppc_contex,(u32)fofs+4));		// stfs fS+1,fofs+4(r30)
			fm_w(fm_dform(32,ppc_rarg2,ppc_contex,(u32)fofs));	// lwz  r5,fofs(r30)
			fm_w(fm_dform(32,ppc_rarg3,ppc_contex,(u32)fofs+4));	// lwz  r6,fofs+4(r30)
			ok&=fm_branch((void*)WriteMem64,1);
			ok&=fm_branch(site+2,0);				// skip the second stfs
		}
	}
	else if (op6==52 && ra==ppc_rarg1 && dd==4)
	{
		// --- pair float write, second word alone (straddling; see LIMITATION).
		int fofs=fm_fr_ofs(rt,false);
		if (fm_find_areg(site,ra)>=0 && fofs>=0)
		{
			ok=true;
			fm_w(fm_dform(14,ppc_rarg0,ppc_rarg1,4));		// addi r3,r4,4 (before r4 dies)
			fm_w(fm_dform(52,rt,ppc_contex,(u32)fofs));		// stfs fS,fofs(r30)
			fm_w(fm_dform(32,ppc_rarg1,ppc_contex,(u32)fofs));	// lwz  r4,fofs(r30)
			ok&=fm_branch((void*)WriteMem32,1);
			ok&=fm_branch(site+1,0);
		}
	}

	if (!ok)
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=insn; s_fm_fail_dar=dar; s_fm_fail_reason=3;
		return 1;
	}

	const u32 bytes=(u32)((u8*)fm_p-(u8*)tramp);
	s_fm_pool_used += (bytes+31)&~31u;
	make_address_range_executable(tramp,bytes);

	// Patch the faulting access with an unconditional branch to the trampoline.
	snat disp=(u8*)tramp-(u8*)site;
	if (disp>=33554432 || disp<-33554432)
	{
		s_fm_fail_pc=pc; s_fm_fail_insn=insn; s_fm_fail_dar=dar; s_fm_fail_reason=4;
		return 1;
	}
	*site=0x48000000|((u32)disp&0x03FFFFFC);
	make_address_range_executable(site,sizeof(u32));

	s_fm_ring[s_fm_ring_n++ & (FM_RING-1)]=pc;
	s_fm_patch_count++;
	if (op6==48 || op6==52)		// lfs / stfs — a JIT_FMOV float shape faulted
		s_fm_float_patches++;
	return 0;
}
#endif  // PS3_FASTMEM_DSI

// A store whose base register a later PREF in the block also uses is taken as a store-queue store.
// The guess only picks the code shape: emit_sq_store still tests the address.
static bool sq_store_guess(DecodedBlock* block,size_t i)
{
	const shil_opcode& st=block->oplist[i];
	if (!st.rs1.is_reg())
		return false;
	for (size_t j=i+1;j<block->oplist.size();j++)
	{
		const shil_opcode& o=block->oplist[j];
		if (o.op==shop_pref && o.rs1.is_reg() && o.rs1._reg==st.rs1._reg)
			return true;
	}
	return false;
}

// Plan 5.31: the check each memory op's fast path runs, decided as the readm and writem cases below decide it
// (SQ shape: JIT SQ STORES' guess, or JIT SQ MISS's look-ahead), then the block's plan.
static void gr_prepare(DecodedBlock* block)
{
	s_gr.n=0;
	if (!get_jit_guard_reuse_preset() || !fm_on() || CCN_MMUCR.AT || block->oplist.size()>GR_MAX_OPS)
		return;
	for (size_t i=0;i<block->oplist.size();i++)
	{
		const shil_opcode& o=block->oplist[i];
		u8 k=GR_NONE;
		if ((o.op==shop_readm || o.op==shop_writem) && o.rs1.is_r32i() && (o.rs3.is_null() || o.rs3.is_imm()))
		{
			k=GR_RAM;
			if (o.op==shop_writem && get_jit_sq_store_preset() && (o.flags==4 || o.flags==8) &&
			    (sq_store_guess(block,i) || (get_jit_sq_miss_preset()>=2 && sqla_guess(block,i,sqla_read16))))
				k=GR_SQ;
		}
		s_gr_kind[i]=k;
	}
	gr_plan(s_gr,*block,s_gr_kind);
	s_gr_validates+=s_gr.validates;
	s_gr_reuses+=s_gr.reuses;
}

// ngen_CompileBlock: Main Block Compilation Loop
DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks)
{
	// Bail out early if there isn't enough space for a worst-case block
	if (emit_FreeSpace() < 16384) // 16*1024
		return 0;

	// JIT_ALIGN: pad the block entry to a 32-byte line. The nops follow an unconditional branch, so they never
	// run, and they sit in the line the previous flush already covers.
	if (get_jit_align_preset())
	{
		while (((u32)(uintptr_t)emit_GetCCPtr() & 31) != 0)
			ppc_emit(0x60000000);	// ppc nop (ori r0,r0,0)
	}

	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();

	ColdReset();          // mem-op cold (slow) fragments deferred to block end
	ngen_Begin(block,force_checks);
	tcopy_begin(compile_state.tc,get_jit_tcopy_preset() ? block : 0);
	gr_prepare(block);

	// JIT_HOTBLOCKS: count real body entries (after the SMC check and the cycle exit); rarg0/rarg1 are free.
	const s32 hb = hotblocks_slot(block);
	u32 hb_probe = 0;
	if (hb>=0)
	{
		hb_probe = (u32)((u8*)emit_GetCCPtr()-(u8*)rv);
		u32 lo = ppc_addr_high(ppc_rarg1,(void*)&s_hb_hits[hb]);
		ppc_lwz(ppc_rarg0,ppc_rarg1,lo);
		ppc_addi(ppc_rarg0,ppc_rarg0,1);
		ppc_stw(ppc_rarg0,ppc_rarg1,lo);
	}

	for (size_t i = 0; i < block->oplist.size(); i++)
	{
		shil_opcode* op=&block->oplist[i];

		// T-FORWARDING: latch whether the previous op left sr_T in rarg0, then clear it, so any op in between
		// cancels the forward.
		const bool t_prev = compile_state.t_in_rarg0;
		compile_state.t_in_rarg0 = false;
		tcopy_op_begin(compile_state.tc,i);

		// JIT_CR0: only the last op, on a block that exits on sr_T, writing sr_T; has_jcond exits test ppc_djump.
		compile_state.t_exit_cr0 =
			get_jit_cr0_preset() &&
			!compile_state.has_jcond &&
			(i + 1 == block->oplist.size()) &&
			(block->BlockType == BET_Cond_0 || block->BlockType == BET_Cond_1) &&
			op->rd.is_reg() && op->rd._reg == reg_sr_T;

		switch(op->op)
		{

		case shop_readm:
			{
				void* fuct=0;
				bool isram=false;
				verify(op->rs1.is_imm() || op->rs1.is_r32i());

				// Load straight into rd's pinned reg when possible, so the trailing store is elided; the MMIO call returns
				// in rrv0 and copies once. Pairs keep the rrv0:rrv1 ABI.
				u32 rdreg = ppc_rrv0;
#if STATIC_GPR_ALLOC
				if (op->flags!=8)
				{
					ppc_ireg rp=GetIntReg(op->rd._reg);
					if (rp!=ppc_rinvalid)
						rdreg=(u32)rp;
				}
#endif
				// Address source for the runtime fast path (set in the rs1-reg
				// branch below). Defaults to rarg0 (the const/imm + pair paths).
				u32 areg = ppc_rarg0;

				// FPU_PIN: a pinned FR dest loads straight into the FPR (runtime addresses; flags 4 scalar, 8 pair with
				// both fr pinned). JIT_FMOV adds the fastmem shapes, which the DSI decoder accepts unconditionally.
				bool fdirect=false;
				u32  fd0=0, fd1=0, fofs=0;
				if (get_fpu_pin_preset() && op->rs1.is_reg()
				    && (!fastmem_on() || get_jit_fmov_preset()))
				{
					if (op->flags==8)
					{
						ppc_freg a=GetFloatReg(op->rd._reg);
						ppc_freg b=GetFloatReg(op->rd._reg+1);
						if (a!=ppc_finvalid && b!=ppc_finvalid)
						{ fdirect=true; fd0=(u32)a; fd1=(u32)b; fofs=Sh4cntx.offset(op->rd._reg); }
					}
					else if (op->flags==4 && op->rd.is_r32f())
					{
						ppc_freg a=GetFloatReg(op->rd._reg);
						if (a!=ppc_finvalid)
						{ fdirect=true; fd0=(u32)a; fofs=Sh4cntx.offset(op->rd._reg); }
					}
				}

				if (op->rs1.is_imm())
				{
					void* ptr=_vmem_read_const(op->rs1._imm,isram,op->flags);
					if (isram)
					{
						if (op->flags==1)
						{
							ppc_lbz(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (op->flags==2)
							ppc_lha(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
						else if (op->flags==4)
							ppc_lwz(rdreg,ppc_r4,ppc_addr_high(ppc_r4,ptr));
						else
						{
							die("Invalid mem read size");
						}
					}
					else
					{
						ppc_li(ppc_rarg0,op->rs1._imm);
						fuct=ptr;
					}
				}
				else
				{
					// areg: a plain @Rn uses rs1's pinned reg directly (no mr); an offset needs the address in rarg0.
					areg = ppc_rarg0;
					if (op->rs3.is_imm())
					{
						verify(op->rs3.is_imm_s16());
						// addr = rs1 + imm -> addi can read pinned rs1 in place and
						// write scratch rarg0, skipping the `mr rarg0,rs1` load.
						u32 a1=src_or_load(op->rs1,ppc_rarg0);
						ppc_addi(ppc_rarg0,a1,op->rs3._imm);
					}
					else if (op->rs3.is_r32i())
					{
						// addr = rs1 + rs3 -> add reads both sources in place.
						u32 a1=src_or_load(op->rs1,ppc_rarg0);
						u32 a3=src_or_load(op->rs3,ppc_rarg1);
						ppc_addx(ppc_rarg0,a1,a3,0,0);
					}
					else if (op->rs3.is_null())
					{
#if STATIC_GPR_ALLOC
						// Only the scalar fast path threads areg; the 64-bit pair
						// path still reads rarg0, so keep pre-loading it there.
						ppc_ireg ap=(op->flags!=8 && op->rs1.is_reg())
						              ?GetIntReg(op->rs1._reg):ppc_rinvalid;
						if (ap!=ppc_rinvalid)
							areg=(u32)ap;		// address lives in pinned reg
						else
#endif
							ppc_sh_load(ppc_rarg0,op->rs1);
					}
					else
					{
						die("invalid rs3");
					}
				}

				if (!isram && fastmem_on())
				{
					// FASTMEM: branchless access through the MMU window. Fixed shapes (the DSI decoder reads the EA
					// register), so keep emission and decoder in sync.
					if (op->rs1.is_imm())
					{
						// Compile-time-known MMIO: call the handler directly, never a faulting access (cheaper than the old 0x8C
						// compare + cold branch, e.g. SB_ISTNRM polls).
						void* f=fuct;
						if (!f) f=(op->flags==1)?(void*)ReadMem8:(op->flags==2)?(void*)ReadMem16:(void*)ReadMem32;
						ppc_call_c(f);
						if (op->flags==1)      ppc_extsbx(rdreg,ppc_rrv0,0);
						else if (op->flags==2) ppc_extshx(rdreg,ppc_rrv0,0);
						else if (rdreg!=ppc_rrv0) ppc_ori(rdreg,ppc_rrv0,0);
					}
					else if (op->flags==8)
					{
						// Pair: address in rarg0, EA scratch must be rarg2; word[addr] -> rrv0, word[addr+4] -> rrv1, as ReadMem64.
						ppc_rlwinmx(ppc_rarg2,ppc_rarg0,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (fdirect)
						{
							// JIT_FMOV: straight into the pinned FPRs; same EA register and displacements, only the opcode differs.
							fm_note_float_site();
							ppc_lfs(fd0,ppc_rarg2,0);		// fault site A
							ppc_lfs(fd1,ppc_rarg2,4);		// fault site B
						}
						else
						{
							ppc_lwz(ppc_rrv0,ppc_rarg2,0);		// fault site A
							ppc_lwz(ppc_rrv1,ppc_rarg2,4);		// fault site B
						}
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);
						// Scalar: EA scratch must be rarg1; areg keeps the original SH4 address for the fault decoder.
						ppc_rlwinmx(ppc_rarg1,areg,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (sz<4)
							ppc_xori(ppc_rarg1,ppc_rarg1,4-sz);	// BE sub-word swizzle
						if (fdirect)
						{
							fm_note_float_site();
							ppc_lfs(fd0,ppc_rarg1,0);		// fault site (sz==4 only)
						}
						else if (sz==1)
						{
							ppc_lbz(rdreg,ppc_rarg1,0);		// fault site
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (sz==2)
							ppc_lha(rdreg,ppc_rarg1,0);		// fault site
						else
							ppc_lwz(rdreg,ppc_rarg1,0);		// fault site
					}
				}
				else if (!isram && !op->rs1.is_imm() && fm_on())
				{
					// FASTMEM: main RAM inline, everything else out of line.
					ColdFrag& c=cold_new();
					c.kind=(op->flags==8)?1:0;
					c.sz=(u8)((op->flags==8)?4:op->flags);
					c.rdreg=(u8)rdreg; c.datareg=(u8)ppc_rarg1;
					c.areg=(u8)areg;   c.fuct=fuct;
					c.fdreg=fdirect?(u8)fd0:0xFF; c.fofs=(u16)fofs;
					c.vmem_tier=1;

					// JIT GUARD REUSE (plan 5.31): an earlier check of this base register stands in for the guard.
					const u8 gr=gr_role(s_gr,(u32)i);
					if (gr==GR_REUSE)
						c.beq=emit_gr_branch(s_gr.bit[i]);	// flag clear -> cold
					else
					{
						fm_emit_guard(areg);
						c.beq=ppc_CreateLabel();
						ppc_bcx(BO_FALSE,BI_CR0_LT,0,0,0);	// bge cold (not main RAM)
						if (gr==GR_VALIDATE)
						{
							emit_gr_set(s_gr.bit[i]);
							c.gr_bit=s_gr.bit[i];
						}
					}

					if (op->flags==8)
					{
						// Pair: rarg2 holds the native address because rrv0/rrv1
						// (r3/r4) are the destinations and r3 is also the address.
						fm_emit_addr(ppc_rarg2,areg,4);
						if (fdirect)
						{
							ppc_lfs(fd0,ppc_rarg2,0);
							ppc_lfs(fd1,ppc_rarg2,4);
						}
						else
						{
							ppc_lwz(ppc_rrv1,ppc_rarg2,4);
							ppc_lwz(ppc_rrv0,ppc_rarg2,0);
						}
					}
					else
					{
						const u32 sz=op->flags;
						fm_emit_addr(ppc_rarg0,areg,sz);
						if (fdirect)
							ppc_lfs(fd0,ppc_rarg0,0);
						else if (sz==1)
						{
							ppc_lbz(rdreg,ppc_rarg0,0);
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (sz==2)
						{
							ppc_lhz(rdreg,ppc_rarg0,0);
							ppc_extshx(rdreg,rdreg,0);
						}
						else
							ppc_lwz(rdreg,ppc_rarg0,0);
					}

					c.done=emit_GetCCPtr();
				}
				else if (!isram)
				{
					// Inline _vmem_readt: iirf = MemInfo[addr>>24] (8-byte entries, low word at +4), ptr = iirf & ~0x1F,
					// rv = *(ptr + ((addr<<sh)>>sh)), MMIO -> ReadMem. Inline so fastmem OFF stays the baseline.
					if (op->flags==8)
					{
						// Pair load: the BE u64 comes back as rrv0:rrv1 = word[addr]:word[addr+4], as ReadMem64.
						ppc_rlwinmx(ppc_r0,ppc_rarg0,11,21,28,0);	// (addr>>24)*8
						u32 lo=ppc_addr_high(ppc_rarg1,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
						if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
						ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
						ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,0,26,0);	// ptr (≠r0)
						ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);		// iirf < 0x20 ? (MMIO)

						// blt -> cold (forward, not-taken: fast path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

						// Direct path: the shift is iirf itself (ptr is 64-byte aligned, so its low 6 bits are the shift field).
						ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);
						ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0);	// mirror mask
						if (fdirect)
						{
							// FPU_PIN: load both words STRAIGHT into the pinned FPRs
							// (word[addr]->fd0, word[addr+4]->fd1); no GPR/rrv bounce.
							ppc_lfsx(fd0,ppc_rarg2,ppc_rarg0);	// word[addr]   -> fd0
							ppc_addi(ppc_rarg0,ppc_rarg0,4);
							ppc_lfsx(fd1,ppc_rarg2,ppc_rarg0);	// word[addr+4] -> fd1
						}
						else
						{
							ppc_lwzx(ppc_rarg3,ppc_rarg2,ppc_rarg0);	// word[addr]
							ppc_addi(ppc_rarg0,ppc_rarg0,4);
							ppc_lwzx(ppc_rrv1,ppc_rarg2,ppc_rarg0);	// word[addr+4] -> r4
							ppc_ori(ppc_rrv0,ppc_rarg3,0);			// r3 = word[addr]
						}

						ColdFrag& c=cold_new();
						c.beq=cold; c.done=emit_GetCCPtr();
						c.kind=1; c.fuct=fuct; c.vmem_tier=0;
						c.fdreg=fdirect?(u8)fd0:0xFF; c.fofs=(u16)fofs;
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);

						// Fast path for P1 main RAM only (addr>>24 == 0x8C): native = addr + (mem_b.data - 0x8C000000), one
						// addis since both are 64 KB aligned; any other region takes the cold path.
						const u32 delta   = (u32)(uintptr_t)mem_b.data - 0x8C000000u;
						const u32 delta_hi= (delta>>16)&0xFFFF;		// low 16 of delta are 0

						// top byte == 0x8C ?  (rlwinm r0,areg,8,24,31 == srwi 24)
						ppc_rlwinmx(ppc_r0,areg,8,24,31,0);		// r0 = addr>>24
						ppc_cmpi(ppc_cr0,ppc_r0,0x8C,0);		// == 0x8C ?

						// bne -> cold (forward, not-taken: RAM path falls through).
						ppc_label* cold=ppc_CreateLabel();
						ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne cold (not RAM)

						// --- fast direct RAM path (fall-through) ---
						// native = addr + delta  (single addis). rarg0 = native addr.
						ppc_addis(ppc_rarg0,areg,delta_hi);
						// areg's upper half is whatever extsb/extsh/srawi left (poison); the 0x8C test reads only the low word,
						// so drop the high one before the addis builds a host address.
						ppc_clrldi32(ppc_rarg0,ppc_rarg0);
						// big-endian sub-word swizzle folded into the address
						// (fdirect is float sz==4 only, so it never swizzles)
						if (sz<4)
							ppc_xori(ppc_rarg0,ppc_rarg0,4-sz);
						// rv = *(T*)native   (d-form, base=rarg0 != r0)
						if (fdirect)
							ppc_lfs(fd0,ppc_rarg0,0);	// FPU_PIN: straight into the FPR
						else if (sz==1)
						{
							ppc_lbz(rdreg,ppc_rarg0,0);
							ppc_extsbx(rdreg,rdreg,0);
						}
						else if (sz==2)
						{
							ppc_lhz(rdreg,ppc_rarg0,0);
							ppc_extshx(rdreg,rdreg,0);
						}
						else
							ppc_lwz(rdreg,ppc_rarg0,0);

						// done = join point; cold path (kind=0) rematerialises rarg0
						// from areg and calls ReadMem<sz>, then branches here.
						ColdFrag& c=cold_new();
						c.beq=cold; c.done=emit_GetCCPtr();
						c.kind=0; c.sz=(u8)sz; c.rdreg=(u8)rdreg;
						c.areg=(u8)areg; c.fuct=fuct; c.vmem_tier=0;
						c.fdreg=fdirect?(u8)fd0:0xFF; c.fofs=(u16)fofs;
					}
				}

				// FPU_PIN's direct path already loaded the pinned FPR(s); fdirect is only set on the runtime path.
				if (!fdirect)
				{
					ppc_sh_store(rdreg,op->rd);

					if (op->flags==8)
					{
						ppc_sh_store(ppc_rrv1,op->rd._reg+1);
					}
				}
			}
			break;

		case shop_writem:
			{
				// Full address first, then the data: the rs3 register-index path uses rarg3 as scratch, which clobbered a
				// pair store's low word (fmov.d FRm,@(R0,Rn)). areg: a plain @Rn reads rs1's pinned reg in place.
				u32 areg = ppc_rarg0;
				if (op->rs3.is_imm())
				{
					verify(op->rs3.is_imm_s16());
					// addr = rs1 + imm: addi reads pinned rs1 in place -> rarg0.
					u32 a1=src_or_load(op->rs1,ppc_rarg0);
					ppc_addi(ppc_rarg0,a1,op->rs3._imm);
				}
				else if (op->rs3.is_r32i())
				{
					// addr = rs1 + rs3: add reads both sources in place -> rarg0.
					u32 a1=src_or_load(op->rs1,ppc_rarg0);
					u32 a3=src_or_load(op->rs3,ppc_rarg3);
					ppc_addx(ppc_rarg0,a1,a3,0,0);
				}
				else if (op->rs3.is_null())
				{
#if STATIC_GPR_ALLOC
					// Scalar only; the 64-bit pair path still reads rarg0.
					ppc_ireg ap=(op->flags!=8 && op->rs1.is_reg())
					              ?GetIntReg(op->rs1._reg):ppc_rinvalid;
					if (ap!=ppc_rinvalid)
						areg=(u32)ap;
					else
#endif
						ppc_sh_load(ppc_rarg0,op->rs1);
				}
				else
				{
					printf("rs3: %08X\n",op->rs3.type);
					die("invalid rs3");
				}

				// FPU_PIN: a pinned FR source stores straight from the FPR (flags 4 scalar, 8 pair); JIT_FMOV adds the
				// fastmem shapes. No rs1 check: a store's address is always a runtime register here.
				bool fdirect=false;
				u32  fs0=0, fs1=0, fofs=0;
				if (get_fpu_pin_preset() && (!fastmem_on() || get_jit_fmov_preset()))
				{
					if (op->flags==8)
					{
						ppc_freg a=GetFloatReg(op->rs2._reg);
						ppc_freg b=GetFloatReg(op->rs2._reg+1);
						if (a!=ppc_finvalid && b!=ppc_finvalid)
						{ fdirect=true; fs0=(u32)a; fs1=(u32)b; fofs=Sh4cntx.offset(op->rs2._reg); }
					}
					else if (op->flags==4 && op->rs2.is_r32f())
					{
						ppc_freg a=GetFloatReg(op->rs2._reg);
						if (a!=ppc_finvalid)
						{ fdirect=true; fs0=(u32)a; fofs=Sh4cntx.offset(op->rs2._reg); }
					}
				}

				// A pinned rs2 is stored straight from its reg (the fast path only reads it); rarg1 is loaded only on the
				// MMIO path, which WriteMem needs. Pairs keep the rarg2:rarg3 ABI.
				u32 datareg = ppc_rarg1;
				if (fdirect)
				{
					// data stays in the pinned FPR(s) fs0[/fs1]; no GPR preload.
				}
				else if (op->flags==8)
				{
					ppc_sh_load(ppc_rarg2,op->rs2);
					ppc_sh_load(ppc_rarg3,op->rs2._reg+1);
				}
				else
				{
#if STATIC_GPR_ALLOC
					ppc_ireg dp=op->rs2.is_reg()?GetIntReg(op->rs2._reg):ppc_rinvalid;
					if (dp!=ppc_rinvalid)
						datareg=(u32)dp;		// store straight from pinned reg
					else
#endif
						ppc_sh_load(ppc_rarg1,op->rs2);	// scratch: load now
				}

				if (fastmem_on())
				{
					// FASTMEM: branchless store through the MMU window; fixed shapes, and areg/datareg (pairs: rarg0,
					// rarg2:rarg3) still hold their values at the store for the fault decoder.
					if (op->flags==8)
					{
						// Pair: address in rarg0, data in rarg2:rarg3 =
						// high:low. EA scratch MUST be rarg1.
						ppc_rlwinmx(ppc_rarg1,ppc_rarg0,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (fdirect)
						{
							// JIT_FMOV: straight from the pinned FPRs; the
							// rarg2:rarg3 preload above was skipped.
							fm_note_float_site();
							ppc_stfs(fs0,ppc_rarg1,0);		// fault site A
							ppc_stfs(fs1,ppc_rarg1,4);		// fault site B
						}
						else
						{
							ppc_stw(ppc_rarg2,ppc_rarg1,0);		// fault site A
							ppc_stw(ppc_rarg3,ppc_rarg1,4);		// fault site B
						}
					}
					else
					{
						const u32 sz=op->flags;
						verify(sz==1||sz==2||sz==4);
						// Scalar: EA scratch must be rarg2; stb/sth store the low byte/halfword, so no data mask (the
						// trampoline's C path masks).
						ppc_rlwinmx(ppc_rarg2,areg,0,3,31,0);	// EA = addr & 0x1FFFFFFF
						if (sz<4)
							ppc_xori(ppc_rarg2,ppc_rarg2,4-sz);	// BE sub-word swizzle
						if (fdirect)
						{
							fm_note_float_site();
							ppc_stfs(fs0,ppc_rarg2,0);		// fault site (sz==4 only)
						}
						else if (sz==1)
							ppc_stb(datareg,ppc_rarg2,0);		// fault site
						else if (sz==2)
							ppc_sth(datareg,ppc_rarg2,0);		// fault site
						else
							ppc_stw(datareg,ppc_rarg2,0);		// fault site
					}
				}
				else if (fm_on())
				{
					// FASTMEM: main RAM inline, everything else out of line. 5.17 Step 3: a guessed
					// store-queue store tests for the SQ inline instead; the cold path takes the rest.
					ColdFrag& c=cold_new();
					c.kind=(op->flags==8)?3:2;
					c.sz=(u8)((op->flags==8)?4:op->flags);
					c.rdreg=(u8)ppc_rrv0; c.datareg=(u8)datareg;
					c.areg=(u8)areg;      c.fuct=0;
					c.fdreg=fdirect?(u8)fs0:0xFF; c.fofs=(u16)fofs;
					c.vmem_tier=1;

					// 5.27 JIT SQ MISS: 2 also looks for the PREF in the static successors, 1 puts the SQ test first
					// in the cold path of the other 32-bit and pair stores.
					const int sqm=get_jit_sq_miss_preset();
					const bool sq_size=op->flags==4 || op->flags==8;
					bool sq_guess=get_jit_sq_store_preset() && sq_size && sq_store_guess(block,i);
					bool sq_la=false;
					if (!sq_guess && sqm>=2 && get_jit_sq_store_preset() && sq_size)
						sq_guess=sq_la=sqla_guess(block,i,sqla_read16);

					// JIT GUARD REUSE (plan 5.31): an earlier check of this base register stands in for this one.
					const u8 gr=gr_role(s_gr,(u32)i);
					verify(gr==GR_NORMAL || s_gr_kind[i]==(sq_guess?GR_SQ:GR_RAM));
					if (sq_guess)
					{
						// A pair keeps its address in rarg0 (areg) and data in rarg2:rarg3; EA in rarg1.
						const bool pair=op->flags==8;
						const u32 sqb=(u32)(uintptr_t)sq_both;
						verify((sqb&63)==0 && (uintptr_t)sq_both<0x7FFF0000u);
						if (gr==GR_REUSE)
							c.beq=emit_gr_branch(s_gr.bit[i]);	// flag clear -> cold
						else
						{
							c.beq=emit_sq_check(areg);
							if (gr==GR_VALIDATE)
							{
								emit_gr_set(s_gr.bit[i]);
								c.gr_bit=s_gr.bit[i];
							}
						}
						emit_sq_body(areg,pair?ppc_rarg1:ppc_rarg2,pair,fdirect,
						             fdirect?fs0:(pair?(u32)ppc_rarg2:datareg),
						             fdirect?fs1:(u32)ppc_rarg3,sqb);
						g_jit_sq_sites++;
						g_jit_sq_la_sites+=sq_la;
					}
					else
					{
						if (sqm>=1 && sq_size)
						{
							c.vmem_tier=2;
							g_jit_sq_cold_sites++;
						}
						if (gr==GR_REUSE)
							c.beq=emit_gr_branch(s_gr.bit[i]);	// flag clear -> cold
						else
						{
							fm_emit_guard(areg);
							c.beq=ppc_CreateLabel();
							ppc_bcx(BO_FALSE,BI_CR0_LT,0,0,0);	// bge cold (not main RAM)
							if (gr==GR_VALIDATE)
							{
								emit_gr_set(s_gr.bit[i]);
								c.gr_bit=s_gr.bit[i];
							}
						}

						if (op->flags==8)
						{
							// Pair: address in rarg0, data in rarg2:rarg3, so the
							// native address goes to rarg1.
							fm_emit_addr(ppc_rarg1,ppc_rarg0,4);
							if (fdirect)
							{
								ppc_stfs(fs0,ppc_rarg1,0);
								ppc_stfs(fs1,ppc_rarg1,4);
							}
							else
							{
								ppc_stw(ppc_rarg2,ppc_rarg1,0);
								ppc_stw(ppc_rarg3,ppc_rarg1,4);
							}
						}
						else
						{
							const u32 sz=op->flags;
							// stb/sth store the low byte/halfword, so no data mask.
							fm_emit_addr(ppc_rarg2,areg,sz);
							if (fdirect)
								ppc_stfs(fs0,ppc_rarg2,0);
							else if (sz==1)
								ppc_stb(datareg,ppc_rarg2,0);
							else if (sz==2)
								ppc_sth(datareg,ppc_rarg2,0);
							else
								ppc_stw(datareg,ppc_rarg2,0);
						}
					}

					c.done=emit_GetCCPtr();
				}
				else
				// Inline _vmem_writet, as the read side (iirf, ptr, (addr<<sh)>>sh, MMIO -> WriteMem). rarg0 = addr,
				// rarg1 = data (pairs rarg2:rarg3 = high:low); both must survive to the slow call.
				if (op->flags==8)
				{
					// Pair store: data in rarg2:rarg3 = high:low, WriteMem64's registers; word[addr] = high, word[addr+4] =
					// low. The lookup may use only r0 and rarg1.
					ppc_rlwinmx(ppc_r0,ppc_rarg0,11,21,28,0);	// (addr>>24)*8
					u32 lo=ppc_addr_high(ppc_rarg1,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
					if (lo) ppc_addi(ppc_rarg1,ppc_rarg1,lo);
					ppc_lwzx(ppc_rarg1,ppc_rarg1,ppc_r0);		// rarg1 = iirf
					// The shift first: andi. writes CR0, so the MMIO compare must follow it (and precede the rlwinm on rarg1).
					ppc_andi(ppc_r0,ppc_rarg1,0x1F);		// r0 = shift (clobbers CR0)
					ppc_cmpli(ppc_cr0,ppc_rarg1,0x20,0);		// iirf < 0x20 ? (sets CR0 last)
					ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,0,26,0);	// rarg1 = ptr (≠r0; Rc=0)

					// blt -> cold (forward, not-taken: fast path falls through).
					ppc_label* cold=ppc_CreateLabel();
					ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

					// --- fast direct path (fall-through; addr masked here only) ---
					ppc_slwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
					ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_r0,0);	// mirror mask
					if (fdirect)
					{
						// FPU_PIN: store both words STRAIGHT from the pinned FPRs
						// (fs0=high->word[addr], fs1=low->word[addr+4]).
						ppc_stfsx(fs0,ppc_rarg1,ppc_rarg0);	// word[addr]   = fs0
						ppc_addi(ppc_rarg0,ppc_rarg0,4);
						ppc_stfsx(fs1,ppc_rarg1,ppc_rarg0);	// word[addr+4] = fs1
					}
					else
					{
						ppc_stwx(ppc_rarg2,ppc_rarg1,ppc_rarg0);	// word[addr]   = high
						ppc_addi(ppc_rarg0,ppc_rarg0,4);
						ppc_stwx(ppc_rarg3,ppc_rarg1,ppc_rarg0);	// word[addr+4] = low
					}

					ColdFrag& c=cold_new();
					c.beq=cold; c.done=emit_GetCCPtr();
					c.kind=3; c.vmem_tier=0;
					c.fdreg=fdirect?(u8)fs0:0xFF; c.fofs=(u16)fofs;
				}
				else
				{
					const u32 sz=op->flags;
					verify(sz==1||sz==2||sz==4);

					// r0 = (addr>>24)*8; areg (rarg0 or rs1's pinned reg) is only read.
					ppc_rlwinmx(ppc_r0,areg,11,21,28,0);
					// rarg2 = &_vmem_MemInfo_ptr
					u32 lo=ppc_addr_high(ppc_rarg2,(void*)((u8*)&_vmem_MemInfo_ptr[0]+4));
					if (lo) ppc_addi(ppc_rarg2,ppc_rarg2,lo);
					ppc_lwzx(ppc_rarg2,ppc_rarg2,ppc_r0);		// rarg2 = iirf
					// rarg3 = ptr = iirf & ~0x1F  (ptr NOT in r0: store-indexed
					// treats base r0 as literal zero, dropping the pointer)
					ppc_rlwinmx(ppc_rarg3,ppc_rarg2,0,0,26,0);
					// MMIO test on iirf directly (iirf<0x20 <=> ptr==0); keeps iirf
					// in rarg2 alive as the shift count below.
					ppc_cmpli(ppc_cr0,ppc_rarg2,0x20,0);		// iirf < 0x20 ?

					// blt -> cold (forward, not-taken: fast path falls through).
					ppc_label* cold=ppc_CreateLabel();
					ppc_bcx(BO_TRUE,BI_CR0_LT,0,0,0);		// blt cold (MMIO)

					// Direct path: r0 = (addr<<sh)>>sh, the shift being iirf in rarg2 (ptr is 64-byte aligned, no andi).
					ppc_slwx(ppc_r0,areg,ppc_rarg2,0);		// r0 = addr<<sh (reads areg)
					ppc_srwx(ppc_r0,ppc_r0,ppc_rarg2,0);		// r0 = (addr<<sh)>>sh
					if (sz<4)
						ppc_xori(ppc_r0,ppc_r0,4-sz);		// big-endian sub-word swizzle
					// *(ptr + eff) = data, ptr in rarg3, eff in r0; datareg is rs2's pinned reg or rarg1 (fdirect: sz 4 only).
					if (fdirect)
						ppc_stfsx(fs0,ppc_rarg3,ppc_r0);	// FPU_PIN: straight from the FPR
					else if (sz==1)
						ppc_stbx(datareg,ppc_rarg3,ppc_r0);
					else if (sz==2)
						ppc_sthx(datareg,ppc_rarg3,ppc_r0);
					else
						ppc_stwx(datareg,ppc_rarg3,ppc_r0);

					ColdFrag& c=cold_new();
					c.beq=cold; c.done=emit_GetCCPtr();
					c.kind=2; c.sz=(u8)sz; c.datareg=(u8)datareg; c.areg=(u8)areg;
					c.vmem_tier=0;
					c.fdreg=fdirect?(u8)fs0:0xFF; c.fofs=(u16)fofs;
				}
			}
			break;

		case shop_ifb:
			{
				const u16 sh4op=(u16)op->rs3._imm;

				// IFB_PROBE: count this opcode; rarg0/rarg1 are free at an op boundary, and this runs before the bracket.
				if (get_ifb_probe_preset())
				{
					u32* slot=ifb_probe_slot(sh4op);
					if (slot)
					{
						u32 lo=ppc_addr_high(ppc_rarg1,(void*)slot);
						ppc_lwz(ppc_rarg0,ppc_rarg1,lo);
						ppc_addi(ppc_rarg0,ppc_rarg0,1);
						ppc_stw(ppc_rarg0,ppc_rarg1,lo);
					}
				}

				// IFB_FLUSH: spill only what this opcode's handler touches (its GPRs; the float file if it can change it);
				// preset off, or an unlisted opcode, spills everything.
				IfbBracket br={IFB_GPR_ALL,true};
				if (get_ifb_flush_preset())
					br=ifb_bracket(sh4op);

				reg_flush_mask(br.gprs);
				if (br.fpu)
					reg_flush_all_fpu();		// self-gates on FPU_PIN

				if (op->rs1._imm)
				{
					ppc_li(ppc_rarg0,op->rs2._imm);
					ppc_sh_store(ppc_rarg0,reg_nextpc);
				}
				ppc_li(ppc_rarg0,op->rs3._imm);
				ppc_call_c(OpDesc[op->rs3._imm]->oph);

				if (br.fpu)
					reg_reload_all_fpu();
				reg_reload_mask(br.gprs);
			}
			break;
			
		case shop_jdyn:
			{
				if (op->rs2.is_imm())
				{
					// djump = rs1 + imm: read pinned rs1 in place into the addi/add,
					// so the `mr djump,rs1` load is folded into the arithmetic.
					u32 a1=src_or_load(op->rs1,ppc_djump);
					if (op->rs2.is_imm_s16())
					{
						ppc_addi(ppc_djump,a1,op->rs2._imm);
					}
					else
					{
						ppc_li(ppc_rarg0,op->rs2._imm);
						ppc_addx(ppc_djump,a1,ppc_rarg0,0,0);
					}
				}
				else
				{
					// djump = rs1 (no offset): a plain move is unavoidable.
					ppc_sh_load(ppc_djump,op->rs1);
				}
			}
			break;
			
		case shop_jcond:
			{
				compile_state.has_jcond=true;
				// Delay-slot branches capture T before the slot; if the compare was the previous op, T is still in rarg0.
				const bool t_rs1 = op->rs1.is_reg() && op->rs1._reg==reg_sr_T;
				if (compile_state.tc.held && t_rs1)
					s_tc_copy++;		// JIT T COPY: the T writer's store already filled djump
				else
				{
					s_tc_miss += compile_state.tc.k>=0;
					if (t_prev && t_rs1)
					{
						ppc_ori(ppc_djump,ppc_rarg0,0);		// mr djump,rarg0
						s_tc_fwd++;
					}
					else
					{
						ppc_sh_load(ppc_djump,op->rs1);
						s_tc_load++;
					}
				}
			}
			break;
			
		case shop_mov64:
			{
				verify(op->rd.is_r64());
				verify(op->rs1.is_r64());

				// mov64 is a float pair (fmov.d, DR<->XD): fmove_reg per word, fmr when pinned, else the f0 bounce.
				fmove_reg(op->rd._reg,   op->rs1._reg);
				fmove_reg(op->rd._reg+1, op->rs1._reg+1);
			}
			break;

		case shop_mov32:
			{
				verify(op->rd.is_r32());

				// fmov FRm,FRn: fmove_reg. The mixed int/float cases (flds, fsts, imm) take the GPR path below unless 5.20
				// or 5.24 applies; its ppc_sh_load/store bounce a pinned FR through its slot.
				if (op->rd.is_r32f() && op->rs1.is_r32f())
				{
					fmove_reg(op->rd._reg,op->rs1._reg);
					break;
				}

				// 5.20: fldi0/fldi1 into a pinned FR load the constant from a table (li + stw + lfs was a load-hit-store).
				if (get_jit_ftrv_preset() && get_fpu_pin_preset() && op->rd.is_r32f() && op->rs1.is_imm() &&
				    (op->rs1._imm==0 || op->rs1._imm==0x3F800000) && GetFloatReg(op->rd._reg)!=ppc_finvalid)
				{
					u32 lo=ppc_addr_high(ppc_rarg0,(void*)&s_fldi_tab[op->rs1._imm ? 1 : 0]);
					ppc_lfs(GetFloatReg(op->rd._reg),ppc_rarg0,lo);
					break;
				}

				// 5.24 JIT FPUL MOVES: flds / fsts between a pinned FR and an unpinned integer slot in one stfs / lfs.
				if (get_jit_fpul_moves_preset() && get_fpu_pin_preset())
				{
					if (op->rs1.is_r32f() && GetFloatReg(op->rs1._reg)!=ppc_finvalid && fpul_slot_dst(op->rd))
					{
						emit_flds_direct(GetFloatReg(op->rs1._reg),ppc_contex,Sh4cntx.offset(op->rd._reg));
						s_ppe_flds++;
						break;
					}
					if (op->rd.is_r32f() && GetFloatReg(op->rd._reg)!=ppc_finvalid && fpul_slot_src(op->rs1))
					{
						emit_fsts_direct(GetFloatReg(op->rd._reg),ppc_contex,Sh4cntx.offset(op->rs1._reg));
						s_ppe_fsts++;
						break;
					}
				}

				// Resolve dest: a pinned (integer) rd is written in place; a
				// non-pinned or float-typed rd uses rarg0 + a context store.
				ppc_ireg rdr = op->rd.is_r32i() ? GetIntReg(op->rd._reg) : ppc_rinvalid;
				u32 dst = (rdr!=ppc_rinvalid) ? (u32)rdr : (u32)ppc_rarg0;

				if (op->rs1.is_imm())
				{
					ppc_li(dst,op->rs1._imm);
				}
				else if (op->rs1.is_r32())
				{
					// reg -> reg. If rs1 is pinned, move/keep it directly.
					ppc_ireg rsr = op->rs1.is_r32i() ? GetIntReg(op->rs1._reg) : ppc_rinvalid;
					if (rsr!=ppc_rinvalid)
					{
						if ((u32)rsr!=dst)
							ppc_ori(dst,rsr,0);	// mr dst, rsr  (skipped if same reg)
					}
					else
					{
						ppc_sh_load(dst,op->rs1);
					}
				}
				else
				{
					die("Invalid mov32 size");
				}

				// Store back only when we used the scratch reg (non-pinned rd).
				if (dst==ppc_rarg0 && rdr==ppc_rinvalid)
					ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		// Single-instruction binops on pinned regs (binop3); a small immediate rs2 folds into the immediate form
		// (addi/subi signed 16-bit, andi./ori/xori unsigned).
		case shop_add:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_s16())
				ppc_addi(bop_d,bop_a,op->rs2._imm);		// d = a + imm
			else
				{ bop_resolve_b(op); ppc_addx(bop_d,bop_a,bop_b,0,0); }
			binop3_end(op);
			break;
		case shop_sub:
			// SH4 sub = rs1 - rs2; fold imm as a + (-imm) when -imm fits s16.
			bop_resolve_a_d(op);
			if (op->rs2.is_imm() && is_s16(0u-op->rs2._imm))
				ppc_addi(bop_d,bop_a,0u-op->rs2._imm);		// d = a - imm
			else
				{ bop_resolve_b(op); ppc_subfx(bop_d,bop_b,bop_a,0,0); }
			binop3_end(op);
			break;

		case shop_or:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_u16())
				ppc_ori(bop_d,bop_a,op->rs2._imm);
			else
				{ bop_resolve_b(op); ppc_orx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_and:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_u16())
				ppc_andi(bop_d,bop_a,op->rs2._imm);		// andi. (Rc=1, harmless)
			else
				{ bop_resolve_b(op); ppc_andx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_xor:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_u16())
				ppc_xori(bop_d,bop_a,op->rs2._imm);
			else
				{ bop_resolve_b(op); ppc_xorx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;

		// Shifts: fold a constant shift amount (0..31) into rlwinm/srawi.
		case shop_shl:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm())
			{
				u32 s=op->rs2._imm&0x1F;
				ppc_rlwinmx(bop_d,bop_a,s,0,31-s,0);		// slwi: rotl s, mask [0..31-s]
			}
			else
				{ bop_resolve_b(op); ppc_slwx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_shr:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm())
			{
				u32 s=op->rs2._imm&0x1F;
				ppc_rlwinmx(bop_d,bop_a,(32-s)&31,s,31,0);	// srwi: rotl 32-s, mask [s..31]
			}
			else
				{ bop_resolve_b(op); ppc_srwx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_sar:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm())
				ppc_srawix(bop_d,bop_a,op->rs2._imm&0x1F,0);	// srawi
			else
				{ bop_resolve_b(op); ppc_srawx(bop_d,bop_a,bop_b,0); }
			binop3_end(op);
			break;
		case shop_mul_i32:
			bop_resolve_a_d(op);
			if (op->rs2.is_imm_s16())
				ppc_mulli(bop_d,bop_a,op->rs2._imm);		// d = a * imm
			else
				{ bop_resolve_b(op); ppc_mullwx(bop_d,bop_a,bop_b,0,0); }
			binop3_end(op);
			break;


		// FPU_PIN: one instruction on pinned fr (fbop_resolve/fbop_end); preset off, the old lfs/lfs/op/stfs.
		case shop_fadd: fbop_resolve(op); ppc_faddsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;
		case shop_fsub: fbop_resolve(op); ppc_fsubsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;
		case shop_fmul: fbop_resolve(op); ppc_fmulsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;
		case shop_fdiv: fbop_resolve(op); ppc_fdivsx(fop_d,fop_a,fop_b,0); fbop_end(op); break;

		// --- Additional native integer binops ---------------------------------
		case shop_mul_u16:
			// rd = (u16)r1 * (u16)r2  (low 32 bits). Zero-extend both, mullw.
			binop_start(op);
			ppc_rlwinmx(ppc_rarg0,ppc_rarg0,0,16,31,0);	// clrlwi rarg0,rarg0,16
			ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,16,31,0);	// clrlwi rarg1,rarg1,16
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);
			binop_end(op);
			break;
		case shop_mul_s16:
			// rd = (s16)r1 * (s16)r2  (low 32 bits). Sign-extend both, mullw.
			binop_start(op);
			ppc_extshx(ppc_rarg0,ppc_rarg0,0);
			ppc_extshx(ppc_rarg1,ppc_rarg1,0);
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);
			binop_end(op);
			break;

		// mul_u64: low -> rd (macl), high -> rd2 (mach). Not rd+1: reg_mach precedes reg_macl, so rd+1 is PR.
		case shop_mul_u64:
			// 64-bit unsigned product of two 32-bit values.
			binop_start(op);
			// high word first (mulhwu) since low word overwrites an input reg.
			ppc_mulhwux(ppc_rarg2,ppc_rarg0,ppc_rarg1,0);	// hi = mulhwu(r1,r2)
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);	// lo = mullw(r1,r2)
			ppc_sh_store(ppc_rarg0,op->rd);
			ppc_sh_store(ppc_rarg2,op->rd2);
			break;
		case shop_mul_s64:
			// 64-bit signed product of two 32-bit values.
			binop_start(op);
			ppc_mulhwx(ppc_rarg2,ppc_rarg0,ppc_rarg1,0);	// hi = mulhw(r1,r2)
			ppc_mullwx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,0);	// lo = mullw(r1,r2)
			ppc_sh_store(ppc_rarg0,op->rd);
			ppc_sh_store(ppc_rarg2,op->rd2);
			break;

		// Matched ROTCL/DIV1 idiom: hardware divide, remainder = dividend - quo*divisor. Divide by zero is
		// undefined, not a trap; these are compiler-generated divisions.
		case shop_div32u:
			binop_start(op);				// rarg0=dividend, rarg1=divisor
			ppc_divwux(ppc_rarg2,ppc_rarg0,ppc_rarg1,0,0);	// quo = divwu
			ppc_mullwx(ppc_rarg3,ppc_rarg2,ppc_rarg1,0,0);	// quo*divisor
			ppc_subfx(ppc_rarg3,ppc_rarg3,ppc_rarg0,0,0);	// rem = dividend - quo*divisor
			ppc_sh_store(ppc_rarg2,op->rd);
			ppc_sh_store(ppc_rarg3,op->rd2);
			break;
		case shop_div32s:
			// divw truncates toward zero, same as the C canonical.
			binop_start(op);
			ppc_divwx(ppc_rarg2,ppc_rarg0,ppc_rarg1,0,0);	// quo = divw
			ppc_mullwx(ppc_rarg3,ppc_rarg2,ppc_rarg1,0,0);
			ppc_subfx(ppc_rarg3,ppc_rarg3,ppc_rarg0,0,0);
			ppc_sh_store(ppc_rarg2,op->rd);
			ppc_sh_store(ppc_rarg3,op->rd2);
			break;
		case shop_div32p2:
			// rd = T ? a : a-b (non-restoring fixup); T is 0/1 (the decoder emits and T,quo,1), so mask = T-1.
			binop_start(op);				// rarg0=a, rarg1=b
			ppc_sh_load(ppc_rarg2,op->rs3);			// T
			ppc_addi(ppc_rarg2,ppc_rarg2,-1);		// mask = T-1
			ppc_andx(ppc_rarg1,ppc_rarg1,ppc_rarg2,0);	// b &= mask
			ppc_subfx(ppc_rarg0,ppc_rarg1,ppc_rarg0,0,0);	// a -= (T ? 0 : b)
			binop_end(op);
			break;

		// --- Shifts with dynamic SH4 semantics (branchless) -------------------
		case shop_ror:
			// rotr by rotating left 32-(r2&31); rlwnm uses rB mod 32, so 32 for a zero count is harmless.
			binop_start(op);
			ppc_rlwinmx(ppc_rarg1,ppc_rarg1,0,27,31,0);	// rarg1 = r2 & 0x1F
			ppc_subfic(ppc_rarg1,ppc_rarg1,32);			// rarg1 = 32 - (r2&31)
			ppc_rlwnmx(ppc_rarg0,ppc_rarg0,ppc_rarg1,0,31,0);	// rotl, full mask
			binop_end(op);
			break;
		case shop_shld:
			// shld, branchless: r2 >= 0 ? r1 << (r2&31) : r1 >> ((-r2)&31), a right count of 0 forced to 32 so srw
			// gives 0 (as SH4 does).
			{
				binop_start(op);				// rarg0=r1, rarg1=r2
				// left = r1 << (r2 & 0x1F)
				ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,27,31,0);	// rarg2 = r2 & 0x1F
				ppc_slwx(ppc_rarg3,ppc_rarg0,ppc_rarg2,0);	// rarg3 = left
				// rcnt = (-r2) & 0x1F
				ppc_negx(ppc_rarg2,ppc_rarg1,0,0);		// -r2
				ppc_rlwinmx(ppc_rarg2,ppc_rarg2,0,27,31,0);	// rcnt = (-r2)&0x1F
				// force 32 when rcnt==0:  isZero=(rcnt-1)>>31 ; rcnt += isZero*32
				ppc_addi(ppc_r0,ppc_rarg2,-1);			// r0 = rcnt-1
				ppc_rlwinmx(ppc_r0,ppc_r0,1,31,31,0);		// r0 = (rcnt==0)?1:0  (sign bit -> bit31)
				ppc_rlwinmx(ppc_r0,ppc_r0,5,0,31,0);		// r0 <<= 5  -> 32 when zero
				ppc_orx(ppc_rarg2,ppc_rarg2,ppc_r0,0);		// rcnt |= 32 when zero
				ppc_srwx(ppc_rarg0,ppc_rarg0,ppc_rarg2,0);	// rarg0 = right
				// select: mask = (r2<0)? ~0 : 0
				ppc_srawix(ppc_r0,ppc_rarg1,31,0);
				ppc_andx(ppc_rarg0,ppc_rarg0,ppc_r0,0);		// right & mask
				ppc_andcx(ppc_rarg3,ppc_rarg3,ppc_r0,0);	// left & ~mask
				ppc_orx(ppc_rarg0,ppc_rarg0,ppc_rarg3,0);
				binop_end(op);
			}
			break;
		case shop_shad:
			// shad, branchless: r2 >= 0 ? r1 << (r2&31) : (s32)r1 >> ((-r2)&31), a count of 0 forced to 31 (sign fill).
			{
				binop_start(op);				// rarg0=r1, rarg1=r2
				ppc_rlwinmx(ppc_rarg2,ppc_rarg1,0,27,31,0);	// r2 & 0x1F
				ppc_slwx(ppc_rarg3,ppc_rarg0,ppc_rarg2,0);	// rarg3 = left
				// rcnt = (-r2) & 0x1F
				ppc_negx(ppc_rarg2,ppc_rarg1,0,0);
				ppc_rlwinmx(ppc_rarg2,ppc_rarg2,0,27,31,0);	// rcnt
				// force 31 when rcnt==0:  isZero=(rcnt-1)>>31 ; rcnt += isZero*31
				ppc_addi(ppc_r0,ppc_rarg2,-1);
				ppc_rlwinmx(ppc_r0,ppc_r0,1,31,31,0);		// (rcnt==0)?1:0
				ppc_mulli(ppc_r0,ppc_r0,31);			// 31 when zero else 0
				ppc_addx(ppc_rarg2,ppc_rarg2,ppc_r0,0,0);	// rcnt = 31 in zero-case
				ppc_srawx(ppc_rarg0,ppc_rarg0,ppc_rarg2,0);	// rarg0 = right (arithmetic)
				// select on sign of r2
				ppc_srawix(ppc_r0,ppc_rarg1,31,0);
				ppc_andx(ppc_rarg0,ppc_rarg0,ppc_r0,0);
				ppc_andcx(ppc_rarg3,ppc_rarg3,ppc_r0,0);
				ppc_orx(ppc_rarg0,ppc_rarg0,ppc_rarg3,0);
				binop_end(op);
			}
			break;

		// JIT_CARRY ops (were shop_ifb), forms in ps3_jit_carry.h: sources in rarg0/rarg1 or pinned, T in rarg2,
		// the dest pinned or rarg3, so none alias.

		case shop_adc:		// addc Rm,Rn : Rn = Rn + Rm + T ; T = carry
		case shop_sbc:		// subc Rm,Rn : Rn = Rn - Rm - T ; T = borrow
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				u32 d=carry_dest(op);
				ppc_sh_load(ppc_rarg2,reg_sr_T);
				if (op->op==shop_adc)
					emit_carry_addc(d,a,b,ppc_rarg2,ppc_rarg0,ppc_rarg1);
				else
					emit_carry_subc(d,a,b,ppc_rarg2,ppc_rarg0,ppc_rarg1);
				ppc_sh_store(ppc_rarg2,reg_sr_T);
				carry_dest_store(op,d);
			}
			break;

		case shop_negc:		// negc Rm,Rn : Rn = 0 - Rm - T ; T = borrow
			{
				// rs1 is Rm here (DM_NEGC), rs2 the T input.
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 d=carry_dest(op);
				ppc_sh_load(ppc_rarg2,reg_sr_T);
				emit_carry_negc(d,a,ppc_rarg2,ppc_rarg0);
				ppc_sh_store(ppc_rarg2,reg_sr_T);
				carry_dest_store(op,d);
			}
			break;

		case shop_addv:		// addv Rm,Rn : Rn = Rn + Rm ; T = signed overflow
		case shop_subv:		// subv Rm,Rn : Rn = Rn - Rm ; T = signed overflow
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				u32 d=carry_dest(op);
				emit_carry_ovf(d,a,b,ppc_rarg2,ppc_rarg0,ppc_rarg1,op->op==shop_subv);
				ppc_sh_store(ppc_rarg2,reg_sr_T);
				carry_dest_store(op,d);
			}
			break;

		case shop_cmpstr:	// cmp/str Rm,Rn : T = any byte lane is equal
			{
				u32 a=src_or_load(op->rs1,ppc_rarg0);
				u32 b=src_or_load(op->rs2,ppc_rarg1);
				emit_carry_cmpstr(a,b,ppc_rarg2,ppc_rarg3,ppc_r0);
				emit_cr0_bit_to_rarg0(BI_CR0_GT);
				binop_end(op);					// rd is sr_T
			}
			break;

		case shop_swaplb:	// swap.b Rm,Rn : swap the two LOW bytes
			{
				// Built in scratch, so a dest that aliases the source is safe.
				u32 s=src_or_load(op->rs1,ppc_rarg0);
				emit_carry_swaplb(ppc_rarg1,s);
				ppc_sh_store(ppc_rarg1,op->rd);
			}
			break;

		case shop_div1:		// div1 Rm,Rn : one non-restoring division step
			{
				// Rn is updated in place when it is pinned (rd == rs1 always holds for DM_ADC), else through rarg7.
				ppc_ireg dpin=GetIntReg(op->rd._reg);
				u32 A;
				bool a_scratch=false;
				if (dpin!=ppc_rinvalid && op->rs1.is_reg() && op->rs1._reg==op->rd._reg)
					A=(u32)dpin;
				else
				{
					ppc_sh_load(ppc_rarg7,op->rs1);
					A=ppc_rarg7;
					a_scratch=true;
				}
				u32 B=src_or_load(op->rs2,ppc_rarg6);

				ppc_sh_load(ppc_rarg0,reg_sr_status);
				ppc_sh_load(ppc_rarg2,reg_sr_T);
				emit_carry_div1(A,B,ppc_rarg0,ppc_rarg2,ppc_rarg1,ppc_rarg3,ppc_rarg5,ppc_rarg4);
				ppc_sh_store(ppc_rarg2,reg_sr_T);
				ppc_sh_store(ppc_rarg0,reg_sr_status);

				if (a_scratch)
					ppc_sh_store(A,op->rd);
			}
			break;

		// JIT_MAC: the @Rm+/@Rn+ reads are ordinary shop_readm ops (DM_MAC). The S==0 result is stored first;
		// only with SR.S set does sh4_mac_l/w redo it from the old accumulator.
		case shop_mac_l:
		case shop_mac_w:
			{
				const bool is_l = (op->op==shop_mac_l);

				ppc_sh_load(ppc_rarg0,op->rs1);			// a
				ppc_sh_load(ppc_rarg1,op->rs2);			// b
				ppc_sh_load(ppc_rarg2,reg_macl);		// old MACL
				ppc_sh_load(ppc_rarg3,reg_mach);		// old MACH
				ppc_sh_load(ppc_rarg4,op->rs3);			// SR.status

				// mac.w operands are already sign-extended (lha), so mullw gives the signed product; its sign is the high word.
				ppc_mullwx(ppc_rarg5,ppc_rarg0,ppc_rarg1,0,0);
				if (is_l)
					ppc_mulhwx(ppc_rarg6,ppc_rarg0,ppc_rarg1,0);
				else
					ppc_srawix(ppc_rarg6,ppc_rarg5,31,0);

				ppc_addcx(ppc_rarg7,ppc_rarg2,ppc_rarg5,0,0);	// MACL + lo
				ppc_addex(ppc_r11,ppc_rarg3,ppc_rarg6,0,0);	// MACH + hi + CA
				ppc_sh_store(ppc_rarg7,op->rd);			// -> MACL
				ppc_sh_store(ppc_r11,op->rd2);			// -> MACH

				// SR.S (value bit 1) is rare, so a predicted-not-taken branch over the call.
				ppc_andi(ppc_r0,ppc_rarg4,2);
				ppc_label* done=ppc_CreateLabel();
				ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq -> done
				if (is_l)
					ppc_call_c(sh4_mac_l);
				else
					ppc_call_c(sh4_mac_w);
				done->MarkLabel();
			}
			break;

		// --- Integer comparisons / test ---------------------------------------
		// (set*/test do their own operand load + immediate folding internally.)
		case shop_test:	// rd = (r1 & r2) == 0
			{
				// and./andi. read pinned rs1/rs2 in place and write scratch rarg0, which the CR0 read then overwrites.
				u32 a1=src_or_load(op->rs1,ppc_rarg0);

				if (op->rs2.is_imm_u16())
					ppc_andi(ppc_rarg0,a1,op->rs2._imm);		// andi. zero-extends the imm, so CR0 is 32-bit safe
				else
				{
					u32 b;
					if (op->rs2.is_imm()) { ppc_li(ppc_rarg1,op->rs2._imm); b=ppc_rarg1; }
					else b=src_or_load(op->rs2,ppc_rarg1);
					// Not `and.`: it sets CR0 from 64 bits, and pinned regs carry poison above bit 31 (a zero low word read
					// as non-zero, T=0 instead of 1).
					ppc_andx(ppc_rarg0,a1,b,0);
					ppc_cmpli(ppc_cr0,ppc_rarg0,0,0);		// cmplwi rarg0,0 -> CR0 from the low word
				}
				// JIT_CR0: the and./andi. above already set CR0.EQ.
				if (emit_t_to_cr0(BI_CR0_EQ,false))
					break;
				emit_cr0_bit_to_rarg0(BI_CR0_EQ);
				binop_end(op);
			}
			break;
		case shop_seteq: emit_setcc_signed(op,BI_CR0_EQ,false); break;
		case shop_setgt: emit_setcc_signed(op,BI_CR0_GT,false); break;
		case shop_setge: emit_setcc_signed(op,BI_CR0_LT,true);  break; // >= == !<
		case shop_setab: emit_setcc_unsigned(op,BI_CR0_GT,false); break;
		case shop_setae: emit_setcc_unsigned(op,BI_CR0_LT,true);  break; // >= == !<

		// --- Unary integer ----------------------------------------------------
		case shop_neg:    unop3_start(op); ppc_negx(bop_d,bop_a,0,0);    unop3_end(op); break;
		case shop_not:    unop3_start(op); ppc_norx(bop_d,bop_a,bop_a,0); unop3_end(op); break;
		case shop_ext_s8: unop3_start(op); ppc_extsbx(bop_d,bop_a,0);    unop3_end(op); break;
		case shop_ext_s16:unop3_start(op); ppc_extshx(bop_d,bop_a,0);    unop3_end(op); break;

		// --- Unary float (single instruction -> in-place under FPU_PIN) -------
		case shop_fabs: fuop_resolve(op); ppc_fabsx(fop_d,fop_a,0); fbop_end(op); break;
		case shop_fneg: fuop_resolve(op); ppc_fnegx(fop_d,fop_a,0); fbop_end(op); break;

		// Float compares: fcmpu reads pinned FPRs in place; only the 0/1 result goes through rarg0.
		case shop_fseteq:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 b=fsrc_or_load(op->rs2,ppc_farg1);
				ppc_fcmpu(ppc_cr0,a,b);
				emit_cr0_bit_to_rarg0(BI_CR0_EQ);
				ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;
		case shop_fsetgt:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				u32 b=fsrc_or_load(op->rs2,ppc_farg1);
				ppc_fcmpu(ppc_cr0,a,b);
				emit_cr0_bit_to_rarg0(BI_CR0_GT);
				ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;

		// Float<->int conversions bounce bit patterns through the context's 8-byte jit_scratch slot, not a red zone.
		case shop_cvt_f2i_t:	// (s32)truncate(f)
			{
				// rs1 float read in place (pinned) or into farg0. JIT FPUL MOVES: stfiwx straight into an unpinned rd;
				// else the result goes through jit_scratch into rarg0.
				u32 a=fsrc_or_load(op->rs1,ppc_farg0);
				const bool direct=get_jit_fpul_moves_preset() && fpul_slot_dst(op->rd);
				emit_ftrc(a,ppc_contex,(u32)offsetof(Sh4Context,jit_scratch),Sh4cntx.offset(op->rd._reg),ppc_rarg0,direct);
				if (direct)
					s_ppe_ftrc++;
				else
					ppc_sh_store(ppc_rarg0,op->rd);
			}
			break;
		case shop_cvt_i2f_n:	// (float)(s32)  round-to-nearest
		case shop_cvt_i2f_z:	// (float)(s32)  (s32->f32 is identical for both modes here)
			{
				// f0 = (double)(s32)r1 (exact), then frsp straight into the pinned rd. JIT FPUL MOVES loads the bias
				// from s_i2f_bias, not built in jit_scratch (a second load-hit-store).
				unop_start(op);					// rarg0 = r1
				u32 bias_at=0;
				if (get_jit_fpul_moves_preset())
				{
					verify((uintptr_t)&s_i2f_bias < 0x7FFF0000u);	// addis + lfd reach it
					bias_at=(u32)(uintptr_t)&s_i2f_bias;
					s_ppe_float++;
				}
				emit_i2f(ppc_rarg0,ppc_rarg1,ppc_farg1,ppc_contex,(u32)offsetof(Sh4Context,jit_scratch),bias_at);
				u32 d=fdst_reg(op->rd,ppc_farg0);
				ppc_frspx(d,ppc_f0,0);				// narrow to single
				fdst_store(op->rd,d,ppc_farg0);
			}
			break;

		// fmac: rd = rs1 + rs2*rs3 as one fmadds on pinned operands; fmadds reads A, B, C before writing D, so
		// aliasing is safe.
		case shop_fmac:
			{
				u32 a=fsrc_or_load(op->rs1,ppc_f0);	// addend (rs1)
				u32 b=fsrc_or_load(op->rs2,ppc_f1);
				u32 c=fsrc_or_load(op->rs3,ppc_f2);
				u32 d=fdst_reg(op->rd,ppc_f0);
				// fmaddsx(D,A,B,C) = A*C + B, so d = b*c + a is fmaddsx(d, b, a, c).
				ppc_fmaddsx(d,b,a,c,0);			// d = b*c + a
				fdst_store(op->rd,d,ppc_f0);
			}
			break;

		// --- FIPR: rd = dot(v1, v2) over 4 elements ---------------------------
		// rs1,rs2 are FV4 vectors (base fr); rd is the 4th element slot.
		case shop_fipr:
			if (get_jit_ftrv_preset())
			{
				FvElem a[4], b[4];
				for (u32 i=0;i<4;i++)
				{
					a[i]=fv_elem(op->rs1._reg+i);
					b[i]=fv_elem(op->rs2._reg+i);
				}
				emit_fipr(a,b,fv_elem(op->rd._reg),ppc_contex);
			}
			else
			{
				ppc_fvec_load(ppc_f1,op->rs1,0);
				ppc_fvec_load(ppc_f2,op->rs2,0);
				ppc_fmulsx(ppc_f0,ppc_f1,ppc_f2,0);		// acc = a0*b0
				for (u32 i=1;i<4;i++)
				{
					ppc_fvec_load(ppc_f1,op->rs1,i);
					ppc_fvec_load(ppc_f2,op->rs2,i);
					ppc_fmaddsx(ppc_f0,ppc_f1,ppc_f0,ppc_f2,0);	// acc = ai*bi + acc
				}
				ppc_sh_store_f32(ppc_f0,op->rd);
			}
			break;

		// ftrv: rd = XMTRX (xf, rs2) x FV (fr, rs1), out[i] = sum of m[4j+i]*v[j]; results go to f4..f7 first,
		// as rd aliases rs1.
		case shop_ftrv:
			if (get_jit_ftrv_preset() && ftrv_v2_ok(op))
			{
				FvElem v[4];
				for (u32 j=0;j<4;j++)
					v[j]=fv_elem(op->rs1._reg+j);
				emit_ftrv(v,Sh4cntx.offset(op->rs2._reg),ppc_contex);
			}
			else
			{
				// Load the input vector once into f8..f11.
				for (u32 j=0;j<4;j++)
					ppc_fvec_load(ppc_f8+j,op->rs1,j);

				for (u32 i=0;i<4;i++)
				{
					ppc_fvec_load(ppc_f1,op->rs2,i);		// m[i]
					ppc_fmulsx(ppc_f0,ppc_f1,ppc_f8,0);		// out = m[i]*v0
					for (u32 j=1;j<4;j++)
					{
						ppc_fvec_load(ppc_f1,op->rs2,j*4+i);	// m[j*4+i]
						ppc_fmaddsx(ppc_f0,ppc_f1,ppc_f0,ppc_f8+j,0);	// out += m*vj
					}
					ppc_fmrx(ppc_f4+i,ppc_f0,0);			// stash out[i]
				}
				for (u32 i=0;i<4;i++)
					ppc_fvec_store(ppc_f4+i,op->rd,i);
			}
			break;

		// fsqrt / fsrra are handled below. fsrra is always the libm call;
		// fsqrt is too unless JIT_FSQRT is on, which inlines it (see above).

		// --- FSCA: rd[0]=sin_table[idx], rd[1]=sin_table[idx+0x4000] ----------
		// idx = rs1 & 0xFFFF. Table entries are f32 (4 bytes).
		case shop_fsca:
			{
				// idx*4 = (fpul & 0xFFFF)*4: rlwinm reads pinned rs1 in place -> rarg0.
				u32 fp=src_or_load(op->rs1,ppc_rarg0);
				ppc_rlwinmx(ppc_rarg0,fp,2,14,29,0);		// rarg0 = (idx & 0xFFFF) * 4
				// rarg1 = &sin_table  (split hi/lo via lis+addi pattern)
				u32 lo=ppc_addr_high(ppc_rarg1,(void*)&sin_table[0]);
				ppc_addi(ppc_rarg1,ppc_rarg1,lo);		// rarg1 = base of sin_table
				ppc_lfsx(ppc_f0,ppc_rarg1,ppc_rarg0);		// sin_table[idx]
				ppc_fvec_store(ppc_f0,op->rd,0);
				// +0x4000 entries * 4 bytes = +0x10000 (doesn't fit addi s16;
				// use addis to add 1<<16).
				ppc_addis(ppc_rarg0,ppc_rarg0,1);		// rarg0 += 0x10000
				ppc_lfsx(ppc_f1,ppc_rarg1,ppc_rarg0);		// sin_table[idx+0x4000]
				ppc_fvec_store(ppc_f1,op->rd,1);
			}
			break;

		// sync_sr: UpdateSR() swaps r[]/r_bank[] in memory on SR.RB, so pinned GPRs are flushed and reloaded. Its
		// return is ignored: every sync_sr block ends in BET_*Intr, which runs UpdateINTC.
		case shop_sync_sr:
			reg_flush_all();
			ppc_call_c(&UpdateSR);
			reg_reload_all();
			break;

		// sync_fpscr: UpdateFPSCR() swaps fr<->xf in memory on an FR change, so pinned FRs are
		// flushed and reloaded around it. JIT_FSCHG: fschg only flips SZ, so keep old_fpscr in step.
		case shop_sync_fpscr:
			if ((op->flags & SYNC_FPSCR_SZ_ONLY) && get_jit_fschg_preset())
			{
				ppc_sh_load(ppc_rarg0, reg_fpscr);
				ppc_stw(ppc_rarg0, ppc_contex, (u32)SH4CTX_OFS_OLD_FPSCR);
			}
			else if (get_jit_frchg_preset() && get_fpu_pin_preset())
			{
				// JIT FRCHG (plan 5.29): only FR changed -> inline bank swap; any other change takes the C path.
				emit_frchg_test(ppc_rarg0, ppc_rarg1, ppc_contex, Sh4cntx.offset(reg_fpscr),
				                (u32)SH4CTX_OFS_OLD_FPSCR);
				ppc_label* full=ppc_CreateLabel();
				ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne full
				emit_frchg_swap(ppc_rarg0, ppc_contex, (u32)SH4CTX_OFS_OLD_FPSCR, Sh4cntx.offset(reg_xf_0),
				                GetFloatReg(reg_fr_0));
				ppc_label* done=ppc_CreateLabel();
				ppc_bx(0,0,0);					// b done
				full->MarkLabel();
				reg_flush_all_fpu();
				ppc_call_c(&UpdateFPSCR);
				reg_reload_all_fpu();
				done->MarkLabelLong();
				s_frchg_sites++;
			}
			else
			{
				reg_flush_all_fpu();
				ppc_call_c(&UpdateFPSCR);
				reg_reload_all_fpu();
			}
			break;

		// pref: only SQ addresses ((addr>>26) == 0x38) flush the store queue; else a no-op, or with JIT PREFETCH
		// (plan 5.30) a host prefetch of the RAM line. MMU or not is chosen at compile time from CCN_MMUCR.AT.
		case shop_pref:
			{
				ppc_sh_load(ppc_rarg0,op->rs1);
				ppc_clrldi32(ppc_rarg0,ppc_rarg0);		// ELFv1: caller extends u32
				ppc_rlwinmx(ppc_rarg1,ppc_rarg0,6,26,31,0);	// rarg1 = addr >> 26
				ppc_cmpi(ppc_cr0,ppc_rarg1,0x38,0);		// SQ region?
				ppc_label* skip=ppc_CreateLabel();
				ppc_bcx(BO_FALSE,BI_CR0_EQ,0,0,0);		// bne skip (not SQ -> no-op)
				if (CCN_MMUCR.AT)
					ppc_call_c(&do_sqw_mmu);			// do_sqw reloads rarg0=addr arg
				else
					ppc_call_c(&do_sqw_nommu);
				if (get_jit_prefetch_preset() && s_fm_ok && !CCN_MMUCR.AT)
				{
					ppc_label* done=ppc_CreateLabel();
					ppc_bx(0,0,0);				// b done
					skip->MarkLabel();
					emit_pref_dcbt(ppc_rarg1,ppc_rarg0,s_fm_ram_hi);
					done->MarkLabelLong();
					s_pref_sites++;
				}
				else
					skip->MarkLabel();
			}
			break;

		// fsqrt / fsrra: single f32->f32 calls (accurate libm path).
		// arg in farg0 (f1), result in frv0 (f1) per PPC FP calling convention.
		case shop_fsqrt:
			if (get_jit_fsqrt_preset())
			{
				static bool announced=false;
				if (!announced)
				{
					announced=true;
					printf("[fsqrt] JIT_FSQRT active: inline fsqrts (no libm call)\n");
					fflush(stdout);
				}
				u32 a=fsrc_or_load(op->rs1,ppc_f0);
				u32 d=fdst_reg(op->rd,ppc_f0);
				ppc_fsqrts(d,a);
				fdst_store(op->rd,d,ppc_f0);
			}
			else
			{
				ppc_sh_load_f32(ppc_farg0,op->rs1);
				ppc_call_c(&rec_fsqrt);
				ppc_sh_store_f32(ppc_frv0,op->rd);
			}
			break;
		case shop_fsrra:
			if (get_jit_fsqrt_preset())
			{
				// Rounds sqrt to single before the divide, exactly as 1.0f/sqrtf(x).
				u32 a=fsrc_or_load(op->rs1,ppc_f0);
				u32 d=fdst_reg(op->rd,ppc_f0);
				ppc_fsqrts(ppc_f2,a);
				u32 lo=ppc_addr_high(ppc_rarg0,(void*)&s_fsrra_one);
				ppc_lfs(ppc_f3,ppc_rarg0,lo);
				ppc_fdivsx(d,ppc_f3,ppc_f2,0);
				fdst_store(op->rd,d,ppc_f0);
			}
			else
			{
				ppc_sh_load_f32(ppc_farg0,op->rs1);
				ppc_call_c(&rec_fsrra);
				ppc_sh_store_f32(ppc_frv0,op->rd);
			}
			break;

		default:
			//canonical fallback ~
      if (!shil_chf[op->op]) {
          printf("OH CRAP %d\n", op->op);
          die("Recompiler doesn't know about that opcode");
      }
			// Canonical fallback: some handlers change context GPRs or fr/xf directly (sync_sr's bank switch, CPT_ptr
			// params to &Sh4cntx.fr[n]), so pinned registers are flushed before and reloaded after.
			reg_flush_all();
			reg_flush_all_fpu();
			shil_chf[op->op](op);
			reg_reload_all();
			reg_reload_all_fpu();
			break;

		}
		tcopy_op_end(compile_state.tc,*op);
	}

	ngen_End(block);

	// JIT_HOTBLOCKS split: up to here is the hot body; FlushCold's out-of-line paths are reported separately.
	u8 *const _hb_cold_start = (u8*)emit_GetCCPtr();

	// JIT CYCLE CHECK 1 and 2: the block's cycle exit, first in the cold area so its 16-bit blt reaches it.
	if (s_cycle_exit)
	{
		const snat d=(u8*)emit_GetCCPtr()-(u8*)s_cycle_exit;
		verify(d>0 && d<32768);
		s_cycle_exit->MarkLabel();
		s_cycle_exit=0;
		ppc_li(ppc_next_pc,block->start);
		ppc_jump(loop_do_update_write);
	}

	// Emit the out-of-line mem slow paths after the block tail.
	const u32 hb_frags = FlushCold(hb>=0 ? &s_hb_cold[hb] : 0);
	make_address_range_executable((u8*)rv, (u8*)emit_GetCCPtr()-(u8*)rv);

	// Sizes and classes cover the hot body (it runs on every entry) without the probe's own counter.
	if (hb>=0)
	{
		HotBlock& h = s_hb[hb];
		const u32 hot = (u32)(_hb_cold_start-(u8*)rv);
		h.ppc_start  = (u8*)rv;
		h.ppc_size   = hot-16;
		h.ppc_cold   = (u32)((u8*)emit_GetCCPtr()-_hb_cold_start);
		h.probe_ofs  = hb_probe;
		h.cold_frags = hb_frags;
		hot_classify((const u32*)rv, hot/4, hb_probe/4, (uintptr_t)rv, (uintptr_t)_hb_cold_start,
		             (uintptr_t)CodeCache, (uintptr_t)CodeCache+CODE_SIZE, h.cls);
	}

	return rv;
}



void ngen_ResetBlocks()
{
	// Block-cache clear: patched fastmem sites died with their blocks, so drop their trampolines too.
	rec_fastmem_reset_pool();
}

// The `bl` word DoStatic() emitted in slot 2 of a static site, recomputed for a
// given site address. Used to prove the site still exists before patching it.
static inline u32 static_site_bl_word(const u32* site)
{
	snat offs = ((u8*)ngen_LinkBlock_Static_stub - (u8*)(site+2)) >> 2;
	return 0x48000000u | ((0xffffffu & (u32)offs) << 2) | 1u;
}

void* FASTCALL ngen_LinkBlock_Static(u32 pc,u32* patch)
{
	next_pc=pc;
	
	DynarecCodeEntry* rv=rdv_FindOrCompile();
	
	// rdv_FindOrCompile may have cleared the cache, and the next compile lands on the dead site: patch only if
	// slot 2 is still our `bl` (this livelocked before), else just enter rv.
	const bool site_live = (patch[2] == static_site_bl_word(patch));

	// Bring-up trace, logged before the write (a bad link kills the SH4 thread); capped for the UDP log.
	if (get_debug_loop())
	{
		static u32 links=0;
		if (links < 64)
		{
			printf("[link] #%u pc=%08X site=%p [%08X %08X %08X] want_bl=%08X %s"
			       " -> rv=%p disp=%d\n",
			       links, pc, (void*)patch, patch[0], patch[1], patch[2],
			       static_site_bl_word(patch), site_live?"LIVE":"DEAD",
			       (void*)rv, (int)(((u8*)rv-(u8*)patch)>>2));
		}
		links++;
	}

	if (!site_live)
		return (void*)rv;

	emit_ptr=patch;
	{
		ppc_jump(rv);
	}
	emit_ptr=0;

	make_address_range_executable(patch, 1*sizeof(u32));

	return (void*)rv;
}

// DYN_IC fill, from the `bl` in slot 0 of a site: pc is the target taken, site is slot 0. Writes the guard over
// the site (once) and returns the target's code for the stub's bctr.
void* FASTCALL ngen_LinkBlock_Dynamic_IC(u32 pc,u32* site)
{
	next_pc=pc;

	DynarecCodeEntry* rv=rdv_FindOrCompile();

	if (!dynic_site_live(site,ngen_LinkBlock_Dynamic_IC_stub))
		return (void*)rv;

	// The guard's `b` reaches 32 MB, which covers the whole cache (CODE_SIZE); a failed compile (rv 0) would not.
	const snat d=(u8*)rv-(u8*)(site+3);
	verify(d<33554432 && d>-33554432);
	emit_ptr=site;
	emit_dynic_guard(ppc_rarg1,ppc_djump,pc,(void*)rv);
	emit_ptr=0;

	make_address_range_executable(site, 4*sizeof(u32));
	s_ic_fills++;

	return (void*)rv;
}

// ---- Main loop ----

extern "C" int get_dynarec_preset();
extern "C" int get_jit_safe_preset();
extern "C" int get_jit_dynends_preset();
extern "C" int get_jit_newops_preset();
extern "C" int get_jit_carry_preset();
extern "C" int get_jit_mac_preset();
extern "C" int get_jit_pcrel_preset();
extern "C" int get_jit_ifb_mask();
extern "C" int get_jit_ifb_group();
extern "C" int get_jit_ifb_lomask();
extern "C" int get_jit_code_cache_preset();

// One line per session with every JIT toggle in force: an install wipes nullDC.cfg, so a log needs its own.
static void ps3_jit_dump_toggles()
{
	printf("[jitcfg] Dynarec=%d JitSafe=%d JitDynEnds=%d -> static_linking=%s\n",
	       get_dynarec_preset(), get_jit_safe_preset(), get_jit_dynends_preset(),
	       (get_jit_safe_preset()||get_jit_dynends_preset()) ? "OFF" : "ON");
	printf("[jitcfg] NewOps=%d Carry=%d Mac=%d PcRelIfb=%d DebugLoop=%d IfbMask=%04X"
	       " IfbGroup=%d IfbLoMask=%04X\n",
	       get_jit_newops_preset(), get_jit_carry_preset(), get_jit_mac_preset(),
	       get_jit_pcrel_preset(), get_debug_loop(), get_jit_ifb_mask(),
	       get_jit_ifb_group(), get_jit_ifb_lomask());
	printf("[jitcfg] bcache=%d dyn_ic=%d fpu_pin=%d jit_align=%d ifb_flush=%d"
	       " hotblocks=%d fastmem=%d cr0=%d tfwd=%d JitFastmem=%d sqst=%d ftrv=%d cprop=%d dve=%d cyc=%d idle=%d"
	       " tcopy=%d sqmiss=%d mfocrf=%d fpul=%d ccache=%d frchg=%d pref=%d greuse=%d\n",
	       get_bcache_preset(), get_dyn_ic_preset(), get_fpu_pin_preset(),
	       get_jit_align_preset(), get_ifb_flush_preset(), get_hotblocks_preset(),
	       get_fastmem_preset(), get_jit_cr0_preset(), get_jit_tfwd_preset(),
	       get_jit_fastmem_preset(), get_jit_sq_store_preset(), get_jit_ftrv_preset(),
	       get_jit_constprop_preset(), get_jit_dve_preset(), get_jit_cycle_check_preset(),
	       get_jit_idle_skip_preset(), get_jit_tcopy_preset(), get_jit_sq_miss_preset(),
	       get_jit_mfocrf_preset(), get_jit_fpul_moves_preset(), get_jit_code_cache_preset(),
	       get_jit_frchg_preset(), get_jit_prefetch_preset(), get_jit_guard_reuse_preset());
}

void ngen_mainloop()
{
	// FASTMEM init before the first compile, after the presets and _vmem_reserve(). The PS3 window is
	// arithmetic: this only checks where the RAM block landed (Dynarec.MD 3.4).
	ps3_fastmem_init();

	if (loop_code==0)
	{
		ps3_jit_dump_toggles();

		loop_code=(void(*)())emit_GetCCPtr();
		{
			// PS3 ELFv1 frame (Dynarec.MD 3.2): 112-byte linkage area, LR at 16(sp), 16-byte alignment, back chain by
			// stdu, and 8-byte register saves (stw would truncate r13..r31).
			const u32 GPR_SAVE_OFS   = 112;
			u32 stac_alloc_size      = GPR_SAVE_OFS + 19*8;   // 264
			stac_alloc_size          = (stac_alloc_size + 15) & ~15u;

			ppc_mfspr(ppc_r0,ppc_spr_lr);
			ppc_std(ppc_r0,16,ppc_sp);                  // into the CALLER's frame
			ppc_stdu(ppc_sp,-(s32)stac_alloc_size,ppc_sp);

			//store gprs r13..r31 (19 preserved regs per ABI)
			for (int i=0;i<19;i++)
			{
				ppc_std(ppc_r13+i,GPR_SAVE_OFS+i*8,ppc_sp);
			}


			//cntx base
			ppc_lip(ppc_contex,&Sh4cntx);

			// Load the pinned SH4 GPRs once, before the loop_no_update entry; they stay live in r14..r28 all run.
			reg_reload_all();
			// Same, for the FPU_PIN preset's fr[0..15] -> f14..f29 (no-op if off).
			reg_reload_all_fpu();

			// Cycle budget per timeslice from the accuracy preset (1792/896/448), the same s_timeslice that
			// UpdateSystem_no_event() advances TMU/PVR by; baked in once per session.
			const s32 jit_timeslice = sh4_GetTimeslice();

			//cycles
			ppc_li(ppc_cycles,jit_timeslice);

			//and pc!
			ppc_sh_load(ppc_next_pc,reg_nextpc);

			//no_update
			loop_no_update=emit_GetCCPtr();

			//handy function !
			ppc_call_and_jump_c(bm_GetCode);

			//do_update_write
			loop_do_update_write=emit_GetCCPtr();


			//next_pc _MUST_ be on ram since update system uses it for interrupt processing
			ppc_sh_store(ppc_next_pc,reg_nextpc);
			ppc_addi(ppc_cycles,ppc_cycles,jit_timeslice);	//add cycles (preset-latched timeslice)

			// UpdateSystem_no_event runs every timeslice without flushing pinned GPRs; only a pending interrupt
			// flushes, runs the GPR-touching handler and reloads.
			ppc_call_c(UpdateSystem_no_event);
			ppc_cmpi(ppc_cr0,ppc_rrv0,0,0);			// rv == 0 ?
			ppc_label* no_intr=ppc_CreateLabel();
			ppc_bcx(BO_TRUE,BI_CR0_EQ,0,0,0);		// beq no_intr (skip handler)
			{
				reg_flush_all();
				ppc_call_c(UpdateSystem_handle_event);
				reg_reload_all();
			}
			no_intr->MarkLabel();
			ppc_sh_load(ppc_next_pc,reg_nextpc);
			//
			ppc_jump(loop_no_update);
			//right


		} //that was mainloop


		//ngen_FailedToFindBlock
		ngen_FailedToFindBlock=(void(*)())emit_GetCCPtr();
		{
			ppc_call_and_jump_c(&rdv_FailedToFindBlock);
		}

    // ---- Static block linking ----

		ngen_LinkBlock_Static_stub=emit_GetCCPtr();
		{
			//not used for now
			ppc_mfspr(ppc_rarg1,ppc_spr_lr);
			
			ppc_addi(ppc_rarg1,ppc_rarg1,(u32)-12);
			// DoStatic's addis+ori pair sign-extends any 0x8C... pc into the
			// upper half (bug 4); ELFv1 makes the CALLER extend a u32 argument.
			ppc_clrldi32(ppc_rarg0,ppc_rarg0);
			ppc_call_and_jump_c(&ngen_LinkBlock_Static);
		}

		ngen_LinkBlock_Dynamic_1st_stub=emit_GetCCPtr();
		{
			//not used for now
		}

		ngen_LinkBlock_Dynamic_2nd_stub=emit_GetCCPtr();
		{
			//not used for now
		}

		// DYN_IC fill stub, entered by slot 0's `bl`: LR = site+4, rarg0 = target pc; patch and jump to the result.
		ngen_LinkBlock_Dynamic_IC_stub=emit_GetCCPtr();
		{
			emit_dynic_stub_args(ppc_rarg0,ppc_rarg1);
			ppc_call_and_jump_c(&ngen_LinkBlock_Dynamic_IC);
		}

		ngen_BlockCheckFail_stub=emit_GetCCPtr();
		{
			ppc_call_and_jump_c(&rdv_BlockCheckFail);
		}

		//Make _SURE_ this code is not overwriten !
		emit_SetBaseAddr();
		
		char file[512];
		snprintf(file, sizeof(file), "dynarec_%08X.bin", (u32)(uintptr_t)loop_code);
		char* path=GetEmuPath(file);

		FILE* f = fopen(path, "wb");
		free(path);

		if (f)
		{
			fwrite((void*)loop_code, 1, (u8*)emit_GetCCPtr()-(u8*)loop_code, f);
			fclose(f);  // fclose flushes; explicit fflush is redundant
		}
		
    // CACHE COHERENCY
    make_address_range_executable((u8*)loop_code, (u8*)emit_GetCCPtr()-(u8*)loop_code);
	}

	ppc_enter_code((void*)loop_code);
}

extern "C" int get_jit_safe_preset();
extern "C" int get_jit_dynends_preset();

void ngen_GetFeatures(ngen_features* dst)
{
	// Two bring-up switches: JITSAFE sends non-branch opcodes to the interpreter, JITDYNENDS turns off static
	// linking, so opcode bugs and linking bugs can be told apart.
	dst->InterpreterFallback = get_jit_safe_preset() != 0;
	dst->OnlyDynamicEnds     = get_jit_safe_preset() != 0 ||
	                           get_jit_dynends_preset() != 0;
}