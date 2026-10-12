// P49 census (log only), shared by the three ARM7 engines' decoders: the forms where they read the PC 4 bytes short
// of the ARM7DI. Plain C: the SPU engine (ps3/spu/arm7_spu.h) includes it too.
#pragma once
#include <stdint.h>

#define A7_P49_KEEP 8

typedef struct
{
	uint32_t n[2];      // decodes of form 1 and form 2
	uint32_t kept[2];   // pairs in the lists
	uint32_t scan;      // instructions looked at
	uint32_t pad;
	uint32_t pc[2][A7_P49_KEEP], op[2][A7_P49_KEEP];   // the first different pc / opcode pairs of each form
} A7P49;

// 1 = data processing, shift amount in a register, Rn = PC (the ARM7 reads insn+12, the engines +8); 2 = STR / STRB
// of PC (the ARM7 stores insn+12, the engines a stale reg[15]). Classes as the engines decode them; NV never runs.
static inline int a7_p49_form(uint32_t op)
{
	if ((op >> 28) == 0xF)
		return 0;
	const uint32_t fam = (op >> 26) & 3;
	if (fam == 1)
		return (!(op & 0x00100000) && ((op >> 12) & 0xF) == 15) ? 2 : 0;
	if (fam != 0 || (op & 0x02000010) != 0x10 || ((op >> 16) & 0xF) != 15)
		return 0;
	if ((op & 0x0FC000F0) == 0x00000090 || (op & 0x0FB00FF0) == 0x01000090)   // MUL / MLA, SWP
		return 0;
	if (((op >> 23) & 3) == 2 && !(op & 0x00100000))                         // MRS / MSR
		return 0;
	const uint32_t op4 = (op >> 21) & 0xF;
	return (op4 != 0xD && op4 != 0xF) ? 1 : 0;   // MOV and MVN have no Rn
}

// One decoded instruction. Returns the form when its pair is new in the list (a block decoded again adds none).
static inline int a7_p49_scan(A7P49* c, uint32_t pc, uint32_t op)
{
	c->scan++;
	const int f = a7_p49_form(op);
	if (!f)
		return 0;
	const int k = f - 1;
	c->n[k]++;
	for (uint32_t i = 0; i < c->kept[k]; i++)
		if (c->pc[k][i] == pc && c->op[k][i] == op)
			return 0;
	if (c->kept[k] >= A7_P49_KEEP)
		return 0;
	c->pc[k][c->kept[k]] = pc;
	c->op[k][c->kept[k]] = op;
	c->kept[k]++;
	return f;
}
