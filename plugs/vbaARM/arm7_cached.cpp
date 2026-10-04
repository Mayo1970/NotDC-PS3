// ARM7DI (ARMv3) cached interpreter: each instruction is decoded once into a uop (computed-goto label + operands) with
// arm-new.h semantics; non-ARMv3 ops trap as undefined. The cache resets only with the core: SMC is not tracked.

#include "arm7.h"
#include "arm_mem.h"
#include "arm7_idle.h"

#define C_CORE
#define DEV_VERSION

// Define to count block entries by start PC and print the 10 hottest about once per wall second; no cost when off.
//#define ARM7_PROFILE_BLOCKS

#ifdef ARM7_PROFILE_BLOCKS
#include <map>
#include <vector>
#include <algorithm>
double os_GetSeconds();   // platform wall-clock (wii/main.cpp); seconds as double
static std::map<u32, unsigned long long> s_blockHits;
static double s_profLast = 0.0;

// Count one entry of the block at `pc` (called from DISPATCH()).
static inline void prof_hit(u32 pc) { s_blockHits[pc]++; }

// Once-per-slice: if >=1s elapsed, print the top 10 hottest blocks and reset.
static void prof_tick()
{
	double now = os_GetSeconds();
	if (s_profLast == 0.0) { s_profLast = now; return; }
	if (now - s_profLast < 1.0) return;
	s_profLast = now;

	std::vector<std::pair<u32, unsigned long long>> v(s_blockHits.begin(), s_blockHits.end());
	std::sort(v.begin(), v.end(),
	          [](const std::pair<u32, unsigned long long>& a,
	             const std::pair<u32, unsigned long long>& b) { return a.second > b.second; });
	int shown = (int)(v.size() < 10 ? v.size() : 10);
	printf("[ARM7 hot blocks] top %d of %d:\n", shown, (int)v.size());
	// NOTE: devkitPPC/newlib printf does not support %zu/%llu -> use %d/%u with
	// explicit casts (mismatched specifiers desync varargs and print garbage).
	for (int i = 0; i < shown; i++) {
		u32 pc = v[i].first;
		printf("  #%d  pc=%08X  hits=%u\n", i + 1, (unsigned)pc, (unsigned)v[i].second);
		// Dump the block's opcodes (up to 32), stopping after a block-ender
		// (branch / PC-write LDR/LDM{pc} / SWI / data-proc to r15).
		u32 a = pc;
		for (int n = 0; n < 32; n++) {
			u32 op = arm_ReadMem32(a & ARAM_MASK);
			printf("      %08X: %08X\n", (unsigned)a, (unsigned)op);
			u32 fam = (op >> 26) & 3;
			bool ender = false;
			if (fam == 2) {                       // LDM/STM or branch
				if ((op >> 25) & 1) ender = true;                 // branch (B/BL)
				else if (((op >> 20) & 1) && (op & 0x8000)) ender = true; // LDM{pc}
			} else if (fam == 3 && ((op >> 24) & 0xF) == 0xF) {
				ender = true;                     // SWI
			} else if (fam == 1) {                // LDR/STR
				if (((op >> 20) & 1) && (((op >> 12) & 0xF) == 15)) ender = true; // LDR pc
			} else if (fam == 0) {                // data-proc (+ mul/swp/psr)
				u32 op4 = (op >> 21) & 0xF;
				if (((op >> 12) & 0xF) == 15 && !(op4 >= 0x8 && op4 <= 0xB)) ender = true; // dp ->pc
			}
			a += 4;
			if (ender) break;
		}
	}
	s_blockHits.clear();
}
#define PROF_HIT(pc)  prof_hit(pc)
#define PROF_TICK()   prof_tick()
#else
#define PROF_HIT(pc)  ((void)0)
#define PROF_TICK()   ((void)0)
#endif

// ── Glue identical to arm7.cpp ──────────────────────────────────────────────
#define CPUReadMemoryQuick(addr) arm_ReadMem32((addr) & ARAM_MASK)
#define CPUReadByte arm_ReadMem8
#define CPUReadMemory arm_ReadMem32
#define CPUReadHalfWord arm_ReadMem16

#define CPUWriteMemory arm_WriteMem32
#define CPUWriteHalfWord arm_WriteMem16
#define CPUWriteByte arm_WriteMem8

#define reg arm_Reg
#define armNextPC arm_ArmNextPC

#define CPUUpdateTicksAccessSeq32(a) 1
#define CPUUpdateTicksAccess32(a) 1
#define CPUUpdateTicksAccess16(a) 1

// ── Shared core state (defined in arm7.cpp) ─────────────────────────────────
enum
{
	R13_IRQ=18, R14_IRQ=19, SPSR_IRQ=20,
	R13_USR=26, R14_USR=27,
	R13_SVC=28, R14_SVC=29, SPSR_SVC=30,
	R13_ABT=31, R14_ABT=32, SPSR_ABT=33,
	R13_UND=34, R14_UND=35, SPSR_UND=36,
	R8_FIQ=37, R9_FIQ=38, R10_FIQ=39, R11_FIQ=40, R12_FIQ=41,
	R13_FIQ=42, R14_FIQ=43, SPSR_FIQ=44,
};

typedef union { u32 I; } reg_pair;

extern u32 arm_ArmNextPC;
extern reg_pair arm_Reg[47];

