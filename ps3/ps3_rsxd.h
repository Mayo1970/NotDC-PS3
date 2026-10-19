#pragma once
// RSX Direct (G17), DRAW COST > "RSX Direct" (live): ps3_rend.cpp's run draws as raw RSX words (spu/rsxd_emit.h).
#include "types.h"
#include "spu/rsxd.h"

// The row's values. CHECK: ps3gl draws, the PPU emitter is compared. SPE: the SPE Tex Hash SPU writes the SPE Build
// runs' words; TR SORT: also the TR triangle sort. Their CHECKs: the PPU draws and the SPU's words are compared.
enum { RSXD_OFF, RSXD_PPU, RSXD_CHECK, RSXD_SPE, RSXD_SPE_CHECK, RSXD_TRIS, RSXD_TRIS_CHECK };
// s_do.rsxd in a section: none, the emitter draws, ps3gl draws and the emitter runs beside it.
enum { RSXD_SEC_NONE, RSXD_SEC_LIVE, RSXD_SEC_CHECK };
// The runs of a section: built by ps3_rend.cpp, or by the SPE Build SPU (draw_runs).
enum { RSXD_K_PPU, RSXD_K_RUNS };

// ps3_rend.cpp's state the rules read and carry; set once by draw_scene, read and written at each section.
struct RsxdRend
{
	u32* pass;
	bool* autosort;
	u32* tri_mode;
	bool* depth_pass;
	bool* clip_base;
	u32* clip_mode;
	int* clip_r;
	u32* flat_shift;
	bool* cache_valid;
	u64* t_tex;
	u32 *offc, *cclamp, *tril;
};

// The scene's rules: RSXD_S_* flags (ps3_rsxd reads TMU1 itself) and the values of rsxd_emit.h's RsxdScene.
struct RsxdSceneIn
{
	u32 flags, pt_blend, flat, clip, fog;
	const float* clamp_min;
	const float* clamp_max;
	int bx, by, bw, bh;
	bool base_scissor;
	int cv_x, cv_y, cv_w, cv_h;
	float cx0, cy0, cw, ch;
	u32 glob_clip_x, glob_clip_y;
};

// draw_tr_tris for the SPU: the lists, the rows, the vertex block's room, the merge masks and the counters it adds to.
struct RsxdTrisIn
{
	const void* vtx;
	const u32* offs;
	u32 tri_sort, first_pass, vb_room, key_pcw, key_isp;
	u32 *strips, *tris, *draws, *fp_runs, *fp_nodepth;
	u64* t_sort;
};

void ps3rsxd_init();
void ps3rsxd_stats(double secs);
// The start of a frame that may draw a scene (draw_frame, rtt_render), after ps3gl_begin_frame.
void ps3rsxd_frame();
// draw_scene with a vertex block: the scene's mode (RSXD_OFF when it cannot run).
int ps3rsxd_scene(const RsxdSceneIn* in, const RsxdRend* rend);
// A section of run draws (kind: RSXD_K_*): begin returns s_do.rsxd's value for it; end gives the RSX back to ps3gl.
u32 ps3rsxd_begin(u32 kind);
void ps3rsxd_end();
// apply_param in a section: SEC_LIVE returns the alpha force; SEC_CHECK compares with ps3gl's (alpha_or).
u32 ps3rsxd_param(const void* param, u32 list, u32 slot);
void ps3rsxd_param_check(const void* param, u32 list, u32 slot, u32 alpha_or);
// draw_vb in a section. SEC_CHECK: before ps3gl's draw; ps3rsxd_draw_check() after it.
void ps3rsxd_draw(u32 prim, u32 vb_off, u32 stride, u32 first, u32 count);
void ps3rsxd_draw_check();
// draw_tr_tris: begin (false: the PPU sorts), each TR strip of 3 or more vertices (param: its DcParam, slot: its
// SPE hash slot), then end (true: the SPU drew the pass). TR SORT CHECK: end is false, tris_check after the PPU path.
bool ps3rsxd_tris_begin(const RsxdTrisIn* in);
bool ps3rsxd_tri_strip(u32 first, u32 count, const void* param, u32 slot);
bool ps3rsxd_tris_end();
void ps3rsxd_tris_check();
// ps3_tex.cpp: the object words of the texture with GL name name, without the sampler (filter, wrap, ctrl's aniso).
void ps3rsxd_tex_name(u32 name, RsxdTex* out);
