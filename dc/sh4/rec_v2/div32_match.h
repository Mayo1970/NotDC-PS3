#pragma once
// Plan 5.38 JIT DIV32: the SHIL for a matched div0u/div0s + 32 x (rotcl r1; div1 r2,r3). FIXED is Flycast's form, a
// 64/32 divide with r3 as the high word; OLD the 32/32 form before 5.38. Needs decoder.h; misc/div32_check tests it.

static void div32_emit(DecodedBlock& b, bool is_unsigned, bool fixed, Sh4RegType r1, Sh4RegType r2, Sh4RegType r3)
{
	const shil_param q(r1), d(r2), h(r3), T(reg_sr_T), tmp(reg_temp), none;
	const shil_param one(FMT_IMM, 1);

	if (!fixed)
	{
		// rs3 empty: r3's high word is dropped, and the signed fixup is the unsigned one.
		b.Emit(is_unsigned ? shop_div32u : shop_div32s, q, q, d, 0, none, h);
		b.Emit(shop_and, T, q, one);
		b.Emit(is_unsigned ? shop_shr : shop_sar, q, q, one);
		b.Emit(shop_div32p2, h, h, d, 0, T);
		return;
	}

	// The last quotient bit stays in T; r1 holds the others under the bit the first rotcl shifted in.
	if (is_unsigned)
	{
		b.Emit(shop_div32u, q, q, d, 0, h, h);
		b.Emit(shop_and, T, q, one);
		b.Emit(shop_shr, q, q, one);
		b.Emit(shop_div32p2, h, h, d, 0, T);
		return;
	}

	// div0s/div1 use one's complement. T bit 31 is div0s's T: the quotient sign for div32p2 and r1's top bit (not
	// sar's copy, wrong for INT_MIN / 1). T ends as 0/1 again for JIT T COPY, T forwarding and JIT CR0.
	b.Emit(shop_xor, T, h, d);
	b.Emit(shop_and, T, T, shil_param(FMT_IMM, 0x80000000));
	b.Emit(shop_div32s, q, q, d, 0, h, h);
	b.Emit(shop_and, tmp, q, one);
	b.Emit(shop_shr, q, q, one);
	b.Emit(shop_or, q, q, T);
	b.Emit(shop_or, T, T, tmp);
	b.Emit(shop_div32p2, h, h, d, 0, T);
	b.Emit(shop_and, T, T, one);
}
