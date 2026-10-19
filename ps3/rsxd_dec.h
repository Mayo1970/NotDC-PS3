#pragma once
// RSX Direct CHECK (G17): reads an RSX command stream as the RSX does and keeps its registers, VP instruction and
// constant memories and the draws. ps3_rsxd.cpp and misc/rsxdirect_check compare two streams with it.
#include <stdint.h>
#include <string.h>

#define RD_REGS   2048   // subchannel 0, methods 0x0000-0x1ffc
#define RD_VPI    512    // VP instructions, 4 words each
#define RD_VPC    512    // VP constants, 4 words each
#define RD_DIRTY  4096

// Method words that are events, not state: they never go into the register file.
static inline int rd_event(uint32_t m)
{
	return m == 0x0100 || m == 0x1714 || m == 0x1710 || m == 0x1808 || m == 0x1814 || m == 0x1fd8 || m == 0x1d94 ||
	       m == 0x1d6c || m == 0x1d70 || m == 0x1d74 || (m >= 0x0b80 && m < 0x0c00) || (m >= 0x1efc && m < 0x1f80) ||
	       m == 0x1e9c;
}

typedef struct
{
	uint32_t reg[RD_REGS];
	uint8_t set[RD_REGS];
	uint32_t vpi[RD_VPI * 4];
	uint8_t vpi_set[RD_VPI];
	uint32_t vpc[RD_VPC * 4];
	uint8_t vpc_set[RD_VPC];
	uint32_t vpi_ptr, vpc_ptr;
	// Writes since the last rd_clean(): register indexes, and VP instruction / constant slots | 0x8000 / 0x4000.
	uint16_t dirty[RD_DIRTY];
	uint32_t ndirty, overflow;
	uint8_t is_dirty[RD_REGS], vpi_dirty[RD_VPI], vpc_dirty[RD_VPC];
	// Events: texture cache clears, vertex cache clears, draws ended; the draw in progress.
	uint32_t n_inval, n_vinval, n_draws;
	uint32_t prim, nbatch, batch_first, batch_count;
	uint32_t err;
} RdRegs;

static inline void rd_reset(RdRegs* r)
{
	memset(r, 0, sizeof(*r));
}

// r gets a's registers and memories; nothing is dirty in either.
static inline void rd_copy(RdRegs* r, const RdRegs* a)
{
	memcpy(r, a, sizeof(*r));
}

static inline void rd_mark(RdRegs* r, uint32_t tag, uint8_t* flag)
{
	if (*flag)
		return;
	*flag = 1;
	if (r->ndirty < RD_DIRTY)
		r->dirty[r->ndirty++] = (uint16_t)tag;
	else
		r->overflow = 1;
}

static inline void rd_clean(RdRegs* r)
{
	for (uint32_t i = 0; i < r->ndirty; i++)
	{
		const uint32_t t = r->dirty[i];
		if (t & 0x8000)
			r->vpi_dirty[t & 0x3FFF] = 0;
		else if (t & 0x4000)
			r->vpc_dirty[t & 0x3FFF] = 0;
		else
			r->is_dirty[t] = 0;
	}
	r->ndirty = 0;
	r->overflow = 0;
}

// One method write. Returns 1 at the end of a draw (BEGIN_END 0).
static inline int rd_write(RdRegs* r, uint32_t m, uint32_t v)
{
	if (m >= 0x0b80 && m < 0x0c00)
	{
		const uint32_t i = r->vpi_ptr++ & (RD_VPI * 4 - 1);
		r->vpi[i] = v;
		r->vpi_set[i >> 2] = 1;
		rd_mark(r, 0x8000 | (i >> 2), &r->vpi_dirty[i >> 2]);
		return 0;
	}
	if (m >= 0x1f00 && m < 0x1f80)
	{
		const uint32_t i = r->vpc_ptr++ & (RD_VPC * 4 - 1);
		r->vpc[i] = v;
		r->vpc_set[i >> 2] = 1;
		rd_mark(r, 0x4000 | (i >> 2), &r->vpc_dirty[i >> 2]);
		return 0;
	}
	switch (m)
	{
	case 0x1e9c: r->vpi_ptr = v * 4; return 0;
	case 0x1efc: r->vpc_ptr = v * 4; return 0;
	case 0x1fd8: r->n_inval++; return 0;
	case 0x1714: r->n_vinval++; return 0;
	case 0x1814:
		if (r->nbatch++ == 0)
			r->batch_first = v & 0xFFFFFF;
		r->batch_count += (v >> 24) + 1;
		return 0;
	case 0x1808:
		if (v)
		{
			r->prim = v;
			r->nbatch = r->batch_first = r->batch_count = 0;
			return 0;
		}
		r->n_draws++;
		return 1;
	default:
		break;
	}
	if (rd_event(m))
		return 0;
	const uint32_t k = (m >> 2) & (RD_REGS - 1);
	r->reg[k] = v;
	r->set[k] = 1;
	rd_mark(r, k, &r->is_dirty[k]);
	return 0;
}

// The words [*pp, end) of a plain stream (no jumps). Stops after a draw end when stop_at_draw: returns 1 there.
// A header whose data runs past end, or a jump, sets err.
static inline int rd_stream(RdRegs* r, const uint32_t** pp, const uint32_t* end, int stop_at_draw)
{
	const uint32_t* p = *pp;
	while (p < end)
	{
		const uint32_t h = *p++;
		if ((h & 0xE0000003u) == 0x20000000u || (h & 3) == 1 || (h & 3) == 2 || h == 0x00020000u)
		{
			r->err = 1;
			break;
		}
		const uint32_t n = (h >> 18) & 0x7FF, m = h & 0x1FFC, sub = (h >> 13) & 7;
		const int ni = (h & 0x40000000u) != 0;
		if ((uint32_t)(end - p) < n)
		{
			r->err = 2;
			p = end;
			break;
		}
		int ended = 0;
		for (uint32_t k = 0; k < n; k++)
		{
			const uint32_t v = *p++;
			if (sub == 0)
				ended |= rd_write(r, ni ? m : m + 4 * k, v);
		}
		if (ended && stop_at_draw)
		{
			*pp = p;
			return 1;
		}
	}
	*pp = p;
	return 0;
}

