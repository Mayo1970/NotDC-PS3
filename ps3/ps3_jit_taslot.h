#pragma once
// P87 JIT TA SLOT: the store-queue stores of a record also go to the TA buffer's next record (r11 holds its address),
// so the PREF's flush reads nothing back. Needs decoder.h and ppc_emitter.h in scope; misc/taslot_check tests it.

enum
{
	TS_WIN   = 1,    // from the record's first store to its PREF: a cold path or another PREF poisons r11
	TS_STORE = 2,    // a store of the record: also at r11 + (addr & 0x1C)
	TS_FIRST = 4,    // the record's first store: r11 = the slot, before the store
	TS_PCW   = 8,    // the record's last store of word 0: its data to r12 for the flush
	TS_END   = 16,   // the record's PREF: the flush gets r11, r12 and the mask of words stored
};
#define TS_MAX_OPS 1024
#define TS_SLOT ppc_r11
#define TS_PCWR ppc_r12

struct TsPlan
{
	u32 n;                     // ops planned; 0 = none
	u8  role[TS_MAX_OPS];
	u8  mask[TS_MAX_OPS];      // TS_END: bit i = word i is in the slot; bit 0 also means the PCW is in r12
	u32 groups;
};

enum { TS_STOP, TS_PASS, TS_MEM, TS_PREF };

// What an op inside a window may be. PASS: no C call and no r11/r12 on its path. MEM: its cold path may call C (the
// cold path poisons r11). STOP: a C call on the fast path (ifb, sync_sr, mac, the canonical fallback).
static inline int ts_class(const shil_opcode& o, bool fsqrt_inline, bool fschg_inline)
{
	switch (o.op)
	{
	case shop_readm:
		return o.rs1.is_r32i() ? TS_MEM : TS_STOP;
	case shop_writem:
		return TS_MEM;
	case shop_pref:
		return TS_PREF;
	case shop_jdyn: case shop_jcond: case shop_mov64: case shop_mov32:
	case shop_add: case shop_sub: case shop_or: case shop_and: case shop_xor:
	case shop_shl: case shop_shr: case shop_sar: case shop_ror: case shop_shld: case shop_shad:
	case shop_mul_i32: case shop_mul_u16: case shop_mul_s16: case shop_mul_u64: case shop_mul_s64:
	case shop_div32u: case shop_div32s: case shop_div32p2: case shop_div1:
	case shop_adc: case shop_sbc: case shop_negc: case shop_addv: case shop_subv: case shop_cmpstr: case shop_swaplb:
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt: case shop_setae: case shop_setab:
	case shop_neg: case shop_not: case shop_ext_s8: case shop_ext_s16:
	case shop_fadd: case shop_fsub: case shop_fmul: case shop_fdiv: case shop_fabs: case shop_fneg:
	case shop_fseteq: case shop_fsetgt: case shop_cvt_f2i_t: case shop_cvt_i2f_n: case shop_cvt_i2f_z:
	case shop_fmac: case shop_fipr: case shop_ftrv: case shop_fsca:
		return TS_PASS;
	case shop_fsqrt: case shop_fsrra:
		return fsqrt_inline ? TS_PASS : TS_STOP;
	case shop_sync_fpscr:
		return fschg_inline && (o.flags & SYNC_FPSCR_SZ_ONLY) ? TS_PASS : TS_STOP;
	default:
		return TS_STOP;
	}
}

// p names GPR g (a register param, ranges by their whole length).
static inline bool ts_writes(const shil_param& p, u32 g)
{
	return p.is_reg() && (u32)p._reg <= g && g < (u32)p._reg + (u32)p.count();
}