// Scratch pseudo-registers of the split shift uops: shifter carry-out and shifted operand2, written by a shift uop
// and read by the data-proc uop after it.
#define R_COUT   45
#define R_SHIFT  46

extern bool C_OUT, N_FLAG, Z_FLAG, C_FLAG, V_FLAG;
extern bool armIrqEnable, armFiqEnable;
extern int armMode;
extern bool Arm7Enabled;
extern BYTE cpuBitsSet[256];

void CPUSwitchMode(int mode, bool saveState, bool breakLoop=true);
void CPUUpdateCPSR();
void CPUUpdateFlags(bool breakLoop);
void CPUSoftwareInterrupt(int comment);
void CPUUndefinedException();
void CPUFiq();

// Same break-loop behaviour as arm_Run(): both run from the SH4 timeslice, and arm7.cpp defaults breakLoop to true.
#define CPUUpdateFlags() CPUUpdateFlags(true)

// ── Cached uop: a struct, not the union of the original spec, which cannot hold the label and operands at once
struct ARM7CACHEOP {
	void* dispatch;   // computed-goto target (&&label)
	u8 r0;            // primary: dest (Rd)
	u8 r1;            // primary: base / source1 (Rn)
	u8 r2;            // primary: source2 register (Rm) / Rs
	u8 cond;          // ARM condition field (opcode>>28)
	u32 imm;          // immediate operand / shift amount / raw opcode (LDM/STM)
};

#define ARM7_CACHE_SIZE   (192*1024)
// Indexed by word (pc >> 2): opcodes are word-aligned, so a byte index wasted 3 of every 4 slots.
#define ARM7_EP_SIZE      (ARAM_SIZE / 4)
#define ARM7_EP_MASK      (ARM7_EP_SIZE - 1)
#define ARM7_EP_INDEX(pc) (((pc) >> 2) & ARM7_EP_MASK)

static ARM7CACHEOP  ARM7_CACHE[ARM7_CACHE_SIZE];
static ARM7CACHEOP* ARM7_ENTRYPOINTS[ARM7_EP_SIZE];
static u32          ARM7_CACHE_LAST_OP;

// Set on reset(); the next non-reset entry (which has label addresses in scope)
// initializes the dispatch sentinel and clears every entrypoint to it.
static bool         s_cache_dirty = true;
static bool         s_idle_safe;   // ARM7 Idle Skip SAFE (6.1), latched with the table

extern "C" int get_arm7_idle_skip_preset();

// The ARM7 JIT borrows the block table rather than adding its own. arm_Reset() latches one engine and flushes both
// (s_cache_dirty makes this one rebuild its tables before its next run).
void arm7_cached_borrow_buffers(u8** code, u32* code_bytes, void*** entries, u32* entry_count)
{
	*code        = (u8*)ARM7_CACHE;
	*code_bytes  = (u32)sizeof(ARM7_CACHE);
	*entries     = (void**)ARM7_ENTRYPOINTS;
	*entry_count = ARM7_EP_SIZE;
}

// Condition handling is now per-uop (lbl_cond_<cc> guard uops in
// arm_Run_Cached); the old cond_check() helper is no longer needed.

// ── LDM/STM, decoded at run time from the raw opcode with arm-new.h's IA/IB/DA/DB rules: ascending registers at
// ascending addresses; a base in the list is written back in transfer order, as arm-new.h (self-tested).

static inline u32 arm_blockBase(u32 opcode, u32* outFirst, u32* outWb)
{
	int base = (opcode >> 16) & 0xF;
	u32 rn   = reg[base].I & 0xFFFFFFFC;
	u32 n    = cpuBitsSet[opcode & 0xFF] + cpuBitsSet[(opcode >> 8) & 0xFF];
	bool P   = (opcode >> 24) & 1;
	bool U   = (opcode >> 23) & 1;
	u32 first, wb;
	if (U) {                 // increment
		first = P ? rn + 4 : rn;
		wb    = rn + 4 * n;
	} else {                 // decrement
		first = P ? rn - 4 * n : rn - 4 * n + 4;
		wb    = rn - 4 * n;
	}
	*outFirst = first & 0xFFFFFFFC;
	*outWb    = wb;
	return base;
}

// Not static: the ARM7 JIT calls these for LDM/STM.
void arm_cached_stm(u32 opcode, int& clockTicks)
{
	u32 first, wb;
	int base = arm_blockBase(opcode, &first, &wb);
	bool W = (opcode >> 21) & 1;
	bool S = (opcode >> 22) & 1;
	u32 address = first;
	clockTicks += 2;

	// S-bit STM stores the user-bank registers; outside user/system mode their copies sit at the R*_USR / fiq indices.
	bool userBank = S && (armMode != 0x10 && armMode != 0x1f);

	bool wroteBase = false;
	for (int i = 0; i <= 15; i++) {
		if (!(opcode & (1 << i)))
			continue;
		u32 v;
		if (i == 15) {
			v = reg[15].I + 4;              // matches arm-new.h (PC+12)
		} else if (i == base && W && !wroteBase) {
			// base is the lowest set reg with writeback: arm-new.h stores the
			// ORIGINAL base then writes back; canonical ARM also stores original.
			v = reg[base].I;
		} else if (userBank) {
			v = (i >= 8 && i <= 12 && armMode == 0x11)
			      ? reg[(R8_FIQ - 8) + i].I
			      : (i == 13 ? reg[R13_USR].I : (i == 14 ? reg[R14_USR].I : reg[i].I));
		} else {
			v = reg[i].I;
		}
		CPUWriteMemory(address, v);
		clockTicks += 1 + CPUUpdateTicksAccess32(address);
		address += 4;
		if (W && !wroteBase) {            // writeback at first transfer
			reg[base].I = wb;
			wroteBase = true;
		}
	}
}

