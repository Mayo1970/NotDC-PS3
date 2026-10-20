#pragma once
// RSX Direct (G17): every frame's draws as raw RSX words (spu/rsxd_emit.h); the rgl calls take GL's names (rsxd_gl.h).
#include "types.h"
#include "spu/rsxd.h"
#include "rsxd_gl.h"

// The runs of a section: built by ps3_rend.cpp, or by the SPE Build SPU (draw_runs); the background quad and the
// modifier volumes (PPU only).
enum { RSXD_K_PPU, RSXD_K_RUNS, RSXD_K_BG, RSXD_K_VOL };

// One vertex of a block: x*W, y*W, W, 1 (the UI: x, y, 0, 1), texcoord 0, texcoord 1 or its 4 bytes, RGBA.
#pragma pack(push, 1)
struct RsxdVertex
{
	float x, y, z, w;
	float u0, v0;
	float u1, v1;
	u32 color;
};
#pragma pack(pop)

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

// The scene's rules: RSXD_S_* flags and the values of rsxd_emit.h's RsxdScene.
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

void ps3rsxd_stats(double secs);
// FOG: the emitter's unit 1 holds the texture with name name (0: none), as fog_upload binds it.
void ps3rsxd_fog_table(u32 name);
// Native Size moved the draws to a w x h surface.
void ps3rsxd_screen(u32 w, u32 h);

// rgl: the GL calls of every frame; glBegin takes 16384 vertices. Out of a section glEnd's texture is the one bound last.
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
// draw_scene with a vertex block: false when the scene cannot draw.
bool ps3rsxd_scene(const RsxdSceneIn* in, const RsxdRend* rend);
// A section of run draws (kind: RSXD_K_*); false: no section.
bool ps3rsxd_begin(u32 kind);
void ps3rsxd_end();
// apply_param in a section: returns the alpha force.
u32 ps3rsxd_param(const void* param, u32 list, u32 slot);
// draw_vb in a section.
void ps3rsxd_draw(u32 prim, u32 vb_off, u32 stride, u32 first, u32 count);
// draw_tr_tris: begin (false: the PPU sorts), each TR strip of 3 or more vertices (param: its DcParam, slot: its
// SPE hash slot), then end (true: the SPU drew the pass).
bool ps3rsxd_tris_begin(const RsxdTrisIn* in);
bool ps3rsxd_tri_strip(u32 first, u32 count, const void* param, u32 slot);
bool ps3rsxd_tris_end();
// ps3_tex.cpp: the object words of the texture with name name, without the sampler (filter, wrap, ctrl's aniso).
void ps3rsxd_tex_name(u32 name, RsxdTex* out);

// The texture calls, on GL_TEXTURE_2D with GL_RGBA bytes, into the emitter's table. rglTexHint: RSXD_HINT_*.
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
// Binds an entry over linear RSX memory another side writes (Native Size, the SPU's 2D buffers).
void rglTexWrap(void* mem, u32 pitch, u32 w, u32 h);
// That memory: linear rows for w x h texels, made once by its owner; pitch: the bytes of a row.
u8* ps3rsxd_fixed(u32 w, u32 h, u32* pitch);

// A vertex block of the frame, in the emitter's ring: count vertices, or bytes.
void* rglVringAlloc(int count, u32* off);
void* rglVringAllocRaw(u32 bytes, u32* off);

// ps3_video.c (C linkage): the boot, each frame's ring fence and its end, the memory once the RSX is idle; the
// frame's counts for ps3_rsxwatch.c.
extern "C" int ps3rsxd_init(void* ctx, u32 w, u32 h);
extern "C" void ps3rsxd_vring_begin(void);
extern "C" void ps3rsxd_vring_end(void);
extern "C" void ps3rsxd_mem_term(void);
extern "C" void ps3rsxd_frame_counts(u32* draws, u32* verts, u32* binds, u32* allocs);
