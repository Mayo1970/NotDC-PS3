#pragma once
// Bundle 21, SPE AICA Defer: SH4 and G2 DMA stores into marked wave RAM wait here, not the SH4 thread. Each one belongs
// to a HOLD SYNC; once the ARM7 SPU stands at that SYNC, the stores go to wave RAM in order. misc/defer_check tests it.
#include "types.h"

#define DEFER_MAX 1024   // stores in flight, a power of two; a full table makes the next store wait

struct DeferStore
{
	u32 addr, val, k;   // k: the HOLD SYNC the store waits for
	u32 size;
};

struct DeferQ
{
	DeferStore e[DEFER_MAX];
	u32 head, tail;      // stores added, stores written
	u32 k_pushed;        // HOLD SYNCs sent
	u32 lo, hi;          // bytes the stores in flight cover; lo > hi: none
};

static inline void defer_init(DeferQ& q)
{
	q.head = q.tail = q.k_pushed = 0;
	q.lo = 1;
	q.hi = 0;
}

static inline bool defer_busy(const DeferQ& q) { return q.head != q.tail; }
static inline bool defer_full(const DeferQ& q) { return q.head - q.tail >= DEFER_MAX; }

// A store at addr..addr+size-1 overlaps one in flight: it must go after it, so it waits too (or a read waits for it).
static inline bool defer_covers(const DeferQ& q, u32 addr, u32 size)
{
	return q.head != q.tail && addr <= q.hi && addr + size - 1 >= q.lo;
}

// The next HOLD SYNC; the stores added after it wait for it.
static inline void defer_sync(DeferQ& q) { q.k_pushed++; }

static inline void defer_add(DeferQ& q, u32 addr, u32 val, u32 size)
{
	DeferStore& s = q.e[q.head++ & (DEFER_MAX - 1)];
	s.addr = addr;
	s.val = val;
	s.k = q.k_pushed;
	s.size = size;
	const u32 end = addr + size - 1;
	if (q.lo > q.hi)
	{
		q.lo = addr;
		q.hi = end;
		return;
	}
	if (addr < q.lo)
		q.lo = addr;
	if (end > q.hi)
		q.hi = end;
}

// The SPU stands at HOLD SYNC `held`: every store up to it goes out through store(addr, val, size), oldest first.
template<class F> static inline u32 defer_apply(DeferQ& q, u32 held, F store)
{
	u32 n = 0;
	while (q.tail != q.head)
	{
		const DeferStore& s = q.e[q.tail & (DEFER_MAX - 1)];
		if ((s32)(s.k - held) > 0)
			break;
		store(s.addr, s.val, s.size);
		q.tail++;
		n++;
	}
	if (q.tail == q.head)
	{
		q.lo = 1;
		q.hi = 0;
	}
	return n;
}