void arm_cached_ldm(u32 opcode, int& clockTicks, bool hasPC)
{
	u32 first, wb;
	int base = arm_blockBase(opcode, &first, &wb);
	bool W = (opcode >> 21) & 1;
	bool S = (opcode >> 22) & 1;
	u32 address = first;
	clockTicks += 2;

	// S without PC -> load into USER bank; S with PC -> restore SPSR after.
	bool userBank = S && !hasPC && (armMode != 0x10 && armMode != 0x1f);

	for (int i = 0; i <= 14; i++) {
		if (!(opcode & (1 << i)))
			continue;
		u32 v = CPUReadMemory(address);
		clockTicks += 1 + CPUUpdateTicksAccess32(address);
		address += 4;
		if (userBank) {
			if (i >= 8 && i <= 12 && armMode == 0x11) reg[(R8_FIQ - 8) + i].I = v;
			else if (i == 13) reg[R13_USR].I = v;
			else if (i == 14) reg[R14_USR].I = v;
			else reg[i].I = v;
		} else {
			reg[i].I = v;
		}
	}

	if (hasPC) {
		reg[15].I = CPUReadMemory(address);
		clockTicks += 2 + CPUUpdateTicksAccess32(address);
		address += 4;

		// Writeback before the S-bit CPUSwitchMode, as arm-new.h: after the switch it lands in the new mode's banked
		// register (seen: sp clobbered with a literal-pool address after LDMFD sp!,{pc}^).
		if (W && !(opcode & (1 << base)))
			reg[base].I = wb;

		if (S)
			CPUSwitchMode(reg[17].I & 0x1f, false);
		armNextPC = reg[15].I & 0xFFFFFFFC;
		reg[15].I = armNextPC + 4;
		return;            // base writeback already handled above
	}

	// Non-PC LDM: writeback after the transfers (no mode switch involved).
	if (W && !(opcode & (1 << base)))
		reg[base].I = wb;
}

// ── Operation macros (verbatim semantics from arm-new.h, C_CORE path) ────────
#define NEG(i) ((i) >> 31)
#define POS(i) ((~(i)) >> 31)
#define ADDCARRY(a, b, c) \
  C_FLAG = ((NEG(a) & NEG(b)) | (NEG(a) & POS(c)) | (NEG(b) & POS(c))) ? true : false;
#define ADDOVERFLOW(a, b, c) \
  V_FLAG = ((NEG(a) & NEG(b) & POS(c)) | (POS(a) & POS(b) & NEG(c))) ? true : false;
#define SUBCARRY(a, b, c) \
  C_FLAG = ((NEG(a) & POS(b)) | (NEG(a) & POS(c)) | (POS(b) & POS(c))) ? true : false;
#define SUBOVERFLOW(a, b, c) \
  V_FLAG = ((NEG(a) & POS(b) & POS(c)) | (POS(a) & NEG(b) & NEG(c))) ? true : false;

// dest = rd index, base = rn index, value = operand2, C_OUT = shifter carry.
#define DO_AND   reg[dest].I = reg[base].I & value;
#define DO_ANDS  reg[dest].I = reg[base].I & value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_EOR   reg[dest].I = reg[base].I ^ value;
#define DO_EORS  reg[dest].I = reg[base].I ^ value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_SUB   reg[dest].I = reg[base].I - value;
#define DO_SUBS  { u32 lhs=reg[base].I, rhs=value, res=lhs-rhs; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(lhs,rhs,res); SUBOVERFLOW(lhs,rhs,res); }
#define DO_RSB   reg[dest].I = value - reg[base].I;
#define DO_RSBS  { u32 lhs=reg[base].I, rhs=value, res=rhs-lhs; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(rhs,lhs,res); SUBOVERFLOW(rhs,lhs,res); }
#define DO_ADD   reg[dest].I = reg[base].I + value;
#define DO_ADDS  { u32 lhs=reg[base].I, rhs=value, res=lhs+rhs; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); ADDCARRY(lhs,rhs,res); ADDOVERFLOW(lhs,rhs,res); }
#define DO_ADC   reg[dest].I = reg[base].I + value + (u32)C_FLAG;
#define DO_ADCS  { u32 lhs=reg[base].I, rhs=value, res=lhs+rhs+(u32)C_FLAG; reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); ADDCARRY(lhs,rhs,res); ADDOVERFLOW(lhs,rhs,res); }
#define DO_SBC   reg[dest].I = reg[base].I - value - !((u32)C_FLAG);
#define DO_SBCS  { u32 lhs=reg[base].I, rhs=value, res=lhs-rhs-!((u32)C_FLAG); reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(lhs,rhs,res); SUBOVERFLOW(lhs,rhs,res); }
#define DO_RSC   reg[dest].I = value - reg[base].I - !((u32)C_FLAG);
#define DO_RSCS  { u32 lhs=reg[base].I, rhs=value, res=rhs-lhs-!((u32)C_FLAG); reg[dest].I=res; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(rhs,lhs,res); SUBOVERFLOW(rhs,lhs,res); }
#define DO_TST   { u32 res=reg[base].I & value; N_FLAG=(res&0x80000000)?true:false; Z_FLAG=(res)?false:true; C_FLAG=C_OUT; }
#define DO_TEQ   { u32 res=reg[base].I ^ value; N_FLAG=(res&0x80000000)?true:false; Z_FLAG=(res)?false:true; C_FLAG=C_OUT; }
#define DO_CMP   { u32 lhs=reg[base].I, rhs=value, res=lhs-rhs; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); SUBCARRY(lhs,rhs,res); SUBOVERFLOW(lhs,rhs,res); }
#define DO_CMN   { u32 lhs=reg[base].I, rhs=value, res=lhs+rhs; \
                   Z_FLAG=(res==0); N_FLAG=NEG(res); ADDCARRY(lhs,rhs,res); ADDOVERFLOW(lhs,rhs,res); }
