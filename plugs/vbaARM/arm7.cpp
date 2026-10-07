#include "arm7.h"
#include "arm_mem.h"
#include "arm7_jit.h"
#include "ps3/ps3_arm_spe.h"

#define ARM7_FIQ_DIAG 0   // 1 = count real FIQs serviced per second (see CPUFiq)

// config.h poisons BIG_ENDIAN/LITTLE_ENDIAN to catch accidental use; system endian headers define them, so undef
// them first (as plugs/nullAICA/aica.cpp).
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif
#include "ps3_timebase.h"   // gettime(), ticks_to_millisecs()

#define C_CORE
#define DEV_VERSION

// ARM7_USE_CACHED routes arm_Run() through the cached interpreter (arm7_cached.cpp), not arm-new.h; same state.
// Rez's post-logo stall (2026-07-21) is the same with both engines on hardware, so the cached one stays on.
#define ARM7_USE_CACHED    // cached/threaded ARM7 interpreter active

#ifdef ARM7_USE_CACHED
void arm_Run_Cached(u32 CycleCount, bool reset);
#endif

//#define CPUReadHalfWordQuick(addr) arm_ReadMem16(addr & 0x7FFFFF)
#define CPUReadMemoryQuick(addr) arm_ReadMem32(addr & ARAM_MASK)
#define CPUReadByte arm_ReadMem8
#define CPUReadMemory arm_ReadMem32
#define CPUReadHalfWord arm_ReadMem16
#define CPUReadHalfWordSigned(addr) ((s16)arm_ReadMem16(addr))

#define CPUWriteMemory arm_WriteMem32
#define CPUWriteHalfWord arm_WriteMem16
#define CPUWriteByte arm_WriteMem8


#define reg arm_Reg
#define armNextPC arm_ArmNextPC


#define CPUUpdateTicksAccesint(a) 1
#define CPUUpdateTicksAccessSeq32(a) 1
#define CPUUpdateTicksAccesshort(a) 1
#define CPUUpdateTicksAccess32(a) 1
#define CPUUpdateTicksAccess16(a) 1


enum
{
	R13_IRQ=18,
	R14_IRQ=19,
	SPSR_IRQ =20,
	R13_USR=26,
	R14_USR=27,
	R13_SVC=28,
	R14_SVC=29,
	SPSR_SVC =30,
	R13_ABT=31,
	R14_ABT=32,
	SPSR_ABT =33,
	R13_UND=34,
	R14_UND=35,
	SPSR_UND =36,
	R8_FIQ= 37,
	R9_FIQ= 38,
	R10_FIQ=39,
	R11_FIQ=40,
	R12_FIQ=41,
	R13_FIQ=42,
	R14_FIQ=43,
	SPSR_FIQ =44,
};

// Plain u32: the old B0-B3 / W0-W1 sub-fields were wrong on big-endian hosts and corrupted STRB / STRH / SWPB and
// register shift amounts, so every access uses masks and shifts on .I.
typedef union {
	u32 I;
} reg_pair;

//bool arm_FiqPending; -- not used , i use the input directly :)
//bool arm_IrqPending;

u32 arm_ArmNextPC;

// 45 architectural/banked registers, plus [45] C_OUT and [46] SHIFT_OUT of the cached interpreter's shift uops.
reg_pair arm_Reg[47];

void CPUSwap(u32 *a, u32 *b)
{
	u32 c = *b;
	*b = *a;
	*a = c;
}

bool C_OUT;
bool N_FLAG;
bool Z_FLAG;
bool C_FLAG;
bool V_FLAG;
bool armIrqEnable;
bool armFiqEnable;
//bool armState;
int armMode;

bool Arm7Enabled=false;

extern "C" int get_arm7_reset_fix_preset();
bool arm_reset_latch();   // arm_mem.cpp

// [aicafix]: AICA Race Fix slice extensions (arm7_cached.cpp, arm7_jit.cpp); ARM7 Reset Fix resets, and how many found
// the FIQ latch or the pending level still set.
u32 g_arm_races, g_arm_resets, g_arm_stale;

BYTE cpuBitsSet[256];

u16 IE;
u16 IF;
u16 IME;

