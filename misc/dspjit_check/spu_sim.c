// OPTIMIZATION_PLAN 5.13 A1b PC test: SPU simulator, written from the SPU ISA (not from the emitter).
// Only the forms the DSP recompiler emits; anything else faults.
#include "spu_sim.h"
#include <stdio.h>
#include <string.h>

#define LSLR 0x3FFFFu
#define RET_MARK 0x3FF00u

static int32_t sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }

void spusim_init(SpuSim* s)
{
	memset(s, 0, sizeof(*s));
	memset(s->qmap, -1, sizeof(s->qmap));
	s->max_steps = 4000000;
}

static int region_of(const SpuSim* s, uint32_t a)
{
	for (int i = 0; i < s->nreg; i++)
		if (a - s->reg[i].ls < s->reg[i].size)
			return i;
	return -1;
}

void spusim_map(SpuSim* s, uint32_t ls, void* host, uint32_t size, int esz)
{
	SimRegion* r = &s->reg[s->nreg++];
	r->ls = ls;
	r->size = size;
	r->host = (uint8_t*)host;
	r->esz = esz;
	for (uint32_t q = ls & ~15u; q < ls + size; q += 16)
	{
		int k = region_of(s, q);
		for (uint32_t i = 1; i < 16; i++)
			if (region_of(s, q + i) != k)
				k = -2;
		s->qmap[q >> 4] = (int8_t)k;
	}
}

void spusim_hostcall(SpuSim* s, uint32_t ls, SimHostFn fn)
{
	s->hc_addr[s->nhc] = ls;
	s->hc_fn[s->nhc++] = fn;
}

static uint8_t* host_byte(const SimRegion* r, uint32_t a)
{
	const uint32_t j = a - r->ls, e = j / r->esz, k = j % r->esz;
	return r->host + e * r->esz + (r->esz - 1 - k);
}

static uint8_t qbyte(const SimQ* q, int i) { return (uint8_t)(q->w[i >> 2] >> (24 - 8 * (i & 3))); }
static void set_qbyte(SimQ* q, int i, uint8_t v)
{
	const int sh = 24 - 8 * (i & 3);
	q->w[i >> 2] = (q->w[i >> 2] & ~(0xFFu << sh)) | ((uint32_t)v << sh);
}

static SimQ load_q(SpuSim* s, uint32_t a)
{
	SimQ q;
	a &= LSLR & ~15u;
	const int k = s->qmap[a >> 4];
	if (k >= 0 && s->reg[k].esz == 4 && !((a - s->reg[k].ls) & 3))
	{
		memcpy(q.w, s->reg[k].host + (a - s->reg[k].ls), 16);
		return q;
	}
	for (int i = 0; i < 16; i++)
	{
		const int r = k >= 0 ? k : region_of(s, a + i);
		set_qbyte(&q, i, r >= 0 ? *host_byte(&s->reg[r], a + i) : 0);
	}
	return q;
}

// Unmapped bytes may only be written back as 0 (the read-modify-write of a quadword edge).
static void store_q(SpuSim* s, uint32_t a, const SimQ* q)
{
	a &= LSLR & ~15u;
	const int k = s->qmap[a >> 4];
	if (k >= 0 && s->reg[k].esz == 4 && !((a - s->reg[k].ls) & 3))
	{
		memcpy(s->reg[k].host + (a - s->reg[k].ls), q->w, 16);
		return;
	}
	for (int i = 0; i < 16; i++)
	{
		const int r = k >= 0 ? k : region_of(s, a + i);
		if (r >= 0)
			*host_byte(&s->reg[r], a + i) = qbyte(q, i);
		else if (qbyte(q, i) && !s->fault)
		{
			s->fault = 1;
			snprintf(s->msg, sizeof(s->msg), "store of %02X to unmapped LS %05X at pc %05X", qbyte(q, i), a + i, s->pc);
		}
	}
}

static SimQ splat(uint32_t v)
{
	SimQ q = {{v, v, v, v}};
	return q;
}

