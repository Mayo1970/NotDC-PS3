#pragma once
// Plan 5.25 JIT IDLE SKIP: which blocks count as work, and which idle-rule blocks may end the timeslice.
// dec_DecodeBlock runs it; misc/idle_check tests it against a SHIL interpreter.
#include "decoder.h"

// A turn that ran one of these may have written guest memory: writem, a store-queue pref, the interpreter.
static bool idle_block_stores(const DecodedBlock& b)
{
	for (size_t i = 0; i < b.oplist.size(); i++)
	{
		const shilop op = b.oplist[i].op;
		if (op == shop_writem || op == shop_pref || op == shop_ifb)
			return true;
	}
	return false;
}

// Integer ops whose inputs and outputs are all params: div1 and mac.* also use SR.Q/M and MACH:MACL.
static bool idle_op_plain(const shil_opcode& o)
{
	switch (o.op)
	{
	case shop_mov32: case shop_jcond: case shop_readm:
	case shop_and: case shop_or: case shop_xor: case shop_not:
	case shop_add: case shop_sub: case shop_neg:
	case shop_adc: case shop_sbc: case shop_negc: case shop_addv: case shop_subv:
	case shop_cmpstr: case shop_swaplb:
	case shop_shl: case shop_shr: case shop_sar: case shop_ror: case shop_shld: case shop_shad:
	case shop_ext_s8: case shop_ext_s16:
	case shop_mul_u16: case shop_mul_s16: case shop_mul_i32: case shop_mul_u64: case shop_mul_s64:
	case shop_div32u: case shop_div32s: case shop_div32p2:
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt: case shop_setae: case shop_setab:
		break;
	default:
		return false;
	}
	const shil_param* const prm[5] = { &o.rd, &o.rd2, &o.rs1, &o.rs2, &o.rs3 };
	for (int k = 0; k < 5; k++)
		if (prm[k]->is_reg() && (!prm[k]->is_r32i() || (u32)prm[k]->_reg >= (u32)sh4_reg_count))
			return false;
	return true;
}

// No register is read before the block writes it and then written (a counter, a moving pointer). With guest
// memory unchanged, a second turn reads the same addresses and leaves the same registers as the first.
static bool idle_turn_is_pure(const DecodedBlock& b)
{
	bool written[sh4_reg_count] = {};
	bool live_in[sh4_reg_count] = {};
	for (size_t i = 0; i < b.oplist.size(); i++)
	{
		const shil_opcode& o = b.oplist[i];
		if (!idle_op_plain(o))
			return false;
		const shil_param* const src[3] = { &o.rs1, &o.rs2, &o.rs3 };
		for (int k = 0; k < 3; k++)
			if (src[k]->is_reg() && !written[src[k]->_reg])
				live_in[src[k]->_reg] = true;
		const shil_param* const dst[2] = { &o.rd, &o.rd2 };
		for (int k = 0; k < 2; k++)
			if (dst[k]->is_reg())
			{
				if (live_in[dst[k]->_reg])
					return false;
				written[dst[k]->_reg] = true;
			}
	}
	return true;
}