#define DO_ORR   reg[dest].I = reg[base].I | value;
#define DO_ORRS  reg[dest].I = reg[base].I | value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_MOV   reg[dest].I = value;
#define DO_MOVS  reg[dest].I = value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_BIC   reg[dest].I = reg[base].I & (~value);
#define DO_BICS  reg[dest].I = reg[base].I & (~value); \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;
#define DO_MVN   reg[dest].I = ~value;
#define DO_MVNS  reg[dest].I = ~value; \
                 N_FLAG=(reg[dest].I&0x80000000)?true:false; Z_FLAG=(reg[dest].I)?false:true; C_FLAG=C_OUT;

// Forward declaration so arm7.cpp's switchable entry can call us.
void arm_Run(u32 CycleCount);

// Operand2: a shift uop (lbl_sh_/lbl_shc_) leaves the value in reg[R_SHIFT], the carry in reg[R_COUT]; the op uop reads
// reg[op->r2] (R_SHIFT or Rm) or op->imm. gen_arm7_cached.py generates these labels and decoders (arm7_cached_gen.inc).

#if !defined(__GNUC__)
#  error "arm7_cached.cpp requires GCC computed-goto (&&label / goto *)."
#endif

// ═══ The cached interpreter. PC model of arm_Run(): the instruction at A runs with reg[15] == A+8 (prefetch) and
// armNextPC == A+4; between slices armNextPC is the next instruction and reg[15] == armNextPC + 4.