static SimQ insert_mask(uint32_t t, int bytes)
{
	SimQ m;
	for (int i = 0; i < 16; i++)
		set_qbyte(&m, i, (uint8_t)(0x10 + i));
	for (int i = 0; i < bytes; i++)
		set_qbyte(&m, (int)t + i, (uint8_t)(4 - bytes + i));
	return m;
}

static SimQ rot_bytes(const SimQ* a, uint32_t n)
{
	SimQ t;
	for (int i = 0; i < 16; i++)
		set_qbyte(&t, i, qbyte(a, (int)((i + n) & 15)));
	return t;
}

static int fault(SpuSim* s, uint32_t w, const char* why)
{
	s->fault = 1;
	snprintf(s->msg, sizeof(s->msg), "%s: insn %08X at pc %05X", why, w, s->pc);
	return 1;
}

// Executes the instruction at pc. Returns 0, or 1 on a fault.
static int step(SpuSim* s)
{
	const SimQ cq = load_q(s, s->pc);
	const uint32_t w = cq.w[(s->pc >> 2) & 3];
	const int rt = w & 0x7F, ra = (w >> 7) & 0x7F, rb = (w >> 14) & 0x7F;
	SimQ* T = &s->r[rt];
	const SimQ A = s->r[ra], B = s->r[rb];
	uint32_t npc = (s->pc + 4) & LSLR;
	s->insns++;

	const uint32_t op4 = w >> 28;
	if (op4 == 0x8 || op4 == 0xB)
	{
		// RRR: rt in bits 4..10, rc in bits 25..31
		const SimQ C = s->r[w & 0x7F];
		SimQ* D = &s->r[(w >> 21) & 0x7F];
		SimQ r;
		if (op4 == 0x8)
			for (int i = 0; i < 4; i++)
				r.w[i] = (C.w[i] & B.w[i]) | (~C.w[i] & A.w[i]);
		else
			for (int i = 0; i < 16; i++)
			{
				const uint8_t c = qbyte(&C, i);
				uint8_t v;
				if ((c & 0xC0) == 0x80)
					v = 0x00;
				else if ((c & 0xE0) == 0xC0)
					v = 0xFF;
				else if ((c & 0xE0) == 0xE0)
					v = 0x80;
				else
					v = (c & 0x10) ? qbyte(&B, c & 15) : qbyte(&A, c & 15);
				set_qbyte(&r, i, v);
			}
		*D = r;
		s->pc = npc;
		return 0;
	}
	if ((w >> 25) == 0x21)
	{
		*T = splat((w >> 7) & 0x3FFFF);   // ila
		s->pc = npc;
		return 0;
	}

	const int32_t i10 = sext((w >> 14) & 0x3FF, 10);
	SimQ r;
	switch (w >> 24)
	{
	case 0x1C: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] + (uint32_t)i10; *T = r; s->pc = npc; return 0;   // ai
	case 0x0C: for (int i = 0; i < 4; i++) r.w[i] = (uint32_t)i10 - A.w[i]; *T = r; s->pc = npc; return 0;   // sfi
	case 0x14: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] & (uint32_t)i10; *T = r; s->pc = npc; return 0;   // andi
	case 0x04: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] | (uint32_t)i10; *T = r; s->pc = npc; return 0;   // ori
	case 0x44: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] ^ (uint32_t)i10; *T = r; s->pc = npc; return 0;   // xori
	case 0x7C: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] == (uint32_t)i10 ? ~0u : 0; *T = r; s->pc = npc; return 0;
	case 0x4C: for (int i = 0; i < 4; i++) r.w[i] = (int32_t)A.w[i] > i10 ? ~0u : 0; *T = r; s->pc = npc; return 0;
	case 0x5C: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] > (uint32_t)i10 ? ~0u : 0; *T = r; s->pc = npc; return 0;
	case 0x34: *T = load_q(s, A.w[0] + ((uint32_t)i10 << 4)); s->pc = npc; return 0;   // lqd
	case 0x24: store_q(s, A.w[0] + ((uint32_t)i10 << 4), T); s->pc = npc; return s->fault;   // stqd
	default: break;
	}

	const uint32_t i16 = (w >> 7) & 0xFFFF;
	const uint32_t aaddr = ((uint32_t)sext(i16, 16) << 2) & LSLR;
	switch (w >> 23)
	{
	case 0x081: *T = splat((uint32_t)sext(i16, 16)); s->pc = npc; return 0;   // il
	case 0x082: *T = splat(i16 << 16); s->pc = npc; return 0;                 // ilhu
	case 0x0C1: for (int i = 0; i < 4; i++) T->w[i] |= i16; s->pc = npc; return 0;   // iohl
	case 0x061: *T = load_q(s, aaddr); s->pc = npc; return 0;                 // lqa
	case 0x041: store_q(s, aaddr, T); s->pc = npc; return s->fault;          // stqa
	case 0x062:                                                               // brasl
		for (int h = 0; h < s->nhc; h++)
			if (s->hc_addr[h] == aaddr)
			{
				const uint32_t res = s->hc_fn[h](s->r[3].w[0], s->r[4].w[0]);
				for (int k = 2; k < 80; k++)
					for (int i = 0; i < 4; i++)
						s->r[k].w[i] = 0xBAD00000u | ((uint32_t)k << 8) | (uint32_t)i;
				s->r[3].w[0] = res;
				s->r[rt] = splat(0);
				s->r[rt].w[0] = npc;
				s->pc = npc;
				return 0;
			}
		*T = splat(0);
		T->w[0] = npc;
		s->pc = aaddr & ~3u;
		return 0;
	case 0x040: s->pc = T->w[0] == 0 ? ((s->pc + ((uint32_t)sext(i16, 16) << 2)) & LSLR) : npc; return 0;   // brz
	case 0x042: s->pc = T->w[0] != 0 ? ((s->pc + ((uint32_t)sext(i16, 16) << 2)) & LSLR) : npc; return 0;   // brnz
	default: break;
	}

	const int32_t i7 = sext((w >> 14) & 0x7F, 7);
	switch (w >> 21)
	{
	case 0x0C0: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] + B.w[i]; break;
	case 0x040: for (int i = 0; i < 4; i++) r.w[i] = B.w[i] - A.w[i]; break;
	case 0x0C1: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] & B.w[i]; break;
	case 0x041: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] | B.w[i]; break;
	case 0x241: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] ^ B.w[i]; break;
	case 0x2C1: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] & ~B.w[i]; break;
	case 0x249: for (int i = 0; i < 4; i++) r.w[i] = ~(A.w[i] ^ B.w[i]); break;
	case 0x049: for (int i = 0; i < 4; i++) r.w[i] = ~(A.w[i] | B.w[i]); break;
	case 0x3C0: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] == B.w[i] ? ~0u : 0; break;
	case 0x240: for (int i = 0; i < 4; i++) r.w[i] = (int32_t)A.w[i] > (int32_t)B.w[i] ? ~0u : 0; break;
	case 0x2C0: for (int i = 0; i < 4; i++) r.w[i] = A.w[i] > B.w[i] ? ~0u : 0; break;
	case 0x05B:   // shl
		for (int i = 0; i < 4; i++)
		{
			const uint32_t n = B.w[i] & 0x3F;
			r.w[i] = n < 32 ? A.w[i] << n : 0;
		}
		break;
	case 0x059:   // rotm
		for (int i = 0; i < 4; i++)
		{
			const uint32_t n = (0 - B.w[i]) & 0x3F;
			r.w[i] = n < 32 ? A.w[i] >> n : 0;
		}
		break;
	case 0x05A:   // rotma
		for (int i = 0; i < 4; i++)
		{
			const uint32_t n = (0 - B.w[i]) & 0x3F;
			r.w[i] = (uint32_t)((int32_t)A.w[i] >> (n < 32 ? n : 31));
		}
		break;
	case 0x1DC: r = rot_bytes(&A, B.w[0] & 15); break;   // rotqby
	case 0x1C4: r = load_q(s, A.w[0] + B.w[0]); break;    // lqx
	case 0x144: store_q(s, A.w[0] + B.w[0], T); s->pc = npc; return s->fault;   // stqx
	case 0x1D6: r = insert_mask((A.w[0] + B.w[0]) & 0xC, 4); break;   // cwx
	case 0x1D5: r = insert_mask((A.w[0] + B.w[0]) & 0xE, 2); break;   // chx
	case 0x3C4:   // mpy
		for (int i = 0; i < 4; i++)
			r.w[i] = (uint32_t)((int32_t)(int16_t)(A.w[i] & 0xFFFF) * (int32_t)(int16_t)(B.w[i] & 0xFFFF));
		break;
	case 0x2A5:   // clz
		for (int i = 0; i < 4; i++)
			r.w[i] = A.w[i] ? (uint32_t)__builtin_clz(A.w[i]) : 32;
		break;
	case 0x2AE: for (int i = 0; i < 4; i++) r.w[i] = (uint32_t)(int32_t)(int16_t)(A.w[i] & 0xFFFF); break;
	case 0x1A8: s->pc = A.w[0] & LSLR & ~3u; return 0;   // bi
	case 0x002: case 0x201: case 0x001: s->pc = npc; return 0;   // sync, nop, lnop
	case 0x07B:   // shli
		for (int i = 0; i < 4; i++)
		{
			const uint32_t n = (uint32_t)i7 & 0x3F;
			r.w[i] = n < 32 ? A.w[i] << n : 0;
		}
		break;
	case 0x079:   // rotmi
		for (int i = 0; i < 4; i++)
		{
			const uint32_t n = (0 - (uint32_t)i7) & 0x3F;
			r.w[i] = n < 32 ? A.w[i] >> n : 0;
		}
		break;
	case 0x07A:   // rotmai
		for (int i = 0; i < 4; i++)
		{
			const uint32_t n = (0 - (uint32_t)i7) & 0x3F;
			r.w[i] = (uint32_t)((int32_t)A.w[i] >> (n < 32 ? n : 31));
		}
		break;
	case 0x1FC: r = rot_bytes(&A, (uint32_t)i7 & 15); break;   // rotqbyi
	case 0x1F6: r = insert_mask((A.w[0] + (uint32_t)i7) & 0xC, 4); break;   // cwd
	case 0x1F5: r = insert_mask((A.w[0] + (uint32_t)i7) & 0xE, 2); break;   // chd
	default: return fault(s, w, "unknown instruction");
	}
	*T = r;
	s->pc = npc;
	return 0;
}

