#pragma once
// RSX Direct (G17): the types and RSX values of the emitter (rsxd_emit.h), for every side that hands it work.
#include <stdint.h>
#include <string.h>

// ---- RSX methods and values (nv40.h, gcm_sys.h) ----

#define RXM_ALPHA_EN      0x0304u
#define RXM_BLEND_EN      0x0310u
#define RXM_BLEND_EQ      0x0320u
#define RXM_SHADE         0x0368u
#define RXM_SCISSOR_H     0x08c0u
#define RXM_DEPTH_FUNC    0x0a6cu
#define RXM_VTXBUF(a)     (0x1680u + 4u * (a))
#define RXM_VTX_INVAL     0x1714u
#define RXM_VTXFMT(a)     (0x1740u + 4u * (a))
#define RXM_BEGIN_END     0x1808u
#define RXM_VB_BATCH      0x1814u
#define RXM_CULL_FACE     0x1830u
#define RXM_TEX_SIZE1     0x1840u
#define RXM_TEX_OFFSET    0x1a00u
#define RXM_VP_CONST_ID   0x1efcu
#define RXM_TEX_CACHE     0x1fd8u

#define RX_HDR(m, n)      (((uint32_t)(n) << 18) | (uint32_t)(m))
#define RX_HDR_NI(m, n)   (RX_HDR(m, n) | 0x40000000u)
#define RX_CALL(off)      ((uint32_t)(off) | 2u)
#define RX_RETURN         0x00020000u

#define RX_FUNC_ADD       0x8006u
#define RX_CULL_FRONT     0x0404u
#define RX_CULL_BACK      0x0405u
#define RX_FLAT           0x1D00u
#define RX_SMOOTH         0x1D01u
#define RX_ALWAYS         0x0207u
#define RX_TRIANGLES      5u
#define RX_TRI_STRIP      6u
#define RX_QUADS          8u

// The texenv ids and fog modes (the program keys' order, rsxd_prog.h), and the first key with a 4-byte texcoord1.
enum { RXE_DISABLED, RXE_MODULATE, RXE_REPLACE, RXE_DECAL, RXE_ADD };
enum { RXF_NONE, RXF_TABLE, RXF_VERTEX, RXF_BASE, RXF_BASE_TA };
#define RSXD_KEY_TC1U8    7
#define RSXD_KEYS         63
#define RSXD_VPS          4
#define RSXD_FP_POOL      1024
#define RSXD_VP_WORDS     96
#define RSXD_NONE         0xFFFFu

// ps3_rend.cpp's list types and draw-rule values. RXL_BG: or'ed into the list of the background param (its rules).
enum { RXL_OPAQUE = 0, RXL_TRANSLUCENT = 2, RXL_PUNCH = 4, RXL_BG = 0x80 };
enum { RXT_NONE, RXT_NODEPTH, RXT_GAME, RXT_OPAQUE };
enum { RXC_NONE, RXC_INSIDE, RXC_OUTSIDE, RXC_ALL, RXC_BASE };

// ---- inputs ----

// One texture as the RSX words of its unit; obj: its table object. valid 0: no memory (the unit is off).
typedef struct RsxdTex
{
	uint32_t offset, format, remap, size0, size1;
	uint32_t ctrl, filter, wrap;
	uint32_t obj, valid, pad[2];
} RsxdTex;

// clip_apply's outcome for one param: RXC_BASE (clip_base), or the mode and rect it sets (GL pixels).
typedef struct
{
	uint32_t mode;
	int32_t r[4];
} RsxdClip;

#define RSXD_S_ZFLOAT     1u    // s_zfloat
#define RSXD_S_SPRCULL    2u    // s_mo_sprite
#define RSXD_S_PT_DEPTH   4u    // s_do.pt_depth
#define RSXD_S_TR_NODEPTH 8u    // s_do.tr_nodepth
#define RSXD_S_OFFS       16u   // s_do.offs
#define RSXD_S_TRIL       32u   // s_do.tril
#define RSXD_S_CLAMP      64u   // s_clamp_on
#define RSXD_S_BUMP       128u  // s_bump_on
#define RSXD_S_LISTS_OFFS 256u  // s_rl.offs is set: bump_offs, fogv_offs and offc_offs can be
#define RSXD_S_TMU1       512u  // unit 1 holds a texture the program key counts (not the fog table)

// One scene's rules and constants. Floats as bits; the cv_ and glob_ fields are for rsxd_clip_of() (PPU only).
typedef struct
{
	uint32_t flags;
	uint32_t pt_blend, flat, clip, fog;
	uint32_t clamp_min[4], clamp_max[4];
	int32_t bx, by, bw, bh;
	uint32_t base_scissor;
	uint32_t screen_w, screen_h;
	uint32_t fog_scale;
	uint32_t mvp[16];
	int32_t cv_x, cv_y, cv_w, cv_h;
	float cv_cx0, cv_cy0, cv_cw, cv_ch;
	uint32_t glob_clip_x, glob_clip_y;
	uint32_t pad[5];
} RsxdScene;

