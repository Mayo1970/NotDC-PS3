#pragma once
// OPTIMIZATION_PLAN 5.12 #3: describe() and hash_texture() of ps3_tex.cpp on the SPU, bit for bit, for each param of a
// request. Include after echo and setup; misc/spetex_check runs it on the PC.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <string.h>
#include "spe_tex.h"

#define TAG_ECHO 1
#define TAG_VRAM 2   // VRAM chunk buffers: 2, 3
#define TAG_PRM  4   // param chunk buffers: 4, 5
#define TAG_RES  6   // result buffers: 6, 7
#define TAG_PAL  8
#define TAG_BG   9

enum { TX_STAGE_RUN, TX_STAGE_WAIT, TX_STAGE_EVENT };

// After each param of a request: spe_tex.c runs a waiting RSX Direct job there.
#ifndef TX_POLL
#define TX_POLL() do {} while (0)
#endif

#define TX_VRAM_MASK (SPETEX_VRAM_SIZE - 1)
// Stream bytes per VRAM chunk, 127 groups of 16 u64 words; a chunk's get takes 16 bytes more for a stream 8 bytes off.
#define TX_CHUNK     16256u
#define TX_PRM_N     256     // params per chunk
#define TX_TBL_BITS  10      // this request's textures, by TCW and size

static uint8_t vbuf[2][16384] __attribute__((aligned(128)));
// A param array 4, 8 or 12 bytes off a quadword needs one more quadword per chunk.
static uint8_t pbuf[2][TX_PRM_N * 16 + 16] __attribute__((aligned(128)));
static SpeTexRes rbuf[2][TX_PRM_N] __attribute__((aligned(128)));
static SpeTexRes bgline[16] __attribute__((aligned(128)));
static uint32_t pal[1024 + 4] __attribute__((aligned(128)));
static int pal_off;  // words from pal to PALETTE_RAM[0]; -1 until this request loads it

typedef struct { uint32_t tcw, tag, sz, hash; } TxEnt;
static TxEnt tbl[1u << TX_TBL_BITS] __attribute__((aligned(128)));

// Per-request counts, added to the echo at the end (its fields are volatile).
static uint32_t n_hashed, n_dups, n_skips, n_bytes, n_wraps;

static void tx_wait(uint32_t mask)
{
	echo.stage = TX_STAGE_WAIT;
	mfc_write_tag_mask(mask);
	mfc_read_tag_status_all();
	echo.stage = TX_STAGE_RUN;
}

// ---- describe() ------------------------------------------------------------

typedef struct { uint32_t fmt, w, h, addr, pitch, vq, pal4, pal8, pal_base, vq_cb, src_bytes; } TxDesc;

static const uint32_t tx_mip_texels[11] = {
	0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558
};
static const uint32_t tx_vq_mip_bytes[11] = {
	0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556
};

// TSP and TCW by their DC bit numbers, which the PPU's bitfields keep on big-endian too.
static void tx_describe(uint32_t tsp, uint32_t tcw, uint32_t text_control, TxDesc* d)
{
	const uint32_t texu = (tsp >> 3) & 7, texv = tsp & 7, pal_sel = (tcw >> 21) & 63;
	d->fmt = (tcw >> 27) & 7;
	d->w = 8u << texu;
	d->h = 8u << texv;
	d->addr = ((tcw & 0x1FFFFFu) << 3) & TX_VRAM_MASK;
	d->pal4 = d->fmt == 5;
	d->pal8 = d->fmt == 6;
	d->vq = ((tcw >> 30) & 1) && !d->pal4 && !d->pal8;
	const uint32_t twiddled = d->pal4 || d->pal8 || d->vq || !((tcw >> 26) & 1);
	d->pal_base = d->pal4 ? pal_sel << 4 : (pal_sel >> 4) << 8;
	d->pitch = d->w;
	d->vq_cb = 0;
	const uint32_t mip = (tcw >> 31) && twiddled;
	if (mip)
		d->h = d->w;
	const uint32_t lvl = texu + 3;
	if (d->vq)
	{
		d->vq_cb = d->addr;
		if (mip)
			d->addr += tx_vq_mip_bytes[lvl];
		d->addr += 2048;
		d->src_bytes = d->w * d->h / 4;
	}
	else if (d->pal4 || d->pal8)
	{
		const uint32_t bits = d->pal4 ? 4 : 8;
		if (mip)
			d->addr += tx_mip_texels[lvl] * bits / 8;
		d->src_bytes = d->w * d->h * bits / 8;
	}
	else
	{
		if (mip)
			d->addr += tx_mip_texels[lvl] * 2;
		if (!twiddled && ((tcw >> 25) & 1))
		{
			const uint32_t stride = (text_control & 31) * 32;
			if (stride)
				d->pitch = stride;
		}
		d->src_bytes = d->pitch * d->h * 2;
	}
}