void arm_Run_Cached(u32 CycleCount, bool reset)
{
	static int clockTicks;            // matches arm-new.h / LDM-STM helper signature
	static ARM7CACHEOP* pc_op;
	// Guard-uop label per condition (opcode[31:28]), filled below: label addresses exist only in this function.
	// AL (0xE) has no guard; NV (0xF) always skips.
	static void* cond_labels[16];

	if (reset) {
		// Point every entrypoint at the decode trampoline and reset the pool.
		// ARM7_CACHE[0] is the reserved "decode this block" sentinel.
		ARM7_CACHE[0].dispatch = NULL;          // patched below to &&lbl_unknown_block
		ARM7_CACHE_LAST_OP = 1;
		// We cannot take &&label outside the function body before first run, so
		// the actual fill happens on the first non-reset call (s_inited path).
		s_cache_dirty = true;
		return;
	}

	if (!Arm7Enabled)
		return;

	// One-time / post-reset initialization that needs label addresses.
	if (s_cache_dirty) {
		ARM7_CACHE[0].dispatch = &&lbl_unknown_block;
		for (u32 i = 0; i < ARM7_EP_SIZE; i++)
			ARM7_ENTRYPOINTS[i] = &ARM7_CACHE[0];
		ARM7_CACHE_LAST_OP = 1;
		cond_labels[0x0] = &&lbl_cond_eq; cond_labels[0x1] = &&lbl_cond_ne;
		cond_labels[0x2] = &&lbl_cond_cs; cond_labels[0x3] = &&lbl_cond_cc;
		cond_labels[0x4] = &&lbl_cond_mi; cond_labels[0x5] = &&lbl_cond_pl;
		cond_labels[0x6] = &&lbl_cond_vs; cond_labels[0x7] = &&lbl_cond_vc;
		cond_labels[0x8] = &&lbl_cond_hi; cond_labels[0x9] = &&lbl_cond_ls;
		cond_labels[0xA] = &&lbl_cond_ge; cond_labels[0xB] = &&lbl_cond_lt;
		cond_labels[0xC] = &&lbl_cond_gt; cond_labels[0xD] = &&lbl_cond_le;
		cond_labels[0xE] = &&lbl_cond_nv; /* AL: unused */
		cond_labels[0xF] = &&lbl_cond_nv;
		s_idle_safe = get_arm7_idle_skip_preset() != 0;
		s_cache_dirty = false;
	}

	clockTicks = 0;

	// Hot-block profiler: once per slice, dump the top 10 if >=1s has elapsed.
	PROF_TICK();

	// dispatch_pc drives dispatch: common uops only advance it, PC-read uops materialize reg[15] = dispatch_pc + 8,
	// PC writes set it. reg[15] / armNextPC are written back only at slice exit (lbl_exit).
	u32 dispatch_pc;

	// Re-home through the entrypoint table, sampling FIQ (CPUFiq takes lr from reg[15] = next instruction + 4). The
	// FIQ pin changes only between SH4 timeslices, so a per-block sample keeps arm-new.h's latency; pc_op++ pays nothing.
	#define DISPATCH() do {                              \
			if (armFiqEnable && e68k_out) {              \
				reg[15].I = dispatch_pc + 4;              \
				CPUFiq();                                 \
				dispatch_pc = armNextPC;                  \
			}                                             \
			if ((u32)clockTicks >= CycleCount) goto lbl_exit; \
			clockTicks += 6;                              \
			PROF_HIT(dispatch_pc);                        \
			pc_op = ARM7_ENTRYPOINTS[ARM7_EP_INDEX(dispatch_pc)]; \
			goto *pc_op->dispatch;                        \
		} while(0)

	// Sequential advance: the next uop is the next slot (a block is laid out in order), so no table lookup; a capped
	// block ends in a rehome uop, so pc_op++ never leaves its block.
	#define ADVANCE() do {                               \
			dispatch_pc += 4;                             \
			if ((u32)clockTicks >= CycleCount) goto lbl_exit; \
			clockTicks += 6;                              \
			pc_op++;                                      \
			goto *pc_op->dispatch;                        \
		} while(0)

	// Re-dispatch after a PC write: the op has already stored the new PC into
	// dispatch_pc. (Kept as a named macro for the PC-write / branch labels.)
	#define DISPATCH_PC() DISPATCH()

	// Materialize the ARM prefetch value for the few uops that read r15.
	#define SET_PC_READ() do { reg[15].I = dispatch_pc + 8; } while(0)

	// Condition guard: the entrypoint uop of a conditional instruction, its op uop next. Pass: pc_op++, same
	// instruction, no charge. Fail: skip it and re-home (+6 for the next one, as an advance). AL has no guard.
	#define COND_PASS()  do { pc_op++; goto *pc_op->dispatch; } while(0)
	#define COND_FAIL()  do { dispatch_pc += 4; DISPATCH(); } while(0)
	#define COND_UOP(cc, expr) \
		lbl_cond_##cc: if (expr) COND_PASS(); else COND_FAIL();

	// Kick off: start dispatching from the incoming PC (armNextPC), exactly the
	// instruction the previous slice / reset / FIQ left pending.
	dispatch_pc = armNextPC;
	DISPATCH();

	// The 14 ARM condition codes (EQ..LE) plus NV (never). cond_check's logic,
	// inlined per-label so the common pass path is a single predicted branch.
	COND_UOP(eq, Z_FLAG)
	COND_UOP(ne, !Z_FLAG)
	COND_UOP(cs, C_FLAG)
	COND_UOP(cc, !C_FLAG)
	COND_UOP(mi, N_FLAG)
	COND_UOP(pl, !N_FLAG)
	COND_UOP(vs, V_FLAG)
	COND_UOP(vc, !V_FLAG)
	COND_UOP(hi, C_FLAG && !Z_FLAG)
	COND_UOP(ls, !C_FLAG || Z_FLAG)
	COND_UOP(ge, N_FLAG == V_FLAG)
	COND_UOP(lt, N_FLAG != V_FLAG)
	COND_UOP(gt, !Z_FLAG && (N_FLAG == V_FLAG))
	COND_UOP(le, Z_FLAG || (N_FLAG != V_FLAG))
	COND_UOP(nv, false)        // NV: never executes -> always skip

	// ── Generated threaded label bodies (data-proc + LDR/STR variants) ──────
	#define GEN_LABELS
	#include "arm7_cached_gen.inc"
	#undef GEN_LABELS

	// ── Hand-written labels for the other ARM7DI instructions; pc_op->imm holds the raw opcode where one needs it.

lbl_mul:
	// MUL/MLA fields from decode: r0 = Rd (19:16), r1 = Rs (11:8), r2 = Rm (3:0); imm bit 8 = S, imm[3:0] = Rn (MLA).
	{
		ARM7CACHEOP* op = pc_op;
		int dest = op->r0;
		u32 rs = reg[op->r1].I;
		reg[dest].I = reg[op->r2].I * rs;
		if (op->imm & 0x100) {
			N_FLAG = (reg[dest].I & 0x80000000) ? true : false;
			Z_FLAG = (reg[dest].I) ? false : true;
		}
		if (((s32)rs) < 0) rs = ~rs;
		if ((rs & 0xFFFFFF00) == 0) clockTicks += 2;
		else if ((rs & 0xFFFF0000) == 0) clockTicks += 3;
		else if ((rs & 0xFF000000) == 0) clockTicks += 4;
		else clockTicks += 5;
	}
	ADVANCE();

lbl_mla:
	{
		ARM7CACHEOP* op = pc_op;
		int dest = op->r0;
		u32 rs = reg[op->r1].I;
		reg[dest].I = reg[op->r2].I * rs + reg[op->imm & 0xF].I;
		if (op->imm & 0x100) {
			N_FLAG = (reg[dest].I & 0x80000000) ? true : false;
			Z_FLAG = (reg[dest].I) ? false : true;
		}
		if (((s32)rs) < 0) rs = ~rs;
		if ((rs & 0xFFFFFF00) == 0) clockTicks += 3;
		else if ((rs & 0xFFFF0000) == 0) clockTicks += 4;
		else if ((rs & 0xFF000000) == 0) clockTicks += 5;
		else clockTicks += 6;
	}
	ADVANCE();

