#pragma once
// OPTIMIZATION_PLAN 5.13 A2: the SPU's 44.1 kHz output, resampled to 48 kHz as ps3_audio.cpp does, into its ring.
// The includer defines SND_GET, SND_PUT, SND_PUTF (fenced) as (ls, ea, size) DMAs and SND_WAIT() for all of them.
#include <stdint.h>

#define SND_OUTQ     256   // frames kept in LS between flushes; a power of two that divides the ring
#define SND_IN_RATE  44100
#define SND_OUT_RATE 48000

typedef struct
{
	uint32_t ring_ea, ring_frames, wr_ea, rd_ea;
	uint32_t w0, made, pub;   // ring_wr at boot; ring indices of the next frame to make / to write
	uint32_t phase;           // in 1/48000 steps
	int32_t prev_l, prev_r;
	uint32_t drops;           // frames the ring had no room for
} SndOut;

static SndOut snd;
static float snd_q[SND_OUTQ * 2] __attribute__((aligned(128)));
static volatile uint32_t snd_word[4] __attribute__((aligned(16)));   // ring_rd in, ring_wr out: same offset as the EA

static void snd_init(uint32_t ring_ea, uint32_t ring_frames, uint32_t wr_ea, uint32_t rd_ea, uint32_t w0)
{
	snd.ring_ea = ring_ea;
	snd.ring_frames = ring_frames;
	snd.wr_ea = wr_ea;
	snd.rd_ea = rd_ea;
	snd.w0 = snd.made = snd.pub = w0;
	snd.phase = 0;
	snd.prev_l = snd.prev_r = 0;
	snd.drops = 0;
}

// Writes the frames made since the last flush, then ring_wr behind a fence. Frames without room are dropped, newest first.
static void snd_flush(void)
{
	uint32_t n = snd.made - snd.pub;
	if (!n)
		return;
	volatile uint32_t* rd = &snd_word[(snd.rd_ea & 15) >> 2];
	SND_GET(rd, snd.rd_ea, 4);
	SND_WAIT();
	const uint32_t used = snd.pub - *rd;
	const uint32_t room = used < snd.ring_frames ? snd.ring_frames - used : 0;
	if (n > room)
	{
		snd.drops += n - room;
		n = room;
		snd.made = snd.pub + n;
		if (!n)
			return;
	}
	const uint32_t end = snd.pub + n;
	for (uint32_t w = snd.pub; w != end;)
	{
		const uint32_t q = w & (SND_OUTQ - 1);
		uint32_t k = end - w;
		if (k > SND_OUTQ - q)   // the LS wrap; the ring wraps only there too
			k = SND_OUTQ - q;
		if (w & 1)              // 8-byte DMAs at odd ends, quadwords between
			k = 1;
		else if (k > 1)
			k &= ~1u;
		SND_PUT(&snd_q[q * 2], snd.ring_ea + (w & (snd.ring_frames - 1)) * 8, k * 8);
		w += k;
	}
	volatile uint32_t* wr = &snd_word[(snd.wr_ea & 15) >> 2];
	*wr = end;
	SND_PUTF(wr, snd.wr_ea, 4);
	SND_WAIT();
	snd.pub = end;
}

// One 44.1 kHz sample: the 48 kHz frames that fall before it (ps3_audio.cpp ps3_audio_push_sample)
static inline void snd_sample(int32_t l, int32_t r)
{
	const float scale = 1.0f / (32768.0f * SND_OUT_RATE);
	uint32_t ph = snd.phase;
	while (ph < SND_OUT_RATE)
	{
		if (snd.made - snd.pub >= SND_OUTQ)
			snd_flush();
		float* f = &snd_q[(snd.made & (SND_OUTQ - 1)) * 2];
		f[0] = (float)(snd.prev_l * (int32_t)(SND_OUT_RATE - ph) + l * (int32_t)ph) * scale;
		f[1] = (float)(snd.prev_r * (int32_t)(SND_OUT_RATE - ph) + r * (int32_t)ph) * scale;
		snd.made++;
		ph += SND_IN_RATE;
	}
	snd.phase = ph - SND_OUT_RATE;
	snd.prev_l = l;
	snd.prev_r = r;
}
