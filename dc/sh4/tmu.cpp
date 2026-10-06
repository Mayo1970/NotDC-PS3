// TMU: the SH4's three countdown timers (OS ticks, audio sync, game timing). Each channel counts TCNT down at
// Pclk/4..1024 and reloads TCOR on underflow. We count Icyc (4 x Pclk), so each prescaler shift is 2 more.

#include "types.h"
#include "tmu.h"
#include "intc.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/sh4/sys_gate.h"

// TCR register bit definitions
#define TMU_TCR_UNF		0x0100	// Underflow flag
#define TMU_TCR_UNIE	0x0020	// Underflow interrupt enable
#define TMU_TCR_TPSC	0x0007	// Timer prescaler select mask

// TSTR channel enable bits
static const u32 tmu_ch_bit[3] = { 1, 2, 4 };

// Interrupt IDs for each channel
static const InterruptID tmu_intID[3] = {
	sh4_TMU0_TUNI0,
	sh4_TMU1_TUNI1,
	sh4_TMU2_TUNI2
};

// Per-channel state
static u32  tmu_prescaler[3];		// Accumulated sub-step cycles
static u32  tmu_prescaler_shift[3];	// Bit-shift for current prescale factor
static u32  tmu_prescaler_mask[3];	// Mask to retain sub-step remainder
static u32  tmu_old_mode[3] = { 0xFFFF, 0xFFFF, 0xFFFF }; // Cached TCR prescaler bits

// Memory-mapped register backing store
u32  tmu_regs_CNT[3];	// TCNT - current countdown value
u32  tmu_regs_COR[3];	// TCOR - reload value on underflow
u16  tmu_regs_CR[3];	// TCR  - control/status register
u8   TMU_TOCR;			// Timer output control register
u8   TMU_TSTR;			// Timer start register (channel enable bits)

// One channel step of clc CPU cycles on the given state; true on an underflow. The SYS GATE's planner and its TCNT
// reads run the same step on copies.
static INLINE bool tmu_step(u32& pres, u32& cnt, u32 shift, u32 mask, u32 cor, u32 clc)
{
	pres += clc;
	u32 steps = pres >> shift;

	// Remove consumed sub-steps from accumulator
	pres &= mask;

	if (steps == 0)
		return false;

	bool unf = false;
	if (steps > cnt)
	{
		// At least one underflow occurred
		steps -= cnt;
		unf = true;

		// More full periods: only when the update runs rarely against a very fast timer.
		if (cor > 0 && steps > cor)
			steps %= cor;

		// Reload counter from TCOR
		cnt = cor;
	}

	cnt -= steps;
	return unf;
}

// Advances channel ch by clc CPU cycles; also takes several underflows in one call.
template<u32 ch>
INLINE void UpdateTMU_chan(u32 clc)
{
	// Skip if channel is not running
	if ((TMU_TSTR & tmu_ch_bit[ch]) == 0)
		return;

	if (tmu_step(tmu_prescaler[ch], tmu_regs_CNT[ch], tmu_prescaler_shift[ch], tmu_prescaler_mask[ch],
	             tmu_regs_COR[ch], clc))
	{
		// Signal underflow interrupt
		tmu_regs_CR[ch] |= TMU_TCR_UNF;
		InterruptPend(tmu_intID[ch], 1);
	}
}

void UpdateTMU(u32 Cycles)
{
	UpdateTMU_chan<0>(Cycles);
	UpdateTMU_chan<1>(Cycles);
	UpdateTMU_chan<2>(Cycles);
}

// Plan 5.36: slice ends up to the first underflow of a running channel, at most max.
s32 tmu_slices_to_event(s32 ts, s32 max)
{
	for (u32 ch = 0; ch < 3; ch++)
	{
		if ((TMU_TSTR & tmu_ch_bit[ch]) == 0)
			continue;
		u32 pres = tmu_prescaler[ch], cnt = tmu_regs_CNT[ch];
		for (s32 n = 1; n < max; n++)
			if (tmu_step(pres, cnt, tmu_prescaler_shift[ch], tmu_prescaler_mask[ch], tmu_regs_COR[ch], (u32)ts))
			{
				max = n;
				break;
			}
	}
	return max;
}