lbl_swp:
	{
		ARM7CACHEOP* op = pc_op;
		u32 address = reg[op->r1].I;          // Rn
		u32 temp = CPUReadMemory(address);
		CPUWriteMemory(address, reg[op->r2].I);   // Rm
		reg[op->r0].I = temp;                 // Rd
		clockTicks += 4;
	}
	ADVANCE();

lbl_swpb:
	{
		ARM7CACHEOP* op = pc_op;
		u32 address = reg[op->r1].I;
		u32 temp = CPUReadByte(address);
		CPUWriteByte(address, reg[op->r2].I & 0xFF);
		reg[op->r0].I = temp & 0xFF;
		clockTicks += 4;
	}
	ADVANCE();

lbl_mrs_cpsr:
	CPUUpdateCPSR();
	reg[pc_op->r0].I = reg[16].I;
	clockTicks += 1;
	ADVANCE();

lbl_mrs_spsr:
	reg[pc_op->r0].I = reg[17].I;
	clockTicks += 1;
	ADVANCE();

lbl_msr_cpsr:
	{
		// imm holds the raw opcode; works for both register and immediate forms.
		u32 opcode = pc_op->imm;
		CPUUpdateCPSR();
		u32 value;
		if (opcode & 0x02000000) {            // immediate form
			value = opcode & 0xFF;
			int shift = (opcode & 0xF00) >> 7;
			if (shift) value = ((value << (32 - shift)) | (value >> shift));
		} else {
			value = reg[opcode & 15].I;
		}
		u32 newValue = reg[16].I;
		if (armMode > 0x10) {
			if (opcode & 0x00010000) newValue = (newValue & 0xFFFFFF00) | (value & 0x000000FF);
			if (opcode & 0x00020000) newValue = (newValue & 0xFFFF00FF) | (value & 0x0000FF00);
			if (opcode & 0x00040000) newValue = (newValue & 0xFF00FFFF) | (value & 0x00FF0000);
		}
		if (opcode & 0x00080000) newValue = (newValue & 0x00FFFFFF) | (value & 0xFF000000);
		newValue |= 0x10;
		CPUSwitchMode(newValue & 0x1f, false);
		reg[16].I = newValue;
		CPUUpdateFlags();
		clockTicks += 1;
	}
	ADVANCE();

lbl_msr_spsr:
	{
		u32 opcode = pc_op->imm;
		if (armMode > 0x10 && armMode < 0x1f) {
			u32 value;
			if (opcode & 0x02000000) {
				value = opcode & 0xFF;
				int shift = (opcode & 0xF00) >> 7;
				if (shift) value = ((value << (32 - shift)) | (value >> shift));
			} else {
				value = reg[opcode & 15].I;
			}
			if (opcode & 0x00010000) reg[17].I = (reg[17].I & 0xFFFFFF00) | (value & 0x000000FF);
			if (opcode & 0x00020000) reg[17].I = (reg[17].I & 0xFFFF00FF) | (value & 0x0000FF00);
			if (opcode & 0x00040000) reg[17].I = (reg[17].I & 0xFF00FFFF) | (value & 0x00FF0000);
			if (opcode & 0x00080000) reg[17].I = (reg[17].I & 0x00FFFFFF) | (value & 0xFF000000);
		}
		clockTicks += 1;
	}
	ADVANCE();

	// Block data transfer: STM, LDM without pc and LDM with pc; the list and P,U,S,W are decoded at run time.
lbl_stm:
	// STM may store r15 (as PC+12 = reg[15]+4) and reads reg[base]; materialize
	// the prefetch PC so any r15 reference matches arm-new.h.
	SET_PC_READ();
	arm_cached_stm(pc_op->imm, clockTicks);
	ADVANCE();

lbl_ldm_nopc:
	SET_PC_READ();
	arm_cached_ldm(pc_op->imm, clockTicks, /*hasPC=*/false);
	ADVANCE();

lbl_ldm_pc:
	SET_PC_READ();
	arm_cached_ldm(pc_op->imm, clockTicks, /*hasPC=*/true);
	// arm_cached_ldm stored the loaded PC into armNextPC; follow it.
	dispatch_pc = armNextPC;
	DISPATCH_PC();

lbl_b:
	{
		// During execution reg[15] reads as A+8 = dispatch_pc+8; target =
		// (A+8) + offset. Update dispatch_pc directly; no reg[15] maintenance.
		s32 offset = pc_op->imm;     // pre-sign-extended, pre-shifted offset
		dispatch_pc = dispatch_pc + 8 + (u32)offset;
		clockTicks += 3;
	}
	DISPATCH_PC();

lbl_b_idle:
	// Idle-loop branch: +100 ticks, so an idle slice ends ~15x sooner and the SH4/AICA, the only thing that can end
	// the loop, moves on. DISPATCH_PC() still samples FIQ on the way out.
	clockTicks += 100;
	{
		s32 offset = pc_op->imm;
		dispatch_pc = dispatch_pc + 8 + (u32)offset;
		clockTicks += 3;
	}
	DISPATCH_PC();

lbl_bl:
	{
		// lr = address of next instruction = A+4 = dispatch_pc+4.
		s32 offset = pc_op->imm;
		reg[14].I = dispatch_pc + 4;
		dispatch_pc = dispatch_pc + 8 + (u32)offset;
		clockTicks += 3;
	}
	DISPATCH_PC();

