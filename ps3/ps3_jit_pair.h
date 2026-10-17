#pragma once
// P105 JIT PAIR CALLS: ELFv1 passes and returns a u64 in one GPR, the cold paths hold it as r5:r6 (store) and r3:r4
// (load). Needs ps3_jit_mmu.h (jm_rldicl, jm_rldicr); misc/pair_check runs both on a PPC64 model.

// WriteMem64's value: r4 = (r5 << 32) | the low word of r6. Junk above either word is allowed.
static inline void emit_pair_arg()
{
	jm_rldicr(ppc_r4, ppc_r5, 32, 31);
	jm_rldicl(ppc_r6, ppc_r6, 0, 32);
	ppc_orx(ppc_r4, ppc_r4, ppc_r6, 0);
}

// ReadMem64's u64 in r3 as the pair: r3 = word[a], r4 = word[a+4], each zero-extended.
static inline void emit_pair_ret()
{
	jm_rldicl(ppc_r4, ppc_r3, 0, 32);
	jm_rldicl(ppc_r3, ppc_r3, 32, 32);
}
