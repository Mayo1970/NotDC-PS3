#pragma once
// OPTIMIZATION_PLAN 5.12 #4/#5: the SPU vector operations of spe_pix_job.h, by lane width. misc/spepix_check has a PC
// header of the same name that models the SPU's big-endian lanes.
#include <spu_intrinsics.h>

typedef vec_uint4 q_t;

#define QW(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
// An initializer from 16 shuffle bytes.
#define QI(b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15) \
	{ QW(b0, b1, b2, b3), QW(b4, b5, b6, b7), QW(b8, b9, b10, b11), QW(b12, b13, b14, b15) }

static inline q_t q_words(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return (q_t){ a, b, c, d }; }
static inline q_t q_ld(const void* p) { return *(const q_t*)p; }
static inline void q_st(void* p, q_t v) { *(q_t*)p = v; }
static inline q_t q_shuf(q_t a, q_t b, q_t m) { return spu_shuffle(a, b, (vec_uchar16)m); }
static inline q_t q_and(q_t a, q_t b) { return spu_and(a, b); }
static inline q_t q_or(q_t a, q_t b) { return spu_or(a, b); }
static inline q_t q_andc(q_t a, q_t b) { return spu_andc(a, b); }
static inline q_t q_sel(q_t a, q_t b, q_t m) { return spu_sel(a, b, m); }
static inline q_t w_splat(uint32_t v) { return spu_splats(v); }
static inline q_t h_splat(uint16_t v) { return (q_t)spu_splats(v); }
static inline uint32_t w_extract(q_t v, int i) { return spu_extract(v, i); }

static inline q_t w_add(q_t a, q_t b) { return spu_add(a, b); }
#define w_sll(v, n) spu_sl((v), (n))
#define h_sll(v, n) ((q_t)spu_sl((vec_ushort8)(v), (n)))
#define h_srl(v, n) ((q_t)spu_rlmask((vec_ushort8)(v), -(n)))
#define h_sra(v, n) ((q_t)spu_rlmaska((vec_short8)(v), -(n)))
static inline q_t h_and(q_t v, uint16_t m) { return (q_t)spu_and((vec_ushort8)v, m); }
static inline q_t b_and(q_t v, uint8_t m) { return (q_t)spu_and((vec_uchar16)v, m); }
static inline q_t h_add(q_t a, q_t b) { return (q_t)spu_add((vec_ushort8)a, (vec_ushort8)b); }
static inline q_t h_sub(q_t a, q_t b) { return (q_t)spu_sub((vec_ushort8)a, (vec_ushort8)b); }
// Signed halfword compare: all ones where a > b.
static inline q_t h_cmpgt(q_t a, q_t b) { return (q_t)spu_cmpgt((vec_short8)a, (vec_short8)b); }
