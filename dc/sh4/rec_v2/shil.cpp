// SHIL block analysis (AnalyseBlock), canonical opcode instantiation, mac.l/mac.w and the JIT C-call census.
// AnalyseBlock runs plan 5.21's passes (shil_opt.h); the older write-after-write pass below stays off.

#include "types.h"
#include "shil.h"
#include "decoder.h"
#include "ngen.h"
#include "shil_opt.h"
#include "ccall_census.h"
#include <stdio.h>

// Old write-after-write pass (A/B switch, off): 1 compiles it in.
#define SHIL_DCE 0

// 1 prints a periodic "blocks / ops removed" line; off, as a printf on the compile path skews a speed A/B.
#define SHIL_DCE_STATS 0

#if SHIL_DCE

// Signed with -1 = "never": as u32, 0xFFFFFFFF read as the newest read and hid the commonest dead write
// (written twice, never read).
static s32 RegisterWrite[sh4_reg_count];
static s32 RegisterRead [sh4_reg_count];

static u32 fallback_blocks = 0;
static u32 total_blocks    = 0;
static u32 REMOVED_OPS     = 0;

// Only pure register ops may go: readm can hit MMIO, writem/pref/sync_*/ifb/jumps and ftrv/fsca do work liveness
// cannot see, and two-output ops (carry ops, div32*, mul64) are judged one register at a time here.
static bool shil_op_is_pure(shilop op)
{
    switch (op)
    {
    case shop_mov32:     case shop_mov64:
    case shop_and:       case shop_or:        case shop_xor:
    case shop_not:       case shop_add:       case shop_sub:
    case shop_neg:       case shop_shl:       case shop_shr:
    case shop_sar:       case shop_ror:       case shop_shld:
    case shop_shad:      case shop_ext_s8:    case shop_ext_s16:
    case shop_mul_u16:   case shop_mul_s16:   case shop_mul_i32:
    case shop_mul_u64:   case shop_mul_s64:
    case shop_cvt_f2i_t: case shop_cvt_i2f_n: case shop_cvt_i2f_z:
    case shop_test:      case shop_seteq:     case shop_setge:
    case shop_setgt:     case shop_setae:     case shop_setab:
    case shop_cmpstr:    case shop_swaplb:
    case shop_fadd:      case shop_fsub:      case shop_fmul:
    case shop_fdiv:      case shop_fabs:      case shop_fneg:
    case shop_fsqrt:     case shop_fipr:      case shop_fmac:
    case shop_fsrra:     case shop_fseteq:    case shop_fsetgt:
        return true;
    default:
        return false;
    }
}

// Record the ordinal of the instruction that *reads* each sub-register of p.
static void RegReadInfo(const shil_param& p, s32 ord)
{
    if (!p.is_reg())
        return;
    const int n = p.count();
    for (int i = 0; i < n; ++i)
        RegisterRead[p._reg + i] = ord;
}

// Record the writer of each sub-register of p. A write is dead when no read came between the previous
// write and this one (RegisterRead[r] < RegisterWrite[r]); the original had the test inverted.
static void RegWriteInfo(shil_opcode* ops, const shil_param& p, s32 ord)
{
    if (!p.is_reg())
        return;
    const int n = p.count();
    for (int i = 0; i < n; ++i)
    {
        const u32 r     = p._reg + i;
        const s32 prevW = RegisterWrite[r];
        const s32 prevR = RegisterRead[r];

        // prevW < 0: no earlier write. prevR < prevW: no read since it (prevR == -1, never read, included).
        if (prevW >= 0 && prevR < prevW && shil_op_is_pure(ops[prevW].op))
        {
            // No per-op log: it floods the console and skews the speed comparison.
            ops[prevW].Flow = 1;
        }
        RegisterWrite[r] = ord;
    }
}

#endif // SHIL_DCE

extern "C" int get_jit_constprop_preset();
extern "C" int get_jit_dve_preset();

// Plan 5.21: constant propagation, then dead value elimination, each on its own JIT SPEED toggle. Neither with the
// MMU JIT (P38): an exception between two ops must find every register written.
extern bool g_jit_mmu;
static void shil_opt_run(DecodedBlock* blk)
{
    const bool cp  = get_jit_constprop_preset() != 0;
    const bool dve = get_jit_dve_preset() != 0;
    if ((!cp && !dve) || g_jit_mmu)
        return;

    shil_opt_stats.blocks++;
    shil_opt_stats.ops_in += blk->oplist.size();
    if (cp)
    {
        ngen_features f;
        ngen_GetFeatures(&f);
        shil_constprop(blk, !f.OnlyDynamicEnds);
    }
    if (dve)
        shil_dead_values(blk);
    shil_opt_stats.ops_out += blk->oplist.size();
}