// Walks back from each PREF through a GPR Rb, last PREF first. A store through Rb at a known offset that lands in the
// PREF's half of the queue is in the record; Rb may move by add/sub #imm. A PREF inside a later window is no end.
static void ts_plan(TsPlan& p, const DecodedBlock& b, bool fsqrt_inline, bool fschg_inline)
{
	p.n = p.groups = 0;
	const u32 n = (u32)b.oplist.size();
	if (n > TS_MAX_OPS)
		return;
	p.n = n;
	for (u32 i = 0; i < n; i++)
		p.role[i] = p.mask[i] = 0;
	u32 lo = n;   // the first op of the last window found
	for (u32 e = n; e-- > 0; )
	{
		const shil_opcode& pf = b.oplist[e];
		if (e >= lo || pf.op != shop_pref || !pf.rs1.is_r32i() || pf.rs1._reg > reg_r15)
			continue;
		const u32 rb = (u32)pf.rs1._reg;
		s32 off = 0;           // Rb at the PREF minus Rb after op j
		u32 mask = 0, first = e, pcw_at = e;
		int w0 = 0;            // the last store of word 0: 1 from an integer register, -1 not, 0 none yet
		for (u32 j = e; j-- > 0; )
		{
			const shil_opcode& o = b.oplist[j];
			const int k = ts_class(o, fsqrt_inline, fschg_inline);
			if (k == TS_STOP)
				break;
			if (k == TS_PREF)
			{
				// The stores before a PREF through Rb are that PREF's record.
				if (o.rs1.is_r32i() && (u32)o.rs1._reg == rb)
					break;
				continue;
			}
			if (o.op == shop_writem)
			{
				if (!o.rs1.is_r32i() || (u32)o.rs1._reg != rb || !(o.rs3.is_null() || o.rs3.is_imm()) ||
				    (o.flags != 4 && o.flags != 8))
					break;
				// The queue is 64 bytes: the store lands on byte (PREF address + at) mod 64. The store body masks a
				// pair's address with 0x38, so a pair at 4 mod 8 lands on the words below it.
				const u32 sz = o.flags;
				const s64 rel = (s64)(o.rs3.is_imm() ? (s32)o.rs3._imm : 0) - off;
				if (rel % sz)
					break;
				const s64 at = ((rel % 64) + 64) % 64;
				if (at + sz > 32)
					continue;   // the other half: in the window, not in the record
				if (at == 0 && w0 == 0)
				{
					w0 = sz == 4 && !o.rs2.is_r32f() ? 1 : -1;
					pcw_at = j;
				}
				mask |= (sz == 8 ? 3u : 1u) << (u32)(at / 4);
				p.role[j] = TS_STORE;
				first = j;
				continue;
			}
			if (!ts_writes(o.rd, rb) && !ts_writes(o.rd2, rb))
				continue;
			const bool step = (o.op == shop_add || o.op == shop_sub) && o.rs1.is_r32i() && (u32)o.rs1._reg == rb &&
			                  o.rs2.is_imm() && o.rd.is_r32i() && !ts_writes(o.rd2, rb);
			if (!step)
				break;
			const s64 d = (s64)off + (o.op == shop_add ? (s64)(s32)o.rs2._imm : -(s64)(s32)o.rs2._imm);
			if (d < -0x10000 || d > 0x10000)
				break;
			off = (s32)d;
		}
		if (!mask || w0 < 0)
		{
			for (u32 j = first; j < e; j++)
				p.role[j] = 0;
			continue;
		}
		p.role[first] |= TS_FIRST;
		if (w0 > 0)
			p.role[pcw_at] |= TS_PCW;
		for (u32 j = first; j <= e; j++)
			p.role[j] |= TS_WIN;
		p.role[e] |= TS_END;
		p.mask[e] = (u8)mask;
		p.groups++;
		lo = first;
	}
}

static inline u8 ts_role(const TsPlan& p, u32 i) { return i < p.n ? p.role[i] : (u8)0; }

// r11 = the slot (u32 at slot_var), before the record's first store.
static inline void emit_ts_load(u32 slot_var)
{
	const s32 lo = (s16)(slot_var & 0xFFFF);
	ppc_addis(TS_SLOT, 0, (slot_var - (u32)lo) >> 16);
	ppc_lwz(TS_SLOT, TS_SLOT, (u32)lo);
}

// After the store-queue store: d0 (and d1 at +4 for a pair) at r11 + (addr & 0x1C), or & 0x18 for a pair. ea: the
// store's address scratch, free again.
static inline void emit_ts_store(u32 areg, u32 ea, bool pair, bool fpr, u32 d0, u32 d1)
{
	ppc_rlwinmx(ppc_r0, areg, 0, 27, pair ? 28 : 29, 0);
	if (!pair)
	{
		if (fpr)
			ppc_stfsx(d0, TS_SLOT, ppc_r0);
		else
			ppc_stwx(d0, TS_SLOT, ppc_r0);
		return;
	}
	ppc_addx(ea, TS_SLOT, ppc_r0, 0, 0);
	if (fpr)
	{
		ppc_stfs(d0, ea, 0);
		ppc_stfs(d1, ea, 4);
	}
	else
	{
		ppc_stw(d0, ea, 0);
		ppc_stw(d1, ea, 4);
	}
}

// r11 = a 32-byte area that is never a slot: the flush takes the copy, and later stores of the record land there.
static inline void emit_ts_poison(u32 area)
{
	const s32 lo = (s16)(area & 0xFFFF);
	ppc_addis(TS_SLOT, 0, (area - (u32)lo) >> 16);
	ppc_addi(TS_SLOT, TS_SLOT, (u32)lo);
}
