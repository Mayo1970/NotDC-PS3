#pragma once
// RSX Direct (G17), DRAW COST > "RSX Direct" (live): ps3_rend.cpp's run draws as raw RSX words (spu/rsxd_emit.h).
#include "types.h"
#include "spu/rsxd.h"

// Row values. CHECK, *_CHECK: ps3gl draws, the emitter is compared. SPE, TR SORT: an SPU writes runs. Plan 9 (A-F):
// FRAME owns the frame, then + volumes, + fog unit 1, + 2D/RTT/quads, + own textures, + own programs and vertex ring.
enum { RSXD_OFF, RSXD_PPU, RSXD_CHECK, RSXD_SPE, RSXD_SPE_CHECK, RSXD_TRIS, RSXD_TRIS_CHECK, RSXD_FRAME, RSXD_FRAME_CHECK,
       RSXD_VOLS, RSXD_VOLS_CHECK, RSXD_FOG, RSXD_FOG_CHECK, RSXD_SCREEN, RSXD_SCREEN_CHECK, RSXD_TEX, RSXD_TEX_CHECK,
       RSXD_RING, RSXD_RING_CHECK };
// The kinds of frame ps3rsxd_frame starts (RTT 0 and GAME 1 were its old bool).
enum { RSXD_F_RTT, RSXD_F_GAME, RSXD_F_2D };
// s_do.rsxd in a section: none, the emitter draws, ps3gl draws and the emitter runs beside it.
enum { RSXD_SEC_NONE, RSXD_SEC_LIVE, RSXD_SEC_CHECK };
// The runs of a section: built by ps3_rend.cpp, or by the SPE Build SPU (draw_runs); the background quad and the
// modifier volumes (PPU only).
enum { RSXD_K_PPU, RSXD_K_RUNS, RSXD_K_BG, RSXD_K_VOL };

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
// The start of a frame (RSXD_F_*) after its begin. From FRAME on the emitter takes a game frame from here, at SCREEN
// also a 2D and an RTT frame; ps3rsxd_lend gives it to ps3gl (true: it had it).
void ps3rsxd_frame(u32 kind);
bool ps3rsxd_lend();
void ps3rsxd_take(bool was);
// The emitter has the frame now (a section may open with no vertex block: the background).
bool ps3rsxd_owns();
// VOLS, FOG and their CHECKs: the emitter has the frame and draws the modifier volumes (a RSXD_K_VOL section).
bool ps3rsxd_volumes();
// FOG: the emitter has the frame and texture unit 1 is its alone (ps3gl only stores the fog table).
bool ps3rsxd_fog_live();
// FOG, FOG CHECK: the emitter's unit 1 holds the texture with GL name name (0: none), as fog_upload binds it.
void ps3rsxd_fog_table(u32 name);
// The next frame of this kind is the emitter's: ps3_video_begin_own, with this file's vring fences (C linkage).
bool ps3rsxd_own_frame(u32 kind = RSXD_F_GAME);
// SCREEN, SCREEN CHECK: the emitter has the frame and draws the Native Size quads, with no lend.
bool ps3rsxd_quads();
extern "C" void ps3rsxd_vring_begin(void);
extern "C" void ps3rsxd_vring_end(void);
// Native Size moved the draws to a w x h surface.
void ps3rsxd_screen(u32 w, u32 h);

// rgl: GL calls of the frames. While the emitter has the frame they set its state (the CHECKs: and ps3gl's), else go
// to ps3gl; glBegin takes PS3GL_MAX_VERTS. Out of a section, glEnd's texture is the one bound last.
void rglEnable(u32 cap);
void rglDisable(u32 cap);
void rglColorMask(u8 r, u8 g, u8 b, u8 a);
void rglDepthMask(u8 flag);
void rglAlphaFunc(u32 func, float ref);
void rglShadeModel(u32 mode);
void rglFrontFace(u32 mode);
void rglStencilMask(u32 mask);
void rglStencilOp(u32 fail, u32 zfail, u32 zpass);
void rglStencilFunc(u32 func, int ref, u32 mask);
void rglDepthFunc(u32 func);
void rglBlendFunc(u32 src, u32 dst);
void rglCullFace(u32 mode);
// glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode).
void rglTexEnvMode(u32 mode);
void rglScissor(int x, int y, int w, int h);
void rglViewport(int x, int y, int w, int h);
void rglDepthRange(double n, double f);
void rglClearColor(float r, float g, float b, float a);
void rglClearDepth(double d);
void rglClearStencil(int s);
void rglClear(u32 mask);
void rglApplyStates();
void rglMatrixMode(u32 mode);
void rglLoadMatrixf(const float* m);
void rglLoadIdentity();
void rglActiveTexture(u32 unit);
void rglTexBump(int on);
void rglTex1Bits(u32 bits);
void rglDcFog(int mode);
void rglDcFogScale(float scale);
void rglDcOffs(int on);
void rglDcXt(int on, const float* cmin, const float* cmax, float tril);
void rglBegin(u32 prim);
void rglColor4ub(u8 r, u8 g, u8 b, u8 a);
void rglTexCoord2f(float u, float v);
void rglVertex3f(float x, float y, float z);
void rglEnd();
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

// Stage E: the texture calls of the game frames, on GL_TEXTURE_2D with GL_RGBA bytes. TEX: the emitter's own table;
// TEX CHECK: ps3gl's objects and a shadow of each, compared; else ps3gl's. rglTexHint: RSXD_HINT_*.
enum { RSXD_HINT_LINEAR, RSXD_HINT_MIPMAPS };
void rglGenTextures(u32* name);
void rglDeleteTexture(u32 name);
void rglBindTexture(u32 name);
void rglTexParameteri(u32 pname, int v);
void rglTexHint(u32 hint);
void rglTexImage2D(int level, int w, int h, const void* rgba);
u32* rglTexDirectBegin(int w, int h, u32* pitch);
void rglTexDirectEnd();
u8* rglTexDirectLevel0(int w, int h, u32* pitch, int* swz);
u8* rglTexDirectLevel(int level);
void rglTexDirectLevelEnd(int level);
int rglTexChainLevels();
void rglTexConvertCheck(void* dst, int w, int h, const void* rgba);
// TEX: binds an entry over linear memory a ps3gl texture owns (Native Size, the SPU's 2D buffers); false: bind that one.
bool rglTexWrap(void* mem, u32 pitch, u32 w, u32 h);
// ps3_rend.cpp: the texture mode changed; the fog table and the 2D texture go, to be made again.
void rend_tex_reset();
// ps3_video_term, the RSX idle: the memory of the texture table, the ring and the program copies goes.
extern "C" void ps3rsxd_mem_term(void);

// Stage F: a vertex block of the frame (ps3gl_vring_alloc, _raw): the emitter's ring at RING and RING CHECK.
void* rglVringAlloc(int count, u32* off);
void* rglVringAllocRaw(u32 bytes, u32* off);
// ps3_video.c: a RING frame has not given ps3gl the RSX yet (no ps3gl call); a ps3gl frame's end (the ring's label).
extern "C" int ps3rsxd_gl_off(void);
extern "C" void ps3rsxd_ring_end(void);
// ps3_rend.cpp, before a frame: true = it is a RING frame, so ps3gl's state path waits for the handoff.
bool ps3rsxd_ring_next();
// The FP address of ps3gl's program for one of the emitter's copies; any other address stays.
u32 ps3rsxd_fp_map(u32 addr);