// [cprop] line, once per stats window while a 5.21 toggle is on and blocks were compiled since the last one.
static void shil_opt_dump()
{
    static u32 last_blocks;
    if ((!get_jit_constprop_preset() && !get_jit_dve_preset()) || shil_opt_stats.blocks == last_blocks)
        return;
    last_blocks = shil_opt_stats.blocks;
    const ShilOptStats& s = shil_opt_stats;
    printf("[cprop] cp=%d dve=%d blocks=%u ops %u -> %u | fold=%u imm=%u shift=%u chain=%u readm=%u"
           " jdyn=%u pref=%u late=%u | dve=%u\n",
           get_jit_constprop_preset(), get_jit_dve_preset(), s.blocks, s.ops_in, s.ops_out,
           s.fold, s.imm, s.shift, s.chain, s.readm, s.jdyn, s.pref, s.late, s.dve);
}

// Old pass: block-local, never removes a block's last write, and assumes no precise mid-block exceptions
// (as the rest of rec_v2 does). Controlled by SHIL_DCE.
void AnalyseBlock(DecodedBlock* blk)
{
    shil_opt_run(blk);
#if SHIL_DCE
    memset(RegisterWrite, 0xFF, sizeof(RegisterWrite));  // -1 as s32
    memset(RegisterRead,  0xFF, sizeof(RegisterRead));   // -1 as s32

    ++total_blocks;

    const s32 nOps = static_cast<s32>(blk->oplist.size());

    for (s32 i = 0; i < nOps; ++i)
    {
        shil_opcode* op = &blk->oplist[i];
        op->Flow = 0;

        // An ifb reads and writes any register: bail on the whole block, not just the ifb itself.
        if (op->op == shop_ifb)
        {
            ++fallback_blocks;
            for (s32 j = 0; j < i; ++j)
                blk->oplist[j].Flow = 0;
            return;
        }

        RegReadInfo(op->rs1, i);
        RegReadInfo(op->rs2, i);
        RegReadInfo(op->rs3, i);

        // div1 reads SR.Q/M through sr_status, which no param carries: without this, a second division's
        // write makes the div0s that set Q/M look dead.
        if (op->op == shop_div1)
            RegReadInfo(shil_param(reg_sr_status), i);

        // mac.l/mac.w accumulate into MACL/MACH, which they also read: two macs in a row look dead otherwise.
        if (op->op == shop_mac_l || op->op == shop_mac_w)
        {
            RegReadInfo(shil_param(reg_macl), i);
            RegReadInfo(shil_param(reg_mach), i);
        }

        RegWriteInfo(&blk->oplist[0], op->rd,  i);
        RegWriteInfo(&blk->oplist[0], op->rd2, i);
    }

    for (int i = static_cast<int>(blk->oplist.size()) - 1; i >= 0; --i)
    {
        if (blk->oplist[i].Flow)
        {
            blk->oplist.erase(blk->oplist.begin() + i);
            ++REMOVED_OPS;
        }
    }

#if SHIL_DCE_STATS
    // Periodic, not per-block: a printf on every compiled block floods the
    // console and skews the very measurement it is meant to support.
    if ((total_blocks & 8191) == 0)
    {
        printf("[DCE] blocks=%u ifb-skipped=%u ops-removed=%u\n",
               total_blocks, fallback_blocks, REMOVED_OPS);
    }
#endif
#endif // SHIL_DCE
}

// Forward declarations the canonical implementations need.
void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);
void UpdateFPSCR();
bool UpdateSR();

#include "dc/sh4/ccn.h"
#include "dc/sh4/sh4_registers.h"

// mac.l / mac.w, the one copy of the SH-4 manual's saturation rules (canonical bodies and the S==1 JIT path).
// The old interpreter died on mac.l with S set and skipped mac.w entirely, post-increments included.
extern "C" void sh4_mac_l(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs)
{
    CCALL(CC_MAC_SAT);
    const u64 mul  = (u64)((s64)(s32)a * (s64)(s32)b);
    u32       res0 = oldl + (u32)mul;
    u32       res2 = (u32)(mul >> 32) + (oldl > res0 ? 1u : 0u);

    if (srs & 2)
    {
        // 48-bit saturation: only the low 16 bits of MACH take part, and the
        // result is clamped to [0xFFFF800000000000, 0x00007FFFFFFFFFFF].
        res2 += (oldh & 0x0000FFFFu);
        if ((s32)res2 < 0 && res2 < 0xFFFF8000u) { res2 = 0xFFFF8000u; res0 = 0x00000000u; }
        if ((s32)res2 > 0 && res2 > 0x00007FFFu) { res2 = 0x00007FFFu; res0 = 0xFFFFFFFFu; }
    }
    else
        res2 += oldh;

    macl = res0;
    mach = res2;
}

