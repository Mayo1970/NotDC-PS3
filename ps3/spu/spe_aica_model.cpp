// OPTIMIZATION_PLAN 5.13 A1: the caique model on the SPU, with wave RAM through an LS cache (spe_aica_ram.h).
// C entry points for spe_aica.c; no constructor runs on the SPU, so the model lives in raw storage.
#include <spu_mfcio.h>
#include "aica_model.h"
#include "spe_aica.h"

#define TAG_ARAM 5

static uint32_t s_aram_ea;

static void aram_fetch(uint8_t* dst, uint32_t line)
{
	mfc_get(dst, s_aram_ea + line, 128, TAG_ARAM, 0, 0);
	mfc_write_tag_mask(1 << TAG_ARAM);
	mfc_read_tag_status_all();
}

#include "spe_aica_ram.h"

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

static uint8_t s_store[sizeof(caique::AicaModel)] __attribute__((aligned(128)));
static caique::AicaModel* const M = (caique::AicaModel*)s_store;

typedef char snap_size_check[sizeof(caique::CtlSnap) == AICA_SNAP_BYTES ? 1 : -1];

static void to(uint32_t sample)
{
	while ((int32_t)(sample - (uint32_t)M->samples) > 0)
		M->step();
}

extern "C" {

void am_init(uint32_t aram_ea)
{
	s_aram_ea = aram_ea;
	M->init();
	aram_invalidate();
}

void am_reset(void)
{
	M->reset();
	aram_invalidate();
	memset(dsp_ring, 0, sizeof(dsp_ring));
}

void am_write(uint32_t sample, uint32_t off, uint32_t val)
{
	to(sample);
	M->write(off, val);
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

uint32_t am_samples(void) { return (uint32_t)M->samples; }
uint32_t am_misses(void) { return aram_misses; }
void am_prof(uint32_t* out) { memcpy(out, s_prof, sizeof(s_prof)); }

}