int cpuSavedTicks;
int* extCpuLoopTicks = NULL;
int* extClockTicks = NULL;
int* extTicks = NULL;

bool intState = false;
bool stopState = false;
bool holdState = false;

#define CPU_BREAK_LOOP \
	cpuSavedTicks = cpuSavedTicks - *extCpuLoopTicks;\
	*extCpuLoopTicks = *extClockTicks;

#define CPU_BREAK_LOOP_2 \
	cpuSavedTicks = cpuSavedTicks - *extCpuLoopTicks;\
	*extCpuLoopTicks = *extClockTicks;\
	*extTicks = *extClockTicks;

void CPUSwitchMode(int mode, bool saveState, bool breakLoop=true);
void CPUUpdateCPSR();
void CPUUpdateFlags(bool breakLoop = true);
void CPUSoftwareInterrupt(int comment);
void CPUUndefinedException();
void CPUInterrupt();
void CPUFiq();

//void CreateTables();
void arm_Init()
{
	//CreateTables();
	arm_Reset();
	
	int i;
	for(i = 0; i < 256; i++) 
	{
		int count = 0;
		int j;
		for(j = 0; j < 8; j++)
			if(i & (1 << j))
				count++;
		cpuBitsSet[i] = count;
	}

	// arm7_jit preset: run the conformance self-test on the JIT once, before
	// any game code (falls back to the cached interpreter on failure).
	arm_jit_after_init();
	g_arm_resets = g_arm_stale = 0;   // [aicafix] counts the game's resets, not the self-test's
}

void CPUSwitchMode(int mode, bool saveState, bool breakLoop)
{
	CPUUpdateCPSR();

	switch(armMode) {
	case 0x10:
	case 0x1F:
		reg[R13_USR].I = reg[13].I;
		reg[R14_USR].I = reg[14].I;
		reg[17].I = reg[16].I;
		break;
	case 0x11:
		CPUSwap(&reg[R8_FIQ].I, &reg[8].I);
		CPUSwap(&reg[R9_FIQ].I, &reg[9].I);
		CPUSwap(&reg[R10_FIQ].I, &reg[10].I);
		CPUSwap(&reg[R11_FIQ].I, &reg[11].I);
		CPUSwap(&reg[R12_FIQ].I, &reg[12].I);
		reg[R13_FIQ].I = reg[13].I;
		reg[R14_FIQ].I = reg[14].I;
		reg[SPSR_FIQ].I = reg[17].I;
		break;
	case 0x12:
		reg[R13_IRQ].I  = reg[13].I;
		reg[R14_IRQ].I  = reg[14].I;
		reg[SPSR_IRQ].I =  reg[17].I;
		break;
	case 0x13:
		reg[R13_SVC].I  = reg[13].I;
		reg[R14_SVC].I  = reg[14].I;
		reg[SPSR_SVC].I =  reg[17].I;
		break;
	case 0x17:
		reg[R13_ABT].I  = reg[13].I;
		reg[R14_ABT].I  = reg[14].I;
		reg[SPSR_ABT].I =  reg[17].I;
		break;
	case 0x1b:
		reg[R13_UND].I  = reg[13].I;
		reg[R14_UND].I  = reg[14].I;
		reg[SPSR_UND].I =  reg[17].I;
		break;
	}

	u32 CPSR = reg[16].I;
	u32 SPSR = reg[17].I;

	switch(mode) {
	case 0x10:
	case 0x1F:
		reg[13].I = reg[R13_USR].I;
		reg[14].I = reg[R14_USR].I;
		reg[16].I = SPSR;
		break;
	case 0x11:
		CPUSwap(&reg[8].I, &reg[R8_FIQ].I);
		CPUSwap(&reg[9].I, &reg[R9_FIQ].I);
		CPUSwap(&reg[10].I, &reg[R10_FIQ].I);
		CPUSwap(&reg[11].I, &reg[R11_FIQ].I);
		CPUSwap(&reg[12].I, &reg[R12_FIQ].I);
		reg[13].I = reg[R13_FIQ].I;
		reg[14].I = reg[R14_FIQ].I;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_FIQ].I;
		break;
	case 0x12:
		reg[13].I = reg[R13_IRQ].I;
		reg[14].I = reg[R14_IRQ].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_IRQ].I;
		break;
	case 0x13:
		reg[13].I = reg[R13_SVC].I;
		reg[14].I = reg[R14_SVC].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_SVC].I;
		break;
	case 0x17:
		reg[13].I = reg[R13_ABT].I;
		reg[14].I = reg[R14_ABT].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_ABT].I;
		break;    
	case 0x1b:
		reg[13].I = reg[R13_UND].I;
		reg[14].I = reg[R14_UND].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_UND].I;
		break;    
	default:
		printf("Unsupported ARM mode %02x\n", mode);
		//gli die("Arm error..");
		break;
	}
	armMode = mode;
	CPUUpdateFlags(breakLoop);
	CPUUpdateCPSR();
}