// One section's state of ps3_rend.cpp: s_pass != 0, s_pass_autosort, s_tri_mode, s_depth_pass, the vertex block.
typedef struct
{
	uint32_t pass_nz, autosort, tri_mode, depth_pass;
	uint32_t vb_off, vb_loc;
	uint32_t pad[2];
} RsxdSec;

// The captured programs: the key of each GL state (rsxd_prog.h's rk_key), each key's FP select words and VP, and
// each VP's load words and constant slots (RSXD_NONE = the VP has none).
typedef struct
{
	uint8_t key_lut[2048];
	uint8_t key_vp[RSXD_KEYS + 1];
	uint16_t fp_at[RSXD_KEYS + 1], fp_n[RSXD_KEYS + 1];
	uint32_t fp_words[RSXD_FP_POOL];
	uint32_t vp_n[RSXD_VPS];
	uint32_t vp_words[RSXD_VPS][RSXD_VP_WORDS];
	uint16_t vp_mvp[RSXD_VPS], vp_mvp_n[RSXD_VPS], vp_fog[RSXD_VPS], vp_cmin[RSXD_VPS], vp_cmax[RSXD_VPS];
	uint16_t pad[4];
} RsxdProg;

// rk_key's inputs as a key_lut index.
static inline uint32_t rsxd_key_index(uint32_t tmu0, uint32_t env, uint32_t bump, uint32_t fog, uint32_t offs, uint32_t xt,
                                      uint32_t tmu1)
{
	return (tmu0 & 1) | ((env & 7) << 1) | ((bump & 1) << 4) | ((fog & 7) << 5) | ((offs & 1) << 8) | ((xt & 1) << 9) |
	       ((tmu1 & 1) << 10);
}

// ---- state ----

// The GL state the draw path sets (render state, unit 0, the dc_ calls), and the rule state of
// ps3_rend.cpp that carries from param to param (s_clip_base, s_clip_mode, s_clip_r, s_flat_shift).
typedef struct
{
	uint32_t blend_en, blend_src, blend_dst;
	uint32_t alpha_en, alpha_func, alpha_ref;
	uint32_t depth_test, depth_func, depth_mask;
	uint32_t cull_en, cull_face, front_face;
	uint32_t scissor_en;
	int32_t sc_x, sc_y, sc_w, sc_h;  // glScissor's: RSX y, with their int16/uint16 casts applied
	uint32_t color_mask;             // R 1, G 2, B 4, A 8
	uint32_t shade;
	uint32_t st_en, st_func, st_ref, st_mask, st_fail, st_zfail, st_zpass, st_wmask;
	uint32_t tex_en, texenv, bump, has_tex;
	uint32_t fog, offs, xt;
	uint32_t xt_v[9];
	RsxdTex tex;
	uint32_t clip_base, clip_mode;
	int32_t clip_r[4];
	uint32_t flat_shift;
	uint32_t pad[2];
} RsxdGl;

// The RSX registers the emitter keeps, in method order (rx_method).
enum
{
	RX_ALPHA_EN, RX_ALPHA_FUNC, RX_ALPHA_REF, RX_BLEND_EN, RX_BLEND_SRC, RX_BLEND_DST,
	RX_BLEND_EQ, RX_COLOR_MASK, RX_ST_EN, RX_ST_WMASK, RX_ST_FUNC, RX_ST_REF, RX_ST_MASK, RX_ST_FAIL, RX_ST_ZFAIL,
	RX_ST_ZPASS, RX_SHADE, RX_SC_H, RX_SC_V, RX_D_FUNC, RX_D_WRITE, RX_D_TEST,
	RX_A_POS, RX_A_COL, RX_A_TC0, RX_A_TC1, RX_F_POS, RX_F_COL, RX_F_TC0, RX_F_TC1,
	RX_CULL_FACE, RX_FRONT_FACE, RX_CULL_EN, RX_T_SIZE1,
	RX_T_OFFSET, RX_T_FORMAT, RX_T_WRAP, RX_T_ENABLE, RX_T_SWIZZLE, RX_T_FILTER, RX_T_SIZE0,
	RX_N
};

static const uint16_t rx_method[RX_N] = {
	0x0304, 0x0308, 0x030c, 0x0310, 0x0314, 0x0318,
	0x0320, 0x0324, 0x0328, 0x032c, 0x0330, 0x0334, 0x0338, 0x033c, 0x0340,
	0x0344, 0x0368, 0x08c0, 0x08c4, 0x0a6c, 0x0a70, 0x0a74,
	0x1680, 0x168c, 0x16a0, 0x16a4, 0x1740, 0x174c, 0x1760, 0x1764,
	0x1830, 0x1834, 0x183c, 0x1840,
	0x1a00, 0x1a04, 0x1a08, 0x1a0c, 0x1a10, 0x1a14, 0x1a18,
};

