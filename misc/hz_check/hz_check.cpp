// OPTIMIZATION_PLAN 6.6 PC check (WSL): the control twin's hz_mark() and hz_hit() against the wave RAM bytes a second
// model's render pass reads: each byte read in the window is marked and hit, few others are. Usage: hz_check seed samples
#include "aica_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static const uint32_t RAM = 2u << 20;
static uint8_t g_ram[RAM];                  // DC order
static std::vector<uint32_t>* g_rec;        // the reads of the sample that runs

uint8_t caique_rd8(uint32_t a)
{
	if (g_rec) g_rec->push_back(a);
	return g_ram[a];
}
uint16_t caique_rd16(uint32_t a)
{
	if (g_rec) { g_rec->push_back(a); g_rec->push_back(a + 1); }
	return (uint16_t)(g_ram[a] | g_ram[(a + 1) & (RAM - 1)] << 8);
}
const uint8_t* caique_line(uint32_t) { abort(); }   // step() reads through rd8 / rd16 only
uint16_t caique_dsp_rd16(uint32_t, uint32_t, uint32_t) { return 0; }
void caique_dsp_wr16(uint32_t, uint32_t, uint32_t, uint16_t) {}

static uint64_t g_rng;
static uint32_t rnd(uint32_t n)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return (uint32_t)(g_rng >> 11) % n;
}

static caique::AicaModel *F, *W;   // full model (render, sample by sample) and the control twin (catches up)
static uint32_t g_t;               // samples F has run

static void wr(uint32_t off, uint32_t v)
{
	F->write(off, v);
	W->advance(g_t - (uint32_t)W->samples, false, 0);
	W->write(off, v);
}

static bool g_on[64];

static void program_voice(int ch)
{
	const uint32_t b = 0x80 * ch, fmt = rnd(10) < 4 ? 0 : rnd(3) == 0 ? 1 : 2 + rnd(2);
	const uint32_t sa = rnd(30) == 0 ? RAM - 1 - rnd(4000) : rnd(RAM);   // some near the wave RAM end
	uint32_t lsa = rnd(2000), lea = lsa + 50 + rnd(rnd(4) ? 3000 : 60000);
	if (lea > 0xFFFF) lea = 0xFFFF;
	if (rnd(25) == 0) { lsa = rnd(0x10000); lea = rnd(0x10000); }   // LEA at or below LSA too
	wr(b + 0x00, (sa >> 16) | fmt << 7 | (rnd(4) ? 1u << 9 : 0));    // LPCTL mostly
	wr(b + 0x04, sa & 0xFFFF);
	wr(b + 0x08, lsa);
	wr(b + 0x0C, lea);
	wr(b + 0x10, 31 | rnd(8) << 6 | rnd(4) << 11);                   // fast attack, slow decays
	wr(b + 0x14, (rnd(6) ? rnd(8) : 20 + rnd(11)) | rnd(16) << 5 | 15 << 10);
	const uint32_t r = rnd(100), oct = r < 30 ? 0 : r < 55 ? 0xF : r < 70 ? 0xE : r < 82 ? 1 : r < 90 ? 2 : r < 95 ? 0xC : 3 + rnd(5);
	wr(b + 0x18, oct << 11 | rnd(0x400));
	wr(b + 0x1C, rnd(4) == 0 ? (rnd(4) << 8 | rnd(32) << 10 | (1 + rnd(7)) << 5) : 0);   // PLFO: all waveforms, noise too
	wr(b + 0x20, 0);
	wr(b + 0x24, 0x0F00);
	wr(b + 0x28, rnd(8) == 0 ? 0x20 : 0);
}

static void burst()
{
	for (uint32_t n = 1 + rnd(6); n; n--)
	{
		const int ch = (int)rnd(64);
		const uint32_t b = 0x80 * ch, k = rnd(10);
		if (k < 5)
		{
			program_voice(ch);
			wr(b, (F->r(b) & 0x47FF) | 0x4000);
			g_on[ch] = true;
		}
		else if (k < 7)
		{
			wr(b, F->r(b) & 0x07FF);   // key off
			g_on[ch] = false;
		}
		else if (k < 9)
			wr(b + 0x18, (F->r(b + 0x18) & 0x7800) | rnd(0x400));
		else
			wr(b + (rnd(2) ? 0x08 : 0x0C), rnd(4) ? F->r(b + 0x0C) - rnd(40) : rnd(0x10000));   // a loop point moves
	}
	wr(0, (F->r(0) & 0x47FF) | 0x8000);   // KYONEX
}

