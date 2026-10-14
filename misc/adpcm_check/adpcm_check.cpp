// OPTIMIZATION_PLAN 6.9 B PC check (WSL): the caique model's ADPCM Loop and ADPCM Stream rows, one voice per case,
// against a decoder written from Flycast's rules (3f70e47ad), on each render path. Usage: adpcm_check seed cases
#include "aica_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using caique::AicaModel;

static uint64_t g_rng;
static uint32_t rnd(uint32_t n)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return (uint32_t)(g_rng >> 11) % n;
}

static const int32_t k_qs[8] = {0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266};
static int32_t ref_adpcm(uint32_t nib, int32_t prev, int32_t& q)
{
	const uint32_t d = nib & 7;
	int32_t rv = (q >> 3) + ((d & 1) ? q >> 2 : 0) + ((d & 2) ? q >> 1 : 0) + ((d & 4) ? q : 0);
	if (rv > 0x7FFF) rv = 0x7FFF;
	rv = (nib & 8) ? prev - rv : prev + rv;
	q = (q * k_qs[d]) >> 8;
	q = q < 127 ? 127 : q > 24576 ? 24576 : q;
	return rv < -32768 ? -32768 : rv > 32767 ? 32767 : rv;
}

// The reference voice: the position walk of a slot with no pitch LFO, and the decoder state as Flycast keeps it
struct Ref
{
	uint32_t fmt, lpctl, lsa, lea, inc, sa;
	bool restore, stream;   // the rows ADPCM Loop RESTORE and ADPCM Stream FIXED
	const uint8_t* ram;
	uint32_t ca, step;
	int32_t q, s0, s1, sq, ss;
	bool armed, ad_in, en, clr;
	uint32_t laps, differ;   // loop laps; laps where the kept state is not the reset state
	uint32_t s_old, s_new;   // stream laps whose LEA is no multiple of 4: with the old rule, with Flycast's

	uint32_t nib(uint32_t p) const { return (ram[(sa + (p >> 1)) & (AicaModel::RAM_SIZE - 1)] >> ((p & 1) * 4)) & 0xF; }

	// Flycast: the first decode of LSA keeps the state; each later one of a PCMS 2 voice loads it. RESET: the old rule.
	void decode(uint32_t p, bool lap, bool last)
	{
		if (p == lsa)
		{
			if (!ad_in) { ad_in = true; sq = q; ss = s0; }
			else if (fmt == 2 && restore) { q = sq; s0 = ss; }
		}
		if (lap && fmt == 2 && !restore) { q = 127; s0 = 0; }
		if (lap && fmt == 2 && (sq != 127 || ss != 0)) differ++;
		s0 = ref_adpcm(nib(p), s0, q);
		int32_t qq = q;
		s1 = last ? ref_adpcm(nib(p + 1), s0, qq) : 0;
	}

	void key_on()
	{
		ca = step = 0;
		q = 127;
		s0 = 0;
		armed = ad_in = clr = false;
		en = true;
		decode(0, false, true);
	}

	void sample()
	{
		if (clr) { clr = false; ca = 0; }
		if (!en) return;
		step += inc;
		uint32_t ip = step >> 14;
		step &= 0x3FFF;
		while (ip > 0)
		{
			ip--;
			uint32_t p = (ca + 1) & 0xFFFF;
			const uint32_t pt = fmt == 3 ? p & ~3u : p;
			const bool was = armed;
			bool lap = false;
			if (p >= lsa) armed = true;
			if (pt >= lea && was)
			{
				// OLD: the model's CA -= LEA - LSA. FIXED: Flycast's rule for a stream, the lap goes back to LSA.
				if (fmt == 3 && lpctl && (lea & 3)) (stream ? s_new : s_old)++;
				p = fmt == 3 && stream ? lsa : (p - (lea - lsa)) & 0xFFFF;
				if (!lpctl) { ca = (ca + 1) & 0xFFFF; en = false; clr = true; return; }   // a one-shot end: no decode
				lap = true;
				laps++;
			}
			ca = p;
			decode(p, lap, ip == 0);
		}
	}
};