// ---- hash_range() on u64 lanes ----------------------------------------------

// Two u64 lanes per vector, high word first: lane 0 is words 0-1, lane 1 words 2-3.
static const vec_uchar16 k_carry = { 4, 5, 6, 7, 0x80, 0x80, 0x80, 0x80, 12, 13, 14, 15, 0x80, 0x80, 0x80, 0x80 };
static const vec_uint4 k_shl10 = { 0xFFFFFFFFu, 0xFFFFFC00u, 0xFFFFFFFFu, 0xFFFFFFFFu };
static const vec_uint4 k_shr6 = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0x03FFFFFFu, 0xFFFFFFFFu };
static const vec_uint4 k_lane0 = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0 };
static const vec_uint4 k_lane1 = { 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu };
// Bytes 8-15 of a, then 0-7 of b: the u64 pair of a stream 8 bytes off the quadword grid.
static const vec_uchar16 k_mid = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };

static inline vec_uint4 add64(vec_uint4 a, vec_uint4 b)
{
	const vec_uint4 c = spu_genc(a, b);
	return spu_add(spu_add(a, b), spu_shuffle(c, c, k_carry));
}

// oaat() on both lanes: h += w; h += h << 10; h ^= h >> 6. The quadword shifts move bits across the lane
// boundary, and the masks clear them.
static inline vec_uint4 oaat2(vec_uint4 h, vec_uint4 w)
{
	h = add64(h, w);
	h = add64(h, spu_and(spu_slqw(spu_slqwbyte(h, 1), 2), k_shl10));
	return spu_xor(h, spu_and(spu_rlmaskqw(h, -6), k_shr6));
}

static inline uint64_t tx_lane(vec_uint4 v, int i)
{
	return (uint64_t)spu_extract(v, 2 * i) << 32 | spu_extract(v, 2 * i + 1);
}

static inline uint64_t oaat64(uint64_t h, uint64_t w)
{
	h += w;
	h += h << 10;
	h ^= h >> 6;
	return h;
}

static inline uint32_t ld32(const uint8_t* p) { return *(const uint32_t*)p; }

// A span past the end of VRAM: every word into lane a at its masked address, as vram32() reads it.
static vec_uint4 hash_wrap(vec_uint4 ab, uint32_t x, uint32_t nw)
{
	n_wraps++;
	while (nw)
	{
		x &= TX_VRAM_MASK & ~3u;
		const uint32_t q = x & ~15u;
		uint32_t span = SPETEX_VRAM_SIZE - q;
		if (span > sizeof(vbuf[0]))
			span = sizeof(vbuf[0]);
		mfc_get(vbuf[0], setup.vram_ea + q, span, TAG_VRAM, 0, 0);
		tx_wait(1u << TAG_VRAM);
		for (uint32_t o = x - q; nw && o < span; o += 4, nw--)
			ab = spu_sel(ab, oaat2(ab, (vec_uint4){ 0, ld32(vbuf[0] + o), 0, 0 }), k_lane0);
		x = q + span;
	}
	return ab;
}

// Chunk k of a span whose gets start at the quadword r and end at end16 (VRAM offsets).
static void vget(uint32_t k, uint32_t r, uint32_t end16)
{
	const uint32_t from = r + k * TX_CHUNK;
	uint32_t size = end16 - from;
	if (size > TX_CHUNK + 16)
		size = TX_CHUNK + 16;
	mfc_get(vbuf[k & 1], setup.vram_ea + from, size, TAG_VRAM + (k & 1), 0, 0);
}

