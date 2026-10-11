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

// The TMU counts SH4 cycles with 12 fraction bits. 6.4 TMU Clock EFF, below 200 MHz: a slice counts x 200 / clock,
// scaled once, so each slice has the same count and the SYS GATE's closed forms hold.
#define TMU_FRAC 12
extern "C" int get_tmu_clock_preset();
static int s_tmu_mhz = 200;
static u32 s_tmu_mul = 1u << (TMU_FRAC + 16);

static INLINE u32 tmu_units(u32 clc)
{
	return (u32)(((u64)clc * s_tmu_mul) >> 16);
}

// Read at a slice update only: the SYS GATE's plan and the slices it skipped then count at one rate.
static INLINE void tmu_clock_latch()
{
	int mhz = get_tmu_clock_preset() ? sh4_clock_eff_mhz() : 200;
	// Below 25 MHz the scale and a slice's count leave 32 bits; the menu stops at 150.
	if (mhz < 25)
		mhz = 25;
	if (mhz == s_tmu_mhz)
		return;
	s_tmu_mhz = mhz;
	s_tmu_mul = (u32)((((u64)200 << (TMU_FRAC + 16)) + (u32)mhz / 2) / (u32)mhz);
}

// P11 TMU Fine TCNT: TMU cycles of the running slice a channel has counted already (a write inside the slice
// commits them). The slice end takes them off.
static u32 tmu_part_base[3];

static INLINE bool tmu_part_any()
{
	return (tmu_part_base[0] | tmu_part_base[1] | tmu_part_base[2]) != 0;
}

// TMU cycles run inside the current slice. Whole slices at the slice's own count, so a slice end takes off the same.
static INLINE u32 tmu_part_now()
{
	const s32 part = sys_slice_part();
	if (part <= 0)
		return 0;
	const u32 slice = (u32)sh4_GetTimeslice();
	return tmu_units(slice) * ((u32)part / slice) + tmu_units((u32)part % slice);
}

// One channel step of clc TMU cycles on the given state; true on an underflow. The SYS GATE's planner and its TCNT
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

// Advances channel ch by clc TMU cycles; also takes several underflows in one call.
template<u32 ch>
INLINE void UpdateTMU_chan(u32 clc)
{
	// P11: the part of this slice that a write inside it has counted already.
	if (const u32 base = tmu_part_base[ch])
	{
		if (base >= clc)
		{
			tmu_part_base[ch] = base - clc;
			return;
		}
		tmu_part_base[ch] = 0;
		clc -= base;
	}

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
	tmu_clock_latch();
	const u32 clc = tmu_units(Cycles);
	UpdateTMU_chan<0>(clc);
	UpdateTMU_chan<1>(clc);
	UpdateTMU_chan<2>(clc);
}