lbl_swi:
	clockTicks += 3;
	// CPUSoftwareInterrupt reads reg[15] (-> lr_svc = PC-4) and sets armNextPC=0x08.
	SET_PC_READ();
	CPUSoftwareInterrupt(pc_op->imm & 0x00FFFFFF);
	dispatch_pc = armNextPC;
	DISPATCH_PC();

lbl_undefined:
#ifdef DEV_VERSION
	printf("Undefined ARM instruction (cached) %08x at %08x\n", pc_op->imm, dispatch_pc);
#endif
	// CPUUndefinedException sets armNextPC to the vector; pick it up via dispatch_pc.
	reg[15].I = dispatch_pc + 8;
	CPUUndefinedException();
	dispatch_pc = armNextPC;
	DISPATCH_PC();

	// Block-cap terminator: the last real uop's pc_op++ lands here, with dispatch_pc already on the next instruction.
lbl_rehome:
	DISPATCH();

	// ── Block decode: fill cache slots for the block starting at dispatch_pc ──
	// Reached when an entrypoint still points at the sentinel ARM7_CACHE[0].
lbl_unknown_block:
	{
		// The decode trampoline is no instruction: refund DISPATCH()'s +6, so the first real instruction pays one +6
		// and a CycleCount=1 slice still runs one instruction.
		clockTicks -= 6;
		// dispatch_pc is the address we must decode (DISPATCH() does not advance it).
		u32 blockStart = dispatch_pc;
		u32 decode_pc = blockStart;

		// (Each uop registers its own entrypoint inside the loop below, so no
		// separate blockStart assignment is needed here.)

		// Cap the block length; a capped block ends in a rehome uop, so the next chunk gets its own block.
		const u32 MAX_BLOCK_UOPS = 128;
		u32 blockUops = 0;

		// Idle loops (lbl_b_idle): blockSafe = no store, SWP, MSR or LDM/STM; Idle Skip SAFE also needs scan.ok (no
		// loop-carried register, no load writeback), so a counted loop pays its true cost.
		bool blockSafe = true;
		Arm7IdleScan scan;
		scan.reset();

		bool last = false;
		do {
			// Each instruction needs up to 4 slots (cond guard + shift uop + op +
			// cap terminator), so keep a margin of 5 before flushing the pool.
			if (ARM7_CACHE_LAST_OP >= ARM7_CACHE_SIZE - 5) {
				// Pool exhausted: flush and restart decoding this block.
				for (u32 i = 0; i < ARM7_EP_SIZE; i++)
					ARM7_ENTRYPOINTS[i] = &ARM7_CACHE[0];
				ARM7_CACHE_LAST_OP = 1;
				decode_pc = blockStart;
				blockUops = 0;          // restart count for the re-decoded block
				scan.reset();
				// (the loop body re-registers entrypoints as it re-decodes)
			}

			u32 opcode = CPUReadMemoryQuick(decode_pc);
			u32 cond = opcode >> 28;
			scan.insn(opcode);

			// The entrypoint is the instruction's first uop: the guard of a conditional one (op uop in the next slot),
			// so a branch into it still tests the condition; for AL, the op itself.
			if (cond != 0xE) {
				ARM7CACHEOP* guard = &ARM7_CACHE[ARM7_CACHE_LAST_OP];
				guard->dispatch = cond_labels[cond];
				ARM7_ENTRYPOINTS[ARM7_EP_INDEX(decode_pc)] = guard;
				ARM7_CACHE_LAST_OP++;
			}

			ARM7CACHEOP* cop = &ARM7_CACHE[ARM7_CACHE_LAST_OP];
			if (cond == 0xE) {
				// AL: the op uop IS the entrypoint.
				ARM7_ENTRYPOINTS[ARM7_EP_INDEX(decode_pc)] = cop;
			}
			cop->cond = cond;
			cop->imm = opcode;          // default: keep raw opcode
			last = false;

			// Decode here: &&label addresses exist only in this function. Classes as in arm-new.h.
			u32 fam = (opcode >> 26) & 3;   // bits[27:26]
			if (fam == 0 || fam == 1) {
				// 00: data-proc / multiply / swp / psr-transfer
				// 01: single data transfer (LDR/STR)
				if (fam == 1) {
					// LDR/STR. The generated decode selects the per-variant label.
					#define GEN_DECODE_MEM
					#include "arm7_cached_gen.inc"
					#undef GEN_DECODE_MEM
					// A store (L==0) makes the block ineligible for idle-skip; a
					// load is safe (read-only) and leaves blockSafe alone.
					if (!((opcode >> 20) & 1))
						blockSafe = false;
					// LDR to PC ends the block (it re-dispatches via dp_pc_done).
					if (((opcode >> 20) & 1) && (((opcode >> 12) & 0xF) == 15))
						last = true;
				} else {
					// fam == 0: data-proc register/immediate domain.
					bool isMulSpace = ((opcode & 0x0FC000F0) == 0x00000090); // 000000xx ...1001
					bool isSwp      = ((opcode & 0x0FB00FF0) == 0x01000090); // 00010B00 ...00001001
					// PSR transfer (MRS/MSR) occupies the TST/TEQ/CMP/CMN encodings
					// with the S bit clear: opcode[24:23]==0b10 && opcode[20]==0.
					bool isMrsMsr   = (((opcode >> 23) & 3) == 2) && (((opcode >> 20) & 1) == 0)
					                  && !isMulSpace && !isSwp;
					if (isMulSpace) {
						// MUL (A=0) / MLA (A=1). r0=Rd(19:16) r1=Rs(11:8) r2=Rm(3:0)
						cop->r0 = (opcode >> 16) & 0xF;
						cop->r1 = (opcode >> 8) & 0xF;
						cop->r2 = opcode & 0xF;
						u32 s = (opcode >> 20) & 1;
						u32 rn = (opcode >> 12) & 0xF;  // accumulate reg (MLA)
						cop->imm = (s << 8) | rn;
						cop->dispatch = (opcode & 0x00200000) ? &&lbl_mla : &&lbl_mul;
					} else if (isSwp) {
						cop->r0 = (opcode >> 12) & 0xF;   // Rd
						cop->r1 = (opcode >> 16) & 0xF;   // Rn
						cop->r2 = opcode & 0xF;           // Rm
						cop->dispatch = (opcode & 0x00400000) ? &&lbl_swpb : &&lbl_swp;
						blockSafe = false;            // SWP writes memory
					} else if (isMrsMsr) {
						// Distinguish MRS vs MSR and CPSR vs SPSR.
						bool toPSR = (opcode >> 21) & 1;  // 0 = MRS, 1 = MSR
						bool spsr  = (opcode >> 22) & 1;
						if (!toPSR) {
							cop->r0 = (opcode >> 12) & 0xF;
							cop->dispatch = spsr ? &&lbl_mrs_spsr : &&lbl_mrs_cpsr;
							// MRS is read-only -> safe.
						} else {
							cop->dispatch = spsr ? &&lbl_msr_spsr : &&lbl_msr_cpsr;
							blockSafe = false;       // MSR changes CPSR/mode
						}
					} else {
						// Data processing. Generated decode picks the (op,form) label.
						#define GEN_DECODE_DP
						#include "arm7_cached_gen.inc"
						#undef GEN_DECODE_DP
						// A data-proc write to PC ends the block; TST/TEQ/CMP/CMN (op4 0x8..0xB) never write Rd.
						{
							u32 op4 = (opcode >> 21) & 0xF;
							if (((opcode >> 12) & 0xF) == 15 && !(op4 >= 0x8 && op4 <= 0xB))
								last = true;
						}
					}
				}
			} else if (fam == 2) {
				// bits[27:26]==10: block data transfer (bit25==0) or branch (bit25==1)
				if (!((opcode >> 25) & 1)) {
					// 100: block data transfer (LDM/STM) -- not idle-skip safe.
					bool load = (opcode >> 20) & 1;
					bool hasPC = (opcode & 0x8000) != 0;
					if (!load)         cop->dispatch = &&lbl_stm;
					else if (hasPC)  { cop->dispatch = &&lbl_ldm_pc;   last = true; }
					else               cop->dispatch = &&lbl_ldm_nopc;
					blockSafe = false;
				} else {
					// 101: branch. Pre-extract the sign-extended, shifted offset.
					s32 offset = opcode & 0x00FFFFFF;
					if (offset & 0x00800000) offset |= 0xFF000000;
					offset <<= 2;
					cop->imm = (u32)offset;
					if (opcode & 0x01000000) {
						cop->dispatch = &&lbl_bl;   // BL writes lr -> not an idle wait
					} else {
						// A backward branch into an idle-safe block is a wait spin: only an FIQ or an SH4/AICA write,
						// both between slices, ends it. Its taken edge goes to lbl_b_idle; the guard takes the exit edge.
						u32 target = decode_pc + 8 + (u32)offset;
						bool selfLoop = blockSafe && (!s_idle_safe || scan.ok) &&
						                target >= blockStart && target <= decode_pc;
						cop->dispatch = selfLoop ? &&lbl_b_idle : &&lbl_b;
					}
					last = true;        // branch ends the block
				}
			} else {
				// bits[27:26]==11: coprocessor (bit25 group) or SWI (bits[27:24]==1111).
				if (((opcode >> 24) & 0xF) == 0xF) {
					cop->dispatch = &&lbl_swi;
					last = true;        // SWI ends the block (mode/PC change)
				} else {
					// coprocessor: not part of the AICA ARM7DI workload.
					cop->dispatch = &&lbl_undefined;
					last = true;
				}
			}

			ARM7_CACHE_LAST_OP++;
			decode_pc += 4;

			// A control-flow instruction ends the block and re-homes itself; the cap ends it on a sequential one,
			// whose pc_op++ needs a rehome uop in the next slot.
			if (!last && ++blockUops >= MAX_BLOCK_UOPS) {
				ARM7_CACHE[ARM7_CACHE_LAST_OP].dispatch = &&lbl_rehome;
				ARM7_CACHE_LAST_OP++;
				last = true;
			}
		} while (!last);

		// Run the new block: dispatch_pc is still blockStart, whose entrypoint is now its first uop.
	}
	DISPATCH();

lbl_exit:
	// The architectural PC as the next slice's kickoff and FIQ entry expect it: next instruction, reg[15] = it + 4.
	armNextPC = dispatch_pc;
	reg[15].I = dispatch_pc + 4;
	return;

	#undef DISPATCH
	#undef ADVANCE
	#undef DISPATCH_PC
	#undef SET_PC_READ
	#undef COND_PASS
	#undef COND_FAIL
	#undef COND_UOP
}