// Lane a takes a 4-byte head word, then the u64 stream runs 16 words per group over lanes a, b, c, d; lane a takes
// the words after the last group and lane d a last 4-byte word.
static uint32_t hash_range(uint32_t addr, uint32_t bytes, uint32_t seed)
{
	addr &= ~3u;
	bytes = (bytes + 3) & ~3u;
	n_bytes += bytes;
	vec_uint4 ab = { 0, seed, 0x9E3779B9u, seed ^ 0x7F4A7C15u };
	vec_uint4 cd = { 0xFFFFFFFFu, ~seed, 0xC2B2AE3Du, seed ^ 0x27D4EB4Fu };

	if (addr + bytes > SPETEX_VRAM_SIZE)
	{
		ab = hash_wrap(ab, addr, bytes >> 2);
		bytes = 0;
	}
	if (bytes)
	{
		const uint32_t head = (addr & 4) ? 4 : 0;
		const uint32_t n = (bytes - head) >> 3, last = (bytes - head) & 4;
		const uint32_t r = addr & ~15u, end16 = (addr + bytes + 15) & ~15u;
		const uint32_t off = addr + head - r;  // 0, 8 or 16
		const uint32_t groups = n >> 4, tail = groups * 128, stream = n * 8 + last;
		const uint32_t nch = stream ? (stream + TX_CHUNK - 1) / TX_CHUNK : 1;
		vget(0, r, end16);
		if (nch > 1)
			vget(1, r, end16);
		for (uint32_t k = 0; k < nch; k++)
		{
			tx_wait(1u << (TAG_VRAM + (k & 1)));
			const uint8_t* b = vbuf[k & 1];
			if (k == 0 && head)
				ab = spu_sel(ab, oaat2(ab, (vec_uint4){ 0, ld32(b + (addr & 15)), 0, 0 }), k_lane0);
			const uint32_t g0 = k * (TX_CHUNK / 128);
			const uint32_t g1 = groups < g0 + TX_CHUNK / 128 ? groups : g0 + TX_CHUNK / 128;
			if (off & 8)
			{
				for (uint32_t g = g0; g < g1; g++)
				{
					const vec_uint4* q = (const vec_uint4*)(b + off - 8 + (g - g0) * 128);
					ab = oaat2(ab, spu_shuffle(q[0], q[1], k_mid));
					cd = oaat2(cd, spu_shuffle(q[1], q[2], k_mid));
					ab = oaat2(ab, spu_shuffle(q[2], q[3], k_mid));
					cd = oaat2(cd, spu_shuffle(q[3], q[4], k_mid));
					ab = oaat2(ab, spu_shuffle(q[4], q[5], k_mid));
					cd = oaat2(cd, spu_shuffle(q[5], q[6], k_mid));
					ab = oaat2(ab, spu_shuffle(q[6], q[7], k_mid));
					cd = oaat2(cd, spu_shuffle(q[7], q[8], k_mid));
				}
			}
			else
			{
				for (uint32_t g = g0; g < g1; g++)
				{
					const vec_uint4* q = (const vec_uint4*)(b + off + (g - g0) * 128);
					ab = oaat2(ab, q[0]);
					cd = oaat2(cd, q[1]);
					ab = oaat2(ab, q[2]);
					cd = oaat2(cd, q[3]);
					ab = oaat2(ab, q[4]);
					cd = oaat2(cd, q[5]);
					ab = oaat2(ab, q[6]);
					cd = oaat2(cd, q[7]);
				}
			}
			// The words after the last group never cross a chunk: they end 4 bytes short of a group.
			if (k == nch - 1 && stream > tail)
			{
				const uint8_t* p = b + off + (tail - k * TX_CHUNK);
				for (uint32_t t = 0; t < (n & 15); t++, p += 8)
					ab = spu_sel(ab, oaat2(ab, (vec_uint4){ ld32(p), ld32(p + 4), 0, 0 }), k_lane0);
				if (last)
					cd = spu_sel(cd, oaat2(cd, (vec_uint4){ 0, 0, 0, ld32(p) }), k_lane1);
			}
			if (k + 2 < nch)
				vget(k + 2, r, end16);
		}
	}

	uint64_t h = oaat64(oaat64(oaat64(tx_lane(ab, 0), tx_lane(ab, 1)), tx_lane(cd, 0)), tx_lane(cd, 1));
	h ^= h >> 33;
	h *= 0xFF51AFD7ED558CCDull;
	h ^= h >> 33;
	h *= 0xC4CEB9FE1A85EC53ull;
	h ^= h >> 33;
	return (uint32_t)(h ^ (h >> 32));
}

// ---- hash_texture() ---------------------------------------------------------

static void pal_load(void)
{
	const uint32_t a = setup.pal_ea & ~15u;
	mfc_get(pal, a, sizeof(pal), TAG_PAL, 0, 0);
	tx_wait(1u << TAG_PAL);
	pal_off = (int)((setup.pal_ea - a) >> 2);
}

// Not for YUV: there the PPU reads the converter's frame counter first.
static uint32_t tex_hash(uint32_t tsp, uint32_t tcw, const SpeTexBell* q)
{
	TxDesc d;
	tx_describe(tsp, tcw, q->text_control, &d);
	uint32_t h = hash_range(d.addr, d.src_bytes, 2166136261u);
	// The codebook is not contiguous with the index map once mipmapped.
	if (d.vq)
		h = hash_range(d.vq_cb, 2048, h);
	if (d.pal4 || d.pal8)
	{
		if (pal_off < 0)
			pal_load();
		const uint32_t n = d.pal4 ? 16 : 256;
		for (uint32_t i = 0; i < n; i++)
			h = (h ^ pal[pal_off + ((d.pal_base + i) & 1023)]) * 16777619u;
		h ^= q->pal_ram_ctrl & 3;
	}
	n_hashed++;
	return h;
}

static inline uint32_t tbl_slot(uint32_t tcw, uint32_t sz)
{
	return ((tcw ^ (sz * 0x9E3779B1u)) * 0x85EBCA6Bu) >> (32 - TX_TBL_BITS);
}

