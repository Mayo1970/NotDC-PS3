// Host-only check for P16 (OPTIMIZATION_POTENTIAL.md): the texture cache slot function under the probe, victim and
// aging rules of ps3/ps3_tex.cpp, on key patterns of the HW logs and on random keys. Not part of the PS3 build.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
typedef uint32_t u32;
typedef uint64_t u64;

// mutate.py swaps in an edited copy.
#ifndef SLOT_SOURCE
#define SLOT_SOURCE "ps3/ps3_tex_slot.h"
#endif
#include SLOT_SOURCE

#define CACHE_SLOTS (1u << TEX_SLOT_BITS)
#define CACHE_PROBE 8
#define CACHE_MAX_AGE 180

// IDEAL is the reference: an independent random slot per key, from a 64-bit mixer.
enum Mode { OLD, HASH, IDEAL };
static const char* const k_mode[] = { "OLD", "HASH", "IDEAL" };

static u64 mix64(u64 x)
{
	x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
	x ^= x >> 27; x *= 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

static u32 g_seed = 12345;
static u32 rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed; }
static u32 rnd(u32 n) { return (u32)(((u64)rnd() * n) >> 32); }

struct Key { u32 key0, key1; };

// The rules of ps3tex_bind and ps3tex_frame_begin without the decode: 8 probes from the slot, the first hit wins,
// else the last empty slot or the least recently used entry; the sweep frees 16 slots a frame after 180 frames.
struct Cache
{
	struct Entry { u32 key0, key1, name, last_frame; } e[CACHE_SLOTS] = {};
	Mode mode;
	u32 frame = 1, sweep = 0, next_name = 1;
	u64 misses = 0, hot = 0;

	explicit Cache(Mode m) : mode(m) {}

	u32 slot(const Key& k) const
	{
		if (mode == IDEAL)
			return (u32)(mix64(((u64)k.key0 << 32 | k.key1) ^ 0x5DEECE66Dull) >> (64 - TEX_SLOT_BITS));
		return tex_slot(k.key0, k.key1, mode == HASH);
	}

	void frame_begin()
	{
		frame++;
		for (u32 i = 0; i < CACHE_SLOTS / 64; i++)
		{
			Entry& x = e[sweep];
			if (x.name && frame - x.last_frame > CACHE_MAX_AGE)
				x = Entry();
			sweep = (sweep + 1) % CACHE_SLOTS;
		}
	}

	void bind(const Key& k)
	{
		const u32 s = slot(k);
		Entry* hit = 0;
		Entry* victim = 0;
		for (u32 i = 0; i < CACHE_PROBE; i++)
		{
			Entry& x = e[(s + i) % CACHE_SLOTS];
			if (x.name && x.key0 == k.key0 && x.key1 == k.key1)
			{
				hit = &x;
				break;
			}
			if (!victim || !x.name || (victim->name && x.last_frame < victim->last_frame))
				victim = &x;
		}
		if (!hit)
		{
			misses++;
			if (victim->name && frame - victim->last_frame <= 4)
				hot++;
			*victim = Entry{ k.key0, k.key1, next_name++, frame };
			hit = victim;
		}
		hit->last_frame = frame;
	}
};

// Binds the set in the same order every frame. Returns the misses after the first frame: a set that fits has none.
static u64 steady_misses(Mode m, const std::vector<Key>& keys, u32 frames = 24)
{
	Cache c(m);
	u64 first = 0;
	for (u32 f = 0; f < frames; f++)
	{
		c.frame_begin();
		for (const Key& k : keys)
			c.bind(k);
		if (f == 0)
			first = c.misses;
	}
	return c.misses - first;
}

// TCW and the TSP part of the key, as ps3tex_bind builds them (DC bit numbering on both hosts).
static u32 tcw(u32 addr, u32 fmt, u32 pal = 0, u32 scan = 0, u32 mip = 0)
{
	return ((addr >> 3) & 0x1FFFFF) | (pal << 21) | (scan << 26) | (fmt << 27) | (mip << 31);
}
static u32 tsp(u32 texu, u32 texv, u32 ignore_a = 0, u32 palctrl = 0, u32 mirror = 0)
{
	return (texu << 3) | texv | (ignore_a << 6) | (palctrl << 7) | (mirror << 9);
}

static int g_fail;
static void check(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		g_fail = 1;
}

static void report(const char* name, u64 m[3], u64 binds)
{
	printf("  %-34s steady misses of %8llu binds: OLD %8llu  HASH %6llu  IDEAL %6llu\n", name,
	       (unsigned long long)binds, (unsigned long long)m[OLD], (unsigned long long)m[HASH],
	       (unsigned long long)m[IDEAL]);
}

static void run_set(const std::vector<Key>& keys, u64 m[3], u64* binds, u32 frames = 24)
{
	for (int mode = OLD; mode <= IDEAL; mode++)
		m[mode] += steady_misses((Mode)mode, keys, frames);
	*binds += (u64)keys.size() * (frames - 1);
}

