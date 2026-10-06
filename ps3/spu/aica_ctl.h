// OPTIMIZATION_PLAN 5.15: nullAICA's registers, timers and interrupts (SPE AICA Sound mode) in portable C, byte-exact
// with aica_reg. The includer defines ac_twin_write, ac_twin_mon, ac_twin_step and ac_cdda_refill.
#pragma once
#include <stdint.h>
#include <string.h>

#define AC_CDDA_SIZE 1176   // s16 per CD-DA sector: a refill every 588 samples

typedef struct
{
	uint8_t reg[0x8000 + 4];   // aica_reg: DC byte a at [a ^ 3]; +4 for an unaligned 16-bit access at the top
	int32_t c_step[3];
	uint32_t m_step[3];
	uint32_t interr, reg_l;    // arm_mem.cpp aica_interr, aica_reg_L
	uint32_t e68k_out, e68k_l, e68k_m;
	uint32_t cdda_index;
	uint32_t sh4_irq;          // UpdateSh4Ints: MCIEB & MCIPD != 0
	uint32_t smp;              // g_aica_smp
} AicaCtl;

static AicaCtl ac;

static void ac_twin_write(uint32_t off, uint32_t data, uint32_t size, uint32_t old16);   // ps3aica_write
static uint32_t ac_twin_mon(uint32_t reg, int keep_lp);                                  // ps3aica_read_mon
static void ac_twin_step(void);                                                           // ps3aica_step
static void ac_cdda_refill(void);   // cdda_refill + ps3aica_cdda

// The 16-bit register at A (A % 4 == 0) is the big-endian halfword at [A + 2], as the AICA_PS3 bitfields lay it out.
static inline uint32_t ac_rd16(uint32_t h) { return ((uint32_t)ac.reg[h] << 8) | ac.reg[h + 1]; }
static inline void ac_wr16(uint32_t h, uint32_t v)
{
	ac.reg[h] = (uint8_t)(v >> 8);
	ac.reg[h + 1] = (uint8_t)v;
}
static inline uint32_t ac_rd32(uint32_t h) { return (ac_rd16(h) << 16) | ac_rd16(h + 2); }
static inline void ac_wr32(uint32_t h, uint32_t v)
{
	ac_wr16(h, v >> 16);
	ac_wr16(h + 2, v & 0xFFFF);
}
static inline void ac_setbit(uint32_t a, uint32_t b) { ac.reg[a + 2 + (b < 8)] |= (uint8_t)(1u << (b & 7)); }

// ReadMemArrRet / WriteMemArr, sz 1 or 2
static inline uint32_t ac_rd(uint32_t addr, uint32_t sz) { return sz == 1 ? ac.reg[addr ^ 3] : ac_rd16(addr ^ 2); }
static inline void ac_wr(uint32_t addr, uint32_t data, uint32_t sz)
{
	if (sz == 1)
		ac.reg[addr ^ 3] = (uint8_t)data;
	else
		ac_wr16(addr ^ 2, data & 0xFFFF);
}

// ── Interrupts (aica.cpp, arm_mem.cpp) ──

static uint32_t ac_getl(uint32_t witch)
{
	if (witch > 7)
		witch = 7;
	const uint32_t bit = 1u << witch;
	uint32_t rv = 0;
	if (ac.reg[0x28AB] & bit) rv = 1;    // SCILV0
	if (ac.reg[0x28AF] & bit) rv |= 2;   // SCILV1
	if (ac.reg[0x28B3] & bit) rv |= 4;   // SCILV2
	return rv;
}

static void ac_update_e68k(void)
{
	if (!ac.e68k_out && ac.interr)
	{
		ac.e68k_out = 1;
		ac.e68k_l = ac.reg_l;
	}
}

static void ac_e68k_accept(void)
{
	ac.e68k_out = 0;
	ac_update_e68k();
}

// update_arm_interrupts: the lowest pending source of bits 0..10 picks L
static void ac_update_arm_ints(void)
{
	const uint32_t p = ac_rd32(0x289C) & ac_rd32(0x28A0);
	uint32_t l = 0;
	if (p & 0x7FF)
		l = ac_getl((uint32_t)__builtin_ctz(p & 0x7FF));
	ac.interr = p != 0;
	if (ac.interr)
		ac.reg_l = l;
	ac_update_e68k();
}

static inline void ac_update_sh4_ints(void) { ac.sh4_irq = (ac_rd32(0x28B4) & ac_rd32(0x28B8)) != 0; }

// ── Timers: count is [0x2893 + 4i], md the low 3 bits of [0x2892 + 4i] ──

static void ac_timer_write(uint32_t i)
{
	const uint32_t n = 1u << (ac.reg[0x2892 + 4 * i] & 7);
	if (n != ac.m_step[i])
	{
		ac.m_step[i] = n;
		ac.c_step[i] = (int32_t)n;
	}
}

static inline void ac_timer_step(uint32_t i)
{
	if (--ac.c_step[i] != 0)
		return;
	ac.c_step[i] = (int32_t)ac.m_step[i];
	if (++ac.reg[0x2893 + 4 * i] != 0)
		return;
	ac_setbit(0x28A0, 6 + i);   // SCIPD Timer A/B/C
	ac_setbit(0x28B8, 6 + i);   // MCIPD
}

