#pragma once
#include "types.h"
#include "sh4_if.h"
#include "intc.h"

#undef sh4op
#define sh4op(str) void FASTCALL str(u32 op)
typedef void (FASTCALL OpCallFP)(u32 op);

// ---- Runtime accuracy preset (defined in ps3_presets.cpp) ----
// 0 = Fast (largest timeslice, loosest timing), 1 = Balanced (moderate, compatible), 2 = Accurate (original, tightest).
extern "C" int get_accuracy_preset();

#define FAST()     (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

// ---- Per-preset SH4 execution constants ----

// TIMESLICE: SH4 cycles per UpdateSystem(), drained by CPU_RATIO (opcode cost); MEDIUM (AICA/DMA), SLOW (GDRom, 2x),
// VSLOW (Maple/RTC, 4x) count UpdateSystem() calls. Fast's longer slice polls peripherals less: cheaper, less accurate.
static const s32 SH4_TIMESLICE_FAST      = 1792;
static const s32 SH4_CPU_RATIO_FAST      = 8;
static const u32 SH4_MEDIUM_FAST         = 4;    // every  7 168 cycles
static const u32 SH4_SLOW_FAST           = 8;    // every 14 336 cycles
static const u32 SH4_VSLOW_FAST          = 16;   // every 28 672 cycles

static const s32 SH4_TIMESLICE_BALANCED  = 896;
static const s32 SH4_CPU_RATIO_BALANCED  = 8;
static const u32 SH4_MEDIUM_BALANCED     = 8;    // every  7 168 cycles
static const u32 SH4_SLOW_BALANCED       = 16;   // every 14 336 cycles
static const u32 SH4_VSLOW_BALANCED      = 32;   // every 28 672 cycles

static const s32 SH4_TIMESLICE_ACCURATE  = 448;
static const s32 SH4_CPU_RATIO_ACCURATE  = 8;
static const u32 SH4_MEDIUM_ACCURATE     = 8;    // every  3 584 cycles // very accurate 1
static const u32 SH4_SLOW_ACCURATE       = 16;   // every  7 168 cycles // very accurate 2
static const u32 SH4_VSLOW_ACCURATE      = 32;   // every 14 336 cycles // very accurate 4

// ---- Opcode classification flags ----
enum OpcodeType
{
	Normal       = 0,    // No special handling required
	ReadsPC      = 1,    // PC must be valid/set before calling
	WritesPC     = 2,    // Opcode writes PC (branch)
	Delayslot    = 4,    // Has a delay-slot opcode (only valid with WritesPC)
	WritesSR     = 8,    // Writes SR  -> UpdateSR() must be called after
	WritesFPSCR  = 16,   // Writes FPSCR -> UpdateFPSCR() must be called after
	Invalid      = 128,  // Undefined/illegal opcode

	ReadWritePC       = ReadsPC | WritesPC,
	WritesSRRWPC      = WritesSR | ReadsPC | WritesPC,

	Branch_dir        = ReadWritePC,
	Branch_rel        = ReadWritePC,
	Branch_dir_d      = Delayslot | Branch_dir,
	Branch_rel_d      = Delayslot | Branch_rel,
};

// ---- Interpreter interface ----
void Sh4_int_Run();
void Sh4_int_Stop();
void Sh4_int_Step();
void Sh4_int_Skip();
void Sh4_int_Reset(bool Manual);
void Sh4_int_Init();
void Sh4_int_Term();
bool Sh4_int_IsCpuRunning();

// Exception helpers (legacy misspelling kept as inline alias for ABI compat)
void FASTCALL sh4_int_RaiseException(u32 ExceptionCode, u32 VectorAddr);
static inline void FASTCALL sh4_int_RaiseExeption(u32 code, u32 vec)
{
	sh4_int_RaiseException(code, vec);
}

u32  Sh4_int_GetRegister(Sh4RegType reg);
void Sh4_int_SetRegister(Sh4RegType reg, u32 regdata);

void ExecuteDelayslot();
void ExecuteDelayslot_RTE();

int FASTCALL UpdateSystem();
// Recompiler split form: _no_event() runs the GPR-free peripheral cascade + pending check (nonzero: an interrupt must
// be dispatched); _handle_event() runs the GPR-touching dispatch. Equivalence invariant: see sh4_interpreter.cpp.
int FASTCALL UpdateSystem_no_event();
int FASTCALL UpdateSystem_handle_event();

// For the recompiler: Sh4_ApplyAccuracyPreset() latches the preset's timing (as Sh4_int_Run does on entry), and
// sh4_GetTimeslice() returns it so the emitted JIT mainloop has a matching cycle budget, not a hardcoded SH4_TIMESLICE.
void Sh4_ApplyAccuracyPreset();
s32  sh4_GetTimeslice();
s32  sh4_GetCpuRatio();

// ---- Cached (pre-decoded / threaded) interpreter, dc/sh4/sh4_cached.cpp ----
// Sh4_int_Run() semantics without the per-op fetch, OpPtr lookup and handler call; selected by the sh4_cached preset.
extern volatile bool sh4_int_bCpuRun;

void Sh4_int_Run_Cached();
void sh4_cached_flush();                                   // drop all blocks
void sh4_cached_invalidate_range(u32 addr, u32 size);      // drop [addr, addr+size)

// Coarse timer (~13,216 kHz) used for block lifecycle tracking
extern u32 gcp_timer;
