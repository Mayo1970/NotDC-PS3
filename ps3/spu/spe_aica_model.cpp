// OPTIMIZATION_PLAN 5.13 A1/A1b/A2/A3: the caique model on the SPU, wave RAM through an LS cache, DSP pass recompiled.
// C entry points for spe_aica.c; no constructor runs on the SPU, so the model lives in raw storage.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include "aica_model.h"
#include "spe_aica.h"

#define TAG_ARAM 5
#define TAG_DSPREP 6
#define TAG_SND 7

static uint32_t s_aram_ea;
static uint64_t s_dbad_ea, s_isabad_ea, s_cdda_ea;

static void aram_fetch(uint8_t* dst, uint32_t line)
{
	mfc_get(dst, s_aram_ea + line, 128, TAG_ARAM, 0, 0);
	mfc_write_tag_mask(1 << TAG_ARAM);
	mfc_read_tag_status_all();
}

#define ARAM_WRITE_BACK
static void aram_store(const uint8_t* src, uint32_t line)
{
	mfc_put((volatile void*)src, s_aram_ea + line, 128, TAG_ARAM, 0, 0);
	mfc_write_tag_mask(1 << TAG_ARAM);
	mfc_read_tag_status_all();
}

#include "spe_aica_ram.h"
#include "spe_dsp_jit.h"

static void dsp_put(const void* ls, uint64_t ea, uint32_t size)
{
	mfc_put((volatile void*)ls, ea, size, TAG_DSPREP, 0, 0);
	mfc_write_tag_mask(1 << TAG_DSPREP);
	mfc_read_tag_status_all();
}

// The generated code runs from LS; spu_sync makes instruction fetch see the new words.
#define DJ_LS(p) ((uint32_t)(uintptr_t)(p))
#define DJ_CALL(ls) (((void (*)(void))(uintptr_t)(ls))(), 0)
#define DJ_SYNC() spu_sync()
#define DJ_TICKS() spu_read_decrementer()
#define DJ_REPORT(r, n) dsp_put(r, s_dbad_ea + (uint64_t)(n) * sizeof(AicaDspBad), sizeof(AicaDspBad))
#define DJ_ISA_REPORT(r) dsp_put(r, s_isabad_ea, sizeof(AicaDspIsaBad))
#include "spe_dsp_check.h"

static uint32_t s_prof[6], s_prof_t;   // decrementer ticks per section (1..5), slots rendered

void caique_prof(int point, uint32_t v)
{
	const uint32_t now = spu_read_decrementer();
	if (point)
		s_prof[point - 1] += s_prof_t - now;
	if (point == 1)
		s_prof[5] += v;
	s_prof_t = now;
}

#define SND_GET(ls, ea, n) mfc_get((volatile void*)(ls), (ea), (n), TAG_SND, 0, 0)
#define SND_PUT(ls, ea, n) mfc_put((volatile void*)(ls), (ea), (n), TAG_SND, 0, 0)
#define SND_PUTF(ls, ea, n) mfc_putf((volatile void*)(ls), (ea), (n), TAG_SND, 0, 0)
#define SND_WAIT() (mfc_write_tag_mask(1 << TAG_SND), mfc_read_tag_status_all())
#include "spe_aica_snd.h"

static uint8_t s_store[sizeof(caique::AicaModel)] __attribute__((aligned(128)));
static caique::AicaModel* const M = (caique::AicaModel*)s_store;
static uint32_t s_dsp_flags;

// 5.13 A2 sound mode: the CD-DA sector EXTS plays (index past the end: EXTS 0)
static bool s_snd;
static int16_t s_cdda[AICA_CDDA_WORDS] __attribute__((aligned(128)));
static uint32_t s_cdda_i = AICA_CDDA_WORDS, s_cdda_n;

typedef char snap_size_check[sizeof(caique::CtlSnap) == AICA_SNAP_BYTES ? 1 : -1];