// Plan 5.36: slice ends up to the first underflow of a running channel, at most max.
s32 tmu_slices_to_event(s32 slice, s32 max)
{
	const u32 ts = tmu_units((u32)slice);
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

// Plan 5.42: the same count without the slice loop. Most plans end at the first test: no underflow in reach.
s32 tmu_slices_to_event_fast(s32 slice, s32 max)
{
	const u32 ts = tmu_units((u32)slice);
	for (u32 ch = 0; ch < 3; ch++)
	{
		if ((TMU_TSTR & tmu_ch_bit[ch]) == 0 || max <= 1)
			continue;
		const u32 sh = tmu_prescaler_shift[ch], pres = tmu_prescaler[ch], cnt = tmu_regs_CNT[ch];
		if (((pres + (u32)ts * (u32)(max - 1)) >> sh) <= cnt)
			continue;
		// First n with (pres + n * ts) >> sh above TCNT. The test above keeps (cnt + 1) << sh in 32 bits.
		const u32 need = (cnt + 1) << sh;
		max = need <= pres ? 1 : (s32)((need - pres + (u32)ts - 1) / (u32)ts);
	}
	return max;
}

// Plan 5.42: channel ch after clc cycles in pres and cnt; false when it underflows in them.
static INLINE bool tmu_quiet_chan(u32 ch, u32 clc, u32& pres, u32& cnt)
{
	pres = tmu_prescaler[ch];
	cnt = tmu_regs_CNT[ch];
	if ((TMU_TSTR & tmu_ch_bit[ch]) == 0)
		return true;
	pres += clc;
	const u32 steps = pres >> tmu_prescaler_shift[ch];
	pres &= tmu_prescaler_mask[ch];
	if (steps > cnt)
		return false;
	cnt -= steps;
	return true;
}

// Plan 5.42: n slices as one step when no running channel underflows in them; else false and no change.
bool tmu_slices_quiet_run(s32 slice, s32 n)
{
	// P11: a slice with a committed part counts less than its whole; the slice-by-slice update takes it off.
	if (tmu_part_any())
		return false;
	const u32 clc = tmu_units((u32)slice) * (u32)n;
	u32 p0, c0, p1, c1, p2, c2;
	if (!tmu_quiet_chan(0, clc, p0, c0) || !tmu_quiet_chan(1, clc, p1, c1) || !tmu_quiet_chan(2, clc, p2, c2))
		return false;
	tmu_prescaler[0] = p0;
	tmu_regs_CNT[0] = c0;
	tmu_prescaler[1] = p1;
	tmu_regs_CNT[1] = c1;
	tmu_prescaler[2] = p2;
	tmu_regs_CNT[2] = c2;
	return true;
}

// [tmu] line counts: TCNT reads through the handler, those with cycles inside the slice, and the commits.
static u32 s_st_reads, s_st_fine, s_st_commits;
static u64 s_st_part;

// TCNT after the slices the SYS GATE skipped (none underflows) and, with TMU Fine TCNT, the cycles run in this one.
// On a copy: an underflow inside the slice reloads the value here; UNF and the interrupt come at the slice end.
static u32 tmu_tcnt_now(u32 ch)
{
	u32 cnt = tmu_regs_CNT[ch];
	const s32 k = sys_pending();
	const u32 part = tmu_part_now();
	s_st_reads++;
	s_st_fine += part != 0;
	s_st_part += part;
	if ((k <= 0 && part == 0) || (TMU_TSTR & tmu_ch_bit[ch]) == 0)
		return cnt;
	u32 pres = tmu_prescaler[ch];
	const u32 ts = tmu_units((u32)sh4_GetTimeslice());
	u64 run = (u64)(k > 0 ? k : 0) * ts + part;
	run = run > tmu_part_base[ch] ? run - tmu_part_base[ch] : 0;
	// Slice by slice, as the update runs them.
	while (run)
	{
		const u32 c = run > ts ? ts : (u32)run;
		tmu_step(pres, cnt, tmu_prescaler_shift[ch], tmu_prescaler_mask[ch], tmu_regs_COR[ch], c);
		run -= c;
	}
	return cnt;
}

// P11: before a write that changes channel ch, the cycles it ran inside this slice go into its count.
static void tmu_commit(u32 ch)
{
	const u32 part = tmu_part_now();
	if (part <= tmu_part_base[ch])
		return;
	const u32 clc = part - tmu_part_base[ch];
	tmu_part_base[ch] = part;
	s_st_commits++;
	if ((TMU_TSTR & tmu_ch_bit[ch]) == 0)
		return;
	if (tmu_step(tmu_prescaler[ch], tmu_regs_CNT[ch], tmu_prescaler_shift[ch], tmu_prescaler_mask[ch],
	             tmu_regs_COR[ch], clc))
	{
		tmu_regs_CR[ch] |= TMU_TCR_UNF;
		InterruptPend(tmu_intID[ch], 1);
	}
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
	shift += 2 + TMU_FRAC;
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
	{
		sys_sync();
		tmu_commit(ch);
	}
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
// P11 uses them too: the reads add the cycles inside the slice, the writes commit them.
template<u32 ch>
static u32 TMU_TCNT_read()
{
	return tmu_tcnt_now(ch);
}

template<u32 ch>
static void TMU_TCNT_write(u32 data)
{
	sys_sync();
	tmu_commit(ch);
	tmu_regs_CNT[ch] = data;
	sys_kick();
}

static void TMU_TSTR_write(u32 data)
{
	sys_sync();
	// A channel that starts here must not count the cycles before the write; one that stops keeps them.
	tmu_commit(0);
	tmu_commit(1);
	tmu_commit(2);
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

// Plan 5.36 and P11: with the SYS GATE or TMU Fine TCNT on, TCNT and the TSTR write go through the handlers above.
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
		tmu_part_base[i] = 0;
	}

	UpdateTMUCounts(0);
	UpdateTMUCounts(1);
	UpdateTMUCounts(2);
	sys_kick();
}

// [tmu] line of the 1 s stats block, only with TMU Fine TCNT on or a TMU rate that is not the SH4's.
void tmu_stats(double secs)
{
	if ((g_slice_part_on || s_tmu_mhz != 200) && secs > 0.0)
		printf("[tmu] rate x%.4f | tcnt reads %.0f/s, %.0f/s inside a slice (avg %u cycles) | commits %.0f/s\n",
		       200.0 / s_tmu_mhz, s_st_reads / secs, s_st_fine / secs,
		       s_st_fine ? (u32)((s_st_part / s_st_fine) >> TMU_FRAC) : 0, s_st_commits / secs);
	s_st_reads = s_st_fine = s_st_commits = 0;
	s_st_part = 0;
}

void tmu_Term()
{
	// Nothing to release; all state is statically allocated.
}