static int32_t g_mix[4 * AicaModel::ADV_MAX] __attribute__((aligned(16)));
static uint64_t g_cmp, g_laps, g_differ, g_cases[4], g_ends, g_live, g_notes, g_s_old, g_s_new;

// n samples on path 0 run(), 1 advance() in chunks, 2 advance() per sample, 3 advance() with the DSP pass (A3)
static void run_model(AicaModel* m, int path, uint32_t n)
{
	if (path == 0)
		for (uint32_t i = 0; i < n; i++) m->step();
	else
	{
		m->advance(n, true, g_mix);
		for (uint32_t i = 0; path == 3 && i < n; i++) m->dsp_sample(i);
	}
}

// One note of slot ch, from its key-on. again: the slot played a note before, so the key-off comes first.
static bool one_note(AicaModel* m, int path, int mode, int ch, bool again, uint32_t seed, uint32_t idx)
{
	Ref r;
	memset(&r, 0, sizeof(r));
	r.ram = m->ram;
	r.restore = m->adpcm_restore;
	r.stream = m->stream_lsa;
	r.fmt = rnd(4) ? 2 : 3;
	r.lpctl = rnd(8) ? 1 : 0;
	r.lea = 2 + rnd(rnd(3) ? 400 : 6000);
	const uint32_t k = rnd(again ? 2 : 6);   // a second note loops from 0 half the time: nothing of the first may stay
	r.lsa = k == 0 ? 0 : k == 1 ? 1 : k == 2 ? r.lea - 1 : rnd(r.lea);
	r.sa = rnd(0x1F0000);
	const int oct = (int)rnd(7) - 3;
	const uint32_t fns = rnd(0x400);
	r.inc = oct + 4 >= 0 ? (1024 + fns) << (oct + 4) : (1024 + fns) >> -(oct + 4);
	for (uint32_t a = 0; a < 0x9000; a++) m->ram[(r.sa + a) & (AicaModel::RAM_SIZE - 1)] = (uint8_t)rnd(256);

	const uint32_t b = 0x80 * ch;
	if (again)
	{
		m->write(b + 0x00, (m->r(b) & 0x07FF) | 0x8000);
		run_model(m, path, 1);
	}
	m->write(b + 0x04, r.sa & 0xFFFF);
	m->write(b + 0x08, r.lsa);
	m->write(b + 0x0C, r.lea);
	m->write(b + 0x10, 31);                                  // AR 31, D1R 0, D2R 0: the voice holds its level
	m->write(b + 0x14, 15u << 10 | (rnd(4) ? 0 : 1u) << 14);   // KRS off, LPSLNK now and then
	m->write(b + 0x18, ((uint32_t)oct & 0xF) << 11 | fns);
	m->write(b + 0x24, 15u << 8);
	m->write(b + 0x28, rnd(2) ? 0x20 : 0x04);                // LPOFF, or the filter with Q 4
	m->write(b + 0x00, (r.sa >> 16) | r.fmt << 7 | r.lpctl << 9 | 0x4000 | 0x8000);

	run_model(m, path, 1);   // the key pass ends this sample (a key-on needs the slot in release)
	run_model(m, path, 1);   // the key-on sample decodes position 0
	r.key_on();
	const uint32_t total = 200 + rnd(rnd(3) ? 3000 : 20000);
	for (uint32_t t = 0; t < total;)
	{
		if (mode == 4 && rnd(50) == 0)
		{
			m->adpcm_restore = r.restore = rnd(2) != 0;
			g_live++;
		}
		if (rnd(8) == 0)   // a KYONEX of another slot with no key event: advance() takes this voice's next sample one by one
			m->write(0x80 * ((ch + 1 + (int)rnd(63)) & 63), 0x8000);
		const uint32_t round = path == 3 ? (uint32_t)AicaModel::DSP_ROUND : (uint32_t)AicaModel::ADV_MAX;
		const uint32_t n = path == 0 || path == 2 ? 1 : 1 + rnd(round);
		run_model(m, path, n);
		for (uint32_t i = 0; i < n; i++) r.sample();
		t += n;
		const caique::Slot& c = m->slot[ch];
		g_cmp++;
		if (c.CA != r.ca || c.step != r.step || c.s0 != r.s0 || c.s1 != r.s1 || c.adpcm_quant != r.q || c.enabled != r.en)
		{
			printf("MISMATCH seed %u case %u path %d mode %d (restore %d stream %d) PCMS %u LPCTL %u LSA %u LEA %u inc %u at sample %u:"
			       " model CA %u step %u s0 %d s1 %d quant %d en %d | reference CA %u step %u s0 %d s1 %d quant %d en %d\n",
			       seed, idx, path, mode, (int)r.restore, (int)r.stream, r.fmt, r.lpctl, r.lsa, r.lea, r.inc, t, c.CA, c.step, c.s0, c.s1,
			       c.adpcm_quant, (int)c.enabled, r.ca, r.step, r.s0, r.s1, r.q, (int)r.en);
			return false;
		}
	}
	g_laps += r.laps;
	g_differ += r.differ;
	g_s_old += r.s_old;
	g_s_new += r.s_new;
	g_ends += !r.en;
	return true;
}

