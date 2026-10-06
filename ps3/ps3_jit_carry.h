#pragma once
// Plan 5.2 JIT Carry. The PPU runs in 64-bit mode, where adde/subfe set CA and addo/subfo set OV from 64 bits, so
// these ops compute in 64 bits from extended operands. Needs ppc_emitter.h; misc/carry_check tests it on the PC.

// rldicl rA,rS,sh,mb (MD-form: sh and mb each split into 5 + 1 bits). The emitter has no 64-bit forms.
static inline void ppc_rldicl(u32 A, u32 S, u32 sh, u32 mb)
{
	ppc_emit(0x78000000 | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((mb >> 5) << 5) |
	         ((sh >> 5) << 1));
}

static inline void ppc_extsw(u32 A, u32 S)
{
	ppc_emit(0x7C0007B4 | (S << 21) | (A << 16));
}

// addc: d = a + b + t. a, b may hold junk above the low word (pinned); zero-extended, the sum is below 2^33, so
// bit 32 is the carry. t is T (0 or 1) in and out; x, y scratch, a may be x and b may be y; d may be a or b.
static inline void emit_carry_addc(u32 d, u32 a, u32 b, u32 t, u32 x, u32 y)
{
	ppc_rldicl(x, a, 0, 32);
	ppc_rldicl(y, b, 0, 32);
	ppc_addx(x, x, y, 0, 0);
	ppc_addx(d, x, t, 0, 0);
	ppc_rldicl(t, d, 32, 63);
}

// subc: d = a - b - t; with a borrow the 64-bit difference is negative, so bit 32 is the borrow. As addc.
static inline void emit_carry_subc(u32 d, u32 a, u32 b, u32 t, u32 x, u32 y)
{
	ppc_rldicl(x, a, 0, 32);
	ppc_rldicl(y, b, 0, 32);
	ppc_subfx(x, y, x, 0, 0);
	ppc_subfx(d, t, x, 0, 0);
	ppc_rldicl(t, d, 32, 63);
}

// negc: d = 0 - a - t, negative (bit 32 set, the borrow) unless a + t is 0. a may be x; d may be a.
static inline void emit_carry_negc(u32 d, u32 a, u32 t, u32 x)
{
	ppc_rldicl(x, a, 0, 32);
	ppc_addx(x, x, t, 0, 0);
	ppc_negx(d, x, 0, 0);
	ppc_rldicl(t, d, 32, 63);
}

// addv / subv: d = a + b or a - b, t = signed overflow: the exact result of the sign-extended operands is then
// not the sign extension of its low word, and the xor of the two has bit 63 set. Registers as addc.
static inline void emit_carry_ovf(u32 d, u32 a, u32 b, u32 t, u32 x, u32 y, bool sub)
{
	ppc_extsw(x, a);
	ppc_extsw(y, b);
	if (sub)
		ppc_subfx(d, y, x, 0, 0);
	else
		ppc_addx(d, x, y, 0, 0);
	ppc_extsw(t, d);
	ppc_xorx(t, t, d, 0);
	ppc_rldicl(t, t, 1, 63);
}

// div1: A = Rn (in place), B = Rm, s = SR.status (Q updated), t = T in and out; m, q, x, y scratch, y not r0.
// sel = old Q ^ M: Rn = ((Rn << 1) | T) + (sel ? Rm : ~Rm) + !sel, new Q = MSB ^ old Q ^ carry ^ 1.
static inline void emit_carry_div1(u32 A, u32 B, u32 s, u32 t, u32 m, u32 q, u32 x, u32 y)
{
	ppc_rlwinmx(m, s, 23, 31, 31, 0);	// M = SR bit 9
	ppc_rlwinmx(x, s, 24, 31, 31, 0);	// old Q = SR bit 8
	ppc_xorx(y, x, m, 0);
	ppc_rlwinmx(q, A, 1, 31, 31, 0);
	ppc_xorx(q, q, x, 0);
	ppc_addi(x, y, -1);
	ppc_xorx(x, B, x, 0);			// Rm, or ~Rm; read before Rn shifts, so div1 Rn,Rn works
	ppc_rldicl(x, x, 0, 32);
	ppc_xori(y, y, 1);
	ppc_rlwinmx(A, A, 1, 0, 30, 0);	// upper half cleared
	ppc_orx(A, A, t, 0);
	ppc_addx(A, A, x, 0, 0);
	ppc_addx(A, A, y, 0, 0);		// below 2^33: bit 32 is the carry
	ppc_rldicl(t, A, 32, 63);
	ppc_xorx(q, q, t, 0);
	ppc_xori(q, q, 1);
	ppc_xorx(t, q, m, 0);
	ppc_xori(t, t, 1);			// T = (new Q == M)
	ppc_rlwimix(s, q, 8, 23, 23, 0);
}

// cmp/str: CR0 GT = a byte of a ^ b is zero (has-zero-byte test, rotated onto 0x01010101, whose clear MSB keeps
// the and. result's upper half zero). t, c, x scratch.
static inline void emit_carry_cmpstr(u32 a, u32 b, u32 t, u32 c, u32 x)
{
	ppc_xorx(t, a, b, 0);
	ppc_addis(c, 0, 0x0101);
	ppc_ori(c, c, 0x0101);
	ppc_subfx(x, c, t, 0, 0);
	ppc_andcx(x, x, t, 0);
	ppc_rlwinmx(x, x, 1, 0, 31, 0);
	ppc_andx(x, x, c, 1);
}

// swap.b: d = s with its two low bytes swapped; d must not be s. rlwimi puts source bit (i+n)%32 in bit i.
static inline void emit_carry_swaplb(u32 d, u32 s)
{
	ppc_rlwinmx(d, s, 0, 0, 15, 0);
	ppc_rlwimix(d, s, 8, 16, 23, 0);
	ppc_rlwimix(d, s, 24, 24, 31, 0);
}
