#pragma once
// Bundle 4 PC check of the draw rules: a scene in plain words, and the GL calls a build of draw_scene makes for it.
#include <stdint.h>

#define R4_PASSES 8

typedef struct
{
	// The lists: vtx 6 words (x, y, w, col, u, v), strip 4 (first, count, list, param), param 4, mtri 9, mvol 5
	// (first, cull, depth, volume_last, list).
	uint32_t nvtx, nstrip, nparam, nmtri, nmvol;
	const uint32_t *vtx, *strip, *param, *mtri, *mvol;
	uint32_t max_inv_w;
	uint32_t opt, npass, pass_strip[R4_PASSES], pass_mvol[R4_PASSES];
	// The render registers.
	uint32_t isp_backgnd_d, pt_alpha_ref, fpu_shad_scale;
	uint32_t pass_autosort[R4_PASSES], mv_shared, glob_clip_x, glob_clip_y;
	uint32_t bg_param[4];
	float bg_vtx[3][4];
	uint32_t bg_col[3];
	// The viewport (GL pixels), whether it is the base scissor, and the canvas.
	int vx, vy, vw, vh, scissor;
	float cx0, cy0, cw, ch;
	// The presets: the draw rules of bundle 4, and the paths.
	uint32_t pt_depth, tr_nodepth, pt_blend, sprite_cull, flat, passes, clip_level, tri_sort;
	// Bundle 14 TR First Pass: 1 = no depth-only run that writes no depth, 2 = also in submission order.
	uint32_t first_pass;
	uint32_t merge, vertex_arrays, depth_float;
} R4Scene;

// One GL call. DRAW (a range of the vertex block) and TRI (glBegin..glEnd) carry the state they draw with.
enum { R4_CALL, R4_DRAW, R4_IMM };
typedef struct
{
	uint32_t kind, op, a[6];
	// DRAW: first, count, stride, colour shift, and a hash of the range's vertex words.
	uint32_t first, count, stride, shift, hash;
	// The state at a draw.
	uint32_t blend, src, dst, depth_mask, depth_func, depth_test, cull, cull_face, shade, alpha_test, texture;
	uint32_t scissor, sc[4], color_mask, stencil_test, stencil_func[3], stencil_op[3], stencil_mask;
	uint32_t alpha_func, alpha_ref;
	// The primitive of a DRAW or an IMM; IMM: its vertex count (hash: of its vertices).
	uint32_t prim, nv;
} R4Call;

#ifdef __cplusplus
extern "C" {
#endif
// Runs draw_scene; the calls stay valid until the build's next run. vb: the words of the scene's vertex blocks, one
// after the other (9 per vertex, a spare vertex between two blocks); a DRAW's first counts from the first block.
uint32_t r4_new_run(const R4Scene* s, const R4Call** calls, const uint32_t** vb, uint32_t* vb_used);
uint32_t r4_ref_run(const R4Scene* s, const R4Call** calls, const uint32_t** vb, uint32_t* vb_used);
// The file of before bundle 14: it has every row but TR First Pass.
uint32_t r4_old_run(const R4Scene* s, const R4Call** calls, const uint32_t** vb, uint32_t* vb_used);
// The x of every vertex the build sent through glVertex3f in GL_TRIANGLES (the volume triangles), in order.
uint32_t r4_new_tris(const float** x);
#ifdef __cplusplus
}
#endif