static bool one_case(uint32_t seed, uint32_t idx)
{
	AicaModel* m = new AicaModel();
	const int path = (int)rnd(4), ch = (int)rnd(64);
	const int mode = (int)rnd(5);   // 0, 1 RESET; 2, 3 RESTORE; 4 the row changes while the voice plays
	m->no_dsp = path != 3;
	m->adpcm_restore = mode == 2 || mode == 3;
	m->stream_lsa = rnd(2) != 0;   // set before boot on the PS3: no change while a voice plays
	m->write(0x2800, 0x000F);
	const uint32_t notes = 1 + rnd(2);
	for (uint32_t i = 0; i < notes; i++)
		if (!one_note(m, path, mode, ch, i != 0, seed, idx))
			return false;
	g_cases[path]++;
	g_notes += notes - 1;
	delete m;
	return true;
}

int main(int argc, char** argv)
{
	const uint32_t seed = argc > 1 ? (uint32_t)atoi(argv[1]) : 1, cases = argc > 2 ? (uint32_t)atoi(argv[2]) : 300;
	g_rng = 0x9E3779B97F4A7C15ull * (seed + 1);
	for (uint32_t i = 0; i < cases; i++)
		if (!one_case(seed, i))
			return 1;
	// A run with no lap that keeps a state, no one-shot end, no second note or no live change tests too little: it fails
	const bool thin = !g_differ || !g_ends || !g_live || !g_notes || !g_cases[0] || !g_cases[1] || !g_cases[2] || !g_cases[3] ||
	                  !g_s_old || !g_s_new;
	printf("seed %u: %s: %u cases (run() %llu, advance %llu, advance by 1 %llu, with the DSP %llu), %llu compares, %llu laps"
	       " (%llu with a kept state), %llu one-shot ends, %llu second notes, %llu live changes, stream laps off 4: %llu old"
	       " rule, %llu fixed\n",
	       seed, thin ? "TOO THIN" : "OK", cases, (unsigned long long)g_cases[0], (unsigned long long)g_cases[1],
	       (unsigned long long)g_cases[2], (unsigned long long)g_cases[3], (unsigned long long)g_cmp,
	       (unsigned long long)g_laps, (unsigned long long)g_differ, (unsigned long long)g_ends, (unsigned long long)g_notes,
	       (unsigned long long)g_live, (unsigned long long)g_s_old, (unsigned long long)g_s_new);
	return thin ? 1 : 0;
}
