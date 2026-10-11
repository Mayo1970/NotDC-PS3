#pragma once
// Plan 5.3 JIT MAC. The PPU runs in 64-bit mode, where addc/adde carry from 64 bits, so the accumulate is one
// 64-bit add. Needs ppc_emitter.h, ps3_jit_carry.h (rldicl, extsw) and ps3_jit_div32.h (rldimi, mulld).

// mac.l / mac.w with S == 0: lo, hi = the words of (h:l) + a * b, a and b signed (mac.w's are sign-extended words).
// Sources may hold junk above the low word, and so may lo. lo, hi, x scratch, none a source; misc/jit8_check.
static inline void emit_mac_acc(u32 lo, u32 hi, u32 a, u32 b, u32 l, u32 h, u32 x)
{
	ppc_extsw(x, a);
	ppc_extsw(hi, b);
	ppc_mulld(x, x, hi);
	ppc_rldicl(lo, l, 0, 32);
	ppc_rldimi(lo, h, 32, 0);		// lo = h:l
	ppc_addx(lo, lo, x, 0, 0);
	ppc_rldicl(hi, lo, 32, 32);
}