static int32_t s_mix[4 * caique::AicaModel::ADV_MAX] __attribute__((aligned(16)));   // slot sums l, r per sample

// ── P20 G2: the LS ring mirrors wave RAM. DSP writes go back at each CHECK; a guest write comes as a RING record ──
#define TAG_RINGX 8
static uint32_t s_flags;               // AICA_SETUP_RING / RING64 while the SPU plays with the DSP
static bool s_ring_ok, s_ring_any;     // the LS ring holds wave RAM at (s_ring_rbp, s_ring_bytes); a line is dirty
static uint32_t s_ring_rbp, s_ring_bytes, s_ring_in, s_ring_out;
static uint8_t s_ring_dirty[DSP_RING_BYTES / 128 / 8];   // bit 7 first: the DSP wrote that 128-byte ring line
#define RING_FN __attribute__((noinline, optimize("Os")))   // LS is short: small and out of line

// The LS ring is in DC byte order and wave RAM holds each 32-bit word reversed: one shuffle, both ways
RING_FN static void ring_swz(uint8_t* p, uint32_t n)
{
	const vec_uchar16 k = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12};
	for (vec_uchar16* q = (vec_uchar16*)p; n; n -= 16, q++)
		*q = spu_shuffle(*q, *q, k);
}

// Ring lines l0..l1-1 to or from wave RAM, one DMA per run: 16 KB at most, and no further than the wave RAM end
RING_FN static void ring_xfer(uint32_t l0, uint32_t l1, bool put)
{
	while (l0 < l1)
	{
		const uint32_t a = (s_ring_rbp + l0 * 128) & (ARAM_SIZE - 1);
		uint32_t n = (l1 - l0) * 128;
		if (n > ARAM_SIZE - a)
			n = ARAM_SIZE - a;
		if (n > 16384)
			n = 16384;
		uint8_t* p = dsp_ring + l0 * 128;
		if (put)
		{
			ring_swz(p, n);
			mfc_put(p, s_aram_ea + a, n, TAG_RINGX, 0, 0);
		}
		else
			mfc_get(p, s_aram_ea + a, n, TAG_RINGX, 0, 0);
		mfc_write_tag_mask(1 << TAG_RINGX);
		mfc_read_tag_status_all();
		ring_swz(p, n);
		l0 += n / 128;
	}
}

RING_FN static void ring_flush(void)
{
	if (!s_ring_any)
		return;
	const uint32_t lines = s_ring_bytes / 128;
	for (uint32_t l = 0; l < lines;)
	{
		if (!(l & 7) && !s_ring_dirty[l >> 3])
		{
			l += 8;
			continue;
		}
		if (!(s_ring_dirty[l >> 3] & (0x80 >> (l & 7))))
		{
			l++;
			continue;
		}
		uint32_t e = l + 1;
		while (e < lines && (s_ring_dirty[e >> 3] & (0x80 >> (e & 7))))
			e++;
		ring_xfer(l, e, true);
		s_ring_out += e - l;
		l = e;
	}
	memset(s_ring_dirty, 0, sizeof(s_ring_dirty));
	s_ring_any = false;
}

