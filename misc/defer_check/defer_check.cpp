// Bundle 21 (WSL): ps3/ps3_aica_defer.h, the stores SPE AICA Defer holds, against a plain model. Random runs of HOLD
// SYNCs, stores and SPU holds: no store goes out before its SYNC, the order and the final RAM are the plain ones.
#include "ps3/ps3_aica_defer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const u32 RAM = 1 << 16;
static u8 g_ram[RAM], g_ref[RAM];
static u64 g_rng = 1;
static u32 pick(u32 n)
{
	g_rng = g_rng * 6364136223846793005ull + 1442695040888963407ull;
	return (u32)(g_rng >> 33) % n;
}

struct Ref { u32 addr, val, size, k; };
static std::vector<Ref> g_flight;   // the model: stores in flight, oldest first
static u32 g_written, g_fail, g_checks;
static u32 g_held;                  // the SPU's last HOLD SYNC for the store callback

static void put(u8* m, u32 addr, u32 val, u32 size)
{
	if (size == 1)
		m[addr ^ 3] = (u8)val;
	else if (size == 2)
		*(u16*)&m[addr ^ 2] = (u16)val;
	else
		*(u32*)&m[addr] = val;
}

static void check(bool ok, const char* what, u32 a, u32 b)
{
	g_checks++;
	if (!ok && g_fail++ < 10)
		printf("FAIL %s: %u %u\n", what, a, b);
}

static DeferQ q;

int main(int argc, char** argv)
{
	const u32 seeds = argc > 1 ? (u32)atoi(argv[1]) : 200;
	for (u32 seed = 1; seed <= seeds; seed++)
	{
		g_rng = 0x9E3779B97F4A7C15ull * seed;
		defer_init(q);
		g_flight.clear();
		memset(g_ram, 0, RAM);
		memset(g_ref, 0, RAM);
		u32 held = 0;
		const u32 span = pick(2) ? RAM : 512;   // a small span makes overlaps and covers hits common
		for (u32 step = 0; step < 4000; step++)
		{
			const u32 r = pick(100);
			if (r < 8)   // a HOLD SYNC; the stores after it wait for it
				defer_sync(q);
			else if (r < 60 && !defer_full(q))
			{
				const u32 size = pick(3) == 0 ? 1 : pick(2) ? 4 : 2;
				const u32 addr = pick(span - 4) & ~(size - 1);
				const u32 val = pick(0xFFFFFFFFu);
				put(g_ref, addr, val, size);   // the plain model writes at once
				// As arm_spe_defer: with no HOLD SYNC still to reach, nothing is in flight and the store goes now
				if (held == q.k_pushed)
				{
					check(!defer_busy(q), "stores in flight past the last HOLD SYNC", q.head - q.tail, held);
					put(g_ram, addr, val, size);
					continue;
				}
				defer_add(q, addr, val, size);
				g_flight.push_back({ addr, val, size, q.k_pushed });
			}
			else if (r < 80 && held < q.k_pushed)   // the SPU reaches its next HOLD SYNC
			{
				held++;
				g_held = held;
				const u32 n0 = (u32)g_flight.size();
				const u32 n = defer_apply(q, held, [&](u32 a, u32 v, u32 s) {
					check(!g_flight.empty(), "a store with none in flight", a, 0);
					if (g_flight.empty())
						return;
					const Ref f = g_flight.front();
					check(f.addr == a && f.val == v && f.size == s, "store order", f.addr, a);
					check((s32)(f.k - g_held) <= 0, "a store before its HOLD SYNC", f.k, g_held);
					g_flight.erase(g_flight.begin());
					put(g_ram, a, v, s);
					g_written++;
				});
				// Every store up to this SYNC went out, and no later one.
				u32 left = 0;
				for (const Ref& f : g_flight)
					left += (s32)(f.k - held) <= 0;
				check(left == 0, "a store left behind", left, held);
				check(n0 - (u32)g_flight.size() == n, "the count", n0 - (u32)g_flight.size(), n);
			}
			else   // covers: never false while a store in flight overlaps the bytes
			{
				const u32 size = 1 + pick(4), addr = pick(span - 4);
				bool real = false;
				for (const Ref& f : g_flight)
					real |= addr <= f.addr + f.size - 1 && addr + size - 1 >= f.addr;
				check(!real || defer_covers(q, addr, size), "covers missed an overlap", addr, size);
				check(defer_busy(q) == !g_flight.empty(), "busy", defer_busy(q), (u32)g_flight.size());
				check(defer_covers(q, addr, size) || !real, "covers", addr, size);
				if (g_flight.empty())
					check(!defer_covers(q, addr, size), "covers with nothing in flight", addr, size);
			}
		}
		// The SPU reaches every SYNC: all stores out, the RAM is the plain model's.
		while (held < q.k_pushed)
		{
			held++;
			g_held = held;
			defer_apply(q, held, [&](u32 a, u32 v, u32 s) {
				if (!g_flight.empty())
					g_flight.erase(g_flight.begin());
				put(g_ram, a, v, s);
				g_written++;
			});
		}
		check(!defer_busy(q) && g_flight.empty(), "stores left at the end", q.head - q.tail, (u32)g_flight.size());
		check(q.lo > q.hi, "range kept after the last store", q.lo, q.hi);
		check(!memcmp(g_ram, g_ref, RAM), "RAM differs from the plain model", seed, 0);
	}
	// A full table: DEFER_MAX stores in flight, the next one must see it.
	defer_init(q);
	defer_sync(q);
	for (u32 i = 0; i < DEFER_MAX; i++)
	{
		check(!defer_full(q), "full too early", i, 0);
		defer_add(q, i * 4, i, 4);
	}
	check(defer_full(q), "not full at DEFER_MAX", q.head - q.tail, DEFER_MAX);
	printf("%s: defer_check, %u checks, %u stores written, %u failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_written,
	       g_fail);
	return g_fail != 0;
}