// TCNT after the slices the SYS GATE skipped. None of them underflows, so TCNT is the only thing they move.
static u32 tmu_tcnt_now(u32 ch)
{
	u32 cnt = tmu_regs_CNT[ch];
	s32 k = sys_pending();
	if (k <= 0 || (TMU_TSTR & tmu_ch_bit[ch]) == 0)
		return cnt;
	u32 pres = tmu_prescaler[ch];
	const u32 ts = (u32)sh4_GetTimeslice();
	while (k-- > 0)
		tmu_step(pres, cnt, tmu_prescaler_shift[ch], tmu_prescaler_mask[ch], tmu_regs_COR[ch], ts);
	return cnt;
}

// Called whenever TCR is written: syncs the interrupt state and the prescaler when the mode changes.
void UpdateTMUCounts(u32 reg)
{
	// Sync interrupt pending/mask state to current TCR flags
	InterruptPend(tmu_intID[reg],  tmu_regs_CR[reg] & TMU_TCR_UNF);
	InterruptMask(tmu_intID[reg],  tmu_regs_CR[reg] & TMU_TCR_UNIE);

	const u32 mode = tmu_regs_CR[reg] & TMU_TCR_TPSC;
	if (tmu_old_mode[reg] == mode)
		return;
	tmu_old_mode[reg] = mode;

	u32 shift;
	switch (mode)
	{
		case 0: shift = 2;  break;	// Pclk / 4
		case 1: shift = 4;  break;	// Pclk / 16
		case 2: shift = 6;  break;	// Pclk / 64
		case 3: shift = 8;  break;	// Pclk / 256
		case 4: shift = 10; break;	// Pclk / 1024
		case 5:
			printf("TMU ch%u: TCR prescaler mode 5 is reserved\n", reg);
			return;
		case 6:
			printf("TMU ch%u: TCR mode RTC (6) not supported on Dreamcast\n", reg);
			return;
		case 7:
			printf("TMU ch%u: TCR mode External (7) not supported on Dreamcast\n", reg);
			return;
		default:
			return;
	}

	// +2 because we count in Icyc (CPU core) cycles; TMU is clocked from Pclk
	// and Icyc = 4 * Pclk on the DC's SH4 (200 MHz Icyc, 50 MHz Pclk).
	shift += 2;
	tmu_prescaler_shift[reg] = shift;
	tmu_prescaler_mask[reg]  = (1u << shift) - 1u;
	tmu_prescaler[reg]       = 0;
}

// TCR writes, one handler per channel for the register table. A new prescaler mode moves the next underflow:
// with the SYS GATE the skipped slices run first at the old mode, and the next slice end plans again.
template<u32 ch>
static void TMU_TCR_write(u32 data)
{
	const bool remode = ((data & TMU_TCR_TPSC) != tmu_old_mode[ch]);
	if (remode)
		sys_sync();
	tmu_regs_CR[ch] = (u16)data;
	UpdateTMUCounts(ch);
	if (remode)
		sys_kick();
}

// TCPR2 - channel 2 input capture register. Not used / not connected on Dreamcast hardware.
u32 TMU_TCPR2_read()
{
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("TMU_TCPR2 read — register not used on Dreamcast\n");
	return 0;
}

void TMU_TCPR2_write(u32 data)
{
	// printf instead of EMUERROR because tiny freeze otherwise
	printf("TMU_TCPR2 write (data=0x%08X) — register not used on Dreamcast\n", data);
}

// Plan 5.36 SYS GATE handlers: TCNT reads add the skipped slices; TCNT and TSTR writes run them first.
template<u32 ch>
static u32 TMU_TCNT_read()
{
	return tmu_tcnt_now(ch);
}

template<u32 ch>
static void TMU_TCNT_write(u32 data)
{
	sys_sync();
	tmu_regs_CNT[ch] = data;
	sys_kick();
}

static void TMU_TSTR_write(u32 data)
{
	sys_sync();
	TMU_TSTR = (u8)data;
	sys_kick();
}