// The address of an IO offset (a jump or call target), or 0.
typedef const uint32_t* (*RdAddr)(void* ctx, uint32_t offset);

// A FIFO as the RSX reads it: from *pp up to stop, through JUMP (old and new forms), CALL and RETURN (one level).
// At most budget words; past it err 3. Stops after a draw end when stop_at_draw: returns 1 there.
typedef struct
{
	const uint32_t* ret;
	uint32_t in_call;
} RdCall;

static inline int rd_fifo(RdRegs* r, const uint32_t** pp, const uint32_t* stop, RdAddr addr, void* actx, RdCall* cs,
                          uint32_t budget, int stop_at_draw)
{
	const uint32_t* p = *pp;
	while (p != stop)
	{
		if (!budget--)
		{
			r->err = 3;
			break;
		}
		const uint32_t h = *p;
		if ((h & 0xE0000003u) == 0x20000000u || (h & 3) == 1)
		{
			const uint32_t* t = addr(actx, (h & 3) == 1 ? h & ~3u : h & 0x1FFFFFFCu);
			if (!t)
			{
				r->err = 4;
				break;
			}
			p = t;
			continue;
		}
		if ((h & 3) == 2)
		{
			const uint32_t* t = addr(actx, h & ~3u);
			if (!t || cs->in_call)
			{
				r->err = 5;
				break;
			}
			cs->ret = p + 1;
			cs->in_call = 1;
			p = t;
			continue;
		}
		if (h == 0x00020000u)
		{
			if (!cs->in_call)
			{
				r->err = 6;
				break;
			}
			cs->in_call = 0;
			p = cs->ret;
			continue;
		}
		const uint32_t n = (h >> 18) & 0x7FF, m = h & 0x1FFC, sub = (h >> 13) & 7;
		const int ni = (h & 0x40000000u) != 0;
		p++;
		int ended = 0;
		for (uint32_t k = 0; k < n; k++)
		{
			const uint32_t v = *p++;
			if (sub == 0)
				ended |= rd_write(r, ni ? m : m + 4 * k, v);
		}
		if (ended && stop_at_draw)
		{
			*pp = p;
			return 1;
		}
	}
	*pp = p;
	return 0;
}

// Compares b's registers and memories with a's where either changed since its rd_clean(): report(what, index, a, b)
// per difference (what: 0 register, 1 VP word, 2 VP constant, 3 set in one only); then both are clean and b = a.
typedef void (*RdReport)(void* ctx, uint32_t what, uint32_t index, uint32_t a, uint32_t b);

static inline uint32_t rd_compare(RdRegs* a, RdRegs* b, RdReport report, void* ctx)
{
	uint32_t bad = 0;
	for (int side = 0; side < 2; side++)
	{
		const RdRegs* d = side ? b : a;
		for (uint32_t i = 0; i < d->ndirty; i++)
		{
			const uint32_t t = d->dirty[i];
			if (t & 0xC000)
			{
				const uint32_t slot = t & 0x3FFF, vp = (t & 0x8000) != 0;
				uint32_t* wa = vp ? &a->vpi[slot * 4] : &a->vpc[slot * 4];
				uint32_t* wb = vp ? &b->vpi[slot * 4] : &b->vpc[slot * 4];
				const uint8_t sa = vp ? a->vpi_set[slot] : a->vpc_set[slot];
				const uint8_t sb = vp ? b->vpi_set[slot] : b->vpc_set[slot];
				if (sa != sb)
				{
					report(ctx, 3, (vp ? 0x10000u : 0x20000u) | slot, sa, sb);
					bad++;
				}
				for (int k = 0; k < 4; k++)
					if (wa[k] != wb[k])
					{
						report(ctx, vp ? 1 : 2, slot * 4 + k, wa[k], wb[k]);
						wb[k] = wa[k];
						bad++;
					}
				if (vp)
					b->vpi_set[slot] = sa;
				else
					b->vpc_set[slot] = sa;
				continue;
			}
			if (a->set[t] != b->set[t])
			{
				report(ctx, 3, t * 4, a->set[t], b->set[t]);
				b->set[t] = a->set[t];
				bad++;
			}
			if (a->reg[t] != b->reg[t])
			{
				report(ctx, 0, t * 4, a->reg[t], b->reg[t]);
				b->reg[t] = a->reg[t];
				bad++;
			}
		}
	}
	if (a->overflow || b->overflow)
	{
		// Too many writes for the lists: a whole compare instead.
		for (uint32_t t = 0; t < RD_REGS; t++)
			if (a->set[t] != b->set[t] || (a->set[t] && a->reg[t] != b->reg[t]))
			{
				report(ctx, 0, t * 4, a->reg[t], b->reg[t]);
				b->reg[t] = a->reg[t];
				b->set[t] = a->set[t];
				bad++;
			}
		if (memcmp(a->vpi, b->vpi, sizeof(a->vpi)) || memcmp(a->vpc, b->vpc, sizeof(a->vpc)))
		{
			report(ctx, 1, 0xFFFF, 0, 0);
			memcpy(b->vpi, a->vpi, sizeof(a->vpi));
			memcpy(b->vpc, a->vpc, sizeof(a->vpc));
			bad++;
		}
	}
	rd_clean(a);
	rd_clean(b);
	return bad;
}
