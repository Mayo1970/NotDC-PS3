#pragma once
#include "types.h"

// ARM7 Idle Skip SAFE (OPTIMIZATION_PLAN 6.1), shared by both engines: a self-loop keeps its +100 ticks only when no
// register the block writes is a source of the same or an earlier instruction, and no load writes back its base.
struct Arm7IdleScan
{
	u32  reads;   // r0-r14 read so far in the block; r15 reads a per-address constant
	bool ok;

	void reset()
	{
		reads = 0;
		ok = true;
	}

	// Same opcode classes, in the same order, as the engines' decoders.
	void insn(u32 op)
	{
		u32 src = 0, dst = 0;
		const u32 fam = (op >> 26) & 3;
		if (fam == 1)
		{
			src = 1u << ((op >> 16) & 0xF);
			if (op & 0x02000000)
				src |= 1u << (op & 0xF);
			if (op & 0x00100000)
			{
				dst = 1u << ((op >> 12) & 0xF);
				if (!(op & 0x01000000) || (op & 0x00200000))
					ok = false;   // post-index or writeback
			}
		}
		else if (fam == 0)
		{
			if ((op & 0x0FC000F0) == 0x00000090)   // MUL/MLA
			{
				src = (1u << (op & 0xF)) | (1u << ((op >> 8) & 0xF));
				if (op & 0x00200000)
					src |= 1u << ((op >> 12) & 0xF);
				dst = 1u << ((op >> 16) & 0xF);
			}
			else if ((op & 0x0FB00FF0) == 0x01000090)   // SWP
				ok = false;
			else if (((op >> 23) & 3) == 2 && !(op & 0x00100000))   // MRS / MSR
			{
				if (op & 0x00200000)
					ok = false;
				else
					dst = 1u << ((op >> 12) & 0xF);
			}
			else
			{
				const u32 op4 = (op >> 21) & 0xF;
				if (op4 != 0xD && op4 != 0xF)
					src |= 1u << ((op >> 16) & 0xF);
				if (!(op & 0x02000000))
				{
					src |= 1u << (op & 0xF);
					if (op & 0x10)
						src |= 1u << ((op >> 8) & 0xF);   // shift by register
				}
				if (op4 < 0x8 || op4 > 0xB)
					dst = 1u << ((op >> 12) & 0xF);
			}
		}
		else if (fam == 2 && !(op & 0x02000000))   // LDM/STM
			ok = false;

		reads |= src & 0x7FFF;
		if (dst & reads)
			ok = false;
	}
};