extern "C" void sh4_mac_w(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs)
{
    CCALL(CC_MAC_SAT);
    const u32 prod = (u32)((s32)a * (s32)b);
    u32       res0 = oldl + prod;
    u32       res2 = oldh;

    if (srs & 2)
    {
        // 32-bit saturation, MACH untouched (its LSB overflow flag is SH-1/SH-DSP, not this core). Overflow
        // only when both addends share a sign and the sum flips it.
        const u32 dest = (s32)oldl < 0 ? 1u : 0u;
        const u32 src  = ((s32)prod < 0 ? 1u : 0u) + dest;
        const u32 ans  = ((s32)res0 < 0 ? 1u : 0u) + dest;
        if (ans == 1)
        {
            if (src == 0) res0 = 0x7FFFFFFFu;
            if (src == 2) res0 = 0x80000000u;
        }
    }
    else
    {
        const u32 sext = (u32)((s32)prod >> 31);        // 0 or 0xFFFFFFFF
        res2 += sext + (oldl > res0 ? 1u : 0u);         // + carry out of the low add
    }

    macl = res0;
    mach = res2;
}

// C call-out census (JIT_CCALLS preset, see ccall_census.h). The storage lives here so dc/ links on its own.
extern "C"
{
	u32 g_ccall[CC_COUNT] = { 0 };
	u32 g_ccall_area[2][CCA_COUNT] = { { 0 } };
	u32 g_ccall_src[2][CCS_COUNT] = { { 0 } };
	const u8* g_ccall_tramp_lo = 0;
	const u8* g_ccall_tramp_hi = 0;
	const u8* g_ccall_cc_lo = 0;
	const u8* g_ccall_cc_hi = 0;
	int g_jit_ccalls_preset = 0;
	u32 g_mem64[2][M64_COUNT] = { { 0 } };
	u32 g_mem64_last[2] = { 0 };
}

// Which side of the JIT a call-out came from: a fastmem trampoline, emitted code, or the emulator's own C code
// (where most system-RAM writes turned out to live: block transfers).
static const char* const s_ccall_src_name[CCS_COUNT] =
{
	"from:tramp", "from:codecache", "from:other",
};

// SH4 physical areas: 1 PVR VRAM, 3 system RAM, 4 TA / texture FIFO; the rest are named so a rare hit reads.
static const char* const s_ccall_area_name[CCA_COUNT] =
{
	"a0:boot/reg", "a1:VRAM", "a2", "a3:RAM",
	"a4:TA", "a5", "a6", "a7",
};

static const char* const s_ccall_name[CC_COUNT] =
{
	"timeslice",   // UpdateSystem_no_event
	"interrupt",   // UpdateSystem_handle_event / UpdateINTC
	"getcode",     // bm_GetCode
	"sqw",         // do_sqw  (store queue -> TA)
	"sync_sr",     // UpdateSR
	"sync_fpscr",  // UpdateFPSCR (full path)
	"fsqrt",       // rec_fsqrt  -> libm
	"fsrra",       // rec_fsrra  -> libm
	"readmem",     // _vmem_ReadMem*
	"writemem",    // _vmem_WriteMem*
	"mac_sat",     // sh4_mac_l/w saturation
};