// The slots whose sample data could hold byte a: for the report of a miss
static std::vector<uint16_t> g_ca;   // the full model's CA of each slot after each sample
static void who(uint32_t a, uint32_t smp)
{
	for (int ch = 0; ch < 64; ch++)
	{
		const caique::Slot& c = W->slot[ch];
		const uint16_t r0 = W->chr(ch, 0x00);
		const uint32_t fmt = (r0 >> 7) & 3, sa = ((uint32_t)(r0 & 0x7F) << 16) | W->chr(ch, 0x04);
		const uint32_t o = (a - sa) & (RAM - 1);
		if ((!c.enabled && !F->slot[ch].enabled) || o > 140000)
			continue;
		printf("    slot %d fmt %u lpctl %u sa %06X byte +%u (pos %u) CA %u step %u rate %u LSA %u LEA %u in_loop %d r1c %04X"
		       " | twin on %d, full model on %d CA %u\n", ch,
		       fmt, (r0 >> 9) & 1, sa, o, fmt == 0 ? o / 2 : fmt == 1 ? o : o * 2, c.CA, c.step, c.update_rate, c.LSA, c.LEA,
		       (int)c.in_loop, W->chr(ch, 0x1C), (int)c.enabled, (int)F->slot[ch].enabled, F->slot[ch].CA);
		if (smp)
			printf("      CA after samples %u..%u: %u %u %u\n", smp - 1, smp + 1, g_ca[(smp - 1) * 64 + ch], g_ca[smp * 64 + ch],
			       g_ca[(smp + 1) * 64 + ch]);
	}
}

// No playing slot has LEA at or below LSA or CA past LEA: such a slot gets every position marked (not tight)
static bool plain_walks()
{
	for (int ch = 0; ch < 64; ch++)
	{
		const caique::Slot& c = W->slot[ch];
		if (c.enabled && (c.LEA <= c.LSA || c.CA > c.LEA))
			return false;
	}
	return true;
}

// A playing slot had CA past LEA inside samples t0..t1-1 (its loop end moved below it: CA then steps back one loop
// length per position). Once that walk is over the twin cannot tell it happened: a known limit, not checked.
static bool wild_walk(uint32_t t0, uint32_t t1)
{
	for (int ch = 0; ch < 64; ch++)
	{
		const caique::Slot& c = W->slot[ch];
		if (!c.enabled || c.LEA <= c.LSA || c.CA > c.LEA)
			continue;   // not playing, or marked in full
		const uint32_t lea = ((W->chr(ch, 0x00) >> 7) & 3) == 3 ? (c.LEA + 3) & ~3u : c.LEA;
		for (uint32_t s = t0; s < t1; s++)
			if (g_ca[(size_t)s * 64 + ch] > lea)
				return true;
	}
	return false;
}

static uint64_t enabled_mask()
{
	uint64_t m = 0;
	for (int ch = 0; ch < 64; ch++)
		if (F->slot[ch].enabled) m |= 1ull << ch;
	return m;
}

