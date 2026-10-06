#pragma once
// Plan 5.38 JIT DIV32: the matched division's ops (div32_match.h) as PPU code. Needs ppc_emitter.h and
// ps3_jit_carry.h (rldicl, extsw); misc/div32_check tests it on the PC.

// 64-bit forms the emitter lacks: XO-form divdu, divd, mulld; MD-form rldimi; XS-form sradi.
static inline void ppc_divdu(u32 D, u32 A, u32 B) { ppc_emit(0x7C000392 | (D << 21) | (A << 16) | (B << 11)); }
static inline void ppc_divd(u32 D, u32 A, u32 B)  { ppc_emit(0x7C0003D2 | (D << 21) | (A << 16) | (B << 11)); }
static inline void ppc_mulld(u32 D, u32 A, u32 B) { ppc_emit(0x7C0001D2 | (D << 21) | (A << 16) | (B << 11)); }

static inline void ppc_rldimi(u32 A, u32 S, u32 sh, u32 mb)
{
	ppc_emit(0x7800000C | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((mb >> 5) << 5) |
	         ((sh >> 5) << 1));
}

static inline void ppc_sradi(u32 A, u32 S, u32 sh)
{
	ppc_emit(0x7C000674 | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((sh >> 5) << 1));
}

// OLD (rs3 empty): the 32/32 forms before 5.38. q, rem scratch.
static inline void emit_div32u_old(u32 q, u32 rem, u32 a, u32 b)
{
	ppc_divwux(q, a, b, 0, 0);
	ppc_mullwx(rem, q, b, 0, 0);
	ppc_subfx(rem, rem, a, 0, 0);
}

static inline void emit_div32s_old(u32 q, u32 rem, u32 a, u32 b)
{
	ppc_divwx(q, a, b, 0, 0);
	ppc_mullwx(rem, q, b, 0, 0);
	ppc_subfx(rem, rem, a, 0, 0);
}

// div32u: q, rem = the low words of (h:a) / b and (h:a) % b; on b == 0, q = 0 and rem = a (Flycast). a, b, h
// may hold junk above their low words. q, rem, x, y scratch, distinct, none a source.
static inline void emit_div32u(u32 q, u32 rem, u32 a, u32 b, u32 h, u32 x, u32 y)
{
	ppc_rldicl(x, a, 0, 32);
	ppc_rldimi(x, h, 32, 0);		// x = h:a
	ppc_rldicl(y, b, 0, 32);
	ppc_divdu(q, x, y);
	ppc_mulld(rem, q, y);
	ppc_subfx(rem, rem, x, 0, 0);		// the dividend when b == 0: q is undefined there, q * 0 is not
	ppc_negx(y, y, 0, 0);
	ppc_sradi(y, y, 63);			// -1 unless b == 0
	ppc_andx(q, q, y, 0);
}

// div32s (Flycast): (h:a) is one's complement, so +1 when negative, then a truncating divide by b; q - 1 when the
// signs differ, else rem - 1 when h < 0. Registers as div32u, and t.
static inline void emit_div32s(u32 q, u32 rem, u32 a, u32 b, u32 h, u32 x, u32 y, u32 t)
{
	ppc_rldicl(x, a, 0, 32);
	ppc_rldimi(x, h, 32, 0);
	ppc_rldicl(t, x, 1, 63);
	ppc_addx(x, x, t, 0, 0);
	ppc_extsw(y, b);
	ppc_divd(q, x, y);
	ppc_negx(t, y, 0, 0);
	ppc_orx(t, t, y, 0);
	ppc_sradi(t, t, 63);			// -1 unless b == 0
	ppc_andx(q, q, t, 0);
	ppc_mullwx(rem, q, y, 0, 0);
	ppc_subfx(rem, rem, x, 0, 0);		// low word: dividend - quo * b
	ppc_xorx(t, h, b, 0);
	ppc_rlwinmx(t, t, 1, 31, 31, 0);
	ppc_subfx(q, t, q, 0, 0);
	ppc_rlwinmx(y, h, 1, 31, 31, 0);
	ppc_andcx(y, y, t, 0);
	ppc_subfx(rem, y, rem, 0, 0);
}

// div32p2 (Flycast): a = remainder, b = divisor, t = T with div32s's quotient sign in bit 31. Sign clear: d = a - b
// unless T bit 0. Sign set: d = a - (b > 0) + (bit 0 ? b : 0). s, l, x, y scratch, not r0; d may be a source.
static inline void emit_div32p2(u32 d, u32 a, u32 b, u32 t, u32 s, u32 l, u32 x, u32 y)
{
	ppc_rlwinmx(s, t, 1, 31, 31, 0);
	ppc_xorx(l, t, s, 0);
	ppc_rlwinmx(l, l, 0, 31, 31, 0);
	ppc_addi(l, l, -1);			// -1 when bit 0 == sign: b is added (sign) or subtracted
	ppc_addi(x, s, -1);
	ppc_xorx(y, b, x, 0);
	ppc_subfx(y, x, y, 0, 0);		// sign ? b : -b
	ppc_andx(y, y, l, 0);
	ppc_extsw(x, b);
	ppc_negx(x, x, 0, 0);
	ppc_rldicl(x, x, 1, 63);		// b > 0
	ppc_andx(x, x, s, 0);
	ppc_subfx(y, x, y, 0, 0);
	ppc_addx(d, a, y, 0, 0);
}
