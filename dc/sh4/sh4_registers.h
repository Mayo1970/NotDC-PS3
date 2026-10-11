#pragma once
#include <stddef.h>
#include "types.h"
#include "sh4_if.h"

struct Sh4Context
{
	f32 xf[16];
	f32 fr[16];

	// Recompiler bounce slot for int<->float conversions, not an SH4 register. Next to the FP files (offset 128)
	// so each bounce hits a resident L1 line; only its 8-byte alignment matters.
	u64 jit_scratch;

	u32 r[16];

	u32 r_bank[8];

	u32 gbr,ssr,spc,sgr,dbr,vbr;
	u32 mach,macl,pr,fpul;
	u32 pc;
	//u16* pc_ptr;

	sr_type sr;
	fpscr_type fpscr;
	sr_type old_sr;
	fpscr_type old_fpscr;

	// Backing store for reg_temp, the decoder's scratch (rotcl/rotcr's T, tas.b's byte, SR pairs). The fields
	// from here on sit at the tail so no older context offset moves.
	u32 jit_temp;

	// reg_temp2: mac.l/mac.w need both loaded operands live at once.
	u32 jit_temp2;

	// Plan 5.25 JIT IDLE SKIP: nonzero once a block that may store runs; the idle check reads and clears it.
	u32 jit_idle_work;

	// Plan 5.36 SYS GATE: slice ends the update has not run yet, and the count at which it must run (0 = next end).
	s32 sys_elapsed;
	s32 sys_due;

	// P11 TMU Fine TCNT: the JIT's cycles left in the slice, stored before a C memory call and at each slice end.
	s32 jit_cycles;

	u32 offset(u32 sh4_reg);
	u32 offset(Sh4RegType sh4_reg) { return offset((u32)sh4_reg); }
};

// JIT context offsets go here, above the macros below: they rewrite bare field names, so offsetof(Sh4Context,fr)
// would not compile after them. SH4CTX_OFS_FR: for the FASTMEM code (ps3_driver.cpp), which cannot call offset().
enum { SH4CTX_OFS_FR = offsetof(Sh4Context, fr) };

// JIT_FSCHG: fschg only toggles FPSCR.SZ, so the backend keeps old_fpscr in step with one stw, not a C call.
enum { SH4CTX_OFS_OLD_FPSCR = offsetof(Sh4Context, old_fpscr) };

extern ALIGN(64) Sh4Context Sh4cntx;
#define r Sh4cntx.r
#define r_bank Sh4cntx.r_bank
#define gbr Sh4cntx.gbr
#define ssr Sh4cntx.ssr
#define spc Sh4cntx.spc
#define sgr Sh4cntx.sgr
#define dbr Sh4cntx.dbr
#define vbr Sh4cntx.vbr
#define mach Sh4cntx.mach
#define macl Sh4cntx.macl
#define pr Sh4cntx.pr
#define fpul Sh4cntx.fpul
#define next_pc Sh4cntx.pc
#define curr_pc (next_pc-2)
#define sr Sh4cntx.sr
#define fpscr Sh4cntx.fpscr
#define old_sr Sh4cntx.old_sr
#define old_fpscr Sh4cntx.old_fpscr
#define fr Sh4cntx.fr
#define xf Sh4cntx.xf
#define fr_hex ((u32*)(fr))
#define xf_hex ((u32*)(xf))
#define dr_hex ((u64*)(fr))
#define xd_hex ((u64*)(xf))



void UpdateFPSCR();
bool UpdateSR();

union DoubleReg
{
	f64 dbl;
	f32 sgl[2];
};

INLINE f64 GetDR(u32 n)
{
#ifdef TRACE
	if (n>7)
		printf("DR_r INDEX OVERRUN %d >7\n",n);
#endif
	DoubleReg t;

	#if HOST_ENDIAN==ENDIAN_BIG
		t.sgl[0]=fr[(n<<1) + 0];
		t.sgl[1]=fr[(n<<1) + 1];
	#else
		t.sgl[1]=fr[(n<<1) + 0];
		t.sgl[0]=fr[(n<<1) + 1];
	#endif

	return t.dbl;
}

INLINE f64 GetXD(u32 n)
{
#ifdef TRACE
	if (n>7)
		printf("XD_r INDEX OVERRUN %d >7\n",n);
#endif
	DoubleReg t;
	#if HOST_ENDIAN==ENDIAN_BIG
		t.sgl[0]=xf[(n<<1) + 0];
		t.sgl[1]=xf[(n<<1) + 1];
	#else
		t.sgl[1]=xf[(n<<1) + 0];
		t.sgl[0]=xf[(n<<1) + 1];
	#endif

	return t.dbl;
}

INLINE void SetDR(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		printf("DR_w INDEX OVERRUN %d >7\n",n);
#endif
	DoubleReg t;
	t.dbl=val;

	#if HOST_ENDIAN==ENDIAN_BIG
		fr[(n<<1) | 0]=t.sgl[0];
		fr[(n<<1) | 1]=t.sgl[1];
	#else
		fr[(n<<1) | 1]=t.sgl[0];
		fr[(n<<1) | 0]=t.sgl[1];
	#endif
}

INLINE void SetXD(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		printf("XD_w INDEX OVERRUN %d >7\n",n);
#endif

	DoubleReg t;
	t.dbl=val;
	#if HOST_ENDIAN==ENDIAN_BIG
		xf[(n<<1) | 0]=t.sgl[0];
		xf[(n<<1) | 1]=t.sgl[1];
	#else
		xf[(n<<1) | 1]=t.sgl[0];
		xf[(n<<1) | 0]=t.sgl[1];
	#endif
}
//needs to be removed
u32* Sh4_int_GetRegisterPtr(Sh4RegType reg);
//needs to be made portable
void SetFloatStatusReg();
extern f32 sin_table[0x10000+0x4000];