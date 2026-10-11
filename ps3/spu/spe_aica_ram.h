#pragma once
// OPTIMIZATION_PLAN 5.13 A1: wave RAM for the SPU copy of the caique model. Included once by spe_aica.cpp (and
// by the PC test); the includer defines aram_fetch(dst, line), a 128-byte get of PS3 aica_ram at that DC address.
#include <stdint.h>
#include <string.h>

// PS3 aica_ram holds each little-endian DC word as a big-endian host word: DC byte a sits at host byte a ^ 3.
#define ARAM_SIZE  (2u << 20)
#define ARAM_LINES 128             // 16 KB direct-mapped, refetched after each check block
#define DSP_RING_BYTES 65536       // DSP ring kept in LS up to RBL 32K words; P20 G2 makes it mirror wave RAM
#define DSP_LOG_MAX 128            // 5.13 A1b check: ring writes logged per sample (one MWT per step)
#define DSP_RLOG_MAX 64            // and reads (odd steps only)

static void aram_fetch(uint8_t* dst, uint32_t line);

static uint8_t aram_line[ARAM_LINES][128] __attribute__((aligned(128)));
static uint32_t aram_tag[ARAM_LINES];    // line address | 1 when valid
static uint32_t aram_misses;
#ifdef ARAM_WRITE_BACK
// P20 G3: the includer defines aram_store(src, line), a 128-byte put to that DC address, done when it returns.
static void aram_store(const uint8_t* src, uint32_t line);
static uint8_t aram_dirty[ARAM_LINES];   // the DSP wrote the cached line
static uint32_t aram_wb, aram_wb_any, aram_wb_n;   // write-back on; some line is dirty; lines written back

static inline void aram_put_back(uint32_t i)
{
	if (aram_dirty[i])
	{
		aram_store(aram_line[i], aram_tag[i] & ~1u);
		aram_dirty[i] = 0;
		aram_wb_n++;
	}
}

// The guest wrote lines lo..hi (address >> 7): the cached copies go, with the DSP's own writes to them.
static void aram_drop(uint32_t lo, uint32_t hi)
{
	for (uint32_t i = 0; i < ARAM_LINES; i++)
		if ((aram_tag[i] & 1) && (aram_tag[i] >> 7) - lo <= hi - lo)
			aram_tag[i] = aram_dirty[i] = 0;
}
#endif
// DC byte order; the quadword after the ring takes the recompiler's dropped (collision) stores
static uint8_t dsp_ring[DSP_RING_BYTES + 16] __attribute__((aligned(128)));

// 5.13 A1b: the interpreter's ring accesses during a checked sample (old value kept for the undo)
typedef struct { uint32_t a, rbp, rbl; uint16_t old, v; } DspRingLog;
static DspRingLog dsp_wlog[DSP_LOG_MAX], dsp_rlog[DSP_RLOG_MAX];
static int dsp_log_on, dsp_wn, dsp_rn;

static void aram_invalidate(void)
{
#ifdef ARAM_WRITE_BACK
	if (aram_wb_any)
	{
		for (uint32_t i = 0; i < ARAM_LINES; i++)
			aram_put_back(i);
		aram_wb_any = 0;
	}
#endif
	memset(aram_tag, 0, sizeof(aram_tag));
}

// Out of line: LS is short and aram_get() is inlined at every wave RAM read
static __attribute__((noinline)) void aram_miss(uint32_t i, uint32_t line)
{
#ifdef ARAM_WRITE_BACK
	aram_put_back(i);
#endif
	aram_fetch(aram_line[i], line);
	aram_tag[i] = line | 1;
	aram_misses++;
}

static inline uint8_t* aram_get(uint32_t a)
{
	const uint32_t line = a & ~127u;
	const uint32_t i = (line >> 7) & (ARAM_LINES - 1);
	if (aram_tag[i] != (line | 1))
		aram_miss(i, line);
	return aram_line[i];
}

uint8_t caique_rd8(uint32_t a) { return aram_get(a)[(a & 127) ^ 3]; }

const uint8_t* caique_line(uint32_t a) { return aram_get(a); }

uint16_t caique_rd16(uint32_t a)
{
	const uint8_t* l = aram_get(a);
	const uint32_t o = a & 127;
	return (uint16_t)(l[o ^ 3] | (l[(o + 1) ^ 3] << 8));
}

// Ring offset of a DSP access, or -1 when it goes through the cache (ring too big, or a TABLE access past it).
static inline int32_t dsp_ring_off(uint32_t a, uint32_t rbp, uint32_t rbl)
{
	const uint32_t bytes = (rbl + 1) * 2, o = (a - rbp) & (ARAM_SIZE - 1);
	return bytes <= DSP_RING_BYTES && o < bytes ? (int32_t)o : -1;
}

static uint16_t dsp_rd16_raw(uint32_t a, uint32_t rbp, uint32_t rbl)
{
	const int32_t o = dsp_ring_off(a, rbp, rbl);
	if (o >= 0)
		return (uint16_t)(dsp_ring[o] | (dsp_ring[o + 1] << 8));
	return caique_rd16(a);
}

// The cache path only changes the LS line: the next invalidate drops the write, or with P20 G3 puts the line back.
static void dsp_wr16_raw(uint32_t a, uint32_t rbp, uint32_t rbl, uint16_t v)
{
	const int32_t o = dsp_ring_off(a, rbp, rbl);
	if (o >= 0)
	{
		dsp_ring[o] = (uint8_t)v;
		dsp_ring[o + 1] = (uint8_t)(v >> 8);
		return;
	}
	uint8_t* l = aram_get(a);
	const uint32_t i = a & 127;
	l[i ^ 3] = (uint8_t)v;
	l[(i + 1) ^ 3] = (uint8_t)(v >> 8);
#ifdef ARAM_WRITE_BACK
	if (aram_wb)
		aram_dirty[(a >> 7) & (ARAM_LINES - 1)] = aram_wb_any = 1;
#endif
}

uint16_t caique_dsp_rd16(uint32_t a, uint32_t rbp, uint32_t rbl)
{
	const uint16_t v = dsp_rd16_raw(a, rbp, rbl);
	if (dsp_log_on && dsp_rn < DSP_RLOG_MAX)
	{
		DspRingLog* r = &dsp_rlog[dsp_rn++];
		r->a = a; r->rbp = rbp; r->rbl = rbl; r->old = r->v = v;
	}
	return v;
}

void caique_dsp_wr16(uint32_t a, uint32_t rbp, uint32_t rbl, uint16_t v)
{
	if (dsp_log_on && dsp_wn < DSP_LOG_MAX)
	{
		DspRingLog* r = &dsp_wlog[dsp_wn++];
		r->a = a; r->rbp = rbp; r->rbl = rbl; r->old = dsp_rd16_raw(a, rbp, rbl); r->v = v;
	}
	dsp_wr16_raw(a, rbp, rbl, v);
}
