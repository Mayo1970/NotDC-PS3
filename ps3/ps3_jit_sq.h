#pragma once
// Plan 5.17 Step 3: the store-queue shape of a 32-bit or pair store. Needs ppc_emitter.h in scope;
// misc/sqstore_check runs what it emits against the canonical store-queue write.

// SQ test on the address in areg, then d0 (and d1 at +4 for a pair) into sq_base + (addr & 0x3C), or
// & 0x38 for a pair; fpr = d0/d1 are FPRs. Returns the bne label that any other address takes.
static inline ppc_label* emit_sq_store(u32 areg, u32 ea, bool pair, bool fpr, u32 d0, u32 d1, u32 sq_base)
{
	ppc_rlwinmx(ppc_r0, areg, 6, 26, 31, 0);   // r0 = addr >> 26; rlwinm clears a poisoned upper half
	ppc_cmpli(ppc_cr0, ppc_r0, 0x38, 0);
	ppc_label* other = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	// sq_base is 64-byte aligned and below 2 GB, so lo + 4 fits and addis adds a positive high part.
	const s32 lo = (s16)(sq_base & 0xFFFF);
	const u32 ha = (sq_base - (u32)lo) >> 16;
	ppc_rlwinmx(ea, areg, 0, 26, pair ? 28 : 29, 0);
	ppc_addis(ea, ea, ha);
	if (fpr)
	{
		ppc_stfs(d0, ea, (u32)lo);
		if (pair)
			ppc_stfs(d1, ea, (u32)(lo + 4));
	}
	else
	{
		ppc_stw(d0, ea, (u32)lo);
		if (pair)
			ppc_stw(d1, ea, (u32)(lo + 4));
	}
	return other;
}