void CPUUpdateCPSR()
{
	u32 CPSR = reg[16].I & 0x40;
	if(N_FLAG)
		CPSR |= 0x80000000;
	if(Z_FLAG)
		CPSR |= 0x40000000;
	if(C_FLAG)
		CPSR |= 0x20000000;
	if(V_FLAG)
		CPSR |= 0x10000000;
	/*if(!armState)
		CPSR |= 0x00000020;*/
	if (!armFiqEnable)
		CPSR |= 0x40;
	if(!armIrqEnable)
		CPSR |= 0x80;
	CPSR |= (armMode & 0x1F);
	reg[16].I = CPSR;
}

void CPUUpdateFlags(bool breakLoop)
{
	u32 CPSR = reg[16].I;

	N_FLAG = (CPSR & 0x80000000) ? true: false;
	Z_FLAG = (CPSR & 0x40000000) ? true: false;
	C_FLAG = (CPSR & 0x20000000) ? true: false;
	V_FLAG = (CPSR & 0x10000000) ? true: false;
	//armState = (CPSR & 0x20) ? false : true;
	armIrqEnable = (CPSR & 0x80) ? false : true;
	armFiqEnable = (CPSR & 0x40) ? false : true;
	if(breakLoop) {
		if(armIrqEnable && (IF & IE) && (IME & 1)) {
			CPU_BREAK_LOOP_2;
		}
	}
}

void CPUSoftwareInterrupt(int comment)
{
	u32 PC = reg[15].I;
	//bool savedArmState = armState;
	CPUSwitchMode(0x13, true, false);
	reg[14].I = PC - 4;
	reg[15].I = 0x08;
	
	armIrqEnable = false;
	armNextPC = 0x08;
	reg[15].I += 4;
}

void CPUUndefinedException()
{
	u32 PC = reg[15].I;
	CPUSwitchMode(0x1b, true, false);
	reg[14].I = PC - 4;
	reg[15].I = 0x04;
	armIrqEnable = false;
	armNextPC = 0x04;
	reg[15].I += 4;  
}

void arm_cached_avoid_race();   // arm7_cached.cpp
void arm_jit_avoid_race();      // arm7_jit.cpp

// AICA Race Fix: each engine acts only while its own slice runs, so an SH4 write changes nothing.
void arm_avoid_race()
{
	arm_cached_avoid_race();
	arm_jit_avoid_race();
}

void arm_Reset()
{
	Arm7Enabled = false;
	// clean registers
	memset(&arm_Reg[0], 0, sizeof(arm_Reg));

	IE       = 0x0000;
	IF       = 0x0000;
	IME      = 0x0000;

	// ARM7 Reset Fix (P44): the ARM7DI is ARMv3 and has no System mode, so a reset enters SVC (Flycast, Reicast).
	const bool reset_fix = get_arm7_reset_fix_preset() != 0;
	armMode = reset_fix ? 0x13 : 0x1F;
	if (reset_fix)
	{
		g_arm_resets++;
		g_arm_stale += arm_reset_latch();
	}

	reg[13].I = 0x03007F00;
	reg[15].I = 0x0000000;
	reg[16].I = 0x00000000;
	reg[R13_IRQ].I = 0x03007FA0;
	reg[R13_SVC].I = 0x03007FE0;
	armIrqEnable = true;      
	armFiqEnable = false;

	//armState = true;
	C_FLAG = V_FLAG = N_FLAG = Z_FLAG = false;

	// disable FIQ
	reg[16].I |= 0x40;

	CPUUpdateCPSR();

	armNextPC = reg[15].I;
	reg[15].I += 4;

	//arm_FiqPending = false;

#ifdef ARM7_USE_CACHED
	// Flush the predecode cache: the only invalidation point (per design).
	arm_Run_Cached(0, true);
#endif
	// Latch cached vs JIT for this run (arm7_jit preset) and flush the JIT.
	// Must come after the cached flush: both engines share its buffers.
	arm_jit_on_reset();
	if (g_arm_spe)
		arm_spe_reset();   // 5.15: the ARM7 that runs the game is the SPU's
}