typedef struct RsxdState RsxdState;
struct RsxdState
{
	RsxdGl gl;
	const RsxdScene* sc;
	const RsxdSec* sec;
	const RsxdProg* prog;
	// The registers: their values, a bit per slot (2 words) where the RSX holds the value, and where it goes out next.
	uint32_t rv[RX_N];
	uint32_t rok[2], rch[2];
	// The program the RSX holds: key and VP (-1 = not known), and the constants last sent with them.
	int32_t fp_key, vp_id;
	uint32_t fog_up, xt_up[9];
	// The output words: room up to oend; rsxd_reserve() makes more.
	uint32_t* op;
	uint32_t* oend;
	uint32_t check;  // 1: rsxd_tex_dirty() keeps the dirty flags (0 on the PS3)
	// Counts the host adds up: draws, vertices, texture sends, cache invalidates, the alpha force (offc, cclamp, tril).
	uint32_t n_draw, n_vtx, n_tex, n_inval, n_offc, n_cclamp, n_tril;
	uint32_t inval_due;  // the PPU's texture unit 1 has new texels: the next draw clears the texture cache
};

// ---- SPU jobs (spe_tex.c, rsxd_job.h): no pointers, EAs as 32 bits, the same layout on the PPU and the SPU ----

enum { RSXD_JOB_EMIT = 1, RSXD_JOB_TRIS = 2 };
// RsxdDone.status.
enum { RSXD_DONE_OK, RSXD_DONE_ROOM, RSXD_DONE_BIG };

// One op of a section, in ps3_rend.cpp's order. PARAM: kind | list << 8 | textured << 16; w = pcw, isp, tsp, tcw; tex
// indexes the job's texture table. DRAW: kind | RX prim << 8 | cache clear << 16; w = vb_off, stride, first, count.
#define RSXD_OP_PARAM 1u
#define RSXD_OP_DRAW  2u
typedef struct
{
	uint32_t kind;
	uint32_t w[4];
	uint32_t tex;
	RsxdClip clip;
	uint32_t pad;
} RsxdOp;

// TRIS: a TR strip of the pass (3 or more vertices) and a param of the job (p: index into the param table).
typedef struct
{
	uint32_t first;
	uint16_t count, p;
} RsxdTriStrip;

// w = pcw, isp, tsp, tcw. tex: texture table index, RSXD_NONE when the bind failed. offs: run_offs() is set.
typedef struct
{
	uint32_t w[4];
	uint16_t tex, offs;
	RsxdClip clip;
	uint32_t pad[2];
} RsxdTriParam;

#define RSXD_TRI_VTX    1280  // TR vertices of a pass the SPU holds in local store; with more the PPU sorts
#define RSXD_TRI_STRIPS 512
#define RSXD_TRI_PARAMS 256
#define RSXD_TRI_TEX    128
#define RSXD_TRI_STRIDE 48    // the SPU's vertex: x*W, y*W, W, 1 | u, v, offset colour, 0 | RGBA, 0, 0, 0

// Written by the PPU, read by the SPU at the job's start.
typedef struct
{
	uint32_t kind, seq;
	uint32_t check;               // 1: the words go to a scratch area and never run
	uint32_t prog_ea, prog_gen;   // RsxdProg; the SPU keeps its copy while gen is the same
	uint32_t out_ea, out_words;   // the words, 128-aligned, at most out_words with the closing RETURN
	uint32_t done_ea;             // RsxdDone
	uint32_t ops_ea, nops;        // EMIT: RsxdOp
	uint32_t tex_ea, ntex;        // RsxdTex; TRIS: pad[0] is 1 while the texture's cache clear is due
	// TRIS: the pass's strips, the lists' vertices (DcVertex) and offset colours, the params; the mode of draw_tr_tris
	// and the room left in the scene's vertex block; the vertex block the SPU fills (IO offset vb_io).
	uint32_t strips_ea, nstrips, vtx_ea, offs_ea, params_ea, nparams;
	uint32_t tri_sort, first_pass, vb_room;
	uint32_t vb_ea, vb_io, vb_cap;
	uint32_t key_pcw, key_isp;    // strip_equivalent's masks
	uint32_t pad[6];
	RsxdScene sc;
	RsxdSec sec;
	RsxdGl gl;
} RsxdJob;

// Written by the SPU after the job, seq last (a fenced put).
typedef struct
{
	uint32_t seq, status, words;
	uint32_t n_draw, n_vtx, n_tex, n_inval, n_offc, n_cclamp, n_tril;
	// TRIS: triangles drawn (after the cut), runs drawn, + OPAQUE DEPTH first-pass runs and those with no depth,
	// vertices written, and the texture table entries whose cache clear went out (a bit each).
	uint32_t n_tris, n_runs, n_fp_runs, n_fp_nodepth, n_vb;
	uint32_t cleared[RSXD_TRI_TEX / 32];
	uint32_t ticks;
	uint32_t pad[4];
	RsxdGl gl;
} RsxdDone;
