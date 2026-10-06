// OPTIMIZATION_PLAN 5.13 A2 PC test: spe_aica_snd.h on a fake MFC. Ring frames must equal ps3_audio.cpp's resampler,
// every DMA must keep the MFC size/alignment rules, and no frame the feeder has not read may be overwritten.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static uint8_t g_mem[1 << 17] __attribute__((aligned(128)));   // main memory: EA = offset
static uint32_t g_dma_bad, g_dmas, g_overwrites;
static const uint32_t RING = 8192, RING_EA = 0x1000;
static uint32_t g_wr_ea, g_rd_ea;

static uint32_t& word(uint32_t ea) { return *(uint32_t*)&g_mem[ea]; }

static void dma(const volatile void* ls, uint64_t ea, uint32_t n, bool put)
{
	const uintptr_t la = (uintptr_t)ls;
	const bool ok = n < 16 ? (n == 1 || n == 2 || n == 4 || n == 8) && (la & (n - 1)) == 0 && (la & 15) == (ea & 15)
	                       : n % 16 == 0 && n <= 16384 && (la & 15) == 0 && (ea & 15) == 0;
	if ((!ok || ea + n > sizeof(g_mem)) && g_dma_bad++ < 10)
		printf("bad DMA: ls %lx ea %llx size %u\n", (unsigned long)la, (unsigned long long)ea, n);
	g_dmas++;
	if (ea + n > sizeof(g_mem))
		return;
	if (put && ea >= RING_EA && ea < RING_EA + RING * 8)   // no frame the feeder has not read may change
	{
		const uint32_t rd = word(g_rd_ea), unread = word(g_wr_ea) - rd;
		for (uint32_t p = (uint32_t)(ea - RING_EA) / 8; p < (ea - RING_EA + n) / 8; p++)
			if (((p - rd) & (RING - 1)) < unread && g_overwrites++ < 5)
				printf("frame position %u overwritten while unread (rd %u wr %u)\n", p, rd, word(g_wr_ea));
	}
	if (put)
		memcpy(&g_mem[ea], (const void*)la, n);
	else
		memcpy((void*)la, &g_mem[ea], n);
}
#define SND_GET(ls, ea, n) dma((ls), (ea), (n), false)
#define SND_PUT(ls, ea, n) dma((ls), (ea), (n), true)
#define SND_PUTF(ls, ea, n) dma((ls), (ea), (n), true)
#define SND_WAIT() ((void)0)
#include "spe_aica_snd.h"

// ps3_audio.cpp ps3_audio_push_sample, frames appended to out
struct Ref
{
	int16_t prev_l = 0, prev_r = 0;
	uint32_t phase = 0;
	void push(int16_t l, int16_t r, std::vector<float>& out)
	{
		const float scale = 1.0f / (32768.0f * 48000);
		while (phase < 48000)
		{
			out.push_back((float)(prev_l * (int32_t)(48000 - phase) + l * (int32_t)phase) * scale);
			out.push_back((float)(prev_r * (int32_t)(48000 - phase) + r * (int32_t)phase) * scale);
			phase += 44100;
		}
		phase -= 48000;
		prev_l = l;
		prev_r = r;
	}
};

static uint32_t g_rng = 12345;
static uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t frames_for(uint32_t n) { return (uint32_t)(((uint64_t)n * 160 + 146) / 147); }   // ps3_aica_spe.cpp

