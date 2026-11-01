#include <stdio.h>
#include "types.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/mem/sh4_mem.h"
#include "ps3_timebase.h"
#include "ps3/ps3_sfres.h"

#define SC_N 32
#define GD_N 48

struct ScRec { u32 pc, a4, a5, a6, a7, sp, ret, n, out[4]; u64 t0, t1; };
struct GdRec { u32 a, b, c, d; u64 t; char kind; };

static struct
{
	ScRec sc[SC_N];
	GdRec gd[GD_N];
	u32 sc_n, gd_n, last, regs;
	u8 dumps[3];
} s_sf;

// The BIOS call vectors: system info, font, flash, GD-ROM, GD-ROM 2, system (menu and reset).
static const u16 k_vec[6] = { 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xE0 };

static u32 ram32(u32 a)
{
	return (a & 0x1C000000) == 0x0C000000 && a < 0xE0000000 ? ReadMem32(0x8C000000 | (a & 0x00FFFFFC)) : 0;
}

static u32 ms(u64 ticks) { return (u32)ticks_to_millisecs(ticks); }

bool sfres_bios_entry(u32 pc)
{
	for (u32 k = 0; k < 6; k++)
	{
		const u32 v = ReadMem32(0x8C000000 | k_vec[k]);
		if (v && !((v ^ pc) & 0x1FFFFFFF))
			return true;
	}
	return false;
}

// Read after the call returned: the BIOS writes its results (drive status, command status) where r4 and r5 point.
static void sc_out(ScRec& c)
{
	c.out[0] = ram32(c.a4);
	c.out[1] = ram32(c.a4 + 4);
	c.out[2] = ram32(c.a5);
	c.out[3] = ram32(c.a5 + 4);
}

void sfres_bios_call(u32 pc, u32 r4, u32 r5, u32 r6, u32 r7, u32 r15)
{
	const u64 now = gettime();
	if (s_sf.sc_n)
		sc_out(s_sf.sc[s_sf.last % SC_N]);
	else
		printf("[sfres] first BIOS call: at %08X r6 %08X r7 %08X pr %08X\n", pc, r6, r7, (u32)pr);
	// A polling loop alternates 2-3 calls, so a match in the last 4 records adds to its count.
	for (u32 k = 1; k <= 4 && k <= s_sf.sc_n; k++)
	{
		const u32 i = s_sf.sc_n - k;
		ScRec& c = s_sf.sc[i % SC_N];
		if (c.pc == pc && c.a6 == r6 && c.a7 == r7 && c.ret == pr)
		{
			c.a4 = r4;
			c.a5 = r5;
			c.sp = r15;
			c.n++;
			c.t1 = now;
			s_sf.last = i;
			return;
		}
	}
	s_sf.sc[s_sf.sc_n % SC_N] = ScRec{ pc, r4, r5, r6, r7, r15, pr, 1, { 0, 0, 0, 0 }, now, now };
	s_sf.last = s_sf.sc_n++;
}

// kind 0: ATA command, 1: SPI packet, 2: drive registers (stored only when they change).
void sfres_gd(u32 kind, u32 a, u32 b, u32 c, u32 d)
{
	if (kind == 2)
	{
		if (a == s_sf.regs)
			return;
		s_sf.regs = a;
	}
	s_sf.gd[s_sf.gd_n++ % GD_N] = GdRec{ a, b, c, d, gettime(), "ASR"[kind] };
}

// why: 0 SB_SFRES write (up to 3 dumps), 1 first Boot ROM write, 2 first bad opcode.
void sfres_dump(u32 why)
{
	if (s_sf.dumps[why]++ >= (why ? 1 : 3))
		return;
	const u64 now = gettime();
	if (s_sf.sc_n)
		sc_out(s_sf.sc[s_sf.last % SC_N]);
	u32 v[6];
	for (u32 k = 0; k < 6; k++)
		v[k] = ReadMem32(0x8C000000 | k_vec[k]);
	printf("[sfres] dump why=%u: pc~%08X pr %08X vbr %08X | vectors B0 %08X B4 %08X B8 %08X BC %08X C0 %08X E0 %08X\n",
	       why, (u32)next_pc, (u32)pr, (u32)vbr, v[0], v[1], v[2], v[3], v[4], v[5]);
	// Oldest first; times are ms before this dump.
	for (u32 i = s_sf.sc_n > SC_N ? s_sf.sc_n - SC_N : 0; i < s_sf.sc_n; i++)
	{
		const ScRec& c = s_sf.sc[i % SC_N];
		printf("[sfres] call %u: -%u..-%u ms x%u at %08X r4 %08X r5 %08X r6 %08X r7 %08X r15 %08X pr %08X"
		       " | @r4 %08X %08X @r5 %08X %08X\n", i, ms(now - c.t0), ms(now - c.t1), c.n, c.pc, c.a4, c.a5, c.a6,
		       c.a7, c.sp, c.ret, c.out[0], c.out[1], c.out[2], c.out[3]);
	}
	if (s_sf.sc_n)
	{
		const u32 sp = s_sf.sc[s_sf.last % SC_N].sp;
		u32 w[16];
		for (u32 k = 0; k < 16; k++)
			w[k] = ram32(sp + k * 4);
		printf("[sfres] stack %08X: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
		       sp, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
	}
	for (u32 i = s_sf.gd_n > GD_N ? s_sf.gd_n - GD_N : 0; i < s_sf.gd_n; i++)
	{
		const GdRec& g = s_sf.gd[i % GD_N];
		printf("[sfres] gd %u: -%u ms %c %08X %08X %08X %08X\n", i, ms(now - g.t), g.kind, g.a, g.b, g.c, g.d);
	}
}
