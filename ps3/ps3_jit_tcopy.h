#pragma once
// Plan 5.26 JIT T COPY: the last T writer before bt.s/bf.s also copies T into the jcond register, so the jcond
// skips its reload of sr_T (a load-hit-store). Needs decoder.h in scope; misc/tcopy_check tests it.

// Ops whose PPC code stores sr_T inline from a register, on every path; the copy follows that store.
static bool tcopy_op_ok(shilop op)
{
	switch (op)
	{
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt: case shop_setae: case shop_setab:
	case shop_cmpstr: case shop_fseteq: case shop_fsetgt:
	case shop_and: case shop_or: case shop_xor: case shop_shl: case shop_shr: case shop_sar: case shop_mov32:
	case shop_adc: case shop_sbc: case shop_negc: case shop_addv: case shop_subv: case shop_div1:
		return true;
	default:
		return false;
	}
}

// An interpreter op may write sr.T in memory; jdyn writes the jcond register itself.
static bool tcopy_may_write_t(const shil_opcode& o)
{
	return (o.rd.is_r32i() && o.rd._reg == reg_sr_T) || (o.rd2.is_r32i() && o.rd2._reg == reg_sr_T) ||
	       o.op == shop_ifb || o.op == shop_jdyn;
}

// The op whose T store feeds the block's jcond, or -1: the last op before it that may write T, if that op is
// in the list above (ifb and jdyn are not).
static int tcopy_writer(const DecodedBlock& b)
{
	size_t j = 0;
	while (j < b.oplist.size() && b.oplist[j].op != shop_jcond)
		j++;
	if (j == b.oplist.size() || !b.oplist[j].rs1.is_r32i() || b.oplist[j].rs1._reg != reg_sr_T)
		return -1;
	for (size_t k = j; k-- > 0;)
		if (tcopy_may_write_t(b.oplist[k]))
			return tcopy_op_ok(b.oplist[k].op) ? (int)k : -1;
	return -1;
}

struct TCopy
{
	int  k;        // the writer, -1 for none
	bool armed;    // op k is being emitted: its sr_T stores also copy
	bool held;     // the jcond register holds the current T
};

static void tcopy_begin(TCopy& s, const DecodedBlock* b)
{
	s.k = b ? tcopy_writer(*b) : -1;
	s.armed = s.held = false;
}

static void tcopy_op_begin(TCopy& s, size_t i) { s.armed = s.k >= 0 && (size_t)s.k == i; }

// At each sr_T store: true when the stored register must also be copied into the jcond register.
static bool tcopy_store(TCopy& s)
{
	if (s.armed)
		s.held = true;
	return s.armed;
}

// A later op that may write T or the jcond register drops the copy, so the jcond loads sr_T again.
static void tcopy_op_end(TCopy& s, const shil_opcode& o)
{
	if (!s.armed && tcopy_may_write_t(o))
		s.held = false;
	s.armed = false;
}
