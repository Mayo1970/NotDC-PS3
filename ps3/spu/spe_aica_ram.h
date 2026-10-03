#pragma once
// OPTIMIZATION_PLAN 5.13 A1: wave RAM for the SPU copy of the caique model. Included once by spe_aica.cpp (and
// by the PC test); the includer defines aram_fetch(dst, line), a 128-byte get of PS3 aica_ram at that DC address.
#include <stdint.h>
#include <string.h>

// PS3 aica_ram holds each little-endian DC word as a big-endian host word: DC byte a sits at host byte a ^ 3.
#define ARAM_SIZE  (2u << 20)
#define ARAM_LINES 128             // 16 KB direct-mapped, refetched after each check block
#define DSP_RING_BYTES 65536       // DSP ring kept in LS up to RBL 32K words, never written back (A1)
#define DSP_LOG_MAX 128            // 5.13 A1b check: ring writes logged per sample (one MWT per step)
#define DSP_RLOG_MAX 64            // and reads (odd steps only)

static void aram_fetch(uint8_t* dst, uint32_t line);

static uint8_t aram_line[ARAM_LINES][128] __attribute__((aligned(128)));
static uint32_t aram_tag[ARAM_LINES];    // line address | 1 when valid
static uint32_t aram_misses;
// DC byte order; the quadword after the ring takes the recompiler's dropped (collision) stores
static uint8_t dsp_ring[DSP_RING_BYTES + 16] __attribute__((aligned(128)));

// 5.13 A1b: the interpreter's ring accesses during a checked sample (old value kept for the undo)
typedef struct { uint32_t a, rbp, rbl; uint16_t old, v; } DspRingLog;
static DspRingLog dsp_wlog[DSP_LOG_MAX], dsp_rlog[DSP_RLOG_MAX];
static int dsp_log_on, dsp_wn, dsp_rn;

static void aram_invalidate(void) { memset(aram_tag, 0, sizeof(aram_tag)); }

static inline uint8_t* aram_get(uint32_t a)
{
	const uint32_t line = a & ~127u;
	const uint32_t i = (line >> 7) & (ARAM_LINES - 1);
	if (aram_tag[i] != (line | 1))
	{
		aram_fetch(aram_line[i], line);
		aram_tag[i] = line | 1;
		aram_misses++;
	}
	return aram_line[i];
}

uint8_t caique_rd8(uint32_t a) { return aram_get(a)[(a & 127) ^ 3]; }

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

// The cache path only changes the LS line: main memory is the guest's, and the next invalidate drops the write.
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