// Both engines read the idle-skip rule while they compile: drop their code so a new rule acts at once.
void arm_FlushCode()
{
#ifdef ARM7_USE_CACHED
	arm_Run_Cached(0, true);
#endif
	arm_jit_on_reset();
	if (g_arm_spe)
		arm_spe_flush();
}

void CPUInterrupt()
{
	u32 PC = reg[15].I;
	//bool savedState = armState;
	CPUSwitchMode(0x12, true, false);
	reg[14].I = PC;
	//if(!savedState)
	//	reg[14].I += 2;
	reg[15].I = 0x18;
	//armState = true;
	armIrqEnable = false;

	armNextPC = reg[15].I;
	reg[15].I += 4;
}

u32 g_arm7s_fiqs;   // [arm7s] (plugs/nullAICA/aica.cpp)

void CPUFiq()
{
	g_arm7s_fiqs++;
	u32 PC = reg[15].I;
	//bool savedState = armState;
	CPUSwitchMode(0x11, true, false);
	reg[14].I = PC;
	//if(!savedState)
	//	reg[14].I += 2;
	reg[15].I = 0x1c;
	//armState = true;
	armIrqEnable = false;
	armFiqEnable = false;

	armNextPC = reg[15].I;
	reg[15].I += 4;

#if ARM7_FIQ_DIAG
	// Off: a gettime() and a 64-bit division per FIQ. FIQs serviced per wall second; far below the AICA interrupt rate
	// means a starved FIQ path (late re-enable, missed samples) and a slowed note sequence.
	static u32 fiq_count = 0;
	static u64 last_check_ms = 0;
	fiq_count++;
	u64 now_ms = ticks_to_millisecs(gettime());
	if (now_ms - last_check_ms >= 1000)
	{
		//printf("[ARM7] real-time FIQ service rate: %u/%llums\n",
		//       fiq_count, (unsigned long long)(now_ms - last_check_ms));
		(void)fiq_count;
		fiq_count = 0;
		last_check_ms = now_ms;
	}
#endif
}


// The AICA has 3 interrupt controllers: the SCSP one, a 68k intc emulation and the ARM7's. The sci* bits feed the
// e68k, whose output drives the ARM7 FIQ pin.

#ifdef _MUDFLAP
__attribute__ ((optimize("O0")))
#endif
// int totalops; // SKMP stuff
void arm_Run(u32 CycleCount)
{
  if (!Arm7Enabled)
	 return;

	if (arm_jit_active())
	{
		arm_jit_run(CycleCount);
		return;
	}

#ifdef ARM7_USE_CACHED
	arm_Run_Cached(CycleCount, false);
#else
	u32 clockTicks=0;
	while (clockTicks<CycleCount)
	{
		if (armFiqEnable && e68k_out)
		{
			CPUFiq();
		}
		
		#include "arm-new.h"
	}
#endif
}

void arm_SetEnabled(bool enabled)
{
	if(!Arm7Enabled && enabled)
			arm_Reset();

	Arm7Enabled=enabled;
}

// ── Self-test accessors: arm7_selftest.cpp drives the core without the internal reg_pair layout ──
u32  arm_GetReg(int n)            { return arm_Reg[n].I; }
void arm_SetReg(int n, u32 value) { arm_Reg[n].I = value; }
u32  arm_GetNextPC()             { return arm_ArmNextPC; }
void arm_SetNextPC(u32 value)    { arm_ArmNextPC = value; }