int main(int argc, char** argv)
{
	const uint32_t seed = argc > 1 ? (uint32_t)atoi(argv[1]) : 1, total = argc > 2 ? (uint32_t)atoi(argv[2]) : 400000;
	g_rng = 0x9E3779B97F4A7C15ull * (seed + 1);
	for (uint32_t a = 0; a < RAM; a++) g_ram[a] = (uint8_t)rnd(256);
	F = new caique::AicaModel();
	W = new caique::AicaModel();
	F->no_dsp = W->no_dsp = true;   // the DSP reads no slot data

	std::vector<std::vector<uint32_t>> reads(total + 1);
	std::vector<uint64_t> mask(total + 1);
	g_ca.resize((size_t)(total + 2) * 64);
	static uint8_t map[1024];
	uint32_t next_burst = 0, last_burst = 0;
	uint32_t fwd_until = 0, fwd_from = 0;   // a forward window under test: F's reads in [fwd_from, fwd_until) must be on the map
	uint64_t tests = 0, skipped = 0, back_reads = 0, fwd_reads = 0, bad = 0, marked = 0, needed = 0, rnd_hits = 0, rnd_n = 0;
	uint64_t live_sum = 0, plain_tests = 0, wild = 0;
	bool fwd_plain = false;
	std::vector<uint8_t> need(8192);

	for (g_t = 0; g_t < total;)
	{
		if (g_t == next_burst && g_t >= fwd_until)
		{
			burst();
			last_burst = g_t;
			next_burst = g_t + 20 + rnd(rnd(3) ? 700 : 3000);
		}
		else if (g_t == next_burst)
			next_burst++;   // not inside a forward window: the map was made without these writes
		// a test: the sound SPU is `lag` samples behind the twin; the map must hold for `fw` samples more
		if (g_t >= fwd_until && rnd(30) == 0)
		{
			const uint32_t lag = 1 + (rnd(4) ? rnd(96) : rnd(600)), fw = 1 + rnd(256);
			// key events land one sample after KYONEX: the window starts after that
			if (g_t >= lag + 3 && g_t - lag >= last_burst + 3)
			{
				// a read of sample s comes from a slot that played before s: the sample before the window counts too
				bool same = true;
				for (uint32_t s = g_t - lag - 1; s < g_t && same; s++)
					same = mask[s] == mask[g_t - 1];
				W->advance(g_t - (uint32_t)W->samples, false, 0);
				if (!same)
					skipped++;   // a slot stopped in the window: its last bytes are not protected (known limit)
				else if (wild_walk(g_t - lag - 1, g_t))
					wild++;
				else
				{
					memset(map, 0, sizeof(map));
					W->hz_mark(map, lag + 8, fw);
					std::fill(need.begin(), need.end(), 0);
					for (uint32_t s = g_t - lag; s < g_t; s++)
						for (uint32_t a : reads[s])
						{
							a &= RAM - 1;
							back_reads++;
							need[a >> 8] = 1;
							const bool on_map = map[a >> 11] & (0x80 >> ((a >> 8) & 7));
							if ((!on_map || W->hz_hit(a, lag + 8) < 0) && bad++ < 4)
							{
								printf("  MISS back: t=%u lag=%u read at sample %u addr %06X map=%d hit=%d\n", g_t, lag, s, a,
								       (int)on_map, W->hz_hit(a, lag + 8));
								who(a, s);
							}
						}
					for (int k = 0; k < 64 && plain_walks(); k++)   // bytes nobody plays: few may hit
					{
						rnd_n++;
						rnd_hits += W->hz_hit(rnd(RAM), lag + 8) >= 0;
					}
					fwd_from = g_t;
					fwd_until = g_t + fw;
					if (next_burst < fwd_until) next_burst = fwd_until;
					tests++;
					fwd_plain = plain_walks();
					if (fwd_plain)
					{
						plain_tests++;
						live_sum += (uint64_t)__builtin_popcountll(mask[g_t - 1]);
					}
				}
			}
		}
		g_rec = &reads[g_t];
		F->step();
		g_rec = 0;
		mask[g_t] = enabled_mask();
		for (int ch = 0; ch < 64; ch++) g_ca[(size_t)g_t * 64 + ch] = (uint16_t)F->slot[ch].CA;
		if (g_t >= fwd_from && g_t < fwd_until)
		{
			for (uint32_t a : reads[g_t])
			{
				a &= RAM - 1;
				fwd_reads++;
				need[a >> 8] = 1;
				if (!(map[a >> 11] & (0x80 >> ((a >> 8) & 7))) && bad++ < 4)
				{
					printf("  MISS fwd: map of t=%u, read at sample %u addr %06X\n", fwd_from, g_t, a);
					who(a, 0);
				}
			}
			if (g_t + 1 == fwd_until && fwd_plain)
			{
				for (uint32_t p = 0; p < 8192; p++)
				{
					needed += need[p];
					marked += (map[p >> 3] >> (7 - (p & 7))) & 1;
				}
			}
		}
		g_t++;
	}
	const double ratio = needed ? (double)marked / (double)needed : 0.0, live = plain_tests ? (double)live_sum / plain_tests : 0.0;
	const double extra = plain_tests ? ((double)marked - (double)needed) / plain_tests / (live > 0 ? live : 1) : 0.0;
	// Tight: under 6 pages more per playing slot and test (page rounding, the 8-sample margin, PLFO), few random hits
	const bool loose = extra > 6.0 || rnd_hits * 50 > rnd_n;
	printf("seed %u: %s | tests %llu (skipped: %llu a slot stopped, %llu CA past LEA) | reads checked back %llu fwd %llu, missed %llu"
	       " | %llu tests with plain loops only: pages marked %llu, needed %llu (x%.2f, %.2f more per slot and test, %.1f slots)"
	       " | random bytes hit %llu of %llu\n",
	       seed, bad || loose || !tests ? "FAIL" : "PASS", (unsigned long long)tests, (unsigned long long)skipped,
	       (unsigned long long)wild,
	       (unsigned long long)back_reads, (unsigned long long)fwd_reads, (unsigned long long)bad,
	       (unsigned long long)plain_tests, (unsigned long long)marked,
	       (unsigned long long)needed, ratio, extra, live, (unsigned long long)rnd_hits, (unsigned long long)rnd_n);
	return bad || loose || !tests ? 1 : 0;
}