// The ring lines a round of n samples wrote. An MWT step writes word MADRS (+1) + MDEC_CT, MDEC_CT down from mdec0;
// ADREB adds the 12-bit signed ADRS (2048 words each way); a TABLE step has no MDEC_CT and can be past the ring.
RING_FN static void ring_mark(uint32_t mdec0, uint32_t n)
{
	const uint32_t rbl = s_ring_bytes / 2 - 1, lines = s_ring_bytes / 128;
	for (int s = 0; s < 128; s++)
	{
		const dsp_op_t& o = M->prog[s];
		if (!o.MWT)
			continue;
		uint32_t lo = M->r(0x3200 + 4 * o.MASA) + o.NXADR, cnt = 1;   // first word and word count
		if (!o.TABLE)
		{
			lo += mdec0 - (n - 1);
			cnt = n;
		}
		if (o.ADREB)
		{
			lo -= 2048;
			cnt += 4095;
		}
		if (o.TABLE && ((lo & 0xFFFF) > rbl || o.ADREB))
		{
			if (!o.ADREB)
				continue;   // past the LS ring: the line cache has that word
			lo = 0;         // a table word that moves with ADRS: any line
			cnt = rbl + 1;
		}
		lo &= rbl;
		uint32_t k = ((lo & 63) + cnt + 63) >> 6;   // lines from lo's, around the ring's end
		if (k > lines)
			k = lines;
		for (uint32_t l = lo >> 6; k; k--, l = (l + 1) & (lines - 1))
			s_ring_dirty[l >> 3] |= (uint8_t)(0x80 >> (l & 7));
		s_ring_any = true;
	}
}

// Before a sample runs: the LS ring holds the wave RAM of the ring the registers name, while a program is set
RING_FN static void ring_prepare(void)
{
	uint32_t rbp, bytes;
	const bool want = M->ring_range(rbp, bytes) && bytes <= DSP_RING_BYTES;
	if (s_ring_ok && (!want || rbp != s_ring_rbp || bytes != s_ring_bytes))
	{
		ring_flush();
		s_ring_ok = false;
		aram_invalidate();   // the line cache may hold these lines as they were before the flush
	}
	if (want && !s_ring_ok)
	{
		aram_invalidate();   // and DSP writes of a bigger ring or a table: they must be in wave RAM for the load
		s_ring_rbp = rbp;
		s_ring_bytes = bytes;
		ring_xfer(0, bytes / 128, false);
		s_ring_in += bytes / 128;
		s_ring_ok = true;
	}
}

// The block's wave RAM changes go back (P20 G2, G3); the cache drops so the next block sees new guest data.
static void am_flush(void)
{
	if (s_ring_ok)
		ring_flush();
	aram_invalidate();
}

// 5.13 A2 high voices: the slots run slot by slot, then per sample the DSP (A3, and the A1 check) and the output stage
static void to(uint32_t sample)
{
	const bool dsp = !M->no_dsp;
	const uint32_t cap = dsp ? caique::AicaModel::DSP_ROUND : caique::AicaModel::ADV_MAX;
	if ((s_flags & AICA_SETUP_RING) && (int32_t)(sample - (uint32_t)M->samples) > 0)
		ring_prepare();
	for (uint32_t n; (int32_t)(n = sample - (uint32_t)M->samples) > 0;)
	{
		if (n > cap)
			n = cap;
		M->advance(n, true, s_mix);
		for (uint32_t i = 0; i < n; i++)
		{
			if (s_cdda_i < AICA_CDDA_WORDS)
			{
				M->EXTS[0] = s_cdda[s_cdda_i];
				M->EXTS[1] = s_cdda[s_cdda_i + 1];
				s_cdda_i += 2;
			}
			else
				M->EXTS[0] = M->EXTS[1] = 0;
			if (dsp)
				M->dsp_sample(i);
			if (s_snd)
			{
				M->final_mix(s_mix[4 * i], s_mix[4 * i + 1]);
				snd_sample(M->outL, M->outR);
			}
			if (dsp)
				caique_prof(5, 0);
		}
		if (!dsp)
			caique_prof(5, 0);   // the output stage shows as "mix"
		if (s_ring_ok)
			ring_mark(M->adv_mdec0, n);
	}
}

