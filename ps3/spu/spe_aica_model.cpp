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

// 5.13 A2 high voices: the slots run slot by slot, then per sample the DSP (A3, and the A1 check) and the output stage
static void to(uint32_t sample)
{
	const bool dsp = !M->no_dsp;
	const uint32_t cap = dsp ? caique::AicaModel::DSP_ROUND : caique::AicaModel::ADV_MAX;
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
	if (s_snd)
		snd_init(su->ring_ea, su->ring_frames, su->wr_ea, su->rd_ea, su->w0);
	aram_invalidate();
	dj_init(M, M->no_dsp ? 0 : (int)(su->dsp_mode & AICA_DSPF_MODE), DJ_LS(&dspjit_rd), DJ_LS(&dspjit_wr), where);
	s_dsp_flags = (uint32_t)dj_mode;
}

// Boot self-test of the recompiler (before any log record). A failure turns it off for the session.
__attribute__((cold)) void am_dsp_selftest(void)
{
	if (!dj_mode)
		return;
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
}

void am_reset(void)
{
	M->reset();
	aram_invalidate();
	memset(dsp_ring, 0, sizeof(dsp_ring));
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
	aram_invalidate();
	return caique::AicaModel::snap_hash(*s);
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