// ── Register reads and writes (aica_mem.cpp, sgc_if.cpp ReadCommonReg / WriteCommonReg8 / RegWrite) ──

static void ac_read_common(uint32_t reg, int byte)
{
	switch (reg)
	{
	case 0x2808: case 0x2809:
		ac.reg[0x280A] |= 0x09;   // MIEMP, MOEMP
		break;
	case 0x2810: case 0x2811:
		ac_wr16(0x2812, ac_twin_mon(0x2810, byte && reg == 0x2810) & 0xFFFF);
		break;
	case 0x2814: case 0x2815:
		ac_wr16(0x2816, ac_twin_mon(0x2814, 0) & 0xFFFF);
		break;
	}
}

// aica_ReadMem_reg: sizes other than 1 read 16 bits
static uint32_t ac_read(uint32_t addr, uint32_t size)
{
	const uint32_t a = addr & 0x7FFF, sz = size == 1 ? 1 : 2;
	if (a >= 0x2800 && a < 0x2818)
		ac_read_common(a, sz == 1);
	return ac_rd(a, sz);
}

// The only register changes of nullAICA's channel writes in SPE sound mode, where its slots never step.
static void ac_chan_write8(uint32_t chan, uint32_t off)
{
	uint8_t* c = &ac.reg[chan * 0x80];
	if (off == 0x01)
		c[0x02] &= 0x7F;   // KYONEX
	else if (off == 0x1C || off == 0x1D)
		c[0x1E] &= 0x7F;   // LFORE
}

static void ac_write_aica(uint32_t reg, uint32_t data, uint32_t sz)
{
	switch (reg)
	{
	case 0x28A0:   // SCIPD: a write can only set SCPU
		if (data & 0x20)
		{
			ac_setbit(0x28A0, 5);
			ac_update_arm_ints();
		}
		return;
	case 0x28A4:   // SCIRE, not stored
		ac_wr32(0x28A0, ac_rd32(0x28A0) & ~data);
		ac_update_arm_ints();
		return;
	case 0x28B8:   // MCIPD
		if (data & 0x20)
		{
			ac_setbit(0x28B8, 5);
			ac_update_sh4_ints();
		}
		return;
	case 0x28BC:   // MCIRE, not stored
		ac_wr32(0x28B8, ac_rd32(0x28B8) & ~data);
		ac_update_sh4_ints();
		return;
	case 0x2890: case 0x2894: case 0x2898:
		ac_wr(reg, data, sz);
		ac_timer_write((reg - 0x2890) >> 2);
		return;
	default:
		ac_wr(reg, data, sz);
		return;
	}
}

// aica_WriteMem_reg: the twin sees the write first, with the halfword nullAICA held
static void ac_write(uint32_t addr, uint32_t data, uint32_t size)
{
	const uint32_t a = addr & 0x7FFF, sz = size == 1 ? 1 : 2;
	ac_twin_write(a, data, size, ac_rd16((addr & 0x7FFC) | 2));
	if (a < 0x2000)
	{
		ac_wr(a, data, sz);
		ac_chan_write8(a >> 7, a & 0x7F);
		if (sz == 2)
			ac_chan_write8(a >> 7, (a & 0x7F) + 1);
		return;
	}
	if (a < 0x2800)
	{
		ac_wr(a, data, sz);
		return;
	}
	if (a < 0x2818)
	{
		ac.reg[a ^ 3] = (uint8_t)data;
		if (sz == 2)
			ac.reg[(a + 1) ^ 3] = (uint8_t)(data >> 8);
		return;
	}
	if (a >= 0x3000)
		ac_wr(a, data, sz);
	ac_write_aica(a, data, sz);
}

// arm_ReadReg / arm_WriteReg: REG_L and REG_M are the e68k's, sizes 1 and 4
static uint32_t ac_arm_read(uint32_t addr, uint32_t size)
{
	const uint32_t a = addr & 0x7FFF;
	uint32_t v;
	if (a == 0x2D00)
		v = ac.e68k_l;
	else if (a == 0x2D04)
		v = ac.e68k_m;
	else
		v = ac_read(a, size);
	return size == 1 ? v & 0xFF : v;
}

static void ac_arm_write(uint32_t addr, uint32_t data, uint32_t size)
{
	const uint32_t a = addr & 0x7FFF;
	if (a == 0x2D00)
		return;
	if (a == 0x2D04)
	{
		if (data & 1)
			ac_e68k_accept();
		return;
	}
	ac_write(a, data, size);
}

// libAICA_TimeStep with AICA_Sample's SPE-mode CD-DA count
static void ac_timestep(void)
{
	ac.smp++;
	if (ac.cdda_index >= AC_CDDA_SIZE)
	{
		ac.cdda_index = 0;
		ac_cdda_refill();
	}
	ac.cdda_index += 2;
	ac_setbit(0x28A0, 10);   // SCIPD SAMPLE_DONE
	ac_timer_step(0);
	ac_timer_step(1);
	ac_timer_step(2);
	ac_update_arm_ints();
	ac_update_sh4_ints();
	ac_twin_step();
}

static void ac_init(void)
{
	memset(&ac, 0, sizeof(ac));
	ac.cdda_index = AC_CDDA_SIZE << 1;
	for (uint32_t i = 0; i < 3; i++)
		ac.m_step[i] = ac.c_step[i] = 1;   // md 0
}