extern "C" {

void am_init(const AicaSetup* su, uint64_t dbad_ea, uint64_t isabad_ea, uint64_t cdda_ea, volatile uint32_t* where)
{
	s_aram_ea = su->aram_ea;
	s_dbad_ea = dbad_ea;
	s_isabad_ea = isabad_ea;
	s_cdda_ea = cdda_ea;
	M->init();
	s_snd = su->snd != 0;
	M->no_dsp = s_snd && !su->snd_dsp;   // A2 plays without the DSP; A3 and the A1 check run it
	M->mono_half = (su->flags & AICA_SETUP_MONO) != 0;
	s_flags = s_snd && su->snd_dsp ? su->flags : 0;   // the ring goes to wave RAM only when this SPU is the sound
	if (s_snd)
		snd_init(su->ring_ea, su->ring_frames, su->wr_ea, su->rd_ea, su->w0);
	aram_invalidate();
	dj_init(M, M->no_dsp ? 0 : (int)(su->dsp_mode & AICA_DSPF_MODE), DJ_LS(&dspjit_rd), DJ_LS(&dspjit_wr), where);
	s_dsp_flags = (uint32_t)dj_mode;
}

// Boot self-test of the recompiler (before any log record). A failure turns it off for the session.
// P20 G3 starts after it: the test's random programs must not reach wave RAM.
__attribute__((cold)) void am_dsp_selftest(void)
{
	aram_wb = (s_flags & AICA_SETUP_RING64) != 0;
	if (!dj_mode)
		return;
	aram_wb = 0;
	uint32_t ran = 0;
	if (dj_isa_probe(16))
		s_dsp_flags |= AICA_DSPF_ISA;
	else if (dj_selftest(24, 16, &ran))
		s_dsp_flags |= AICA_DSPF_FAILED;
	else
		dj_bad = dj_checks = 0;
	s_dsp_flags |= AICA_DSPF_TESTED | (ran << 16);
	if (s_dsp_flags & (AICA_DSPF_ISA | AICA_DSPF_FAILED))
	{
		dj_mode = 0;
		s_dsp_flags &= ~AICA_DSPF_MODE;
	}
	M->reset();
	dj_tables_all();
	aram_wb = (s_flags & AICA_SETUP_RING64) != 0;
}

void am_reset(void)
{
	M->reset();
	aram_invalidate();
	memset(dsp_ring, 0, sizeof(dsp_ring));
	memset(s_ring_dirty, 0, sizeof(s_ring_dirty));
	s_ring_ok = s_ring_any = false;
	dj_tables_all();
}

void am_write(uint32_t sample, uint32_t off, uint32_t val)
{
	to(sample);
	M->write(off, val);
	dj_reg_written(off);
}

// Steps to the check sample and returns the snapshot hash. The cache is dropped so the next block sees new guest data.
uint32_t am_check(uint32_t sample, void* snap)
{
	to(sample);
	caique::CtlSnap* s = (caique::CtlSnap*)snap;
	M->snap(*s);
	am_flush();
	return caique::AicaModel::snap_hash(*s);
}

// Plan 6.6: a batch of records starts after a wait, so wave RAM may have changed under the cache. The bell's words:
// "SPE AICA Gain" and "ADPCM Loop" for the samples from here on; a new peak_seq starts a new peak window.
void am_batch(uint32_t gain, uint32_t peak_seq, uint32_t adpcm_loop)
{
	static uint32_t seq;
	aram_invalidate();
	M->out_gain = gain;
	M->adpcm_restore = adpcm_loop != 0;
	if (peak_seq != seq)
	{
		seq = peak_seq;
		M->peak = 0;
	}
}

// Plan 6.6 SYNC: the guest holds a wave RAM write until this SPU has played up to the write's sample.
void am_sync(uint32_t sample)
{
	to(sample);
	am_flush();
}

// P20 G2 RING: the guest wrote wave RAM lines lo..hi (address >> 7) in the ring. Its data wins over DSP writes not
// yet written back to those lines.
RING_FN void am_ring(uint32_t sample, uint32_t lo, uint32_t hi)
{
	to(sample);
	aram_drop(lo, hi);
	if (!s_ring_ok || hi < lo)
		return;
	const uint32_t all = ARAM_SIZE / 128, lines = s_ring_bytes / 128, base = s_ring_rbp / 128;
	uint32_t left = hi - lo + 1;
	if (left > all)
		left = all;
	for (uint32_t l = lo; left;)
	{
		const uint32_t r = (l - base) & (all - 1);
		uint32_t k = r < lines ? lines - r : all - r;   // ring lines from here, or the lines up to the ring's start
		if (k > left)
			k = left;
		if (r < lines)
		{
			for (uint32_t e = r; e < r + k; e++)
				s_ring_dirty[e >> 3] &= (uint8_t)~(0x80 >> (e & 7));
			ring_xfer(r, r + k, false);
			s_ring_in += k;
		}
		l += k;
		left -= k;
	}
}

// P20 G4 DSPRD: TEMP, MEMS, MIXS or EFREG as the guest reads it at this sample. AicaModel::read()'s rules for
// 0x4000-0x45BF, written out here: read() itself costs 1.3 KB of LS.
RING_FN uint32_t am_dsp_read(uint32_t sample, uint32_t off)
{
	to(sample);
	const uint32_t i = (off >> 3) & 0x7F, hi = off & 4;
	if (off < 0x4000 || off >= 0x45C0)
		return 0;
	if (off >= 0x4580)
		return (uint32_t)M->EFREG[(off - 0x4580) >> 2] & 0xFFFF;
	if (off >= 0x4500)   // the bank of this sample's parity, where the CPU's own writes go
	{
		const int32_t v = M->MIXS_bank[M->samples & 1][i & 15];
		return hi ? (v >> 4) & 0xFFFF : v & 0xF;
	}
	const int32_t v = off >= 0x4400 ? M->MEMS[i & 31] : M->TEMP[i];
	return hi ? (v >> 8) & 0xFFFF : v & 0xFF;
}

// clip left, clip right, ring lines in, ring lines out, cache lines out, peak: the AicaEcho order from clip on
void am_hz_stats(uint32_t* out)
{
	out[0] = M->clip[0];
	out[1] = M->clip[1];
	out[2] = s_ring_in;
	out[3] = s_ring_out;
	out[4] = aram_wb_n;
	out[5] = M->peak;
}

// A CD-DA sector (or silence) for EXTS from this sample on; the slot is free again once the record is done.
void am_cdda(uint32_t sample, uint32_t val)
{
	to(sample);
	s_cdda_n++;
	s_cdda_i = AICA_CDDA_WORDS;
	if (val == AICA_CDDA_SILENT)
		return;
	SND_GET(s_cdda, s_cdda_ea + (val & (AICA_CDDA_SLOTS - 1)) * sizeof(AicaCddaSlot), sizeof(s_cdda));
	SND_WAIT();
	s_cdda_i = 0;
}

void am_snd_flush(void)
{
	if (s_snd)
		snd_flush();
}

// frames written, frames dropped, CD-DA records: the AicaEcho order from snd_frames on
void am_snd_stats(uint32_t* out)
{
	out[0] = snd.pub - snd.w0;
	out[1] = snd.drops;
	out[2] = s_cdda_n;
}

uint32_t am_samples(void) { return (uint32_t)M->samples; }
uint32_t am_misses(void) { return aram_misses; }
void am_prof(uint32_t* out) { memcpy(out, s_prof, sizeof(s_prof)); }

// flags, compiles, fails, odd, words, live, slow, checks, bad, bad_n, ticks: the AicaEcho order from dsp_flags on
void am_dsp_stats(uint32_t* out)
{
	out[0] = s_dsp_flags;
	out[1] = dj_compiles;
	out[2] = dj_fails;
	out[3] = dj_odd;
	out[4] = dj_ok ? dj_info.words : 0;
	out[5] = dj_ok ? dj_info.live : 0;
	out[6] = dj_ok ? dj_info.slow : 0;
	out[7] = dj_checks;
	out[8] = dj_bad;
	out[9] = dj_bad_n;
	out[10] = dj_ticks;
}

}