extern "C" void ccall_census_dump(double seconds, double vbs)
{
	// The stats window calls this every second, so plan 5.21's [cprop] line rides on it.
	shil_opt_dump();

	if (!g_jit_ccalls_preset || seconds <= 0.0)
		return;

	// [mem64] (plan 6.7): one line per window while the row is ON, so that a count of 0 is a result too.
	static const char* const s_m64_name[M64_COUNT] = { "aram", "aica", "a0", "vram", "ram", "ta", "p4", "other" };
	char m64[256];
	int m64n = snprintf(m64, sizeof(m64), "[mem64] %.2fs  jit 64-bit C calls:", seconds);
	for (u32 rw = 0; rw < 2; rw++)
	{
		u32 sum = 0;
		for (u32 k = 0; k < M64_COUNT; k++)
			sum += g_mem64[rw][k];
		m64n += snprintf(m64 + m64n, sizeof(m64) - m64n, "%s %u", rw ? " | wr" : " rd", sum);
		for (u32 k = 0; k < M64_COUNT; k++)
		{
			if (g_mem64[rw][k])
				m64n += snprintf(m64 + m64n, sizeof(m64) - m64n, " %s %u", s_m64_name[k], g_mem64[rw][k]);
			g_mem64[rw][k] = 0;
		}
		if (sum)
			m64n += snprintf(m64 + m64n, sizeof(m64) - m64n, " last %08X", g_mem64_last[rw]);
	}
	printf("%s\n", m64);

	// Sort by rate, descending. CC_COUNT is tiny, so a selection sort once a
	// second costs nothing and keeps qsort out of the build.
	u32 idx[CC_COUNT];
	u32 n = 0;
	for (u32 i = 0; i < CC_COUNT; i++)
		if (g_ccall[i])
			idx[n++] = i;

	if (n == 0)
	{
		// Say so explicitly: a silent probe is indistinguishable from one that
		// was never switched on, and that ambiguity has cost a test run before.
		printf("[CC] %.2fs  no C call-outs counted\n", seconds);
		return;
	}

	for (u32 a = 0; a < n; a++)
	{
		u32 best = a;
		for (u32 b = a + 1; b < n; b++)
			if (g_ccall[idx[b]] > g_ccall[idx[best]])
				best = b;
		u32 t = idx[a]; idx[a] = idx[best]; idx[best] = t;
	}

	double total = 0.0;
	for (u32 i = 0; i < CC_COUNT; i++)
		total += (double)g_ccall[i];

	printf("[CC] %.2fs  %.0f call-outs/s total  (%.1f vbl/s)\n",
	       seconds, total / seconds, vbs);

	for (u32 a = 0; a < n; a++)
	{
		const u32 i = idx[a];
		const double per_s = (double)g_ccall[i] / seconds;
		const double per_f = (vbs > 0.0) ? per_s / vbs : 0.0;
		printf("[CC]   %-11s %10.0f/s  %9.1f/frame  %5.1f%%\n",
		       s_ccall_name[i], per_s, per_f,
		       total > 0.0 ? 100.0 * (double)g_ccall[i] / total : 0.0);

		// readmem/writemem are the only two with a per-area split, and it is
		// the split -- not the total -- that says whether anything can be done.
		if (i == CC_READMEM || i == CC_WRITEMEM)
		{
			const u32 rw = (i == CC_WRITEMEM) ? 1u : 0u;
			for (u32 k = 0; k < CCA_COUNT; k++)
			{
				if (!g_ccall_area[rw][k])
					continue;
				printf("[CC]     %-11s %10.0f/s  %9.1f/frame  %5.1f%% of that\n",
				       s_ccall_area_name[k],
				       (double)g_ccall_area[rw][k] / seconds,
				       (vbs > 0.0) ? (double)g_ccall_area[rw][k] / seconds / vbs : 0.0,
				       g_ccall[i] ? 100.0 * (double)g_ccall_area[rw][k] / (double)g_ccall[i] : 0.0);
			}
			for (u32 k = 0; k < CCS_COUNT; k++)
			{
				if (!g_ccall_src[rw][k])
					continue;
				printf("[CC]     %-14s %10.0f/s  %9.1f/frame  %5.1f%% of that\n",
				       s_ccall_src_name[k],
				       (double)g_ccall_src[rw][k] / seconds,
				       (vbs > 0.0) ? (double)g_ccall_src[rw][k] / seconds / vbs : 0.0,
				       g_ccall[i] ? 100.0 * (double)g_ccall_src[rw][k] / (double)g_ccall[i] : 0.0);
			}
		}
	}

	for (u32 i = 0; i < CC_COUNT; i++)
		g_ccall[i] = 0;
	for (u32 rw = 0; rw < 2; rw++)
	{
		for (u32 k = 0; k < CCA_COUNT; k++)
			g_ccall_area[rw][k] = 0;
		for (u32 k = 0; k < CCS_COUNT; k++)
			g_ccall_src[rw][k] = 0;
	}
}

// Instantiate canonical (portable C) implementations (SHIL_MODE 1).
#define SHIL_MODE 1
#include "shil_canonical.h"

// Instantiate the dispatch table (SHIL_MODE 3).
#define SHIL_MODE 3
#include "shil_canonical.h"