int main(int argc, char** argv)
{
	const u32 trials = argc > 1 ? (u32)atoi(argv[1]) : 200;

	// The contract: a slot below CACHE_SLOTS; OLD exactly the slot before P16, so OFF changes nothing.
	bool range_ok = true, old_ok = true;
	for (u32 i = 0; i < 4000000; i++)
	{
		const u32 k0 = rnd(), k1 = rnd(1024);
		range_ok &= tex_slot(k0, k1, true) < CACHE_SLOTS && tex_slot(k0, k1, false) < CACHE_SLOTS;
		old_ok &= tex_slot(k0, k1, false) == ((k0 * 2654435761u) ^ k1) % 1024;
	}
	check(range_ok, "every slot is below CACHE_SLOTS, both modes");
	check(old_ok, "OLD is the slot before P16");

	// Soul Calibur opening: 80 YUV tiles of 64x64 (8 KB each), back to back, drawn every frame.
	{
		u64 m[3] = {}, binds = 0, worst_hash = 0;
		for (u32 b = 0; b < 256; b++)
		{
			const u32 base = rnd(8u << 20 >> 5) << 5;
			std::vector<Key> keys;
			for (u32 i = 0; i < 80; i++)
				keys.push_back({ tcw((base + i * 8192) & 0x7FFFFF, 3, 0, 1), tsp(3, 3) });
			u64 one[3] = {};
			run_set(keys, one, &binds);
			worst_hash = one[HASH] > worst_hash ? one[HASH] : worst_hash;
			for (int k = 0; k < 3; k++)
				m[k] += one[k];
		}
		report("fmv 80 x 8 KB tiles, 256 bases", m, binds);
		check(m[OLD] * 2 > binds, "OLD misses most FMV tile binds (the HW logs)");
		check(m[HASH] == 0, "HASH: no FMV tile misses after the first frame");
	}

	// N textures of one size and format, a power of two apart, from 32 bytes to 1 MB.
	{
		const u32 counts[] = { 16, 32, 64, 88, 128 };
		bool ok = true;
		for (u32 n : counts)
		{
			u64 m[3] = {}, binds = 0;
			for (u32 lg = 5; lg <= 20; lg++)
			{
				if ((u64)n << lg > (8u << 20))
					continue;
				for (u32 b = 0; b < 16; b++)
				{
					const u32 base = rnd(8u << 20 >> 5) << 5;
					const u32 fmt = rnd(3);
					std::vector<Key> keys;
					for (u32 i = 0; i < n; i++)
						keys.push_back({ tcw((base + (i << lg)) & 0x7FFFFF, fmt), tsp(2 + b % 4, 2 + b % 4) });
					u64 one[3] = {};
					run_set(keys, one, &binds);
					if (one[HASH] && n <= 88)
					{
						printf("  HASH misses %llu: n=%u stride=%u base=%06X\n", (unsigned long long)one[HASH], n,
						       1u << lg, base);
						ok = false;
					}
					for (int k = 0; k < 3; k++)
						m[k] += one[k];
				}
			}
			char name[64];
			snprintf(name, sizeof(name), "strides n=%u, 32 B..1 MB, 16 bases", n);
			report(name, m, binds);
		}
		check(ok, "HASH: no stride set of up to 88 textures misses after the first frame");
	}

	// Palette textures: one image under many PalSelect values (pal4 and pal8), as in the Soul Calibur fight.
	{
		u64 m[3] = {}, binds = 0;
		for (u32 b = 0; b < 64; b++)
		{
			const u32 base = rnd(8u << 20 >> 5) << 5;
			std::vector<Key> p4, p8;
			for (u32 a = 0; a < 2; a++)
				for (u32 pal = 0; pal < 64; pal++)
					p4.push_back({ tcw(base + a * 8192, 5, pal), tsp(3, 3, 0, b & 3) });
			for (u32 a = 0; a < 32; a++)
				for (u32 bank = 0; bank < 4; bank++)
					p8.push_back({ tcw(base + a * 2048, 6, bank << 4), tsp(2, 2, 0, b & 3) });
			run_set(p4, m, &binds);
			run_set(p8, m, &binds);
		}
		report("palettes, 128 keys, 64 bases", m, binds);
		check(m[OLD] > 0, "OLD misses palette variants of one image");
		check(m[HASH] == 0, "HASH: no palette set misses after the first frame");
	}

	// One image under many TSP parts of the key: sizes, IgnoreTexA, mirror.
	{
		u64 m[3] = {}, binds = 0;
		for (u32 b = 0; b < 64; b++)
		{
			const u32 base = rnd(8u << 20 >> 5) << 5;
			std::vector<Key> keys;
			for (u32 a = 0; a < 8; a++)
				for (u32 v = 0; v < 16; v++)
					keys.push_back({ tcw(base + a * 8192, 1), tsp(v & 3, v >> 2, v & 1, 0, (v >> 3) & 1) });
			run_set(keys, m, &binds);
		}
		report("TSP variants, 128 keys, 64 bases", m, binds);
		check(m[HASH] == 0, "HASH: no TSP variant set misses after the first frame");
	}

	// Random sets: HASH must do no worse than an ideal random slot, beyond noise.
	{
		const u32 counts[] = { 64, 88, 128, 200, 300 };
		bool ok = true;
		for (u32 n : counts)
		{
			u64 m[3] = {}, binds = 0;
			for (u32 t = 0; t < trials; t++)
			{
				std::vector<Key> keys;
				for (u32 i = 0; i < n; i++)
				{
					const u32 fmt = rnd(7);
					keys.push_back({ tcw(rnd(8u << 20), fmt, fmt >= 5 ? rnd(64) : 0, rnd(2), rnd(2)),
					                 tsp(rnd(8), rnd(8), rnd(2), t & 3, rnd(8) == 0) });
				}
				run_set(keys, m, &binds);
			}
			char name[64];
			snprintf(name, sizeof(name), "random n=%u, %u sets", n, trials);
			report(name, m, binds);
			ok &= m[HASH] <= 2 * m[IDEAL] + binds / 100000;
		}
		check(ok, "HASH: random sets miss no more than twice the ideal slot");
	}

	printf("%s\n", g_fail ? "FAIL" : "PASS");
	return g_fail;
}