// tmu_Init  — register memory-mapped I/O bindings
void tmu_Init()
{
	// Helper macro to reduce repetition in the register table setup
#define TMU_REG(addr, _flags, rfn, wfn, field) \
	do { \
		auto& r = TMU[(addr & 0xFF) >> 2]; \
		r.flags         = (_flags); \
		r.readFunction  = (rfn); \
		r.writeFunction = (wfn); \
		r.field; \
	} while(0)

	// TOCR - Timer output control (8-bit, R/W)
	TMU_REG(TMU_TOCR_addr,  REG_8BIT_READWRITE  | REG_READ_DATA | REG_WRITE_DATA, 0, 0, data8  = &TMU_TOCR);

	// TSTR - Timer start (8-bit, R/W)
	TMU_REG(TMU_TSTR_addr,  REG_8BIT_READWRITE  | REG_READ_DATA | REG_WRITE_DATA, 0, 0, data8  = &TMU_TSTR);

	// Channel 0
	TMU_REG(TMU_TCOR0_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,                 data32 = &tmu_regs_COR[0]);
	TMU_REG(TMU_TCNT0_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,                 data32 = &tmu_regs_CNT[0]);
	TMU_REG(TMU_TCR0_addr,  REG_16BIT_READWRITE | REG_READ_DATA,                  0, TMU_TCR_write<0>, data16 = &tmu_regs_CR[0]);

	// Channel 1
	TMU_REG(TMU_TCOR1_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,                 data32 = &tmu_regs_COR[1]);
	TMU_REG(TMU_TCNT1_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,                 data32 = &tmu_regs_CNT[1]);
	TMU_REG(TMU_TCR1_addr,  REG_16BIT_READWRITE | REG_READ_DATA,                  0, TMU_TCR_write<1>, data16 = &tmu_regs_CR[1]);

	// Channel 2
	TMU_REG(TMU_TCOR2_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,                 data32 = &tmu_regs_COR[2]);
	TMU_REG(TMU_TCNT2_addr, REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA, 0, 0,                 data32 = &tmu_regs_CNT[2]);
	TMU_REG(TMU_TCR2_addr,  REG_16BIT_READWRITE | REG_READ_DATA,                  0, TMU_TCR_write<2>, data16 = &tmu_regs_CR[2]);

	// TCPR2 - input capture (not connected on DC)
	TMU_REG(TMU_TCPR2_addr, REG_32BIT_READWRITE, TMU_TCPR2_read, TMU_TCPR2_write, data32 = 0);

#undef TMU_REG
}

// Plan 5.36: with the SYS GATE on, TCNT and the TSTR write go through the handlers above.
void tmu_gate_install()
{
	static RegReadFP* const rd[3] = { TMU_TCNT_read<0>, TMU_TCNT_read<1>, TMU_TCNT_read<2> };
	static RegWriteFP* const wr[3] = { TMU_TCNT_write<0>, TMU_TCNT_write<1>, TMU_TCNT_write<2> };
	static const u32 cnt_addr[3] = { TMU_TCNT0_addr, TMU_TCNT1_addr, TMU_TCNT2_addr };
	for (u32 i = 0; i < 3; i++)
	{
		RegisterStruct& e = TMU[(cnt_addr[i] & 0xFF) >> 2];
		e.flags &= ~(u32)(REG_READ_DATA | REG_WRITE_DATA);
		e.readFunction  = rd[i];
		e.writeFunction = wr[i];
	}
	RegisterStruct& t = TMU[(TMU_TSTR_addr & 0xFF) >> 2];
	t.flags &= ~(u32)REG_WRITE_DATA;
	t.writeFunction = TMU_TSTR_write;
}

// tmu_Reset  — restore power-on defaults
void tmu_Reset(bool Manual)
{
	TMU_TOCR = TMU_TSTR = 0;

	for (int i = 0; i < 3; i++)
	{
		tmu_regs_COR[i] = 0xFFFFFFFF;
		tmu_regs_CNT[i] = 0xFFFFFFFF;
		tmu_regs_CR[i]  = 0;
		tmu_old_mode[i] = 0xFFFF;	// Force prescaler recalculation
		tmu_prescaler[i] = 0;
	}

	UpdateTMUCounts(0);
	UpdateTMUCounts(1);
	UpdateTMUCounts(2);
	sys_kick();
}

void tmu_Term()
{
	// Nothing to release; all state is statically allocated.
}
