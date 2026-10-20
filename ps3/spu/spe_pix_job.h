#pragma once
// OPTIMIZATION_PLAN 5.12 #4/#5: decode() + rsxd_tex.h's rt_convert(), and present_fb_convert(), bit for bit. Include after
// echo and setup; misc/spepix_check runs it on the PC with its own spe_pix_simd.h and spu_mfcio.h.
#include <spu_mfcio.h>
#include "spe_pix.h"
// Angle brackets: the PC check puts its own copy first on the include path.
#include <spe_pix_simd.h>

#define TAG_ECHO 1
#define TAG_IN   2  // input buffers: 2, 3
#define TAG_OUT  4  // output buffers: 4, 5
#define TAG_TBL  6  // codebook and palette

enum { PX_STAGE_RUN, PX_STAGE_WAIT, PX_STAGE_EVENT };
enum { PX_TW16, PX_PL16, PX_PAL8, PX_PAL4, PX_VQ };

#define PX_VRAM_MASK (SPEPIX_VRAM_SIZE - 1)
#define PX_T    32    // largest tile side
#define PX_INB  2688  // a 32x32 twiddled tile (2 KB) or 32 planar rows of 80 bytes, with read slack
#define PX_FB_W 768   // FB2D_W in ps3_rend.cpp
#define PX_FBIN 6448  // one line's words 8 bytes apart, in up to two segments, with read slack

static uint8_t inb[2][PX_INB] __attribute__((aligned(128)));
static q_t ta[PX_T * PX_T / 4] __attribute__((aligned(128)));       // phase A: ARGB in source order
static q_t outb[2][PX_T * PX_T / 4] __attribute__((aligned(128)));  // tiles in the destination layout
// DMA lists, two 8-byte elements (size word, address word) per quadword.
static q_t lin[2][PX_T / 2] __attribute__((aligned(16)));
static q_t lout[2][PX_T / 2] __attribute__((aligned(16)));
static q_t lut[256] __attribute__((aligned(128)));                   // VQ codebook or palette as ARGB
static uint8_t rawb[2048 + 48] __attribute__((aligned(128)));       // raw codebook or palette words
static uint8_t fbin[2][PX_FBIN] __attribute__((aligned(128)));
static q_t fbw[PX_FB_W / 4 + 4] __attribute__((aligned(128)));       // one line's 32-bit words in order
static q_t fbout[2][PX_FB_W / 4] __attribute__((aligned(128)));

typedef struct
{
	uint32_t fmt, w, h, addr, pitch, flags, pal_base, vq_cb, pal_ctrl, dst_ea, dst_pitch;
	uint32_t T, lnx, nx, ny;  // tile side; tiles across (log2) and down
	uint32_t kind, yuv, swz;
	uint32_t o;               // source offset in its first quadword (all tiles and rows share it)
	uint32_t in_bytes;        // twiddled: source bytes per tile
	uint32_t esz;             // planar: bytes per row of the input list
	uint32_t cols;            // planar: texels per row that exist (decode()'s cols)
	q_t pat, pat_ps;          // source fetch pattern; the same with the RSX order of a 4x4 block's 16 bytes
	q_t igna, fill;           // alpha 255 for IgnoreTexA; a tile past cols
	uint32_t dcx[128], dcy[128], rsx[128], rsy[128];  // tile Morton offsets: DC (y first), RSX (x first)
} PxJob;
static PxJob job;
static uint32_t dcb[64], rsb[64];  // per 4x4 block of a tile, row-major: its DC and RSX block index
static uint32_t blk_T;             // the tile side dcb and rsb are for

typedef struct { uint32_t n, n1, l1, l2, a0; } FbLine;
static FbLine fbl[2];