// One run: n samples, a flush every 1..max_gap samples, the feeder takes 256-frame blocks with p_read % chance per
// try at each flush. Returns the failures.
static int run(uint32_t w0, uint32_t woff, uint32_t roff, uint32_t n, uint32_t max_gap, uint32_t p_read, bool expect_drops)
{
	const uint32_t wr_ea = g_wr_ea = 0x11000 + woff, rd_ea = g_rd_ea = 0x11010 + roff;
	memset(g_mem, 0xEE, sizeof(g_mem));
	word(wr_ea) = w0;
	word(rd_ea) = w0;
	snd_init(RING_EA, RING, wr_ea, rd_ea, w0);
	Ref ref;
	std::vector<float> rf;   // reference frames from w0 on
	size_t j = 0;            // next reference frame a published frame may match (subsequence)
	int fails = 0;
	uint32_t gap = 1 + rnd() % max_gap, read_frames = 0;
	for (uint32_t s = 1; s <= n && fails < 5; s++)
	{
		const uint32_t k = rnd();
		const int16_t l = (k & 0x300) ? (int16_t)rnd() : (k & 0x400) ? 32767 : -32768;
		const int16_t r = (k & 0x3000) ? (int16_t)rnd() : 0;
		snd_sample(l, r);
		ref.push(l, r, rf);
		if (!expect_drops && snd.made - w0 != frames_for(s))
		{
			printf("frames after %u samples: %u, expected %u\n", s, snd.made - w0, frames_for(s));
			fails++;
		}
		if (--gap)
			continue;
		gap = 1 + rnd() % max_gap;
		snd_flush();
		if (word(wr_ea) != snd.pub)
		{
			printf("ring_wr %u, pub %u\n", word(wr_ea), snd.pub);
			fails++;
		}
		if (snd.pub - word(rd_ea) > RING)
		{
			printf("ring over-full: wr %u rd %u\n", snd.pub, word(rd_ea));
			fails++;
		}
		// The feeder: whole 256-frame blocks, each frame the next one of the reference (in order, some dropped)
		while ((rnd() % 100) < p_read && word(wr_ea) - word(rd_ea) >= 256)
		{
			for (uint32_t i = 0; i < 256; i++)
			{
				const uint32_t w = word(rd_ea) + i;
				const float* f = (const float*)&g_mem[RING_EA + (w & (RING - 1)) * 8];
				while (j < rf.size() / 2 && memcmp(&rf[j * 2], f, 8))
					j++;
				if (j >= rf.size() / 2)
				{
					printf("frame %u (%g %g) is not the next reference frame\n", w, f[0], f[1]);
					fails++;
					break;
				}
				if (!expect_drops && j != w - w0)
				{
					printf("frame %u matched reference %zu\n", w, j);
					fails++;
				}
				j++;
			}
			word(rd_ea) += 256;
			read_frames += 256;
		}
	}
	const uint32_t made = frames_for(n);
	if ((snd.pub - w0) + snd.drops + (snd.made - snd.pub) != made)
	{
		printf("accounting: pub %u drops %u queued %u, made %u\n", snd.pub - w0, snd.drops, snd.made - snd.pub, made);
		fails++;
	}
	if (expect_drops != (snd.drops != 0))
	{
		printf("drops %u, expected %s\n", snd.drops, expect_drops ? "some" : "none");
		fails++;
	}
	printf("w0 %u wr+%u rd+%u gap<=%u read %u%%: frames %u read %u drops %u -> %s\n", w0, woff, roff, max_gap, p_read,
	       snd.pub - w0, read_frames, snd.drops, fails ? "FAIL" : "ok");
	return fails;
}

int main()
{
	int fails = 0;
	for (uint32_t i = 0; i < 4; i++)
	{
		fails += run(i * 12345, (i * 4) & 15, (12 - i * 4) & 15, 3000000, 1 + 63 * (i & 1) + 400 * (i == 2), 100, false);
		fails += run(0xFFFFF000u + i * 7, (i * 8) & 15, (i * 4) & 15, 3000000, 64, 2, true);
	}
	for (uint32_t n = 0; n < 2000000; n++)   // frames_for against the resampler, sample by sample
	{
		static Ref ref;
		static std::vector<float> rf;
		ref.push(0, 0, rf);
		if (rf.size() / 2 != frames_for(n + 1))
		{
			printf("frames_for(%u) = %u, resampler %zu\n", n + 1, frames_for(n + 1), rf.size() / 2);
			fails++;
			break;
		}
	}
	printf("A2 snd_unit: %u DMAs, %u bad, %u unread frames overwritten; %s\n", g_dmas, g_dma_bad, g_overwrites,
	       fails || g_dma_bad || g_overwrites ? "FAIL" : "PASS");
	return fails || g_dma_bad || g_overwrites;
}