// One param's slot. A texture already hashed in this request (same TCW and size) takes the same hash; *fresh says
// whether this one cost a VRAM pass.
static SpeTexRes tex_result(uint32_t pcw, uint32_t tsp, uint32_t tcw, const SpeTexBell* q, uint32_t tag, int* fresh)
{
	SpeTexRes r = { 0, tag | SPETEX_SKIP };
	*fresh = 0;
	// PCW Texture (bit 3), and PixelFmt 3 (YUV).
	if (!(pcw & 8) || ((tcw >> 27) & 7) == 3)
	{
		n_skips++;
		return r;
	}
	const uint32_t sz = tsp & 63;
	const uint32_t s = tbl_slot(tcw, sz);
	TxEnt* fill = 0;
	for (uint32_t i = 0; i < 8; i++)
	{
		TxEnt* e = &tbl[(s + i) & ((1u << TX_TBL_BITS) - 1)];
		if (e->tag != tag)
		{
			fill = e;
			break;
		}
		if (e->tcw == tcw && e->sz == sz)
		{
			n_dups++;
			r.hash = e->hash;
			r.tag = tag;
			return r;
		}
	}
	r.hash = tex_hash(tsp, tcw, q);
	r.tag = tag;
	*fresh = 1;
	if (fill)
	{
		fill->tcw = tcw;
		fill->tag = tag;
		fill->sz = sz;
		fill->hash = r.hash;
	}
	return r;
}

// Params of chunk c: param c * TX_PRM_N lands off bytes into its buffer.
static void pget(uint32_t c, uint32_t a0, uint32_t off, uint32_t n)
{
	uint32_t cnt = n - c * TX_PRM_N;
	if (cnt > TX_PRM_N)
		cnt = TX_PRM_N;
	mfc_get(pbuf[c & 1], a0 + c * TX_PRM_N * 16, (off + cnt * 16 + 15) & ~15u, TAG_PRM + (c & 1), 0, 0);
}

// One request: the background slot, then every param in order. A result line goes out after each fresh hash and when
// it is full, with fenced puts, so a later put of the line never lands before an earlier one.
static void run(const SpeTexBell* q)
{
	const uint32_t tag = q->seq & SPETEX_SEQ;
	pal_off = -1;
	int fresh;
	bgline[0] = tex_result(q->bg_pcw, q->bg_tsp, q->bg_tcw, q, tag, &fresh);
	mfc_put(bgline, setup.res_ea + SPETEX_BG * sizeof(SpeTexRes), sizeof(bgline), TAG_BG, 0, 0);

	const uint32_t n = q->nparam < SPE_MAX_PARAMS ? q->nparam : SPE_MAX_PARAMS;
	const uint32_t a0 = q->params_ea & ~15u, off = q->params_ea & 15u;
	const uint32_t nch = (n + TX_PRM_N - 1) / TX_PRM_N;
	if (nch)
		pget(0, a0, off, n);
	if (nch > 1)
		pget(1, a0, off, n);
	for (uint32_t c = 0; c < nch; c++)
	{
		// The result buffer's puts from chunk c - 2 must be out before it is written again.
		tx_wait((1u << (TAG_PRM + (c & 1))) | (1u << (TAG_RES + (c & 1))));
		uint32_t cnt = n - c * TX_PRM_N;
		if (cnt > TX_PRM_N)
			cnt = TX_PRM_N;
		const uint8_t* pb = pbuf[c & 1] + off;
		SpeTexRes* rb = rbuf[c & 1];
		const uint32_t tg = TAG_RES + (c & 1);
		for (uint32_t i = 0; i < cnt; i++, pb += 16)
		{
			const uint32_t line = i & ~15u;
			// The buffer still holds chunk c - 2's results, with this request's tag: a partial put must not send them.
			if (i == line)
				memset(&rb[line], 0, 16 * sizeof(SpeTexRes));
			rb[i] = tex_result(ld32(pb), ld32(pb + 8), ld32(pb + 12), q, tag, &fresh);
			if (fresh || (i & 15) == 15 || i == cnt - 1)
				mfc_putf(&rb[line], setup.res_ea + (c * TX_PRM_N + line) * sizeof(SpeTexRes), 128, tg, 0, 0);
			TX_POLL();
		}
		if (c + 2 < nch)
			pget(c + 2, a0, off, n);
	}
	tx_wait((1u << TAG_BG) | (3u << TAG_RES));
	echo.params += n + 1;
	echo.hashed += n_hashed;
	echo.dups += n_dups;
	echo.skips += n_skips;
	echo.bytes += n_bytes;
	echo.wraps += n_wraps;
	n_hashed = n_dups = n_skips = n_bytes = n_wraps = 0;
}