// Two planes' low bytes, interleaved.
static const q_t k_ar = QI(1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31);
static const q_t k_lo4 = QI(0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23);
static const q_t k_hi4 = QI(8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31);
// ARGB of texels 0, 2, 1, 3 and 4, 6, 5, 7 of an 8-texel plane pair: a DC 2x2 block in RSX order.
static const q_t k_swa = QI(0, 1, 16, 17, 4, 5, 20, 21, 2, 3, 18, 19, 6, 7, 22, 23);
static const q_t k_swb = QI(8, 9, 24, 25, 12, 13, 28, 29, 10, 11, 26, 27, 14, 15, 30, 31);
// A 4x4 block's 16 bytes from DC to RSX texel order: bit pairs swapped.
static const q_t k_ps4 = QI(0, 2, 1, 3, 8, 10, 9, 11, 4, 6, 5, 7, 12, 14, 13, 15);
static const q_t k_wswap = QI(0, 1, 2, 3, 8, 9, 10, 11, 4, 5, 6, 7, 12, 13, 14, 15);
static const q_t k_row0 = QI(0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27);  // words 0, 2 of a and b
static const q_t k_row1 = QI(4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31);  // words 1, 3
static const q_t k_lo2 = QI(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23);
static const q_t k_hi2 = QI(8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31);
static const q_t k_rot8 = QI(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
// Byte i of the index quadword to the low byte of word lane i & 3 (zero above).
static const q_t k_b2w0 = QI(0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 1, 0x80, 0x80, 0x80, 2, 0x80, 0x80, 0x80, 3);
static const q_t k_b2w1 = QI(0x80, 0x80, 0x80, 4, 0x80, 0x80, 0x80, 5, 0x80, 0x80, 0x80, 6, 0x80, 0x80, 0x80, 7);
static const q_t k_b2w2 = QI(0x80, 0x80, 0x80, 8, 0x80, 0x80, 0x80, 9, 0x80, 0x80, 0x80, 10, 0x80, 0x80, 0x80, 11);
static const q_t k_b2w3 = QI(0x80, 0x80, 0x80, 12, 0x80, 0x80, 0x80, 13, 0x80, 0x80, 0x80, 14, 0x80, 0x80, 0x80, 15);
static const q_t k_m1 = { 0, 0xFFFFFFFFu, 0, 0 };
static const q_t k_m3 = { 0, 0, 0, 0xFFFFFFFFu };
static const q_t k_m23 = { 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu };
// yuv_pair()'s left texels (U, Y0) and right texels (V, Y1) of two quadwords of 8 halfwords. Planar: pairs (0,1)
// (2,3)...; twiddled: each DC 2x2 block's rows, (0,2) and (1,3).
static const q_t k_yl_pl = QI(0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29);
static const q_t k_yr_pl = QI(2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31);
static const q_t k_yl_tw = QI(0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27);
static const q_t k_yr_tw = QI(4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31);
// The 8 pairs' left and right ARGB words back in source order.
static const q_t k_yo0_pl = QI(0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23);
static const q_t k_yo1_pl = QI(8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31);
static const q_t k_yo0_tw = QI(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23);
static const q_t k_yo1_tw = QI(8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31);
// ARGB words of pairs 0-3 and 4-7 from the interleaved R/G plane and the B plane (alpha 255).
static const q_t k_yw03 = QI(0xC0, 0, 1, 17, 0xC0, 2, 3, 19, 0xC0, 4, 5, 21, 0xC0, 6, 7, 23);
static const q_t k_yw47 = QI(0xC0, 8, 9, 25, 0xC0, 10, 11, 27, 0xC0, 12, 13, 29, 0xC0, 14, 15, 31);
// FB words 8 bytes apart: the even or the odd words of two quadwords.
static const q_t k_pick0 = QI(0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27);
static const q_t k_pick1 = QI(4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31);

static void px_wait(uint32_t mask)
{
	echo.stage = PX_STAGE_WAIT;
	mfc_write_tag_mask(mask);
	mfc_read_tag_status_all();
	echo.stage = PX_STAGE_RUN;
}

static inline q_t q_zero(void) { return w_splat(0); }

// Bits 0-7 of v to the even bit positions.
static inline uint32_t spread(uint32_t v)
{
	v &= 0xFF;
	v = (v | (v << 4)) & 0x0F0F;
	v = (v | (v << 2)) & 0x3333;
	return (v | (v << 1)) & 0x5555;
}

// Morton offset of (x, y) on a 2^lx x 2^ly grid: the bits interleave while both sizes last, y first for the DC twiddle
// (ps3_tex.cpp twop), x first for the RSX swizzle (rsxd_tex.h rt_tables); the larger size's bits follow.
static uint32_t morton(uint32_t x, uint32_t y, uint32_t lx, uint32_t ly, int yfirst)
{
	const uint32_t k = lx < ly ? lx : ly, m = (1u << k) - 1;
	const uint32_t a = yfirst ? spread(y & m) | spread(x & m) << 1 : spread(x & m) | spread(y & m) << 1;
	return a | ((lx > ly ? x : y) >> k) << (2 * k);
}

// The DC block of RSX block m on a square grid: x and y swap in every bit pair.
static inline uint32_t pswap(uint32_t m) { return ((m & 0x55555555u) << 1) | ((m >> 1) & 0x55555555u); }

static uint32_t log2u(uint32_t v)
{
	uint32_t l = 0;
	while ((1u << l) < v)
		l++;
	return l;
}

// Shuffle that realigns a source starting o bytes into its quadword and puts VRAM's host words back in DC order:
// 8-bit data byte-reversed per word, 16-bit data halfword-swapped per word.
static q_t fetch_pat(uint32_t o, int bytes8)
{
	uint32_t w[4] = { 0, 0, 0, 0 };
	for (uint32_t j = 0; j < 16; j++)
	{
		const uint32_t s = bytes8 ? ((o + j) & ~3u) + 3 - ((o + j) & 3) : ((o + (j & ~1u)) ^ 2) + (j & 1);
		w[j >> 2] |= s << (24 - 8 * (j & 3));
	}
	return q_words(w[0], w[1], w[2], w[3]);
}

static inline q_t fetch(const uint8_t* s, q_t pat) { return q_shuf(q_ld(s), q_ld(s + 16), pat); }

// DMA list elements r = 0..n-1 (n even): size bytes at ea + r * step.
static void list_fill(q_t* l, uint32_t n, uint32_t size, uint32_t ea, uint32_t step)
{
	q_t e = q_words(size, ea, size, ea + step);
	const q_t inc = q_words(0, 2 * step, 0, 2 * step);
	for (uint32_t r = 0; r < n / 2; r++)
	{
		l[r] = e;
		e = w_add(e, inc);
	}
}

// 8 halfword texels of format f (cvt16: 1 565, 2 4444, 4 bump, else 1555) to the A-R and G-B byte pairs of argb words.
static inline __attribute__((always_inline)) void planes16(q_t p, const uint32_t f, q_t igna, q_t* ar, q_t* gb)
{
	q_t A, R, G, B;
	if (f == 1)
	{
		A = h_splat(0xFF);
		R = q_or(h_and(h_srl(p, 8), 0xF8), h_srl(p, 13));
		G = q_or(h_and(h_srl(p, 3), 0xFC), h_and(h_srl(p, 9), 3));
		B = q_or(h_and(h_sll(p, 3), 0xF8), h_and(h_srl(p, 2), 7));
	}
	else if (f == 2)
	{
		A = q_or(h_and(h_srl(p, 8), 0xF0), h_srl(p, 12));
		R = q_or(h_and(h_srl(p, 4), 0xF0), h_and(h_srl(p, 8), 15));
		G = q_or(h_and(p, 0xF0), h_and(h_srl(p, 4), 15));
		B = q_or(h_and(h_sll(p, 4), 0xF0), h_and(p, 15));
	}
	else if (f == 4)
	{
		A = R = G = B = h_splat(0xFF);
	}
	else
	{
		A = h_sra(p, 15);
		R = q_or(h_and(h_srl(p, 7), 0xF8), h_and(h_srl(p, 12), 7));
		G = q_or(h_and(h_srl(p, 2), 0xF8), h_and(h_srl(p, 7), 7));
		B = q_or(h_and(h_sll(p, 3), 0xF8), h_and(h_srl(p, 2), 7));
	}
	*ar = q_shuf(q_or(A, igna), R, k_ar);
	*gb = q_shuf(G, B, k_ar);
}

// n quadwords of 8 halfword texels from s to 2n quadwords of ARGB at d, same order; returns the OR of the words.
static inline __attribute__((always_inline)) q_t conv16(q_t* d, const uint8_t* s, uint32_t n, q_t pat, q_t igna,
                                                        const uint32_t f)
{
	q_t acc = q_zero();
	q_t c = q_ld(s);
	for (uint32_t k = 0; k < n; k++, d += 2)
	{
		const q_t nx = q_ld(s + 16 * (k + 1));
		q_t ar, gb;
		planes16(q_shuf(c, nx, pat), f, igna, &ar, &gb);
		c = nx;
		const q_t o0 = q_shuf(ar, gb, k_lo4), o1 = q_shuf(ar, gb, k_hi4);
		d[0] = o0;
		d[1] = o1;
		acc = q_or(acc, q_or(o0, o1));
	}
	return acc;
}

// Rows of planar texels: row r at s + r * esz, out at d + r * T / 4. One loop over every quadword of the tile, so the
// SPU has one loop branch to hint.
static inline __attribute__((always_inline)) q_t rows16(q_t* d, const uint8_t* s, uint32_t T, uint32_t esz, q_t pat,
                                                        q_t igna, const uint32_t f)
{
	q_t acc = q_zero();
	const uint32_t n = T * T / 8, last = T / 8 - 1, jump = esz - T * 2;
	const uint8_t* p = s;
	for (uint32_t k = 0; k < n; p += 16 + ((k & last) == last ? jump : 0), k++, d += 2)
	{
		q_t ar, gb;
		planes16(q_shuf(q_ld(p), q_ld(p + 16), pat), f, igna, &ar, &gb);
		const q_t o0 = q_shuf(ar, gb, k_lo4), o1 = q_shuf(ar, gb, k_hi4);
		d[0] = o0;
		d[1] = o1;
		acc = q_or(acc, q_or(o0, o1));
	}
	return acc;
}

// Twiddled texels straight into RSX swizzle: RSX 4x4 block m is DC block pswap(m), whose texels 0-7 give its
// quadwords 0 and 2, texels 8-15 quadwords 1 and 3.
static inline __attribute__((always_inline)) q_t tw16_sw(q_t* d, const uint8_t* s, uint32_t nb, q_t pat, q_t igna,
                                                         const uint32_t f)
{
	q_t acc = q_zero();
	for (uint32_t m = 0; m < nb; m++, d += 4)
	{
		const uint8_t* const b = s + 32 * pswap(m);
		const q_t c0 = q_ld(b), c1 = q_ld(b + 16), c2 = q_ld(b + 32);
		q_t ar0, gb0, ar1, gb1;
		planes16(q_shuf(c0, c1, pat), f, igna, &ar0, &gb0);
		planes16(q_shuf(c1, c2, pat), f, igna, &ar1, &gb1);
		const q_t o0 = q_shuf(ar0, gb0, k_swa), o1 = q_shuf(ar1, gb1, k_swa);
		const q_t o2 = q_shuf(ar0, gb0, k_swb), o3 = q_shuf(ar1, gb1, k_swb);
		d[0] = o0;
		d[1] = o1;
		d[2] = o2;
		d[3] = o3;
		acc = q_or(acc, q_or(q_or(o0, o1), q_or(o2, o3)));
	}
	return acc;
}

static q_t conv16_fmt(q_t* d, const uint8_t* s, uint32_t n, q_t pat, q_t igna, uint32_t fmt)
{
	switch (fmt)
	{
	case 1: return conv16(d, s, n, pat, igna, 1);
	case 2: return conv16(d, s, n, pat, igna, 2);
	case 4: return conv16(d, s, n, pat, igna, 4);
	default: return conv16(d, s, n, pat, igna, 0);
	}
}

static q_t rows16_fmt(q_t* d, const uint8_t* s, uint32_t T, uint32_t esz, q_t pat, q_t igna, uint32_t fmt)
{
	switch (fmt)
	{
	case 1: return rows16(d, s, T, esz, pat, igna, 1);
	case 2: return rows16(d, s, T, esz, pat, igna, 2);
	case 4: return rows16(d, s, T, esz, pat, igna, 4);
	default: return rows16(d, s, T, esz, pat, igna, 0);
	}
}

static q_t tw16_sw_fmt(q_t* d, const uint8_t* s, uint32_t nb, q_t pat, q_t igna, uint32_t fmt)
{
	switch (fmt)
	{
	case 1: return tw16_sw(d, s, nb, pat, igna, 1);
	case 2: return tw16_sw(d, s, nb, pat, igna, 2);
	case 4: return tw16_sw(d, s, nb, pat, igna, 4);
	default: return tw16_sw(d, s, nb, pat, igna, 0);
	}
}

// yuv_pair() exactly: C division truncates toward zero.
static inline q_t h_div(q_t x, int k) { return h_sra(h_add(x, h_and(h_sra(x, 15), (uint16_t)((1 << k) - 1))), k); }
static inline q_t h_clamp(q_t x)
{
	x = q_andc(x, h_sra(x, 15));
	return q_or(x, h_cmpgt(x, h_splat(255)));
}

// 16 YUV texels (two quadwords of 8) to 4 ARGB quadwords in the same order; tw: twiddled pairs.
static inline __attribute__((always_inline)) void yuv16(q_t q0, q_t q1, const int tw, q_t* o)
{
	const q_t l = q_shuf(q0, q1, tw ? k_yl_tw : k_yl_pl), r = q_shuf(q0, q1, tw ? k_yr_tw : k_yr_pl);
	const q_t u = h_sub(h_and(l, 0xFF), h_splat(128)), v = h_sub(h_and(r, 0xFF), h_splat(128));
	const q_t y0 = h_srl(l, 8), y1 = h_srl(r, 8);
	const q_t v11 = h_add(h_add(h_sll(v, 3), h_sll(v, 1)), v);
	const q_t u11 = h_add(h_add(h_sll(u, 3), h_sll(u, 1)), u);
	const q_t dr = h_div(v11, 3);
	const q_t dg = h_div(h_add(u11, h_sll(v11, 1)), 5);
	const q_t db = h_div(h_add(h_sll(u11, 3), h_sll(u11, 1)), 6);
	const q_t rg0 = q_shuf(h_clamp(h_add(y0, dr)), h_clamp(h_sub(y0, dg)), k_ar);
	const q_t rg1 = q_shuf(h_clamp(h_add(y1, dr)), h_clamp(h_sub(y1, dg)), k_ar);
	const q_t b0 = h_clamp(h_add(y0, db)), b1 = h_clamp(h_add(y1, db));
	const q_t l03 = q_shuf(rg0, b0, k_yw03), l47 = q_shuf(rg0, b0, k_yw47);
	const q_t r03 = q_shuf(rg1, b1, k_yw03), r47 = q_shuf(rg1, b1, k_yw47);
	const q_t ko0 = tw ? k_yo0_tw : k_yo0_pl, ko1 = tw ? k_yo1_tw : k_yo1_pl;
	o[0] = q_shuf(l03, r03, ko0);
	o[1] = q_shuf(l03, r03, ko1);
	o[2] = q_shuf(l47, r47, ko0);
	o[3] = q_shuf(l47, r47, ko1);
}

static inline q_t or4(const q_t* o) { return q_or(q_or(o[0], o[1]), q_or(o[2], o[3])); }

// n16 groups of 16 twiddled YUV texels from s to d.
static q_t yuv_tw(q_t* d, const uint8_t* s, uint32_t n16, q_t pat)
{
	q_t acc = q_zero();
	for (uint32_t k = 0; k < n16; k++, d += 4, s += 32)
	{
		yuv16(fetch(s, pat), fetch(s + 16, pat), 1, d);
		acc = q_or(acc, or4(d));
	}
	return acc;
}

// Planar YUV rows (pairs within each quadword); rows of 8 texels go two per group.
static q_t yuv_rows(q_t* d, const uint8_t* s, uint32_t T, uint32_t esz, q_t pat)
{
	q_t acc = q_zero();
	if (T == 8)
	{
		for (uint32_t r = 0; r < 8; r += 2, d += 4, s += 2 * esz)
		{
			yuv16(fetch(s, pat), fetch(s + esz, pat), 0, d);
			acc = q_or(acc, or4(d));
		}
		return acc;
	}
	const uint32_t n = T * T / 16, last = T / 16 - 1, jump = esz - T * 2;
	const uint8_t* p = s;
	for (uint32_t g = 0; g < n; p += 32 + ((g & last) == last ? jump : 0), g++, d += 4)
	{
		yuv16(fetch(p, pat), fetch(p + 16, pat), 0, d);
		acc = q_or(acc, or4(d));
	}
	return acc;
}

// 16 index bytes to the 16 lut quadwords they name, at d.
static inline void vq16(q_t idx, q_t* d)
{
	const q_t a = w_sll(q_shuf(idx, idx, k_b2w0), 4), b = w_sll(q_shuf(idx, idx, k_b2w1), 4);
	const q_t c = w_sll(q_shuf(idx, idx, k_b2w2), 4), e = w_sll(q_shuf(idx, idx, k_b2w3), 4);
	const uint8_t* const t = (const uint8_t*)lut;
	d[0] = q_ld(t + w_extract(a, 0));
	d[1] = q_ld(t + w_extract(a, 1));
	d[2] = q_ld(t + w_extract(a, 2));
	d[3] = q_ld(t + w_extract(a, 3));
	d[4] = q_ld(t + w_extract(b, 0));
	d[5] = q_ld(t + w_extract(b, 1));
	d[6] = q_ld(t + w_extract(b, 2));
	d[7] = q_ld(t + w_extract(b, 3));
	d[8] = q_ld(t + w_extract(c, 0));
	d[9] = q_ld(t + w_extract(c, 1));
	d[10] = q_ld(t + w_extract(c, 2));
	d[11] = q_ld(t + w_extract(c, 3));
	d[12] = q_ld(t + w_extract(e, 0));
	d[13] = q_ld(t + w_extract(e, 1));
	d[14] = q_ld(t + w_extract(e, 2));
	d[15] = q_ld(t + w_extract(e, 3));
}

// 16 palette indices to 4 ARGB quadwords (each lut quadword holds its entry in every lane).
static inline void pal16(q_t idx, q_t* d)
{
	q_t e[16];
	vq16(idx, e);
	for (int g = 0; g < 4; g++)
		d[g] = q_sel(q_sel(e[4 * g], e[4 * g + 1], k_m1), q_sel(e[4 * g + 2], e[4 * g + 3], k_m3), k_m23);
}

// Palette texels straight into RSX swizzle: a DC 4x4 block is 16 index bytes; pat_ps puts them in RSX order.
static q_t pal8_sw(q_t* d, const uint8_t* s, uint32_t nb, q_t pat_ps)
{
	q_t acc = q_zero();
	for (uint32_t m = 0; m < nb; m++, d += 4)
	{
		pal16(fetch(s + 16 * pswap(m), pat_ps), d);
		acc = q_or(acc, or4(d));
	}
	return acc;
}

static q_t pal8_tw(q_t* d, const uint8_t* s, uint32_t n, q_t pat)
{
	q_t acc = q_zero();
	for (uint32_t k = 0; k < n; k++, d += 4, s += 16)
	{
		pal16(fetch(s, pat), d);
		acc = q_or(acc, or4(d));
	}
	return acc;
}

// Texel 2i is the low nibble of index byte i, texel 2i + 1 the high one.
static q_t pal4_tw(q_t* d, const uint8_t* s, uint32_t n, q_t pat)
{
	static const q_t k_n0 = QI(0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
	static const q_t k_n1 = QI(8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
	q_t acc = q_zero();
	for (uint32_t k = 0; k < n; k++, d += 8, s += 16)
	{
		const q_t i = fetch(s, pat);
		const q_t lo = b_and(i, 0x0F), hi = h_and(h_srl(i, 4), 0x0F0F);
		pal16(q_shuf(lo, hi, k_n0), d);
		pal16(q_shuf(lo, hi, k_n1), d + 4);
		acc = q_or(acc, q_or(or4(d), or4(d + 4)));
	}
	return acc;
}

// VQ: each index byte is one 2x2 block, its codebook quadword already ARGB (in DC order, or RSX order for swizzle).
// Straight into RSX swizzle, RSX 8x8 group m is DC group pswap(m), its 16 blocks put in RSX order by pat_ps.
static q_t vq_sw(q_t* d, const uint8_t* s, uint32_t ng, q_t pat_ps)
{
	q_t acc = q_zero();
	for (uint32_t m = 0; m < ng; m++, d += 16)
	{
		vq16(fetch(s + 16 * pswap(m), pat_ps), d);
		acc = q_or(acc, q_or(q_or(or4(d), or4(d + 4)), q_or(or4(d + 8), or4(d + 12))));
	}
	return acc;
}

static q_t vq_tw(q_t* d, const uint8_t* s, uint32_t n, q_t pat)
{
	q_t acc = q_zero();
	for (uint32_t k = 0; k < n; k++, d += 16, s += 16)
	{
		vq16(fetch(s, pat), d);
		acc = q_or(acc, q_or(q_or(or4(d), or4(d + 4)), q_or(or4(d + 8), or4(d + 12))));
	}
	return acc;
}

// ---- phase B: source order to the destination layout ---------------------

// DC twiddle to RSX swizzle: each DC 4x4 block's 2x2 sub-blocks (0,0) (0,1) (1,0) (1,1) go out as (0,0) (1,0) (0,1)
// (1,1), with their middle texels swapped.
static void phase_tw_sw(q_t* out)
{
	const uint32_t nbb = (job.T / 4) * (job.T / 4);
	for (uint32_t m = 0; m < nbb; m++, out += 4)
	{
		const q_t* s = ta + pswap(m) * 4;
		out[0] = q_shuf(s[0], s[0], k_wswap);
		out[1] = q_shuf(s[2], s[2], k_wswap);
		out[2] = q_shuf(s[1], s[1], k_wswap);
		out[3] = q_shuf(s[3], s[3], k_wswap);
	}
}

// DC twiddle to rows.
static void phase_tw_lin(q_t* out)
{
	const uint32_t tq = job.T / 4;
	for (uint32_t by = 0; by < tq; by++)
		for (uint32_t bx = 0; bx < tq; bx++)
		{
			const q_t* s = ta + dcb[by * tq + bx] * 4;
			q_t* d = out + by * 4 * tq + bx;
			d[0] = q_shuf(s[0], s[2], k_row0);
			d[tq] = q_shuf(s[0], s[2], k_row1);
			d[2 * tq] = q_shuf(s[1], s[3], k_row0);
			d[3 * tq] = q_shuf(s[1], s[3], k_row1);
		}
}

// Rows to RSX swizzle.
static void phase_pl_sw(q_t* out)
{
	const uint32_t tq = job.T / 4;
	for (uint32_t by = 0; by < tq; by++)
		for (uint32_t bx = 0; bx < tq; bx++)
		{
			const q_t* s = ta + by * 4 * tq + bx;
			q_t* d = out + rsb[by * tq + bx] * 4;
			d[0] = q_shuf(s[0], s[tq], k_lo2);
			d[1] = q_shuf(s[0], s[tq], k_hi2);
			d[2] = q_shuf(s[2 * tq], s[3 * tq], k_lo2);
			d[3] = q_shuf(s[2 * tq], s[3 * tq], k_hi2);
		}
}

// ---- texture jobs -----------------------------------------------------------

static void tex_setup(const SpePixTex* q)
{
	PxJob* const j = &job;
	j->fmt = q->fmt;
	j->w = q->w;
	j->h = q->h;
	j->addr = q->addr;
	j->pitch = q->pitch;
	j->flags = q->flags;
	j->pal_base = q->pal_base;
	j->vq_cb = q->vq_cb;
	j->pal_ctrl = q->pal_ctrl;
	j->dst_ea = q->dst_ea;
	j->dst_pitch = q->dst_pitch;
	uint32_t T = j->w < j->h ? j->w : j->h;
	if (T > PX_T)
		T = PX_T;
	j->T = T;
	j->nx = j->w / T;
	j->ny = j->h / T;
	j->lnx = log2u(j->nx);
	j->yuv = j->fmt == 3;
	j->swz = (j->flags & SPEPIX_SWZ) != 0;
	j->o = j->addr & 15;
	if (j->flags & SPEPIX_PAL4)
	{
		j->kind = PX_PAL4;
		j->in_bytes = T * T / 2;
	}
	else if (j->flags & SPEPIX_PAL8)
	{
		j->kind = PX_PAL8;
		j->in_bytes = T * T;
	}
	else if (j->flags & SPEPIX_VQ)
	{
		j->kind = PX_VQ;
		j->in_bytes = T * T / 4;
	}
	else if (j->flags & SPEPIX_TW)
	{
		j->kind = PX_TW16;
		j->in_bytes = T * T * 2;
	}
	else
	{
		j->kind = PX_PL16;
		j->in_bytes = 0;
	}
	j->pat = fetch_pat(j->o, j->kind == PX_PAL4 || j->kind == PX_PAL8 || j->kind == PX_VQ);
	j->pat_ps = q_shuf(j->pat, j->pat, k_ps4);
	j->cols = j->pitch < j->w ? j->pitch : j->w;
	j->esz = (j->o + T * 2 + 15) & ~15u;
	const int igna = (j->flags & SPEPIX_IGNA) && !j->yuv;
	j->igna = igna ? h_splat(0xFF) : q_zero();
	j->fill = igna ? w_splat(0xFF000000u) : q_zero();
	const uint32_t lx = j->lnx, ly = log2u(j->ny);
	for (uint32_t x = 0; x < j->nx; x++)
	{
		j->dcx[x] = morton(x, 0, lx, ly, 1);
		j->rsx[x] = morton(x, 0, lx, ly, 0);
	}
	for (uint32_t y = 0; y < j->ny; y++)
	{
		j->dcy[y] = morton(0, y, lx, ly, 1);
		j->rsy[y] = morton(0, y, lx, ly, 0);
	}
	if (blk_T != T)
	{
		const uint32_t tq = T / 4, lq = log2u(tq);
		for (uint32_t by = 0; by < tq; by++)
			for (uint32_t bx = 0; bx < tq; bx++)
			{
				dcb[by * tq + bx] = morton(bx, by, lq, lq, 1);
				rsb[by * tq + bx] = morton(bx, by, lq, lq, 0);
			}
		blk_T = T;
	}
}

static inline uint32_t sx5(uint32_t v) { return (v << 3) | (v >> 2); }
static inline uint32_t sx6(uint32_t v) { return (v << 2) | (v >> 4); }
static inline uint32_t sx4(uint32_t v) { return (v << 4) | v; }

// cvt_pal() of one PALETTE_RAM word, as ARGB.
static uint32_t pal_argb(uint32_t ctrl, uint32_t c)
{
	switch (ctrl)
	{
	case 0: return ((c & 0x8000) ? 0xFF000000u : 0) | sx5((c >> 10) & 31) << 16 | sx5((c >> 5) & 31) << 8 | sx5(c & 31);
	case 1: return 0xFF000000u | sx5((c >> 11) & 31) << 16 | sx6((c >> 5) & 63) << 8 | sx5(c & 31);
	case 2: return sx4((c >> 12) & 15) << 24 | sx4((c >> 8) & 15) << 16 | sx4((c >> 4) & 15) << 8 | sx4(c & 15);
	default: return c;
	}
}

// The palette entries this texture reads, or its VQ codebook, into lut.
static void tex_tables(void)
{
	PxJob* const j = &job;
	if (j->kind == PX_PAL4 || j->kind == PX_PAL8)
	{
		const uint32_t n = j->kind == PX_PAL4 ? 16 : 256;
		const uint32_t ea = setup.pal_ea + j->pal_base * 4, o = ea & 15;
		mfc_get(rawb, ea & ~15u, (o + n * 4 + 15) & ~15u, TAG_TBL, 0, 0);
		px_wait(1u << TAG_TBL);
		const uint32_t igna = (j->flags & SPEPIX_IGNA) ? 0xFF000000u : 0;
		for (uint32_t i = 0; i < n; i++)
			lut[i] = w_splat(pal_argb(j->pal_ctrl, *(const uint32_t*)(rawb + o + 4 * i)) | igna);
	}
	else if (j->kind == PX_VQ)
	{
		const uint32_t o = j->vq_cb & 15;
		mfc_get(rawb, (setup.vram_ea + j->vq_cb) & ~15u, (o + 2048 + 15) & ~15u, TAG_TBL, 0, 0);
		px_wait(1u << TAG_TBL);
		// Entry order (0,0) (0,1) (1,0) (1,1) is the DC texel order of its 2x2 block, so it converts as one.
		const q_t pat = fetch_pat(o, 0);
		if (j->yuv)
			yuv_tw(lut, rawb, 64, pat);
		else
			conv16_fmt(lut, rawb, 128, pat, j->igna, j->fmt);
		// Swizzled output takes each block whole: (0,0) (1,0) (0,1) (1,1).
		if (j->swz)
			for (uint32_t i = 0; i < 256; i++)
				lut[i] = q_shuf(lut[i], lut[i], k_wswap);
	}
}

// Tile i's source into inb[i & 1]: one get (twiddled), or a list of row pieces (planar).
static void tex_in(uint32_t i)
{
	const PxJob* const j = &job;
	const uint32_t b = i & 1, tx = i & (j->nx - 1), ty = i >> j->lnx, T = j->T;
	if (j->kind == PX_PL16)
	{
		if (tx * T >= j->cols)
			return;
		const uint32_t ea = setup.vram_ea + j->addr + (ty * T * j->pitch + tx * T) * 2;
		list_fill(lin[b], T, j->esz, ea & ~15u, j->pitch * 2);
		mfc_getl(inb[b], 0, lin[b], T * 8, TAG_IN + b, 0, 0);
		return;
	}
	const uint32_t ea = setup.vram_ea + j->addr + (j->dcx[tx] | j->dcy[ty]) * j->in_bytes;
	mfc_get(inb[b], ea & ~15u, (j->o + j->in_bytes + 15) & ~15u, TAG_IN + b, 0, 0);
}

// Tile i to the destination: one put (swizzled), or a list of rows (linear).
static void tex_out(uint32_t i)
{
	const PxJob* const j = &job;
	const uint32_t b = i & 1, tx = i & (j->nx - 1), ty = i >> j->lnx, T = j->T;
	if (j->swz)
	{
		mfc_put(outb[b], j->dst_ea + (j->rsx[tx] | j->rsy[ty]) * T * T * 4, T * T * 4, TAG_OUT + b, 0, 0);
		return;
	}
	list_fill(lout[b], T, T * 4, j->dst_ea + ty * T * j->dst_pitch + tx * T * 4, j->dst_pitch);
	mfc_putl(outb[b], 0, lout[b], T * 8, TAG_OUT + b, 0, 0);
}

// One tile from inb[i & 1] to outb[i & 1]; returns the OR of its words.
static q_t tex_tile(uint32_t i)
{
	const PxJob* const j = &job;
	const uint32_t b = i & 1, tx = i & (j->nx - 1), T = j->T, tq = T / 4;
	const uint8_t* const in = inb[b];
	q_t* const out = outb[b];
	q_t acc;
	if (j->kind == PX_PL16)
	{
		if (tx * T >= j->cols)
		{
			for (uint32_t k = 0; k < T * tq; k++)
				out[k] = j->fill;
			return j->fill;
		}
		q_t* const a = j->swz ? ta : out;
		acc = j->yuv ? yuv_rows(a, in, T, j->esz, j->pat) : rows16_fmt(a, in, T, j->esz, j->pat, j->igna, j->fmt);
		if (j->swz)
			phase_pl_sw(out);
		return acc;
	}
	// Swizzled 16-bit, VQ and 8-bit palette textures go straight to the RSX order.
	if (j->swz && j->kind != PX_PAL4)
	{
		if (j->kind == PX_TW16)
			return tw16_sw_fmt(out, in, T * T / 16, j->pat, j->igna, j->fmt);
		if (j->kind == PX_PAL8)
			return pal8_sw(out, in, T * T / 16, j->pat_ps);
		return vq_sw(out, in, T * T / 64, j->pat_ps);
	}
	switch (j->kind)
	{
	case PX_TW16:
		acc = j->yuv ? yuv_tw(ta, in, T * T / 16, j->pat) : conv16_fmt(ta, in, T * T / 8, j->pat, j->igna, j->fmt);
		break;
	case PX_PAL8: acc = pal8_tw(ta, in, T * T / 16, j->pat); break;
	case PX_PAL4: acc = pal4_tw(ta, in, T * T / 32, j->pat); break;
	default: acc = vq_tw(ta, in, T * T / 64, j->pat); break;
	}
	if (j->swz)
		phase_tw_sw(out);
	else
		phase_tw_lin(out);
	return acc;
}

// One texture; returns SPEPIX_LIT | SPEPIX_OPAQUE as decode()'s checks would find them.
static uint32_t tex_job(const SpePixTex* q)
{
	tex_setup(q);
	tex_tables();
	const uint32_t n = job.nx * job.ny;
	tex_in(0);
	if (n > 1)
		tex_in(1);
	q_t acc = q_zero();
	for (uint32_t i = 0; i < n; i++)
	{
		const uint32_t b = i & 1;
		px_wait((1u << (TAG_IN + b)) | (1u << (TAG_OUT + b)));
		acc = q_or(acc, tex_tile(i));
		tex_out(i);
		if (i + 2 < n)
			tex_in(i + 2);
	}
	px_wait((1u << TAG_OUT) | (1u << (TAG_OUT + 1)));
	echo.tex_kb += job.w * job.h * 4 / 1024;
	const uint32_t m = w_extract(acc, 0) | w_extract(acc, 1) | w_extract(acc, 2) | w_extract(acc, 3);
	return ((m & 0x00FFFFFFu) ? SPEPIX_LIT : 0) | ((m & 0xFF000000u) ? SPEPIX_OPAQUE : 0);
}

// ---- framebuffer jobs -------------------------------------------------------

// A 32-bit area word address to its VRAM offset (ps3_rend.cpp vri_off): two 4 MB banks, word-interleaved.
static inline uint32_t fb_stor(uint32_t a) { return ((a & 0x3FFFFCu) << 1) | (((a >> 22) & 1) << 2); }

// Line y's words: one get per run that stays in one bank (a line crosses a 4 MB edge at most once).
static void fb_in(const SpePixFb* q, uint32_t y)
{
	const uint32_t b = y & 1;
	FbLine* const L = &fbl[b];
	const uint32_t a0 = q->sof1 + y * q->line_bytes;
	const uint32_t n = q->depth == 3 ? q->w : ((a0 & 2) + 2 * q->w + 3) >> 2;
	const uint32_t w0 = a0 & PX_VRAM_MASK & ~3u;
	uint32_t n1 = (0x400000u - (w0 & 0x3FFFFFu)) >> 2;
	if (n1 > n)
		n1 = n;
	L->n = n;
	L->n1 = n1;
	L->a0 = a0;
	const uint32_t s1 = fb_stor(w0), sz1 = ((s1 & 15) + 8 * n1 - 4 + 15) & ~15u;
	mfc_get(fbin[b], (setup.vram_ea + s1) & ~15u, sz1, TAG_IN + b, 0, 0);
	L->l1 = s1 & 15;
	L->l2 = 0;
	if (n1 < n)
	{
		const uint32_t s2 = fb_stor((w0 + n1 * 4) & PX_VRAM_MASK);
		mfc_get(fbin[b] + sz1, (setup.vram_ea + s2) & ~15u, ((s2 & 15) + 8 * (n - n1) - 4 + 15) & ~15u, TAG_IN + b, 0, 0);
		L->l2 = sz1 + (s2 & 15);
	}
}

// The first run's words are 8 bytes apart from LS offset l1: realign by 8 bytes if needed, then every other word.
static void fb_gather(const uint8_t* s, uint32_t l1, uint32_t n1)
{
	const uint32_t p = l1 >> 2, nq = (n1 + 3) / 4;
	const q_t pick = (p & 1) ? k_pick1 : k_pick0;
	q_t* d = fbw;
	if (p >= 2)
	{
		for (uint32_t m = 0; m < nq; m++, s += 32)
		{
			const q_t q1 = q_ld(s + 16);
			d[m] = q_shuf(q_shuf(q_ld(s), q1, k_rot8), q_shuf(q1, q_ld(s + 32), k_rot8), pick);
		}
	}
	else
	{
		for (uint32_t m = 0; m < nq; m++, s += 32)
			d[m] = q_shuf(q_ld(s), q_ld(s + 16), pick);
	}
}

// A line of 16-bit pixels from fbw: 565, or 0555 with alpha 255.
static inline __attribute__((always_inline)) void fb_line16(q_t* o, uint32_t n8, q_t pat, const uint32_t f)
{
	const q_t ff = h_splat(0xFF);
	for (uint32_t k = 0; k < n8; k++, o += 2)
	{
		q_t ar, gb;
		planes16(q_shuf(fbw[k], fbw[k + 1], pat), f, ff, &ar, &gb);
		o[0] = q_shuf(ar, gb, k_lo4);
		o[1] = q_shuf(ar, gb, k_hi4);
	}
}

static void fb_job(const SpePixFb* q)
{
	const uint32_t h = q->h, nq = (q->w + 3) / 4, n8 = (q->w + 7) / 8;
	// A 16-bit line starting on an odd pixel begins in the high half of its first word.
	const q_t pat0 = fetch_pat(0, 0), pat2 = fetch_pat(2, 0);
	const q_t alpha = w_splat(0xFF000000u);
	fb_in(q, 0);
	if (h > 1)
		fb_in(q, 1);
	for (uint32_t y = 0; y < h; y++)
	{
		const uint32_t b = y & 1;
		const FbLine* const L = &fbl[b];
		px_wait((1u << (TAG_IN + b)) | (1u << (TAG_OUT + b)));
		fb_gather(fbin[b], L->l1, L->n1);
		uint32_t* const fw = (uint32_t*)fbw;
		for (uint32_t k = L->n1; k < L->n; k++)
			fw[k] = *(const uint32_t*)(fbin[b] + L->l2 + 8 * (k - L->n1));
		q_t* const o = fbout[b];
		if (q->depth == 3)
		{
			// 0888: the top byte becomes alpha 255.
			for (uint32_t m = 0; m < nq; m++)
				o[m] = q_or(fbw[m], alpha);
		}
		else if (q->depth == 1)
			fb_line16(o, n8, (L->a0 & 2) ? pat2 : pat0, 1);
		else
			fb_line16(o, n8, (L->a0 & 2) ? pat2 : pat0, 0);
		mfc_put(o, q->dst_ea + y * q->dst_pitch, (q->w * 4 + 15) & ~15u, TAG_OUT + b, 0, 0);
		if (y + 2 < h)
			fb_in(q, y + 2);
	}
	px_wait((1u << TAG_OUT) | (1u << (TAG_OUT + 1)));
}