int spusim_call(SpuSim* s, uint32_t entry, uint32_t sp)
{
	SimQ keep[48];
	uint32_t seed = 0x2545F491u ^ entry;
	for (int k = 80; k < 128; k++)
		for (int i = 0; i < 4; i++)
		{
			seed = seed * 1664525u + 1013904223u;
			s->r[k].w[i] = seed;
		}
	memcpy(keep, &s->r[80], sizeof(keep));
	for (int k = 2; k < 80; k++)   // the generated code takes no arguments: catch reads of unset temporaries
		for (int i = 0; i < 4; i++)
			s->r[k].w[i] = 0xF00D0000u | ((uint32_t)k << 8) | (uint32_t)i;
	s->r[0] = splat(0);
	s->r[0].w[0] = RET_MARK;
	s->r[1] = splat(0);
	s->r[1].w[0] = sp;
	s->pc = entry;
	s->fault = 0;
	for (uint32_t n = 0; s->pc != RET_MARK; n++)
	{
		if (n >= s->max_steps)
			return fault(s, 0, "step limit");
		if (step(s))
			return 1;
	}
	if (s->r[1].w[0] != sp)
	{
		snprintf(s->msg, sizeof(s->msg), "$1 = %05X on return, was %05X", s->r[1].w[0], sp);
		return s->fault = 1;
	}
	for (int k = 80; k < 128; k++)
		if (memcmp(&s->r[k], &keep[k - 80], sizeof(SimQ)))
		{
			snprintf(s->msg, sizeof(s->msg), "callee-saved $%d changed", k);
			return s->fault = 1;
		}
	return 0;
}
