// PVR renderer on RSX via ps3gl: OP, PT, TR lists with textures and the background color.
#include "plugs/drkPvr/Renderer_if.h"
#include "ps3_phase.h"
#include "plugs/drkPvr/regs.h"
#include "ps3_video.h"
#include "ps3_tex.h"
#include "ps3_rend_regs.h"
#include "ps3_rtt.h"
#include "ps3_screen.h"
#include "ps3_shot.h"
#include "ps3_ui.h"
#include "ps3_vmulcd.h"
#include "ps3_depth.h"
#include "ps3_spe.h"
#include "spu/spe_sprite.h"
#include "ps3_spevb.h"
#include "ps3_spetex.h"
#include "ps3_spepix.h"
#include "ps3_menu.h"
#include "ps3_presets.h"
#include "ps3_tasplit.h"
#include "ps3_stall.h"
#include "dc/pvr/pvr_if.h"
#include "dc/pvr/pvr_vram.h"
#include "dc/sh4/sh4_sqw.h"
#include "dc/sh4/sys_gate.h"
#include "dc/sh4/sh4_interpreter.h"
#include <algorithm>
#include <math.h>
#include <malloc.h>
#include <stddef.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/cond.h>

// ps3gl.h silences C-only warnings, which g++ reports as unknown pragmas.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
extern "C" {
#include "ps3gl.h"
}
#pragma GCC diagnostic pop

using namespace TASplitter;

extern s32 render_end_pending_cycles;
extern "C" int get_render_delay_preset();
extern "C" int get_render_thread_preset();
extern "C" int get_ta_fast_preset();
extern "C" int get_drop_policy_preset();
extern "C" int get_strip_merge_preset();
extern "C" int get_vertex_array_preset();
extern "C" int get_depth_float_preset();
extern "C" uint8_t* ps3gl_tex_fixed(GLsizei max_w, GLsizei max_h, GLsizei w, GLsizei h, uint32_t* pitch);
extern "C" int ps3gl_tex_chain_levels(void);
extern s32 render_isp_pending_cycles;
extern s32 render_tsp_pending_cycles;
extern s32 render_vd_pending_cycles;
extern u32 g_rdelay_n, g_rdelay_cap;   // Render Delay FLYCAST: the [rdelay] line of SPG.cpp
extern u64 g_rdelay_sum;
extern s32 g_rdelay_max;

#define MAX_VTX    (64 * 1024)
#define MAX_STRIPS (16 * 1024)
#define MAX_PARAMS (8 * 1024)

// x, y in DC screen pixels; w = eye depth (1 / the TA's 1/W).
struct DcVertex
{
	float x, y, w;
	u32 col; // DC ARGB8888
	float u, v;
};

struct DcStrip
{
	u32 first;
	u16 count;
	u8 list;
	u16 param;
};

// nullDC-v1 ModTriangle: three corners of x, y and 1/W.
struct ModTri
{
	float x0, y0, z0, x1, y1, z1, x2, y2, z2;
};

// nullDC-v1 ISP_Modvol: where this volume's triangles start, plus its ISP bits.
// A terminator entry closes the list, so entry i owns [first, next.first).
struct ModVolParam
{
	u32 first;
	u8 cull, depth, volume_last;
	u8 list;  // 0 = the opaque volume list, 1 = the translucent one (P27)
};
// P66 Volume Cap: the arrays hold 8192 triangles; the cap is MAX_MODTRI_OLD without the option.
#define MAX_MODTRI 8192
#define MAX_MODTRI_OLD 4096
#define MAX_MODVOL 256
// P10 List Passes: the passes one context keeps (Flycast MAX_PASSES is 10).
#define MAX_PASSES 8

// TR1: the SH4 thread appends raw 32-byte TA records here; StartRender decodes them.
// TR5: 2 MB against a measured high-water of 993 KB (Sonic Adventure, 30K vtx).
#define TA_RAW_SIZE  (2 * 1024 * 1024)
#define MAX_CONT_MARKS 64

// One TA context per parameter buffer, as nullDC-v1 SetCurrentTARC/SetCurrentPVRRC:
// the TA fills the one at TA_ISP_BASE, STARTRENDER draws the one at PARAM_BASE.
struct TaContext
{
	u32 addr;
	DcVertex vtx[MAX_VTX];
	// Offset colours (ARGB): of bump (3j) and vertex-fog (3a) polygons, or with TAOPT_OFFS of every vertex (3b).
	u32 offs[MAX_VTX];
	DcStrip strip[MAX_STRIPS];
	DcParam param[MAX_PARAMS];
	u32 nvtx, nstrip, nparam;
	u32 strip_first;
	float min_w, max_w;
	// reicast fZ_max: the largest usable 1/W of the frame, which alone sets the
	// near plane. min_w/max_w are kept for the stats line only.
	float max_inv_w;
	u32 mv_tris[5];
	ModTri mtri[MAX_MODTRI];
	ModVolParam mvol[MAX_MODVOL];
	u32 nmtri, nmvol;
	// Bundle 4: the TAOPT_* of this context, latched at its list init for both decoders.
	u32 opt;
	// P10 List Passes: nstrip and nmvol at each TA_LIST_CONT of the PPU replay; the SPU lists carry none.
	u32 pass_strip[MAX_PASSES], pass_mvol[MAX_PASSES];
	u32 npass;
	// Raw TA stream, decoded lazily. cont_marks holds the raw_used values at
	// which TA_LIST_CONT arrived, so the replay can split at the same points.
	u8* raw;
	u32 raw_used, raw_decoded;
	u32 cont_marks[MAX_CONT_MARKS];
	u32 n_cont, n_cont_done;
	bool raw_overflow;
	// 5.12: raw bytes already sent to the SPE, and the ring number of the last DATA that names them.
	u32 spe_pub;
	u32 spe_cmd;
	// Vertices of the last job's lists, for the render-end IRQ delay. In S3 nvtx stays at the PPU replay's.
	u32 rend_nvtx;
	// P95: a STARTRENDER took this list (a drop too), so TA Context Split may give the slot to another buffer.
	bool posted;
};
#define TA_CONTEXTS 4

// 5.12 S2: the SPU decoder writes these lists in the same layout.
static_assert(TA_CONTEXTS == SPE_CTX && MAX_VTX == SPE_MAX_VTX && MAX_STRIPS == SPE_MAX_STRIPS &&
              MAX_PARAMS == SPE_MAX_PARAMS && MAX_MODTRI == SPE_MAX_MODTRI && MAX_MODVOL == SPE_MAX_MODVOL &&
              MAX_MODTRI_OLD == SPE_MODTRI_OLD && MAX_PASSES == RR_MAX_PASSES, "SPE list sizes");
static_assert(sizeof(DcVertex) == sizeof(SpeVtx) && offsetof(DcVertex, col) == offsetof(SpeVtx, col) &&
              offsetof(DcVertex, u) == offsetof(SpeVtx, u), "SpeVtx layout");
static_assert(sizeof(DcStrip) == sizeof(SpeStrip) && offsetof(DcStrip, count) == offsetof(SpeStrip, count) &&
              offsetof(DcStrip, list) == offsetof(SpeStrip, list) && offsetof(DcStrip, param) == offsetof(SpeStrip, param),
              "SpeStrip layout");
static_assert(sizeof(DcParam) == sizeof(SpeParam) && sizeof(ModTri) == sizeof(SpeModTri), "SpeParam/SpeModTri layout");
static_assert(sizeof(ModVolParam) == sizeof(SpeModVol) && offsetof(ModVolParam, cull) == offsetof(SpeModVol, cull) &&
              offsetof(ModVolParam, volume_last) == offsetof(SpeModVol, volume_last) &&
              offsetof(ModVolParam, list) == offsetof(SpeModVol, list), "SpeModVol layout");
static TaContext s_ctx[TA_CONTEXTS];
// s_ta is the store stage's target and belongs to the SH4 thread. s_dec (replay)
// and s_rc (draw) belong to the render thread once it runs.
static TaContext* s_ta = &s_ctx[0];
static TaContext* s_dec = &s_ctx[0];
static TaContext* s_rc = &s_ctx[0];

// What the draw reads: s_rc's own lists, or the SPU's copy of them at the job's fence (5.12 S3).
struct DrawLists
{
	const DcVertex* vtx;
	const DcStrip* strip;
	const DcParam* param;
	const ModTri* mtri;
	const ModVolParam* mvol;
	const u32* offs;  // 3j: vtx's offset colours, NULL for the SPU lists
	u32 nvtx, nstrip, nparam, nmtri, nmvol;
	float min_w, max_w, max_inv_w;
	u32 mv_tris[5];
	// Bundle 4: the lists' TAOPT_*, and the PPU replay's pass marks (npass is 0 for the SPU lists).
	u32 opt, npass;
	const u32* pass_strip;
	const u32* pass_mvol;
};
static DrawLists s_rl;
// 5.12 #2: the context whose SpeOut lists s_rl points at, or -1 for the PPU replay's lists.
static int s_rl_spe = -1;
static u32 s_cur_list;
static u32 s_overflow;
// TA raw-stream health: dropped records, and the high-water mark for sizing.
static u32 s_stat_raw_of;
static u32 s_stat_raw_peak;

// TR5: the peak is taken at store time, not at replay time. A stream that grows
// after STARTRENDER latches it, or is never drawn at all, still counts here.
static inline void raw_note_peak(const TaContext* c)
{
	if (c->raw_used > s_stat_raw_peak)
		s_stat_raw_peak = c->raw_used;
}

// Plan 5.36 TA Peak Frame, latched in InitRenderer: no note per record; list init, STARTRENDER and the
// stats line note the peak instead. The stream only grows between list inits, so the peak is the same.
static bool s_peak_frame;

// P87 JIT TA SLOT: where the JIT's store-queue stores of a record also go: s_ta's next record, or 32 spare bytes when
// it has no room. Never below raw_used, so those stores never touch a stored record; only the SH4 thread moves it.
u32 g_ta_slot;
static u8 s_ta_slot_spare[32] __attribute__((aligned(32)));
static u32 s_stat_slot_hit, s_stat_slot_copy;

// The JIT loads the slot with lwz, so a buffer must end below 2 GB to have one.
static inline void ta_slot_sync()
{
	const TaContext* c = s_ta;
	const bool room = c->raw && (uintptr_t)c->raw < 0x7FE00000u && c->raw_used + 32 <= TA_RAW_SIZE;
	g_ta_slot = (u32)(uintptr_t)(room ? c->raw + c->raw_used : s_ta_slot_spare);
}

// P98 TA Line Zero: dcbz takes a 128-byte line into the L2 as zeros, with no read from memory. raw is memalign(128).
#define TA_DCBZ(p) __asm__ volatile("dcbz 0,%0" : : "r"(p) : "memory")
static u32 s_stat_ta_zero;

// P98 TA Line Zero: a record at the start of a line zeroes the next line, which holds no record yet.
static inline __attribute__((always_inline)) void ta_line_zero(const TaContext* c)
{
	if (g_ta_line_zero_preset && !(c->raw_used & 127) && c->raw_used + 256 <= TA_RAW_SIZE)
	{
		TA_DCBZ(c->raw + c->raw_used + 128);
		s_stat_ta_zero++;
	}
}

// Intensity polys scale this face color per vertex (PolyParam 1/2B/4B).
static float s_face_a = 1, s_face_r = 1, s_face_g = 1, s_face_b = 1;
// P8 Intensity Clamp: the face colour and the face offset colour as saturated ARGB bytes, taken at the header.
static u32 s_face8 = 0xFFFFFFFF, s_foffs8 = 0xFFFFFFFF;
// Bundle 4: the TAOPT_* of the context in replay (s_dec->opt), and the last User_Tile_Clip rect (P10).
static u32 s_dopt;
static u32 s_clip_rect;
static u32 s_sprite_col = 0xFFFFFFFF;
// Plan 3j: the face offset colour (PolyParam 2B) and the sprite's; offset colours are kept while s_bump_param.
static float s_foffs_a = 1, s_foffs_r = 1, s_foffs_g = 1, s_foffs_b = 1;
static u32 s_sprite_offs;
static bool s_bump_param;
// P80 Sprite Corner: sprites of the PPU replay whose fourth corner came from the plane, per stats window.
static u32 s_stat_spr_plane;
// Plan 3j Bump Maps, latched per job: the decoder keeps offset colours, the draw picks the bump programs.
static bool s_bump_on;
static u32 s_stat_bump_ppu;
// Plan 3a Fog, latched per job: 0 = off, 1 = table fog, 2 = also vertex fog, which needs the offset colours.
static u32 s_fog;
static u32 s_stat_fog_ppu, s_stat_fog_up;
// Bundle 17, 3b Offset Colour, latched per job: the draw reads the offset colours of lists that carry them all.
static bool s_offc_row;

static float s_fb_scale_x = 1.0f, s_fb_scale_y = 1.0f;

// g_rr is the draw side's copy, read only by whoever is drawing. s_snap is
// filled on the SH4 thread, s_job_rr carries one posted job's values across.
RenderRegs g_rr;
static RenderRegs s_snap;
static RenderRegs s_job_rr;

static u32 s_stat_renders, s_stat_rtt, s_stat_vtx;
static u32 s_stat_strips[5];
// Strips as the TA builds them, so a list that is captured but never drawn shows up.
static u32 s_stat_built[5];
// Modifier volume triangles per list for the stats line (1 = opaque MV, 3 = translucent MV).
static u32 s_mv_list;
static u32 s_stat_mv[5];
static double s_stat_t0;
#define STAT_WINDOW_S 5.0

// -- TR3: render thread. One job at a time (libswirl single-slot queue): the SH4 snapshots and posts,
// this thread replays the TA stream and owns every RSX call. A second frame drops; only RTT waits.
enum { JOB_NONE = 0, JOB_RENDER, JOB_RTT, JOB_PRESENT, JOB_FLIP };

static sys_ppu_thread_t s_rend_tid;
static sys_mutex_t s_rmx;
static sys_cond_t s_job_cv;    // render thread waits here for work
static sys_cond_t s_done_cv;   // SH4 thread waits here for decode or frame end
static bool s_rend_running;
static volatile int s_rend_quit;

static int  s_job_type;
static bool s_job_pending;     // posted, not taken yet
static bool s_job_busy;        // posted or in flight
static bool s_job_decoded;     // this job's TA replay has finished
static TaContext* s_job_ctx;
static TaContext* s_busy_ctx;  // context the render thread owns right now
static u32 s_job_raw_end, s_job_cont_end, s_job_fence;
// Plan 5.41 Frame Hold: the timebase tick of the emulated vblank a render's flip waits for, 0 = none.
static u64 s_snap_hold, s_job_hold;
static u32 s_hold_last_vbl, s_hold_run;
// P95 RTT Async (under s_rmx): one RTT that waits behind the job in flight; the render thread takes it next.
static bool s_q_valid;
static TaContext* s_q_ctx;
static u32 s_q_raw_end, s_q_cont_end, s_q_fence;
static RenderRegs s_q_rr;
// P95, per stats window: RTTs queued and posted at once, the SH4's time in RTT posts, list inits moved and not.
static u32 s_stat_rtt_q, s_stat_rtt_direct, s_stat_ctx_moves, s_stat_ctx_full;
static double s_stat_rtt_sh4;
static u32 s_stat_dropped;
// TR5: a vblank present the render thread was too busy to take. Unlike a dropped
// render this loses nothing - the framebuffer is unmarked, so the next vblank retries.
static u32 s_stat_fb_defer;
// P26 FB Write Watch: a StartRender since the last vblank (Flycast render_called); presents only the watch caused.
static bool s_render_since_vbl;
static u32 s_stat_fb_watch;
// P60 Present Rule, SH4 thread: a posted render waits for its flip; its FB_W_SOF1 (Flycast fb_w_cur); the last two
// render addresses (Flycast fbAddrHistory). s_flip_want (under s_rmx): flip the held frame after the job in flight.
static bool s_fb_held;
static u32 s_fbw_cur = 1, s_fbw_hist[2] = { 1, 1 };
static bool s_flip_want;
// P76 Draw Retry: a 2D present found no free buffer; the next vblank presents again.
static volatile bool s_fb_retry;
// Bundle 11, per stats window: renders held and renders flipped by an FB_R_SOF write, renders with no colour clear
// and with a region scissor, RTT waits for a buffer, 2D presents tried again and blanked, carried frames.
static u32 s_stat_held, s_stat_shown, s_stat_keep, s_stat_scis, s_stat_rtt_wait, s_stat_retry, s_stat_blank;
static u32 s_stat_carry;
// TR5: total time the SH4 spent waiting for the job slot, so the drop policy costs a number.
static double s_stat_wait;
static double s_stat_job_max;
// Render-thread busy time per window: TA replay, draw_frame (incl. texture decode), end_frame.
static double s_stat_t_dec, s_stat_t_draw, s_stat_t_end;
// Split of draw time (timebase ticks): apply_param, its texture bind, glEnd, TR sort keys, modifier volumes.
enum { DT_PARAM, DT_TEX, DT_GLEND, DT_SORT, DT_MODVOL, DT_COUNT };
static u64 s_dt[DT_COUNT];
// Depth diagnostic: the widest W range and the tightest near plane of the window.
static float s_stat_w_lo = 1e30f, s_stat_w_hi = 0.0f, s_stat_near_lo = 1e30f;
// Vertices that hit set_pos's 1/W floor: they all land on one depth, so a scene
// drawn beyond it cannot separate itself from the background plane.
static u32 s_stat_wclamp, s_stat_wtotal;
static float s_stat_bg_inv_w; static u32 s_stat_bg_zw;

static void reset_lists(TaContext* c)
{
	c->nvtx = 0;
	c->nstrip = 0;
	c->nparam = 0;
	c->strip_first = 0;
	c->min_w = 1e30f;
	c->max_w = 0.0f;
	c->max_inv_w = 1.0f;
	c->nmtri = 0;
	c->nmvol = 0;
	c->npass = 0;
	memset(c->mv_tris, 0, sizeof(c->mv_tris));
	c->rend_nvtx = 0;
}

// Bundle 4: the decode options a context takes at its list init (ps3_presets.cpp; all live).
static u32 ta_opts_now()
{
	return (g_int_clamp_preset ? TAOPT_INT_CLAMP : 0) | (g_face_reset_preset ? TAOPT_FACE_RESET : 0) |
	       (g_tr_volumes_preset ? TAOPT_TR_VOL : 0) | (g_volume_cap_preset ? TAOPT_VOL_8K : 0) |
	       (g_tile_clip_preset ? TAOPT_TILE_CLIP : 0) | (g_sprite_corner_preset ? TAOPT_SPR_PLANE : 0) |
	       (g_offset_colour_preset ? TAOPT_OFFS : 0);
}

// The stored stream is per list-init, so it is cleared with the decoded lists.
static void reset_raw(TaContext* c)
{
	// The SPU must have pulled the old bytes before the SH4 writes over them.
	if (g_spe_on)
		ps3spe_wait_cmd(c->spe_cmd);
	raw_note_peak(c);
	c->raw_used = 0;
	c->raw_decoded = 0;
	c->n_cont = 0;
	c->n_cont_done = 0;
	c->raw_overflow = false;
	c->spe_pub = 0;
	c->posted = false;
	// The SPU decoder takes the same options with the RESET, so both decoders give one set of lists.
	c->opt = ta_opts_now();
	if (g_spe_on)
		ps3spe_reset((u32)(c - s_ctx), c->opt);
	ta_slot_sync();
}

// 5.12: sends the stored bytes the SPE has not seen yet.
static inline void spe_flush(TaContext* c)
{
	if (g_spe_on && c->raw_used > c->spe_pub)
	{
		c->spe_cmd = ps3spe_data((u32)(c - s_ctx), c->raw, c->spe_pub, c->raw_used);
		c->spe_pub = c->raw_used;
	}
}

static inline void spe_note(TaContext* c)
{
	if (g_spe_on && c->raw_used - c->spe_pub >= SPE_BATCH)
		spe_flush(c);
}

// Every byte up to pos goes out before the fence, so the SPE answer covers the whole replay.
static inline u32 spe_fence(TaContext* c)
{
	if (!g_spe_on || !c)
		return 0;
	spe_flush(c);
	return ps3spe_fence((u32)(c - s_ctx), c->raw_used);
}

static void reset_all_contexts()
{
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		s_ctx[i].addr = 0xFFFFFFFF;
		reset_lists(&s_ctx[i]);
		reset_raw(&s_ctx[i]);
	}
	s_ta = s_dec = s_rc = &s_ctx[0];
	ta_slot_sync();
}

// rttq_check: begin slots
static TaContext* find_context(u32 addr)
{
	for (u32 i = 0; i < TA_CONTEXTS; i++)
		if (s_ctx[i].addr == addr)
			return &s_ctx[i];
	return 0;
}

// The contexts a job may read: the last one decoded, the job's in flight and the waiting RTT's (P95).
static void rth_used(TaContext* used[3])
{
	used[0] = s_rc;
	used[1] = used[2] = 0;
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	used[1] = s_job_busy ? s_busy_ctx : 0;
	used[2] = s_q_valid ? s_q_ctx : 0;
	sysMutexUnlock(s_rmx);
}

// P95: a job in flight or waiting holds this context: its stream until the replay, its lists until the draw ends.
static bool rth_holds(const TaContext* c)
{
	TaContext* used[3];
	rth_used(used);
	return c == used[1] || c == used[2];
}

// Switches the TA to the context of its parameter buffer; a new buffer reuses the oldest slot.
static void set_ta_context(u32 addr)
{
	static u32 s_next_slot;
	addr &= 0xF00000;
	if (s_ta->addr == addr)
		return;
	spe_flush(s_ta);
	TaContext* c = find_context(addr);
	if (!c)
	{
		TaContext* used[3];
		rth_used(used);
		c = &s_ctx[s_next_slot++ % TA_CONTEXTS];
		// Skip the slot being drawn and the ones a job holds.
		for (u32 i = 0; i < TA_CONTEXTS && (c == used[0] || c == used[1] || c == used[2]); i++)
			c = &s_ctx[s_next_slot++ % TA_CONTEXTS];
		c->addr = addr;
		reset_lists(c);
		reset_raw(c);
	}
	s_ta = c;
	ta_slot_sync();
}

// P95 TA Context Split: the TA's address moves to a slot no job holds (no address first, then one a STARTRENDER
// took; a list none took stays), so nobody waits for the old slot's job. False = none free: the old wait.
static bool ctx_move()
{
	TaContext* used[3];
	rth_used(used);
	for (u32 pass = 0; pass < 2; pass++)
		for (u32 i = 0; i < TA_CONTEXTS; i++)
		{
			TaContext* c = &s_ctx[i];
			if (c == s_ta || c == used[0] || c == used[1] || c == used[2] ||
			    (pass == 0 ? c->addr != 0xFFFFFFFF : !c->posted))
				continue;
			c->addr = s_ta->addr;
			s_ta->addr = 0xFFFFFFFF;
			reset_lists(c);
			s_ta = c;
			ta_slot_sync();
			s_stat_ctx_moves++;
			return true;
		}
	s_stat_ctx_full++;
	return false;
}
// rttq_check: end slots

// misc/bump_check compiles the code from here to "bump_check: end" against a model of the offset colours.
static inline u32 clamp_byte(float f)
{
	// Flycast c0f0a758d: NaN is max. PPC fctiwz turns NaN into 0x80000000, which clamped to black.
	if (f != f)
		return 255;
	s32 v = (s32)(f * 255.0f);
	return v < 0 ? 0 : (v > 255 ? 255 : (u32)v);
}

static inline u32 float_argb(float a, float r, float g, float b)
{
	return (clamp_byte(a) << 24) | (clamp_byte(r) << 16) | (clamp_byte(g) << 8) | clamp_byte(b);
}

// P8, Flycast vert_face_base_color (Red Dog): each channel is face * intensity / 256 on the saturated bytes,
// and alpha is the face alpha.
static inline u32 int_scale(u32 face, u32 sat)
{
	return (face & 0xFF000000u) | (((((face >> 16) & 0xFF) * sat) >> 8) << 16) |
	       (((((face >> 8) & 0xFF) * sat) >> 8) << 8) | (((face & 0xFF) * sat) >> 8);
}

static inline u32 intensity_argb(float i)
{
	if (s_dopt & TAOPT_INT_CLAMP)
		return int_scale(s_face8, clamp_byte(i));
	return float_argb(s_face_a, s_face_r * i, s_face_g * i, s_face_b * i);
}

// Flycast vert_face_offs_color: the face offset colour scaled by the vertex's offset intensity, alpha unscaled.
static inline u32 intensity_offs(float i)
{
	if (s_dopt & TAOPT_INT_CLAMP)
		return int_scale(s_foffs8, clamp_byte(i));
	return float_argb(s_foffs_a, s_foffs_r * i, s_foffs_g * i, s_foffs_b * i);
}

// Only the opaque volume list is kept, as nullDC-v1 does; P27 TR Volumes keeps the translucent one too.
static inline bool mv_kept(u32 list)
{
	return list == ListType_Opaque_Modifier_Volume ||
	       (list == ListType_Translucent_Modifier_Volume && (s_dopt & TAOPT_TR_VOL));
}

static inline u32 mv_cap()
{
	return (s_dopt & TAOPT_VOL_8K) ? MAX_MODTRI : MAX_MODTRI_OLD;
}

// Floor on 1/W to keep W finite. 1e-4 merged every plane past W=10000 (SC5's video backdrop failed
// DepthMode 4 against the background); reicast has none, 1e-9 keeps x*W in float range.
#define INV_W_FLOOR 1e-9f

// reicast update_fz: one integer compare drops negatives, NaN and anything at or
// above 1024*1024, so a single rogue vertex cannot pull the near plane down.
static inline void track_inv_w(float inv_w)
{
	union { float f; s32 i; } z, m;
	z.f = inv_w;
	m.f = s_dec->max_inv_w;
	if (m.i < z.i && z.i < 0x49800000)
		s_dec->max_inv_w = inv_w;
}

// Modifier volume corners carry no color, so only their depth joins the range.
static inline void track_w(float inv_w)
{
	float safe = (inv_w >= INV_W_FLOOR) ? inv_w : INV_W_FLOOR;
	float w = 1.0f / safe;
	if (w < s_dec->min_w) s_dec->min_w = w;
	if (w > s_dec->max_w) s_dec->max_w = w;
	s_stat_wtotal++;
	if (safe != inv_w) s_stat_wclamp++;
	track_inv_w(inv_w);
}

// The TA sends 1/W; the floor keeps W finite (NaN also takes the floor).
static inline void set_pos(DcVertex* v, float x, float y, float inv_w)
{
	float safe = (inv_w >= INV_W_FLOOR) ? inv_w : INV_W_FLOOR;
	v->x = x;
	v->y = y;
	v->w = 1.0f / safe;
	track_w(inv_w);
}

// 16-bit UVs are the top half of a float.
static inline float uv16(u32 uv)
{
	union { u32 i; float f; } c;
	c.i = uv << 16;
	return c.f;
}

static inline DcVertex* vtx_slot()
{
	if (s_dec->nvtx < MAX_VTX)
		return &s_dec->vtx[s_dec->nvtx];
	s_overflow++;
	return 0;
}

// One-part vertex: position and color in the same TA packet.
#define VTX_ONE(pkt, color)                                          \
	DcVertex* v = vtx_slot();                                        \
	if (!v) return;                                                  \
	set_pos(v, (pkt)->xyz[0], (pkt)->xyz[1], (pkt)->xyz[2]);        \
	v->col = (color);                                                \
	s_dec->nvtx++;

// Two-part vertex: part A has the position, part B the color and the commit.
#define VTX_A(pkt)                                                   \
	DcVertex* v = vtx_slot();                                        \
	if (!v) return;                                                  \
	set_pos(v, (pkt)->xyz[0], (pkt)->xyz[1], (pkt)->xyz[2]);

// Vertex i's offset colour; argb is evaluated only when the polygon keeps it (3j, 3a; 3b: every polygon).
#define VTX_OFFS(i, argb)                                            \
	do { if (s_bump_param) s_dec->offs[i] = (argb); } while (0)
// 3b: a vertex type with no offset colour gets 0, so both decoders write one value for each vertex.
#define VTX_NO_OFFS() VTX_OFFS(v - s_dec->vtx, 0)

#define VTX_B(color, offs)                                           \
	if (s_dec->nvtx >= MAX_VTX) return;                              \
	s_dec->vtx[s_dec->nvtx].col = (color);                           \
	VTX_OFFS(s_dec->nvtx, offs);                                     \
	s_dec->nvtx++;

#define VTX_B_KEEP()                                                 \
	if (s_dec->nvtx >= MAX_VTX) return;                              \
	s_dec->nvtx++;

struct Ps3VertexDecoder
{
	static void StartList(u32 ListType) { s_cur_list = ListType; }
	// nullDC-v1 closes the volume list with a terminator holding the final count.
	static void EndList(u32 ListType)
	{
		if (!mv_kept(ListType) || s_dec->nmvol >= MAX_MODVOL)
			return;
		ModVolParam& p = s_dec->mvol[s_dec->nmvol++];
		p.first = s_dec->nmtri;
		p.cull = p.depth = p.volume_last = 0;
		p.list = (u8)(ListType == ListType_Translucent_Modifier_Volume);
	}

	template <class T>
	static void add_param(T* pp)
	{
		// When full, keep rewriting the last slot: wrong state, but no overrun.
		if (s_dec->nparam < MAX_PARAMS)
			s_dec->nparam++;
		DcParam& p = s_dec->param[s_dec->nparam - 1];
		p.pcw = pp->pcw;
		p.isp = pp->isp;
		p.tsp = pp->tsp;
		p.tcw = pp->tcw;
		if (s_dopt & TAOPT_TILE_CLIP)
		{
			// P10: the user clip rect in the reserved bits (ISP 19:0, PCW 9:8), 0 without a clip mode. The mode
			// stays in the PCW (Flycast takes it from every header, Group_En or not).
			const u32 clip = ((p.pcw.full >> 16) & 3) >= 2 ? s_clip_rect : 0;
			p.isp.full = (p.isp.full & 0xFFF00000u) | (clip & 0xFFFFFu);
			p.pcw.full = (p.pcw.full & ~0x300u) | ((clip >> 20) << 8);
		}
		s_bump_param = s_bump_on && pp->pcw.Texture && pp->tcw.NO_PAL.PixelFmt == 4;
		// 3a: a vertex-fog polygon keeps its offset colours too; their alpha is the fog.
		if (s_fog >= 2 && pp->pcw.Texture && pp->pcw.Offset && pp->tsp.FogCtrl == 1)
			s_bump_param = true;
		// 3b Offset Colour: every polygon keeps them, as the SPU decoder does with the same option.
		if (s_dopt & TAOPT_OFFS)
			s_bump_param = true;
	}

	static void AppendPolyParam0(TA_PolyParam0* pp) { add_param(pp); }
	static void AppendPolyParam1(TA_PolyParam1* pp)
	{
		add_param(pp);
		s_face_a = pp->FaceColorA; s_face_r = pp->FaceColorR;
		s_face_g = pp->FaceColorG; s_face_b = pp->FaceColorB;
		s_face8 = float_argb(s_face_a, s_face_r, s_face_g, s_face_b);
	}
	static void AppendPolyParam2A(TA_PolyParam2A* pp) { add_param(pp); }
	static void AppendPolyParam2B(TA_PolyParam2B* pp)
	{
		s_face_a = pp->FaceColorA; s_face_r = pp->FaceColorR;
		s_face_g = pp->FaceColorG; s_face_b = pp->FaceColorB;
		s_foffs_a = pp->FaceOffsetA; s_foffs_r = pp->FaceOffsetR;
		s_foffs_g = pp->FaceOffsetG; s_foffs_b = pp->FaceOffsetB;
		s_face8 = float_argb(s_face_a, s_face_r, s_face_g, s_face_b);
		s_foffs8 = float_argb(s_foffs_a, s_foffs_r, s_foffs_g, s_foffs_b);
	}
	static void AppendPolyParam3(TA_PolyParam3* pp) { add_param(pp); }
	static void AppendPolyParam4A(TA_PolyParam4A* pp) { add_param(pp); }
	// Two-volume polys: only volume 0 is drawn.
	static void AppendPolyParam4B(TA_PolyParam4B* pp)
	{
		s_face_a = pp->FaceColor0A; s_face_r = pp->FaceColor0R;
		s_face_g = pp->FaceColor0G; s_face_b = pp->FaceColor0B;
		s_face8 = float_argb(s_face_a, s_face_r, s_face_g, s_face_b);
	}

	static void StartPolyStrip() { s_dec->strip_first = s_dec->nvtx; }
	static void EndPolyStrip()
	{
		u32 count = s_dec->nvtx - s_dec->strip_first;
		StripCount++;
		if (count < 3 || s_dec->nparam == 0 || s_dec->nstrip >= MAX_STRIPS)
			return;
		DcStrip& s = s_dec->strip[s_dec->nstrip++];
		s.first = s_dec->strip_first;
		s.count = (u16)(count > 0xFFFF ? 0xFFFF : count);
		s.list = (u8)s_cur_list;
		s.param = (u16)(s_dec->nparam - 1);
		if (s_cur_list < 5)
			s_stat_built[s_cur_list]++;
	}

	// Types without UV write 0, so every vertex field is defined (the SPE decoder compares them, 5.12 S2).
	static void AppendPolyVertex0(TA_Vertex0* p) { VTX_ONE(p, p->BaseCol); v->u = v->v = 0.0f; VTX_NO_OFFS(); }
	static void AppendPolyVertex1(TA_Vertex1* p)
	{
		VTX_ONE(p, float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); v->u = v->v = 0.0f; VTX_NO_OFFS();
	}
	static void AppendPolyVertex2(TA_Vertex2* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = v->v = 0.0f; VTX_NO_OFFS(); }
	static void AppendPolyVertex3(TA_Vertex3* p)
	{
		VTX_ONE(p, p->BaseCol); v->u = p->u; v->v = p->v;
		VTX_OFFS(v - s_dec->vtx, p->OffsCol);
	}
	static void AppendPolyVertex4(TA_Vertex4* p)
	{
		VTX_ONE(p, p->BaseCol); v->u = uv16(p->u); v->v = uv16(p->v);
		VTX_OFFS(v - s_dec->vtx, p->OffsCol);
	}
	static void AppendPolyVertex5A(TA_Vertex5A* p) { VTX_A(p); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex5B(TA_Vertex5B* p)
	{
		VTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB), float_argb(p->OffsA, p->OffsR, p->OffsG, p->OffsB));
	}
	static void AppendPolyVertex6A(TA_Vertex6A* p) { VTX_A(p); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex6B(TA_Vertex6B* p)
	{
		VTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB), float_argb(p->OffsA, p->OffsR, p->OffsG, p->OffsB));
	}
	static void AppendPolyVertex7(TA_Vertex7* p)
	{
		VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = p->u; v->v = p->v;
		VTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt));
	}
	static void AppendPolyVertex8(TA_Vertex8* p)
	{
		VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = uv16(p->u); v->v = uv16(p->v);
		VTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt));
	}
	static void AppendPolyVertex9(TA_Vertex9* p) { VTX_ONE(p, p->BaseCol0); v->u = v->v = 0.0f; VTX_NO_OFFS(); }
	static void AppendPolyVertex10(TA_Vertex10* p) { VTX_ONE(p, intensity_argb(p->BaseInt0)); v->u = v->v = 0.0f; VTX_NO_OFFS(); }
	// Volume 0 is drawn, so its offset colour is the one kept.
	static void AppendPolyVertex11A(TA_Vertex11A* p)
	{
		VTX_A(p); v->col = p->BaseCol0; v->u = p->u0; v->v = p->v0;
		VTX_OFFS(v - s_dec->vtx, p->OffsCol0);
	}
	static void AppendPolyVertex11B(TA_Vertex11B* p) { VTX_B_KEEP(); }
	static void AppendPolyVertex12A(TA_Vertex12A* p)
	{
		VTX_A(p); v->col = p->BaseCol0; v->u = uv16(p->u0); v->v = uv16(p->v0);
		VTX_OFFS(v - s_dec->vtx, p->OffsCol0);
	}
	static void AppendPolyVertex12B(TA_Vertex12B* p) { VTX_B_KEEP(); }
	static void AppendPolyVertex13A(TA_Vertex13A* p)
	{
		VTX_A(p); v->col = intensity_argb(p->BaseInt0); v->u = p->u0; v->v = p->v0;
		VTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt0));
	}
	static void AppendPolyVertex13B(TA_Vertex13B* p) { VTX_B_KEEP(); }
	static void AppendPolyVertex14A(TA_Vertex14A* p)
	{
		VTX_A(p); v->col = intensity_argb(p->BaseInt0); v->u = uv16(p->u0); v->v = uv16(p->v0);
		VTX_OFFS(v - s_dec->vtx, intensity_offs(p->OffsInt0));
	}
	static void AppendPolyVertex14B(TA_Vertex14B* p) { VTX_B_KEEP(); }

	static void AppendSpriteParam(TA_SpriteParam* spr)
	{
		add_param(spr);
		s_sprite_col = spr->BaseCol;
		s_sprite_offs = spr->OffsCol;
	}

	// Sprite corners A,B,C arrive with UVs; D is sent without z or UV, so it completes the parallelogram.
	// Strip order D,C,A,B makes the quad from two triangles. P80 Sprite Corner: D from the plane of A, B and C.
	static DcVertex s_spr[4];
	static float s_spr_z[3];
	static void AppendSpriteVertexA(TA_Sprite1A* sv)
	{
		s_spr[0].x = sv->x0; s_spr[0].y = sv->y0; s_spr_z[0] = sv->z0;
		s_spr[1].x = sv->x1; s_spr[1].y = sv->y1; s_spr_z[1] = sv->z1;
		s_spr[2].x = sv->x2;
	}
	static void AppendSpriteVertexB(TA_Sprite1B* sv)
	{
		if (s_dec->nvtx + 4 > MAX_VTX)
		{
			s_overflow++;
			return;
		}
		s_spr[2].y = sv->y2; s_spr_z[2] = sv->z2;
		float z3 = s_spr_z[0] + s_spr_z[2] - s_spr_z[1];
		s_spr[0].u = uv16(sv->u0); s_spr[0].v = uv16(sv->v0);
		s_spr[1].u = uv16(sv->u1); s_spr[1].v = uv16(sv->v1);
		s_spr[2].u = uv16(sv->u2); s_spr[2].v = uv16(sv->v2);
		s_spr[3].u = s_spr[0].u + s_spr[2].u - s_spr[1].u;
		s_spr[3].v = s_spr[0].v + s_spr[2].v - s_spr[1].v;
		if (s_dopt & TAOPT_SPR_PLANE)
		{
			// The bits in, the bits out: spr_plane is the SPU decoder's text too.
			const u32 x[4] = { spr_bits(s_spr[0].x), spr_bits(s_spr[1].x), spr_bits(s_spr[2].x), spr_bits(sv->x3) };
			const u32 y[4] = { spr_bits(s_spr[0].y), spr_bits(s_spr[1].y), spr_bits(s_spr[2].y), spr_bits(sv->y3) };
			const u32 z[3] = { spr_bits(s_spr_z[0]), spr_bits(s_spr_z[1]), spr_bits(s_spr_z[2]) };
			const u32 u[3] = { spr_bits(s_spr[0].u), spr_bits(s_spr[1].u), spr_bits(s_spr[2].u) };
			const u32 t[3] = { spr_bits(s_spr[0].v), spr_bits(s_spr[1].v), spr_bits(s_spr[2].v) };
			u32 o[3];
			if (spr_plane(x, y, z, u, t, o))
			{
				memcpy(&z3, &o[0], 4);
				memcpy(&s_spr[3].u, &o[1], 4);
				memcpy(&s_spr[3].v, &o[2], 4);
				s_stat_spr_plane++;
			}
		}
		set_pos(&s_spr[0], s_spr[0].x, s_spr[0].y, s_spr_z[0]);
		set_pos(&s_spr[1], s_spr[1].x, s_spr[1].y, s_spr_z[1]);
		set_pos(&s_spr[2], s_spr[2].x, s_spr[2].y, s_spr_z[2]);
		set_pos(&s_spr[3], sv->x3, sv->y3, z3);

		static const int order[4] = { 3, 2, 0, 1 };
		StartPolyStrip();
		for (int i = 0; i < 4; i++)
		{
			s_dec->vtx[s_dec->nvtx] = s_spr[order[i]];
			s_dec->vtx[s_dec->nvtx].col = s_sprite_col;
			VTX_OFFS(s_dec->nvtx, s_sprite_offs);
			s_dec->nvtx++;
		}
		EndPolyStrip();
	}

	static void StartModVol(TA_ModVolParam* param)
	{
		s_mv_list = s_cur_list;
		if (!mv_kept(s_cur_list) || s_dec->nmvol >= MAX_MODVOL)
			return;
		ModVolParam& p = s_dec->mvol[s_dec->nmvol++];
		p.first = s_dec->nmtri;
		p.cull = (u8)param->isp.CullMode;
		p.depth = (u8)param->isp.DepthMode;
		p.volume_last = (u8)param->pcw.Volume;
		p.list = (u8)(s_cur_list == ListType_Translucent_Modifier_Volume);
	}
	static void ModVolStripEnd() {}
	// Part A opens the triangle, part B closes it: both test the same cap.
	static void AppendModVolVertexA(TA_ModVolA* mvv)
	{
		s_dec->mv_tris[s_mv_list % 5]++;
		if (!mv_kept(s_cur_list) || s_dec->nmtri >= mv_cap())
			return;
		ModTri& t = s_dec->mtri[s_dec->nmtri];
		t.x0 = mvv->x0; t.y0 = mvv->y0; t.z0 = mvv->z0;
		t.x1 = mvv->x1; t.y1 = mvv->y1; t.z1 = mvv->z1;
		t.x2 = mvv->x2;
		track_w(mvv->z0);
		track_w(mvv->z1);
	}
	static void AppendModVolVertexB(TA_ModVolB* mvv)
	{
		if (!mv_kept(s_cur_list) || s_dec->nmtri >= mv_cap())
			return;
		ModTri& t = s_dec->mtri[s_dec->nmtri++];
		t.y2 = mvv->y2;
		t.z2 = mvv->z2;
		track_w(mvv->z2);
	}

	// P10: the rect as Flycast's tileclip_val keeps it; add_param packs it into the next headers.
	static void SetTileClip(u32 xmin, u32 ymin, u32 xmax, u32 ymax)
	{
		s_clip_rect = xmin | (xmax << 6) | (ymin << 12) | (ymax << 17);
	}
	static void TileClipMode(u32 mode) {}

	// As nullDC-v1: list init clears the context, a soft reset keeps the lists. ListInit/ListCont
	// pick the context, because by replay time TA_ISP_BASE may point elsewhere.
	static void ListCont()
	{
		// P10 List Passes: a TA_LIST_CONT ends a pass; the draw takes each pass's strips and volumes by these marks.
		if (s_dec->npass < MAX_PASSES)
		{
			s_dec->pass_strip[s_dec->npass] = s_dec->nstrip;
			s_dec->pass_mvol[s_dec->npass] = s_dec->nmvol;
			s_dec->npass++;
		}
	}
	// Flycast ta_parse_reset: P63 makes the face colours white (NHL 2K2), P10 puts the clip rect back.
	static void ListInit()
	{
		reset_lists(s_dec);
		if (s_dopt & TAOPT_FACE_RESET)
		{
			s_face_a = s_face_r = s_face_g = s_face_b = 1;
			s_foffs_a = s_foffs_r = s_foffs_g = s_foffs_b = 1;
			s_face8 = s_foffs8 = 0xFFFFFFFF;
		}
		if (s_dopt & TAOPT_TILE_CLIP)
			s_clip_rect = TA_CLIP_RESET;
	}
	static void SoftReset() {}
};
DcVertex Ps3VertexDecoder::s_spr[4];
float Ps3VertexDecoder::s_spr_z[3];
// bump_check: end

// The decode stage replays bytes the store stage already answered for, so it
// must not raise the list-end interrupts a second time.
FifoSplitter<Ps3VertexDecoder, false> TileAccel;

// Store stage: keeps list state and fires the list-end interrupts, and decodes
// nothing. All vertex work happens later, on the replay (TArender.MD TR1).
struct TaStoreDecoder
{
#define TA_STORE_NOP1(name, type) static void name(type*) {}
	static void StartList(u32) {}
	static void EndList(u32) {}
	static void StartPolyStrip() {}
	static void EndPolyStrip() {}
	static void ModVolStripEnd() {}
	static void SetTileClip(u32, u32, u32, u32) {}
	static void TileClipMode(u32) {}
	TA_STORE_NOP1(AppendPolyParam0, TA_PolyParam0)
	TA_STORE_NOP1(AppendPolyParam1, TA_PolyParam1)
	TA_STORE_NOP1(AppendPolyParam2A, TA_PolyParam2A)
	TA_STORE_NOP1(AppendPolyParam2B, TA_PolyParam2B)
	TA_STORE_NOP1(AppendPolyParam3, TA_PolyParam3)
	TA_STORE_NOP1(AppendPolyParam4A, TA_PolyParam4A)
	TA_STORE_NOP1(AppendPolyParam4B, TA_PolyParam4B)
	TA_STORE_NOP1(AppendPolyVertex0, TA_Vertex0)
	TA_STORE_NOP1(AppendPolyVertex1, TA_Vertex1)
	TA_STORE_NOP1(AppendPolyVertex2, TA_Vertex2)
	TA_STORE_NOP1(AppendPolyVertex3, TA_Vertex3)
	TA_STORE_NOP1(AppendPolyVertex4, TA_Vertex4)
	TA_STORE_NOP1(AppendPolyVertex5A, TA_Vertex5A)
	TA_STORE_NOP1(AppendPolyVertex5B, TA_Vertex5B)
	TA_STORE_NOP1(AppendPolyVertex6A, TA_Vertex6A)
	TA_STORE_NOP1(AppendPolyVertex6B, TA_Vertex6B)
	TA_STORE_NOP1(AppendPolyVertex7, TA_Vertex7)
	TA_STORE_NOP1(AppendPolyVertex8, TA_Vertex8)
	TA_STORE_NOP1(AppendPolyVertex9, TA_Vertex9)
	TA_STORE_NOP1(AppendPolyVertex10, TA_Vertex10)
	TA_STORE_NOP1(AppendPolyVertex11A, TA_Vertex11A)
	TA_STORE_NOP1(AppendPolyVertex11B, TA_Vertex11B)
	TA_STORE_NOP1(AppendPolyVertex12A, TA_Vertex12A)
	TA_STORE_NOP1(AppendPolyVertex12B, TA_Vertex12B)
	TA_STORE_NOP1(AppendPolyVertex13A, TA_Vertex13A)
	TA_STORE_NOP1(AppendPolyVertex13B, TA_Vertex13B)
	TA_STORE_NOP1(AppendPolyVertex14A, TA_Vertex14A)
	TA_STORE_NOP1(AppendPolyVertex14B, TA_Vertex14B)
	TA_STORE_NOP1(AppendSpriteParam, TA_SpriteParam)
	TA_STORE_NOP1(AppendSpriteVertexA, TA_Sprite1A)
	TA_STORE_NOP1(AppendSpriteVertexB, TA_Sprite1B)
	TA_STORE_NOP1(StartModVol, TA_ModVolParam)
	TA_STORE_NOP1(AppendModVolVertexA, TA_ModVolA)
	TA_STORE_NOP1(AppendModVolVertexB, TA_ModVolB)
#undef TA_STORE_NOP1
	static void ListInit() {}
	static void ListCont() {}
	static void SoftReset() {}
};

FifoSplitter<TaStoreDecoder> TaStore;

// -- TR4: table-driven store stage. The store only needs record size and list state, so one table
// lookup per 32-byte record replaces the splitter (libswirl ta_fsm); a state says what the next word is.
enum
{
	TAS_NOLIST = 0,  // no list open; a header must open one
	TAS_P32,         // list open, not a modifier volume, 32-byte vertices
	TAS_P64,         // same, 64-byte vertices
	TAS_SKIP32,      // second half of a 64-byte record, then TAS_P32
	TAS_SKIP64,      // second half of a 64-byte record, then TAS_P64
	TAS_MV,          // modifier volume list open; its vertices are 64 bytes
	TAS_SKIPMV,      // second half of a modvol vertex, then TAS_MV
	TAS_COUNT = 8
};
#define TAS_SLOW 0x80

// Indexed (state << 8) | (ParaType << 5) | (obj_ctrl >> 2). UV_16bit/Gouraud/Shadow never change a
// record's size, so the key drops them; without the mask a sprite aliases a 16-bit-UV polygon.
static u8 s_fsm[TAS_COUNT * 256];
static u8 s_fsm_state = TAS_NOLIST;
static u32 s_store_list = ListType_None;
// Latched once at InitRenderer, so a run never changes store path mid-stream.
static bool s_ta_fast;

// P66 TA Data Gate (Flycast ta_ctx == NULL, Vigilante 8): no list init is current before the first one and after a
// STARTRENDER of the TA's context. With the row ON the TA records of that time are dropped.
static bool s_ta_nolist = true;
static bool s_ta_closed;
// TA Fast Store with the gate open: the flat SQ path's test, so a closed gate costs it nothing.
static bool s_flat_ok;
static u32 s_stat_gate;

static void ta_gate_set(bool nolist)
{
	s_ta_nolist = nolist;
	s_ta_closed = nolist && g_ta_data_gate_preset != 0;
	s_flat_ok = s_ta_fast && !s_ta_closed;
}

static inline u32 fsm_key(u32 para, u32 objc)
{
	return (para << 5) | ((objc >> 2) & 31);
}

// The 64-byte vertex types, from the table above poly_data_type_id() in ta.h.
static inline bool ta_vtx_is_64(u32 pdid)
{
	return pdid == 5 || pdid == 6 || pdid == 11 || pdid == 12 || pdid == 13 || pdid == 14;
}

// Built from drkPvr's ta_type_lut, so the two store paths size every record the
// same way. TileAccel.Init() must have run first.
static void ta_fsm_build()
{
	for (u32 st = 0; st < TAS_COUNT; st++)
	for (u32 pt = 0; pt < 8; pt++)
	for (u32 k = 0; k < 32; k++)
	{
		u8 e;
		if (st == TAS_SKIP32)
			e = TAS_P32;
		else if (st == TAS_SKIP64)
			e = TAS_P64;
		else if (st == TAS_SKIPMV)
			e = TAS_MV;
		else if (st >= TAS_COUNT - 1)
			e = TAS_NOLIST;  // state 7 is unused
		else switch (pt)
		{
		case ParamType_User_Tile_Clip:
			e = (u8)st;      // 32 bytes, ignored by the store stage
			break;

		case ParamType_Polygon_or_Modifier_Volume:
			if (st == TAS_NOLIST)
				e = TAS_SLOW | (u8)st;   // opens the list, so it needs ListType
			else if (st == TAS_MV)
				e = TAS_MV;              // 32-byte header, 64-byte vertices
			else
			{
				u32 uid = TileAccel.ta_type_lut[k << 2];
				u8 vtx = ta_vtx_is_64((u8)uid) ? TAS_P64 : TAS_P32;
				if ((uid >> 30) == SZ64)
					e = (vtx == TAS_P64) ? TAS_SKIP64 : TAS_SKIP32;
				else
					e = vtx;
			}
			break;

		case ParamType_Sprite:
			// Sprite vertices are 64 bytes, like modvol ones, so a modvol list
			// keeps its state either way.
			if (st == TAS_NOLIST)
				e = TAS_SLOW | (u8)st;
			else
				e = (st == TAS_MV) ? TAS_MV : TAS_P64;
			break;

		case ParamType_Vertex_Parameter:
			if (st == TAS_P64)
				e = TAS_SKIP64;
			else if (st == TAS_MV)
				e = TAS_SKIPMV;
			else
				e = (u8)st;  // 32-byte vertex, or a stray one with no list open
			break;

		default:             // End_Of_List and the reserved types
			e = TAS_SLOW | (u8)st;
			break;
		}
		s_fsm[(st << 8) | (pt << 5) | k] = e;
	}
}

static void ta_store_reset()
{
	s_fsm_state = TAS_NOLIST;
	s_store_list = ListType_None;
}

// The records the table cannot answer from obj_ctrl alone: they open or close a
// list, so they need the live list state.
static u8 ta_store_slow(Ta_Dma* d, u8 st)
{
	PCW pcw = d->pcw;
	switch (pcw.ParaType)
	{
	case ParamType_End_Of_List:
		// As the splitter: an end with no list open adopts the parameter's own
		// ListType, and spg_SchedListEndIrq drops anything above 4.
		if (s_store_list == ListType_None)
			s_store_list = pcw.ListType;
		spg_SchedListEndIrq(s_store_list);
		s_store_list = ListType_None;
		spe_flush(s_ta);
		return TAS_NOLIST;

	case ParamType_Polygon_or_Modifier_Volume:
	case ParamType_Sprite:
		if (s_store_list == ListType_None)
			s_store_list = pcw.ListType;
		if (IsModVolList(s_store_list))
			return TAS_MV;
		// The list is open and ordinary now, so the table row answers the rest.
		return s_fsm[(TAS_P32 << 8) | fsm_key(pcw.ParaType, pcw.obj_ctrl)];

	default:
		// Object_List_Set and the two reserved types. The splitter calls die()
		// here, which on PS3 is a silent freeze; log once and skip instead.
		{
			static bool warned;
			if (!warned)
			{
				warned = true;
				printf("[ps3rend] TA parameter type %u ignored\n", pcw.ParaType);
			}
		}
		return st;
	}
}

// The table entry for state st and a record whose first word is pcw. ParaType is bits 31..29 and
// obj_ctrl the low byte, in both endians.
static inline u32 ta_fsm_entry(u32 st, u32 pcw)
{
	return s_fsm[(st << 8) | ((pcw >> 24) & 0xE0) | ((pcw >> 2) & 0x1F)];
}

// One record at p: the next state. A list open or close takes ta_store_slow.
static inline u32 ta_fsm_step(u32 st, u32 pcw, Ta_Dma* p)
{
	const u32 e = ta_fsm_entry(st, pcw);
	return (e & TAS_SLOW) ? ta_store_slow(p, (u8)(e & 7)) : e;
}

// The state carries across calls, so a 64-byte record split over two SQ or DMA blocks resumes by itself.
static void ta_store_fsm_run(Ta_Dma* p, u32 words)
{
	u32 st = s_fsm_state;
	for (u32 i = 0; i < words; i++, p++)
		st = ta_fsm_step(st, p->pcw.full, p);
	s_fsm_state = (u8)st;
}

// rttq_check: begin waits
// The SH4 must not disturb a raw stream the render thread is still replaying.
static void rth_wait_decode(TaContext* c)
{
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	// P95: the RTT that waits behind the job in flight is not replayed yet either.
	while ((s_job_busy && s_busy_ctx == c && !s_job_decoded) || (s_q_valid && s_q_ctx == c))
		sysCondWait(s_done_cv, 0);
	sysMutexUnlock(s_rmx);
}

static void rth_wait_idle()
{
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	while (s_job_busy)
		sysCondWait(s_done_cv, 0);
	sysMutexUnlock(s_rmx);
}

static bool rth_busy()
{
	if (!s_rend_running)
		return false;
	sysMutexLock(s_rmx, 0);
	bool busy = s_job_busy;
	sysMutexUnlock(s_rmx);
	return busy;
}
// rttq_check: end waits

// Replays [from, to) of the stored stream. Joining SQ and DMA blocks is safe: a 64-byte record split
// at a block end still feeds both halves to the same decoder calls.
static void decode_range(TaContext* c, u32 from, u32 to)
{
	Ta_Dma* p = (Ta_Dma*)(c->raw + from);
	Ta_Dma* end = (Ta_Dma*)(c->raw + to) - 1;
	do
	{
		p = TileAccel.TaCmd(p, end);
	}
	while (p <= end);
}

// Decodes [raw_decoded, raw_end), split at TA_LIST_CONT; raw_end is latched at STARTRENDER, later bytes
// decode next time. Nothing new keeps the lists, so a second STARTRENDER redraws them (nullDC-v1).
static void decode_pending(TaContext* c, u32 raw_end, u32 cont_end)
{
	if (!c || !c->raw)
		return;
	s_dec = c;
	s_dopt = c->opt;
	if (c->raw_decoded == 0)
		TileAccel.ListInit();

	u32 pos = c->raw_decoded;
	while (c->n_cont_done < cont_end && c->cont_marks[c->n_cont_done] <= raw_end)
	{
		u32 mark = c->cont_marks[c->n_cont_done++];
		if (mark > pos)
		{
			decode_range(c, pos, mark);
			pos = mark;
		}
		TileAccel.ListCont();
	}
	if (raw_end > pos)
		decode_range(c, pos, raw_end);
	c->raw_decoded = raw_end;
}

// On overflow the store stage still runs over the source bytes, so list ends still interrupt; only the
// replay copy is dropped. flat (5.17): one inline FSM step on the PCW read before the copy, not from it.
template<bool probe, bool flat>
static inline void store_sq(u32* data)
{
	if (s_ta_closed)
	{
		s_stat_gate++;
		return;
	}
	if (probe)
		g_ta_split.t_store = ta_stamp();
	const u32 pcw = data[0];
	TaContext* c = s_ta;
	Ta_Dma* rec = (Ta_Dma*)data;
	if (c->raw && c->raw_used + 32 <= TA_RAW_SIZE)
	{
		u8* dst = c->raw + c->raw_used;
		ta_line_zero(c);
		memcpy(dst, data, 32);
		c->raw_used += 32;
		ta_slot_sync();
		if (!s_peak_frame)
			raw_note_peak(c);
		rec = (Ta_Dma*)dst;
		spe_note(c);
	}
	else if (!c->raw_overflow)
	{
		c->raw_overflow = true;
		s_stat_raw_of++;
	}
	if (probe)
		g_ta_split.t_copy = ta_stamp();
	if (!s_ta_fast)
	{
		verify(TaStore.TaCmd != 0);
		TaStore.TaCmd(rec, rec);
	}
	else if (flat)
		s_fsm_state = (u8)ta_fsm_step(s_fsm_state, pcw, rec);
	else
		ta_store_fsm_run(rec, 1);
	if (probe)
	{
		g_ta_split.t_fsm = ta_stamp();
		g_ta_split.hit = 1;
	}
}

// A 5.17 sample takes the stamped copy; the other blocks run the same code without stamps.
void ps3ta_store_sq(u32* data)
{
	if (g_ta_split.armed)
		store_sq<true, false>(data);
	else
		store_sq<false, false>(data);
}

// The flat path's flushes: TA SQ Direct and TA Fast Store ON, and TA polygon data, which is area 4
// below 8 MB, as TAWriteSQ decides. YUV and direct VRAM keep the old path.
static inline bool sqw_flat_target(u32 dest)
{
	if (!g_ta_sq_direct_preset || !s_flat_ok)
		return false;
	const u32 area = (dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area;
	return area == 4 && (dest & 0x01800000) == 0;
}

// Plan 5.44 RAM SQ Direct: a flush to main RAM (QACR area 3) is a 32-byte copy into the RAM image, which is all
// WriteMemBlock_nommu_ptr does there while the cached interpreter has no blocks to drop.
static inline bool sqw_ram_target(u32 dest)
{
	const u32 area = (dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area;
	return area == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;
}

// addr is the target in any RAM mirror; bit 5 of dest picks the queue.
static inline void sqw_ram_copy(u32 addr, u32 dest)
{
	g_vram_split.sq_mem++;
	memcpy(&mem_b[addr & (RAM_MASK & ~31u)], &sq_both[dest & 0x20], 32);
}

// The rare cases of do_sqw_nommu. It reaches them by tail calls, so its fast path needs no stack frame.
// The old path; do_sqw_nommu has already counted the flush for the probe.
static __attribute__((noinline)) void sqw_chain(u32 dest)
{
	CCALL(CC_SQW);
	do_sqw_target<false>(dest);
}

// A 5.17 sample, stamped from here on whichever path the flush takes.
static __attribute__((noinline)) void sqw_sampled(u32 dest)
{
	const u64 t_entry = ta_stamp();
	CCALL(CC_SQW);
	if (sqw_flat_target(dest))
	{
		store_sq<true, true>((u32*)&sq_both[dest & 0x20]);
		TaCalls++;
	}
	else if (sqw_ram_target(dest))
	{
		g_ta_split.t_store = ta_stamp();
		sqw_ram_copy(dest, dest);
		g_ta_split.t_copy = ta_stamp();
		g_ta_split.ram = 1;
	}
	else
	{
		g_ta_split.armed = 1;   // ps3ta_store_sq stamps the store if the old path reaches it
		do_sqw_target<false>(dest);
	}
	ta_split_leave(t_entry);
}

// No raw buffer yet, or it is full.
static __attribute__((noinline)) void sqw_flat_general(u32* data)
{
	store_sq<false, true>(data);
	TaCalls++;
}

// After the copy: an SPE batch is due, or the record opens or closes a list.
static __attribute__((noinline)) void sqw_finish(u32 pcw, Ta_Dma* rec)
{
	spe_note(s_ta);
	s_fsm_state = (u8)ta_fsm_step(s_fsm_state, pcw, rec);
	TaCalls++;
}

// One flat body for a TA polygon flush (TA SQ Direct, plan 5.17: no TAWriteSQ / libPvr_TaSQ / ps3ta_store_sq).
static inline __attribute__((always_inline)) void sqw_ta_flat(u32 dest, bool peak)
{
	CCALL(CC_SQW);
	u32* data = (u32*)&sq_both[dest & 0x20];
	TaContext* c = s_ta;
	if (!c->raw || c->raw_used + 32 > TA_RAW_SIZE)
		return sqw_flat_general(data);
	// The PCW comes from the source, so the FSM never reloads the copy it just stored.
	const u32 pcw = data[0];
	u8* dst = c->raw + c->raw_used;
	ta_line_zero(c);
	memcpy(dst, data, 32);
	c->raw_used += 32;
	ta_slot_sync();
	if (peak)
		raw_note_peak(c);
	const u32 e = ta_fsm_entry(s_fsm_state, pcw);
	if ((e & TAS_SLOW) || (g_spe_on && c->raw_used - c->spe_pub >= SPE_BATCH))
		return sqw_finish(pcw, (Ta_Dma*)dst);
	s_fsm_state = (u8)e;
	TaCalls++;
}

// The JIT's PREF target without the MMU: the flat TA body, or a main RAM flush (RAM SQ Direct, plan 5.44); the
// rest: the old path.
template<bool peak>
static inline __attribute__((always_inline)) void do_sqw_flat(u32 dest)
{
	if (++g_ta_split.calls == g_ta_split.next)
		return sqw_sampled(dest);
	if (!sqw_flat_target(dest))
	{
		if (!sqw_ram_target(dest))
			return sqw_chain(dest);
		CCALL(CC_SQW);
		return sqw_ram_copy(dest, dest);
	}
	sqw_ta_flat(dest, peak);
}

void FASTCALL do_sqw_nommu(u32 dest)
{
	do_sqw_flat<true>(dest);
}

// Plan 5.36 TA Peak Frame: the JIT calls this one; the peak is noted per frame, not per record.
void FASTCALL do_sqw_nommu_frame(u32 dest)
{
	do_sqw_flat<false>(dest);
}

// P87 JIT TA SLOT: the record's stores are at slot already (mask bit i: word i; bit 0: pcw is word 0), so nothing is
// read back from the store queue. A slot that is not s_ta's next record (r11 poisoned) or an unaligned PREF: the copy.
template<bool peak>
static inline __attribute__((always_inline)) void do_sqw_slot_t(u32 dest, u32 slot, u32 pcw, u32 mask)
{
	if (++g_ta_split.calls == g_ta_split.next)
		return sqw_sampled(dest);
	if (!sqw_flat_target(dest))
	{
		if (!sqw_ram_target(dest))
			return sqw_chain(dest);
		CCALL(CC_SQW);
		return sqw_ram_copy(dest, dest);
	}
	// g_ta_slot is the spare when s_ta has no room, and the spare is never s_ta's next record.
	TaContext* c = s_ta;
	if (slot != g_ta_slot || slot != (u32)(uintptr_t)(c->raw + c->raw_used) || (dest & 0x1F))
	{
		s_stat_slot_copy++;
		return sqw_ta_flat(dest, peak);
	}
	CCALL(CC_SQW);
	s_stat_slot_hit++;
	u8* dst = c->raw + c->raw_used;
	ta_line_zero(c);
	if (mask != 0xFF)
	{
		// The words the block did not store were stored long before: no wait on their read.
		const u32* q = (const u32*)&sq_both[dest & 0x20];
		u32* w = (u32*)dst;
		if (!(mask & 1))
			pcw = q[0];
		for (u32 i = 0; i < 8; i++)
			if (!(mask & (1u << i)))
				w[i] = q[i];
	}
	c->raw_used += 32;
	ta_slot_sync();
	if (peak)
		raw_note_peak(c);
	const u32 e = ta_fsm_entry(s_fsm_state, pcw);
	if ((e & TAS_SLOW) || (g_spe_on && c->raw_used - c->spe_pub >= SPE_BATCH))
		return sqw_finish(pcw, (Ta_Dma*)dst);
	s_fsm_state = (u8)e;
	TaCalls++;
}

void FASTCALL do_sqw_slot(u32 dest, u32 slot, u32 pcw, u32 mask)
{
	do_sqw_slot_t<true>(dest, slot, pcw, mask);
}

void FASTCALL do_sqw_slot_frame(u32 dest, u32 slot, u32 pcw, u32 mask)
{
	do_sqw_slot_t<false>(dest, slot, pcw, mask);
}

// P74 MMU SQ Direct: with MMUCR.AT on, the UTLB's sq_remap slot gives the target, as in do_sqw<true>. TA polygon
// data and main RAM then take the flat bodies, under the same rows as without the MMU.
static inline bool sqw_mmu_ta(u32 addr)
{
	return g_ta_sq_direct_preset && s_flat_ok && ((addr >> 26) & 7) == 4 && (addr & 0x01800000) == 0;
}

static inline bool sqw_mmu_ram(u32 addr)
{
	return ((addr >> 26) & 7) == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;
}

// The row OFF: the old chain, with its own probe count.
static __attribute__((noinline)) void sqw_mmu_old(u32 dest)
{
	do_sqw<true>(dest);
}

// The row ON and a target the flat bodies do not take; do_sqw_mmu has already counted the flush for the probe.
static __attribute__((noinline)) void sqw_mmu_chain(u32 dest)
{
	CCALL(CC_SQW);
	do_sqw_target<true>(dest);
}

// A 5.17 sample with the row ON, stamped as sqw_sampled. flatten: sqw_sampled keeps its stamped store inline.
static __attribute__((noinline, flatten)) void sqw_mmu_sampled(u32 dest)
{
	const u64 t_entry = ta_stamp();
	CCALL(CC_SQW);
	const u32 addr = mmu_TranslateSQW(dest & 0xFFFFFFE0);
	if (sqw_mmu_ta(addr))
	{
		store_sq<true, true>((u32*)&sq_both[dest & 0x20]);
		TaCalls++;
	}
	else if (sqw_mmu_ram(addr))
	{
		g_ta_split.t_store = ta_stamp();
		sqw_ram_copy(addr, dest);
		g_ta_split.t_copy = ta_stamp();
		g_ta_split.ram = 1;
	}
	else
	{
		g_ta_split.armed = 1;
		do_sqw_target<true>(dest);
	}
	ta_split_leave(t_entry);
}

// The JIT's PREF target with MMUCR.AT on.
void FASTCALL do_sqw_mmu(u32 dest)
{
	if (!g_mmu_sq_direct_preset)
		return sqw_mmu_old(dest);
	if (++g_ta_split.calls == g_ta_split.next)
		return sqw_mmu_sampled(dest);
	const u32 addr = mmu_TranslateSQW(dest & 0xFFFFFFE0);
	if (!sqw_mmu_ta(addr))
	{
		if (!sqw_mmu_ram(addr))
			return sqw_mmu_chain(dest);
		CCALL(CC_SQW);
		return sqw_ram_copy(addr, dest);
	}
	sqw_ta_flat(dest, !s_peak_frame);
}

void ps3ta_store_dma(u32* data, u32 size)
{
	if (s_ta_closed)
	{
		s_stat_gate += size;
		return;
	}
	TaContext* c = s_ta;
	u32 bytes = size * 32;
	Ta_Dma* p = (Ta_Dma*)data;
	if (c->raw && c->raw_used + bytes <= TA_RAW_SIZE)
	{
		u8* dst = c->raw + c->raw_used;
		memcpy(dst, data, bytes);
		c->raw_used += bytes;
		ta_slot_sync();
		if (!s_peak_frame)
			raw_note_peak(c);
		p = (Ta_Dma*)dst;
		spe_note(c);
	}
	else if (!c->raw_overflow)
	{
		c->raw_overflow = true;
		s_stat_raw_of++;
	}
	if (s_ta_fast)
	{
		ta_store_fsm_run(p, size);
		return;
	}
	verify(TaStore.TaCmd != 0);
	Ta_Dma* end = p + size - 1;
	do
	{
		p = TaStore.TaCmd(p, end);
	}
	while (p <= end);
}

// P50: SPG gives a new text once a second on the SH4 thread; the draw reads the slot that was finished last.
static char s_fps_text[2][64];
static volatile u32 s_fps_slot;

void SetFpsText(char* text)
{
	const u32 n = s_fps_slot ^ 1;
	snprintf(s_fps_text[n], sizeof(s_fps_text[n]), "%s", text);
	__asm__ volatile("lwsync" ::: "memory");
	s_fps_slot = n;
}

// Canvas scale from SPG: 240p modes and SCALER_CTL hscale change the TA screen size.
void SetFbScale(float x, float y)
{
	s_fb_scale_x = x;
	s_fb_scale_y = y;
}

// spepix_check: begin vram (misc/spepix_check compiles the spepix_check regions as the SPU convert's reference)
// VRAM is stored as two interleaved 32-bit banks.
static u32 vri_off(u32 addr)
{
	addr &= VRAM_MASK;
	return ((addr & 0x3FFFFC) << 1) | (((addr >> 22) & 1) << 2) | (addr & 3);
}

static u32 vri(u32 addr)
{
	return *(u32*)&PVRPARAMS.vram[vri_off(addr)];
}

static void vwi(u32 addr, u32 val)
{
	*(u32*)&PVRPARAMS.vram[vri_off(addr & ~3u)] = val;
}

// A native word holds two pixels; the low half is the left one, as in ps3_tex.cpp.
static u32 vri16(u32 addr)
{
	u32 w = *(u32*)&PVRPARAMS.vram[vri_off(addr & ~3u)];
	return (addr & 2) ? (w >> 16) : (w & 0xFFFF);
}
// spepix_check: end vram

// nullDC-v1 UsingAutoSort(): with FPU_PARAM_CFG bit 21 set, the region array header picks the mode.
static bool using_autosort()
{
	if (((FPU_PARAM_CFG >> 21) & 1) == 0)
		return (ISP_FEED_CFG & 1) == 0;
	return ((vri(REGION_BASE) >> 29) & 1) == 0;
}

// P10 Region Array, Flycast getRegionTileAddrAndSize: an entry is 5 words, 6 with FPU_PARAM_CFG bit 21 or a
// pre-sort bit in it; a first entry whose list pointers are all empty (bit 31) is skipped.
static void region_entry(u32* addr, u32* size)
{
	u32 a = REGION_BASE;
	const bool type1 = ((FPU_PARAM_CFG >> 21) & 1) == 0;
	u32 sz = (type1 ? 5 : 6) * 4;
	bool empty = true;
	for (int i = type1 ? 4 : 5; i > 0 && empty; i--)
		empty = (vri(a + i * 4) & 0x80000000u) != 0;
	if (empty)
		a += sz;
	if ((vri(a) >> 29) & 1)
		sz = 6 * 4;
	*addr = a;
	*size = sz;
}

// Flycast getRegionTileClipping and setTileClipping: the box of the region array's tiles (X in bits 2-7, Y in bits
// 8-13, bit 31 ends the array), in pixels, inside TA_GLOB_TILE_CLIP. Needs glob_clip_x/y.
static void snapshot_region_box()
{
	const bool type1 = ((FPU_PARAM_CFG >> 21) & 1) == 0;
	u32 addr = REGION_BASE, size = (type1 ? 5 : 6) * 4;
	if (g_region_array_preset)
		region_entry(&addr, &size);
	u32 x0 = 63, x1 = 0, y0 = 63, y1 = 0, left = 3000, tile;
	do
	{
		tile = vri(addr);
		const u32 x = (tile >> 2) & 63, y = (tile >> 8) & 63;
		x0 = x < x0 ? x : x0;
		x1 = x > x1 ? x : x1;
		y0 = y < y0 ? y : y0;
		y1 = y > y1 ? y : y1;
		addr += size;
	}
	while (!(tile >> 31) && --left);
	s_snap.reg_ok = left != 0;
	s_snap.reg_x0 = x0 * 32;
	s_snap.reg_y0 = y0 * 32;
	s_snap.reg_x1 = std::min((x1 + 1) * 32, s_snap.glob_clip_x);
	s_snap.reg_y1 = std::min((y1 + 1) * 32, s_snap.glob_clip_y);
}

// Each pass's region-array entry (Flycast getRegionSettings), for passes 0..last: its sort mode, and whether its
// opaque and translucent volume pointers are one list (P27; the plan also asks for a pointer that is not empty).
static void snapshot_region(u32 last)
{
	const bool on = g_region_array_preset != 0;
	const bool type1 = ((FPU_PARAM_CFG >> 21) & 1) == 0;
	u32 addr = REGION_BASE, size = (type1 ? 5 : 6) * 4;
	if (on)
		region_entry(&addr, &size);
	s_snap.mv_shared = false;
	for (u32 k = 0; k < RR_MAX_PASSES; k++)
	{
		const u32 e = addr + k * size;
		// OFF: every pass sorts as nullDC-v1 UsingAutoSort says for the first entry.
		s_snap.pass_autosort[k] = !on ? s_snap.autosort : type1 ? (ISP_FEED_CFG & 1) == 0 : ((vri(e) >> 29) & 1) == 0;
		const u32 w8 = vri(e + 8);
		if (k <= last && w8 == vri(e + 16) && !(w8 & 0x80000000u))
			s_snap.mv_shared = true;
	}
	s_snap.autosort = s_snap.pass_autosort[0];
}

// Background plane (ISP_BACKGND_T): 3 vertices; the PVR extends their plane over the whole screen.
// Bitfields are extracted by hand; the regs.h struct layout is little-endian.
static void background_plane(BgVertex* out, DcParam* param)
{
	u32 param_base = s_snap.param_base;
	u32 bgt = ISP_BACKGND_T;
	u32 tag_offset = bgt & 0x7;
	u32 tag_address = (bgt >> 3) & 0x1FFFFF;
	u32 skip = (bgt >> 24) & 0x7;
	u32 shadow = (bgt >> 27) & 1;

	u32 strip_base = param_base + tag_address * 4;
	u32 vs = (3 + skip) * 4;
	if ((FPU_SHAD_SCALE & 0x100) && shadow)
		vs += skip * 4;
	u32 ptr = strip_base + 3 * 4 + tag_offset * vs;

	// Drawn as the first opaque poly, like nullDC-v1: depth always passes, no culling.
	ISP_TSP isp;
	isp.full = vri(strip_base);
	param->isp = isp;
	param->isp.DepthMode = 7;
	param->isp.CullMode = 0;
	param->tsp.full = vri(strip_base + 4);
	param->tcw.full = vri(strip_base + 8);
	param->pcw.full = 0;
	param->pcw.Texture = isp.Texture;
	param->pcw.Offset = isp.Offset;
	param->pcw.Gouraud = isp.Gouraud;
	param->pcw.Shadow = shadow;

	u32 col_off = 12; // x, y, 1/W
	if (isp.Texture)
		col_off += isp.UV_16b ? 4 : 8;
	for (u32 k = 0; k < 3; k++, ptr += vs)
	{
		u32 xi = vri(ptr), yi = vri(ptr + 4);
		memcpy(&out[k].x, &xi, 4);
		memcpy(&out[k].y, &yi, 4);
		out[k].u = out[k].v = 0.0f;
		if (isp.Texture && isp.UV_16b)
		{
			// nullDC-v1 decode_pvr_vertex: U in the low half, V in the high half.
			u32 uv = vri(ptr + 12);
			out[k].u = uv16(uv & 0xFFFF);
			out[k].v = uv16(uv >> 16);
		}
		else if (isp.Texture)
		{
			u32 ui = vri(ptr + 12), vi = vri(ptr + 16);
			memcpy(&out[k].u, &ui, 4);
			memcpy(&out[k].v, &vi, 4);
		}
		out[k].col = vri(ptr + col_off);
	}
}

// TR2: the framebuffer read state the present path uses, latched at vblank.
static void snapshot_present_regs()
{
	s_snap.fb_r_sof1 = FB_R_SOF1;
	s_snap.fb_r_size = FB_R_SIZE;
	s_snap.fb_r_depth = FB_R_CTRL.fb_depth;
	// P26: what Flycast's ReadFramebuffer reads besides those. P76: VO_CONTROL bit 3 blanks the video.
	s_snap.fb_r_sof2 = FB_R_SOF2;
	s_snap.fb_interlace = SPG_CONTROL.interlace != 0;
	s_snap.fb_field2 = SPG_STATUS.fieldnum != 0;
	s_snap.fb_vclk = FB_R_CTRL.vclk_div != 0;
	s_snap.blank = g_blank_video_preset && ((VO_CONTROL >> 3) & 1);
	s_snap.vo_border_col = VO_BORDER_COL;
}

// TR2/TR3: everything the draw path reads, latched on the SH4 thread at STARTRENDER into s_snap.
// Order matters: param_base feeds background_plane, which reads VRAM.
static void snapshot_render_regs()
{
	s_snap.param_base = PARAM_BASE & 0xF00000;
	s_snap.isp_backgnd_d = ISP_BACKGND_D;
	s_snap.pt_alpha_ref = PT_ALPHA_REF;
	s_snap.fb_w_sof1 = FB_W_SOF1;
	s_snap.fb_w_sof2 = FB_W_SOF2;
	s_snap.fb_w_ctrl = FB_W_CTRL;
	s_snap.fb_w_linestride = FB_W_LINESTRIDE;
	s_snap.fb_x_clip_min = FB_X_CLIP.min;
	s_snap.fb_x_clip_max = FB_X_CLIP.max;
	s_snap.fb_y_clip_min = FB_Y_CLIP.min;
	s_snap.fb_y_clip_max = FB_Y_CLIP.max;
	s_snap.pal_ram_ctrl = PAL_RAM_CTRL;
	s_snap.text_control = TEXT_CONTROL;
	s_snap.fpu_shad_scale = FPU_SHAD_SCALE;
	s_snap.fb_scale_x = s_fb_scale_x;
	s_snap.fb_scale_y = s_fb_scale_y;
	// Bundle 12 Screen Scaler (P10): the canvas also follows the scaler's vertical factor and the pixel-double bit.
	if (g_screen_scaler_preset)
		screen_scaler(SCALER_CTL.full, VO_CONTROL, SPG_CONTROL.interlace != 0, &s_snap.fb_scale_x, &s_snap.fb_scale_y);
	s_snap.autosort = using_autosort();
	// P10 Tile Clip: the TA's global clip, in pixels (Flycast globClip).
	s_snap.glob_clip_x = ((TA_GLOB_TILE_CLIP & 0x3F) + 1) * 32;
	s_snap.glob_clip_y = (((TA_GLOB_TILE_CLIP >> 16) & 0xF) + 1) * 32;
	// Bundle 11: SCALER_CTL for a render to texture; StartRender sets flip and keep (P60); the tile box (a walk of
	// up to 300 region words) only for Region Scissor and for a render to texture, whose [rtt] line prints it.
	s_snap.scaler_ctl = SCALER_CTL.full;
	s_snap.flip = true;
	s_snap.keep = false;
	s_snap.reg_ok = false;
	if (g_region_scissor_preset || (s_snap.fb_w_sof1 & 0x1000000))
		snapshot_region_box();
	// Bundle 12: WIDE needs the tiles too, to know a render that the game's own clip narrows.
	else if (g_screen_format_preset == SCREEN_WIDE)
		snapshot_region_box();
	background_plane(s_snap.bg_vtx, &s_snap.bg_param);
	s_snap.fog_clamp_min = FOG_CLAMP_MIN;
	s_snap.fog_clamp_max = FOG_CLAMP_MAX;
	// 3a: the fog registers are native u32, so the draw's shifts are the DC's bit numbers.
	if (g_fog_preset)
	{
		s_snap.fog_col_ram = FOG_COL_RAM;
		s_snap.fog_col_vert = FOG_COL_VERT;
		s_snap.fog_density = FOG_DENSITY;
		for (u32 i = 0; i < 128; i++)
			s_snap.fog_table[i] = (u16)FOG_TABLE[i];
	}
}

// -- 3a Fog (Flycast d3d_shaders.cpp fog_mode2) ---------------------------------

// The fog texture, FOG_TEX_W x 4. Rows 0-1: texel k is FOG_COL_RAM with the table's value at z = 2^(8 (k + 0.5) / W),
// so the fragment program reads it at log2(z) / 8 with no floor and no pow. Rows 2-3: FOG_COL_VERT.
#define FOG_TEX_W 512
static GLuint s_fog_tex;
static bool s_fog_tex_ok;
static u8 s_fog_idx[FOG_TEX_W], s_fog_frac[FOG_TEX_W];
static u8 s_fog_pix[FOG_TEX_W * 4 * 4];
// The registers the texture holds now.
static u16 s_fog_tab_up[128];
static u32 s_fog_ram_up, s_fog_vert_up;

// Each texel's table entry and the weight (of 256) of its low byte: z = (1 + m / 16) 2^e is entry 16 e + floor(m).
static void fog_tex_init()
{
	for (u32 k = 0; k < FOG_TEX_W; k++)
	{
		// The end texels hold the ends of the PVR's z clamp, where a clamped lookup lands.
		const float z = k == 0 ? 1.0f : k == FOG_TEX_W - 1 ? 255.9999f : powf(2.0f, 8.0f * (k + 0.5f) / FOG_TEX_W);
		int e;
		const float m = frexpf(z, &e) * 32.0f - 16.0f;
		const int mi = (int)m;
		s_fog_idx[k] = (u8)((e - 1) * 16 + mi);
		s_fog_frac[k] = (u8)((m - mi) * 256.0f);
	}
}

// The texture's RGBA bytes for g_rr's fog registers.
static void fog_tex_pixels()
{
	const u32 ram = g_rr.fog_col_ram, vert = g_rr.fog_col_vert;
	const u32 row = FOG_TEX_W * 4;
	for (u32 k = 0; k < FOG_TEX_W; k++)
	{
		// A table entry holds its own value in bits 15:8 and the value at its end in bits 7:0.
		const u32 e = g_rr.fog_table[s_fog_idx[k]], f = s_fog_frac[k];
		u8* p = &s_fog_pix[k * 4];
		p[0] = (u8)(ram >> 16); p[1] = (u8)(ram >> 8); p[2] = (u8)ram;
		p[3] = (u8)(((e >> 8) * (256 - f) + (e & 0xFF) * f + 128) >> 8);
		p += 2 * row;
		p[0] = (u8)(vert >> 16); p[1] = (u8)(vert >> 8); p[2] = (u8)vert; p[3] = 255;
	}
	memcpy(s_fog_pix + row, s_fog_pix, row);
	memcpy(s_fog_pix + 3 * row, s_fog_pix + 2 * row, row);
}

// The fog VP's scale, 1 / FOG_DENSITY (mantissa / 128 x 2^exponent), so that W x scale is 1/z.
static float fog_scale(u32 d)
{
	const float den = ldexpf((float)((d >> 8) & 0xFF) / 128.0f, (int)(s8)(d & 0xFF));
	// Kept finite: at 1e20 every z is under the clamp's 1 (a density of 0), at 1e-20 over its 256.
	float scale = den > 0.0f ? 1.0f / den : 1e20f;
	if (!(scale < 1e20f))
		scale = 1e20f;
	if (scale < 1e-20f)
		scale = 1e-20f;
	return scale;
}

// Binds the fog texture on TMU 1 as the programs' table and writes it again when a register changed.
static bool fog_upload()
{
	if (!s_fog_tex)
	{
		glGenTextures(1, &s_fog_tex);
		fog_tex_init();
	}
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_2D, s_fog_tex);
	if (!s_fog_tex_ok || s_fog_ram_up != g_rr.fog_col_ram || s_fog_vert_up != g_rr.fog_col_vert ||
	    memcmp(s_fog_tab_up, g_rr.fog_table, sizeof(s_fog_tab_up)) != 0)
	{
		fog_tex_pixels();
		if (!s_fog_tex_ok)
		{
			// Linear rows: no swizzle for a 512 x 4 table. The clamp is the z clamp's other half.
			ps3gl_texture_hint_no_swizzle();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FOG_TEX_W, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_fog_pix);
		s_fog_tex_ok = ps3gl_tex_chain_levels() != 0;
		s_fog_ram_up = g_rr.fog_col_ram;
		s_fog_vert_up = g_rr.fog_col_vert;
		memcpy(s_fog_tab_up, g_rr.fog_table, sizeof(s_fog_tab_up));
		s_stat_fog_up++;
	}
	if (s_fog_tex_ok)
	{
		glEnable(GL_TEXTURE_2D);
		ps3gl_tex_aux(1);
	}
	glActiveTextureARB(GL_TEXTURE0_ARB);
	return s_fog_tex_ok;
}

// The scene's fog state: the table on TMU 1 and the VP's scale. Without the texture the scene draws with no fog.
static void fog_scene_begin()
{
	if (!s_fog)
		return;
	if (!fog_upload())
	{
		s_fog = 0;
		return;
	}
	ps3gl_dc_fog_scale(fog_scale(g_rr.fog_density));
}

static void fog_scene_end()
{
	if (!s_fog)
		return;
	ps3gl_dc_fog(PS3GL_FOG_NONE);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	ps3gl_tex_aux(0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
}

// Plane through the 3 background vertices, evaluated per color channel and for UV at (x, y).
static void bg_eval(const BgVertex* v, float x, float y, BgVertex* out)
{
	float den = (v[1].y - v[2].y) * (v[0].x - v[2].x) + (v[2].x - v[1].x) * (v[0].y - v[2].y);
	if (den == 0.0f)
	{
		*out = v[0];
		return;
	}
	float w0 = ((v[1].y - v[2].y) * (x - v[2].x) + (v[2].x - v[1].x) * (y - v[2].y)) / den;
	float w1 = ((v[2].y - v[0].y) * (x - v[2].x) + (v[0].x - v[2].x) * (y - v[2].y)) / den;
	float w2 = 1.0f - w0 - w1;
	u32 col = 0;
	for (int sh = 0; sh < 32; sh += 8)
	{
		float c = ((v[0].col >> sh) & 0xFF) * w0 + ((v[1].col >> sh) & 0xFF) * w1 + ((v[2].col >> sh) & 0xFF) * w2;
		s32 ci = (s32)(c + 0.5f);
		col |= (u32)(ci < 0 ? 0 : (ci > 255 ? 255 : ci)) << sh;
	}
	out->x = x;
	out->y = y;
	out->u = v[0].u * w0 + v[1].u * w1 + v[2].u * w2;
	out->v = v[0].v * w0 + v[1].v * w1 + v[2].v * w2;
	out->col = col;
}

// Draws ps3gl actually emitted for the translucent pass, against draws offered.

// P18 Depth Float, latched per scene so the clear, the projection and the tests agree (ps3_depth.h).
static bool s_zfloat;

// PVR DepthMode values with a fixed use below.
enum { DC_GREATER = 4, DC_GEQUAL = 6 };

// DepthMode 0-7 is in GL's NEVER..ALWAYS order; depth_cmp picks the test for the scene's depth mapping.
static inline GLenum dc_depth_func(u32 mode)
{
	return GL_NEVER + depth_cmp(s_zfloat, mode);
}

// TSP SrcInstr/DstInstr; "other color" is the destination for src and the source for dst.
static const GLenum blend_src[8] = {
	GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA
};
static const GLenum blend_dst[8] = {
	GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA
};

// ShadInstr 0 decal, 1 modulate, 2 decal alpha, 3 modulate alpha. Offset color is not added yet.
static const GLenum tex_env[4] = { GL_REPLACE, GL_MODULATE, GL_DECAL, GL_MODULATE };

// nullDC-v1 CullMode[]: 2 culls counterclockwise, 3 clockwise, as seen on screen.
static void set_cull(u32 mode)
{
	if (mode < 2)
	{
		glDisable(GL_CULL_FACE);
		return;
	}
	glEnable(GL_CULL_FACE);
	glCullFace(mode == 2 ? GL_FRONT : GL_BACK);
}

// nullDC-v1 GPstate_cache: sorted drawing revisits the same polygon state often,
// and repeating it would re-bind the texture and re-push every GL state.
static u32 s_cache_isp, s_cache_tsp, s_cache_tcw, s_cache_pcw, s_cache_list, s_cache_or;
static bool s_cache_valid;

static void reset_param_cache()
{
	s_cache_valid = false;
}

// Bundle 4 draw rules (plan section 7), latched per job in rth_job_decode. All 0 is the old path.
struct DrawOpts
{
	bool pt_depth;     // P25: punch-through always writes depth
	bool tr_nodepth;   // P25: an autosorted translucent polygon writes no depth
	u8 pt_blend;       // P25: 1 = punch-through blends with its TSP factors; P79: 2 = with alpha 1 after its test
	u8 flat;           // P25: 0 = always Gouraud, 1 = flat from the RSX's last vertex, 2 = from its first
	u8 passes;         // P10: 0 = one pass, 1 = one per TA_LIST_CONT, 2 = + the TR depth pass
	u8 clip;           // P10: 0 = no user clip, 1 = inside, 2 = + outside; 0 when the lists carry no rects
	bool tr_vol;       // P27: the lists hold the translucent volume list
	u8 tri_sort;       // 3k: 0 = sort per strip, 1 = per triangle with no depth write, 2 = + depth write, 3 = + opaque depth
	u8 first_pass;     // bundle 14, + opaque depth's first pass: 1 = no run that writes no depth, 2 = also in game order
	// Bundle 17. offs needs lists that carry every offset colour (TAOPT_OFFS); clamp needs FOG_CLAMP values that clamp.
	bool offs;         // 3b: a textured polygon with the Offset bit adds its offset colour
	bool clamp;        // 3b: a polygon with ColorClamp clamps its colour to FOG_CLAMP_MIN..MAX before the fog
	bool tril;         // P25: a trilinear pass scales its colour by its weight
};
static DrawOpts s_do;
// The pass in draw (0 with one pass), its sort mode, and P10's TR depth pass.
static u32 s_pass;
static bool s_pass_autosort;
static bool s_depth_pass;
// 3k, while the sorted triangles draw: no depth write (Flycast's SortingEnabled), the game's depth write, or the
// first pass of + OPAQUE DEPTH (depth alone, for texels with alpha of TRI_OPAQUE_REF or more).
enum { TRI_NONE, TRI_NODEPTH, TRI_GAME, TRI_OPAQUE };
#define TRI_OPAQUE_REF 240
static u32 s_tri_mode;
static u32 s_stat_tris;
// Bundle 14, the first pass per stats window: its runs, and those of them that write no depth.
static u32 s_stat_fp_runs, s_stat_fp_nodepth;

// + OPAQUE DEPTH: 2 = the polygon hides what is behind it at any alpha (ONE, ZERO), 1 = at full alpha (SRC_ALPHA,
// INV_SRC_ALPHA), 0 = never (an additive glow adds to what is behind it; Rayman 2 has such quads with no texture alpha).
static inline u32 tri_covers(const DcParam& p)
{
	if (p.tsp.SrcInstr == 1 && p.tsp.DstInstr == 0)
		return 2;
	return p.tsp.SrcInstr == 4 && p.tsp.DstInstr == 5;
}

// The first pass of + OPAQUE DEPTH draws a run that can hide something. Bundle 14 TR First Pass (Flycast's depth
// pass): and that writes depth; a run it does not draw is not applied either.
static inline bool tri_first_draws(const DcParam& p)
{
	return tri_covers(p) && !(s_do.first_pass && p.isp.ZWriteDis);
}
// P25 FIRST VERTEX: vertices the colour attribute of the next draw starts later (0 or 2).
static u32 s_flat_shift;
static u32 s_stat_multi, s_stat_pass_ppu, s_stat_trvol, s_stat_cont;
// TA Bad Records: the records ta.h's splitter skipped (P9, P66), per stats window.
u32 g_ta_bad_records;

// P10 Tile Clip: the scene's viewport in GL pixels, the canvas it shows, and the base scissor: with scissor set
// the scene draws only inside bx..bh, which is the viewport or (P60) a part of it; else bx..bh is the viewport.
struct ClipView
{
	int x, y, w, h;
	bool scissor;
	float cx0, cy0, cw, ch;
	int bx, by, bw, bh;
};
static ClipView s_cv;

// The viewport and the base scissor of a scene: all of the viewport.
static void clip_view(int x, int y, int w, int h, bool scissor)
{
	s_cv.x = s_cv.bx = x;
	s_cv.y = s_cv.by = y;
	s_cv.w = s_cv.bw = w;
	s_cv.h = s_cv.bh = h;
	s_cv.scissor = scissor;
}

// P60: cuts the base scissor to the canvas area cx0..cy1 (DC pixels, y down). False = no pixel is left.
static bool clip_view_cut(float cx, float cy, float cw, float ch, float cx0, float cy0, float cx1, float cy1)
{
	const float sx = (float)s_cv.w / cw, sy = (float)s_cv.h / ch;
	int r[4] = { s_cv.x + (int)lroundf((cx0 - cx) * sx), s_cv.y + s_cv.h - (int)lroundf((cy1 - cy) * sy),
	             s_cv.x + (int)lroundf((cx1 - cx) * sx), s_cv.y + s_cv.h - (int)lroundf((cy0 - cy) * sy) };
	const int lim[4] = { s_cv.x, s_cv.y, s_cv.x + s_cv.w, s_cv.y + s_cv.h };
	for (int k = 0; k < 4; k++)
	{
		const int lo = lim[k & 1], hi = lim[2 + (k & 1)];
		r[k] = r[k] < lo ? lo : r[k] > hi ? hi : r[k];
	}
	if (r[2] <= r[0] || r[3] <= r[1])
		return false;
	s_cv.bx = r[0];
	s_cv.by = r[1];
	s_cv.bw = r[2] - r[0];
	s_cv.bh = r[3] - r[1];
	s_cv.scissor = true;
	return true;
}
// The applied param's clip: none, inside (the scissor is set), outside (one draw per side of the rect), or all cut.
enum { CLIP_NONE, CLIP_INSIDE, CLIP_OUTSIDE, CLIP_ALL };
static u32 s_clip_mode;
static int s_clip_r[4];  // x0, y0, x1, y1 in GL pixels, inside the viewport
static bool s_clip_base = true;

static void clip_base()
{
	if (s_clip_base)
		return;
	s_clip_base = true;
	if (s_cv.scissor)
		glScissor(s_cv.bx, s_cv.by, s_cv.bw, s_cv.bh);
	else
		glDisable(GL_SCISSOR_TEST);
}

// Flycast getTileClip: mode 2 draws inside the tile rect, mode 3 outside; a rect over the whole TA area is no clip.
static void clip_apply(const DcParam& p)
{
	const u32 mode = (p.pcw.full >> 16) & 3;
	s_clip_mode = CLIP_NONE;
	if (mode < 2 || (mode == 3 && s_do.clip < 2) || &p == &g_rr.bg_param)
		return clip_base();
	const u32 rect = (p.isp.full & 0xFFFFFu) | (((p.pcw.full >> 8) & 3) << 20);
	const float csx = (float)((rect & 63) * 32), cex = (float)((((rect >> 6) & 63) + 1) * 32);
	const float csy = (float)(((rect >> 12) & 31) * 32), cey = (float)((((rect >> 17) & 31) + 1) * 32);
	if (csx == 0 && csy == 0 && cex >= (float)g_rr.glob_clip_x && cey >= (float)g_rr.glob_clip_y)
		return clip_base();
	// DC y goes down, GL y goes up from the viewport's lower edge.
	const float sx = (float)s_cv.w / s_cv.cw, sy = (float)s_cv.h / s_cv.ch;
	int r[4] = { s_cv.x + (int)lroundf((csx - s_cv.cx0) * sx), s_cv.y + s_cv.h - (int)lroundf((cey - s_cv.cy0) * sy),
	             s_cv.x + (int)lroundf((cex - s_cv.cx0) * sx), s_cv.y + s_cv.h - (int)lroundf((csy - s_cv.cy0) * sy) };
	const int lim[4] = { s_cv.bx, s_cv.by, s_cv.bx + s_cv.bw, s_cv.by + s_cv.bh };
	for (int k = 0; k < 4; k++)
	{
		const int lo = lim[k & 1], hi = lim[2 + (k & 1)];
		r[k] = r[k] < lo ? lo : r[k] > hi ? hi : r[k];
	}
	const bool empty = r[2] <= r[0] || r[3] <= r[1];
	// An outside clip around no pixel cuts nothing.
	if (mode == 3 && empty)
		return clip_base();
	memcpy(s_clip_r, r, sizeof(r));
	s_clip_base = false;
	glEnable(GL_SCISSOR_TEST);
	if (mode == 3)
	{
		s_clip_mode = CLIP_OUTSIDE;
		return;
	}
	s_clip_mode = empty ? CLIP_ALL : CLIP_INSIDE;
	if (!empty)
		glScissor(r[0], r[1], r[2] - r[0], r[3] - r[1]);
}

// Applies one polygon's state. Returns the OR mask that forces vertex alpha to 255.
static u32 apply_param_body(const DcParam& p, u32 list);
static inline const u32* bump_offs(const DcParam& p);
static inline const u32* fogv_offs(const DcParam& p);
static inline const u32* offc_offs(const DcParam& p);
static inline u32 eff_cull(const DcParam& p);

// 3a, Flycast pp_FogCtrl: 0 = table fog, 1 = vertex fog, 3 = the fog texel is the base colour (with ShadInstr 1
// the texture alone gives the alpha). The background has no offset colours.
static inline int fog_mode(const DcParam& p, bool textured)
{
	switch (p.tsp.FogCtrl)
	{
	case 0: return PS3GL_FOG_TABLE;
	case 1: return textured && &p != &g_rr.bg_param && fogv_offs(p) ? PS3GL_FOG_VERTEX : PS3GL_FOG_NONE;
	case 3: return textured && p.tsp.ShadInstr == 1 ? PS3GL_FOG_BASE_TA : PS3GL_FOG_BASE;
	default: return PS3GL_FOG_NONE;
	}
}

// Bundle 17, per scene: Colour Clamp with FOG_CLAMP values that clamp (Flycast SetGPState), and the ends as RGBA.
static bool s_clamp_on;
static float s_clamp_min[4], s_clamp_max[4];
// Per stats window: states applied with the offset colour, with the clamp, with a trilinear weight.
static u32 s_stat_offc, s_stat_cclamp, s_stat_tril;

static void xt_scene_begin()
{
	s_clamp_on = s_do.clamp && (g_rr.fog_clamp_min != 0 || g_rr.fog_clamp_max != 0xFFFFFFFFu);
	static const int shift[4] = { 16, 8, 0, 24 };
	for (int k = 0; k < 4; k++)
	{
		s_clamp_min[k] = (float)((g_rr.fog_clamp_min >> shift[k]) & 0xFF) / 255.0f;
		s_clamp_max[k] = (float)((g_rr.fog_clamp_max >> shift[k]) & 0xFF) / 255.0f;
	}
}

// P25 (Flycast SetGPState): FilterMode 2 and 3 are the two passes of a trilinear polygon; MipMapD's fraction is the
// second pass's weight. Not in the punch-through list, and only for a mipmapped texture.
static inline float tril_weight(const DcParam& p, u32 list, bool textured)
{
	if (!textured || p.tsp.FilterMode < 2 || list == ListType_Punch_Through || !p.tcw.NO_PAL.MipMapped)
		return 1.0f;
	const float w = 0.25f * (float)(p.tsp.MipMapD & 3);
	return p.tsp.FilterMode == 2 ? 1.0f - w : w;
}

// The polygon's clamp and weight: one program does both, so the one that is off gets a value that changes nothing.
static void xt_apply(const DcParam& p, u32 list, bool textured)
{
	const bool clamp = s_clamp_on && p.tsp.ColorClamp;
	const float tril = s_do.tril ? tril_weight(p, list, textured) : 1.0f;
	if (!clamp && tril == 1.0f)
	{
		ps3gl_dc_xt(0, 0, 0, 1.0f);
		return;
	}
	static const float lo[4] = { -1e9f, -1e9f, -1e9f, -1e9f }, hi[4] = { 1e9f, 1e9f, 1e9f, 1e9f };
	ps3gl_dc_xt(1, clamp ? s_clamp_min : lo, clamp ? s_clamp_max : hi, tril);
	s_stat_cclamp += clamp;
	s_stat_tril += tril != 1.0f;
}

// 5.12 #3: p's slot in this scene's SPU texture hashes: its list index, or the background's slot.
static inline u32 spetex_slot(const DcParam& p)
{
	if (&p == &g_rr.bg_param)
		return SPETEX_BG;
	const uintptr_t off = (uintptr_t)&p - (uintptr_t)s_rl.param;
	return off < (uintptr_t)s_rl.nparam * sizeof(DcParam) ? (u32)(off / sizeof(DcParam)) : SPETEX_NONE;
}

static u32 apply_param(const DcParam& p, u32 list)
{
	if (s_cache_valid && s_cache_list == list && s_cache_isp == p.isp.full &&
	    s_cache_tsp == p.tsp.full && s_cache_tcw == p.tcw.full && s_cache_pcw == p.pcw.full)
		return s_cache_or;
	const u64 t0 = PERF_TICKS();
	u32 rv = apply_param_body(p, list);
	s_dt[DT_PARAM] += PERF_TICKS() - t0;
	return rv;
}

static u32 apply_param_body(const DcParam& p, u32 list)
{
	// P10 List Passes, the TR depth pass (Flycast drawSorted, Cosmic Smash): the pass's translucent polygons
	// again, depth only, so the next pass tests against them. The caller holds the colour mask off.
	if (s_depth_pass)
	{
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_TEXTURE_2D);
		ps3gl_tex_bump(0);
		if (s_fog)
			ps3gl_dc_fog(PS3GL_FOG_NONE);
		if (s_do.offs)
			ps3gl_dc_offs(0);
		if (s_clamp_on || s_do.tril)
			ps3gl_dc_xt(0, 0, 0, 1.0f);
		glDepthFunc(dc_depth_func(DC_GEQUAL));
		glDepthMask(p.isp.ZWriteDis ? GL_FALSE : GL_TRUE);
		set_cull(eff_cull(p));
		if (s_do.clip)
			clip_apply(p);
		s_cache_valid = false;
		return 0;
	}

	if (list == ListType_Translucent)
	{
		glEnable(GL_BLEND);
		glBlendFunc(blend_src[p.tsp.SrcInstr], blend_dst[p.tsp.DstInstr]);
		// Zero alpha leaves the pixel unchanged with these modes; clip it so it does not write Z.
		bool clip_zero = p.tsp.SrcInstr == 4 && (p.tsp.DstInstr == 1 || p.tsp.DstInstr == 5);
		if (clip_zero)
			glEnable(GL_ALPHA_TEST);
		else
			glDisable(GL_ALPHA_TEST);
		// Autosort ignores DepthMode and compares ">=" on 1/W.
		glDepthFunc(dc_depth_func(s_pass_autosort ? DC_GEQUAL : p.isp.DepthMode));
	}
	else
	{
		// P25 PT Blend: punch-through blends (Flycast 1a019b79b). P10 List Passes: so does opaque after pass 0.
		if ((list == ListType_Punch_Through && s_do.pt_blend) || (list == ListType_Opaque && s_pass != 0))
		{
			u32 src = p.tsp.SrcInstr, dst = p.tsp.DstInstr;
			// P79 (Flycast GL): a punch-through texel that passes the alpha test has alpha 1, so SRC_ALPHA is ONE
			// and INV_SRC_ALPHA is ZERO; ONE with ZERO is no blend (Jet Grind Radio's whole list).
			const bool one = list == ListType_Punch_Through && s_do.pt_blend == 2;
			if (one)
			{
				src = src == 4 ? 1 : src == 5 ? 0 : src;
				dst = dst == 4 ? 1 : dst == 5 ? 0 : dst;
			}
			if (one && src == 1 && dst == 0)
				glDisable(GL_BLEND);
			else
			{
				glEnable(GL_BLEND);
				glBlendFunc(blend_src[src], blend_dst[dst]);
			}
		}
		else
			glDisable(GL_BLEND);
		// Punch-through ignores DepthMode and compares ">=" on 1/W.
		glDepthFunc(dc_depth_func(list == ListType_Punch_Through ? DC_GEQUAL : p.isp.DepthMode));
	}
	// P25 (Flycast SetGPState): punch-through always writes depth (Worms World Party, Bust-a-Move 4, Re-Volt), and
	// an autosorted translucent polygon never does.
	bool zwrite = !p.isp.ZWriteDis;
	if (list == ListType_Punch_Through && s_do.pt_depth)
		zwrite = true;
	else if (list == ListType_Translucent && s_pass_autosort && (s_do.tr_nodepth || s_tri_mode == TRI_NODEPTH))
		zwrite = false;
	glDepthMask(zwrite ? GL_TRUE : GL_FALSE);
	set_cull(eff_cull(p));
	// P25 Flat Shading: a polygon without Gouraud takes one colour per triangle; the background stays smooth.
	if (s_do.flat)
	{
		const bool flat = !p.pcw.Gouraud && &p != &g_rr.bg_param;
		glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
		s_flat_shift = (flat && s_do.flat == 2) ? 2 : 0;
	}
	if (s_do.clip)
		clip_apply(p);
	// Flycast SetGPState: stencil bit 7 marks pixels whose polygon takes modifier-volume shadows.
	glStencilFunc(GL_ALWAYS, p.pcw.Shadow ? 0x80 : 0, 0xFF);

	const u64 t_tex = PERF_TICKS();
	bool textured = p.pcw.Texture && ps3tex_bind(p.tsp, p.tcw, spetex_slot(p), list == ListType_Punch_Through);
	s_dt[DT_TEX] += PERF_TICKS() - t_tex;
	if (textured)
	{
		glEnable(GL_TEXTURE_2D);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, tex_env[p.tsp.ShadInstr]);
	}
	else
		glDisable(GL_TEXTURE_2D);
	// 3j: the texenv's bump program for a bump texture; the background has no offset colours.
	ps3gl_tex_bump(textured && &p != &g_rr.bg_param && bump_offs(p) != 0);
	// 3a: the texenv's fog program, by the polygon's FogCtrl.
	if (s_fog)
		ps3gl_dc_fog(fog_mode(p, textured));
	// Bundle 17, 3b: the texenv's program with the offset colour; the background has no offset colours.
	if (s_do.offs)
	{
		const bool offc = textured && &p != &g_rr.bg_param && offc_offs(p) != 0;
		ps3gl_dc_offs(offc);
		s_stat_offc += offc;
	}
	if (s_clamp_on || s_do.tril)
		xt_apply(p, list, textured);

	// 3k + OPAQUE DEPTH, first pass: the polygon's own state, but depth alone and only where alpha passes the test.
	if (s_tri_mode == TRI_OPAQUE)
	{
		const u32 covers = tri_covers(p);
		glDisable(GL_BLEND);
		if (covers == 2)
			glDisable(GL_ALPHA_TEST);
		else
			glEnable(GL_ALPHA_TEST);
		glDepthMask(covers && !p.isp.ZWriteDis ? GL_TRUE : GL_FALSE);
	}

	// UseAlpha=0 means opaque vertex alpha; modulate (1) takes alpha from the texture only.
	bool force = !p.tsp.UseAlpha || (textured && p.tsp.ShadInstr == 1);
	s_cache_isp = p.isp.full;
	s_cache_tsp = p.tsp.full;
	s_cache_tcw = p.tcw.full;
	s_cache_pcw = p.pcw.full;
	s_cache_list = list;
	s_cache_or = force ? 0xFF000000u : 0;
	s_cache_valid = true;
	return s_cache_or;
}

// offs: the 3j offset colour (ARGB), 0 outside a bump run; texcoord1 takes it as RGBA bytes.
static inline void emit_vertex(const DcVertex* v, u32 alpha_or, u32 offs)
{
	u32 c = v->col | alpha_or;
	glColor4ub((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
	glTexCoord2f(v->u, v->v);
	ps3gl_tex1_bits((offs << 8) | (offs >> 24));
	glVertex3f(v->x * v->w, v->y * v->w, v->w);
}

// misc/spevb_check compiles the code from here to "spevb_check: end" as the reference for the SPU build.

// Flycast ta_util.cpp makeIndex(): consecutive strips with the same state share one GL
// strip, joined by degenerate triangles, and inf vertices split a strip.
#define RUN_MAX_VTX 16000   // ps3gl drops vertices past PS3GL_MAX_VERTS (16384) per glBegin

static bool s_merge;          // Strip Merge preset, latched per scene
static u32 s_stat_draws[5];   // glBegin/glEnd pairs per list

// Bundle 4 merge options, latched per scene: P25's sprite cull flip, and strip_equivalent's PCW and ISP masks,
// which hold the clip rect bits (PCW 9:8, ISP 19:0) when the lists carry rects (P10).
static bool s_mo_sprite;
static u32 s_key_pcw = 0x300CE, s_key_isp = 0xF4000000;
// The strips the list passes below walk: one pass of P10 List Passes, or every strip.
static u32 s_strip_lo, s_strip_hi;

// P25 Sprite Cull (Flycast 6ffe4c028): a sprite's cull direction is the other one; modes 0 and 1 both cull nothing.
static inline u32 eff_cull(const DcParam& p)
{
	return p.isp.CullMode ^ (u32)(s_mo_sprite && (p.pcw.full >> 29) == 5);
}

// Vertex Arrays preset: the scene's merged strips are written once, straight into one vring
// block, and each run is a draw of a range of it. Null = the glBegin/glEnd path.
static ps3gl_vertex_t* s_vb;
static u32 s_vb_off, s_vb_used, s_vb_cap;

// One run of the scene's vertex block, stride bytes per vertex. P25 FIRST VERTEX gives the colour attribute a start
// 2 vertices on. P10: an outside clip draws the run once per side of the rect; a clip that cuts all draws nothing.
static void draw_vb(GLenum prim, u32 stride, u32 first, u32 count)
{
	if (s_clip_mode == CLIP_ALL)
		return;
	if (s_clip_mode != CLIP_OUTSIDE)
	{
		ps3gl_draw_vb_shift(prim, s_vb_off, stride, (int)first, (int)count, s_flat_shift);
		return;
	}
	const int x1 = s_cv.bx + s_cv.bw, y1 = s_cv.by + s_cv.bh;
	const int side[4][4] = { { s_cv.bx, s_cv.by, s_clip_r[0], y1 }, { s_clip_r[2], s_cv.by, x1, y1 },
	                         { s_clip_r[0], s_cv.by, s_clip_r[2], s_clip_r[1] }, { s_clip_r[0], s_clip_r[3], s_clip_r[2], y1 } };
	for (int k = 0; k < 4; k++)
	{
		if (side[k][2] <= side[k][0] || side[k][3] <= side[k][1])
			continue;
		glScissor(side[k][0], side[k][1], side[k][2] - side[k][0], side[k][3] - side[k][1]);
		ps3gl_draw_vb_shift(prim, s_vb_off, stride, (int)first, (int)count, s_flat_shift);
	}
}

struct StripRun
{
	bool open;
	bool reversed;          // current strip goes out with flipped winding
	u8 cull;                // CullMode of the run's first strip
	u16 param;              // the run's first strip; its state is the one applied
	u32 list;
	u32 count;              // vertices in the open GL strip
	u32 first;              // Vertex Arrays: the run's first vertex in s_vb
	u32 alpha_or;
	const DcVertex* last;   // last vertex emitted
	const u32* offs;        // 3j: the offset colours of a bump run's vertices, else NULL
};
static StripRun s_run;

// 3j: the offset colours a bump run carries in texcoord1, NULL for any other run.
static inline const u32* bump_offs(const DcParam& p)
{
	return s_bump_on && p.pcw.Texture && p.tcw.NO_PAL.PixelFmt == 4 ? s_rl.offs : 0;
}
// 3a: a vertex-fog polygon (Flycast: FogCtrl 1 with an offset colour and no bump map); its offset alpha is the fog.
static inline bool fogv_param(const DcParam& p)
{
	return p.pcw.Texture && p.pcw.Offset && p.tsp.FogCtrl == 1 && p.tcw.NO_PAL.PixelFmt != 4;
}
static inline const u32* fogv_offs(const DcParam& p)
{
	return s_fog >= 2 && fogv_param(p) ? s_rl.offs : 0;
}
// 3b Offset Colour (Flycast pp_Offset): a textured polygon with the Offset bit and no bump map adds its offset colour.
static inline bool offc_param(const DcParam& p)
{
	return p.pcw.Texture && p.pcw.Offset && p.tcw.NO_PAL.PixelFmt != 4;
}
static inline const u32* offc_offs(const DcParam& p)
{
	return s_do.offs && offc_param(p) ? s_rl.offs : 0;
}
// The offset colours a run carries in texcoord1: a bump run's, a vertex-fog run's, or an offset-colour run's.
static inline const u32* run_offs(const DcParam& p)
{
	const u32* offs = bump_offs(p);
	if (!offs)
		offs = fogv_offs(p);
	return offs ? offs : offc_offs(p);
}
static u32 s_stat_bump_strips;  // 3j: strips drawn with their offset colours

// Flycast PolyParam::equivalentIgnoreCullingDirection: ISP 0xF4000000 keeps cull on/off, not its direction.
static inline bool strip_equivalent(const DcParam& a, const DcParam& b)
{
	return ((a.pcw.full ^ b.pcw.full) & s_key_pcw) == 0
	    && ((a.isp.full ^ b.isp.full) & s_key_isp) == 0
	    && a.tsp.full == b.tsp.full && a.tcw.full == b.tcw.full;
}

// Flycast is_vertex_inf. set_pos stores W, so its "1/W > 3.4e37" is W < 1/3.4e37 (+inf 1/W gives W = 0).
static inline bool vertex_inf(const DcVertex* v)
{
	return v->x != v->x || fabsf(v->x) > 1e25f || v->y != v->y || fabsf(v->y) > 1e25f
	    || v->w < 1.0f / 3.4e37f;
}

// emit_vertex in ps3gl_vertex_t form (z = W, w = 1, RGBA). Word stores only: RSX memory is
// cache-inhibited, and GCC's merged u1/v1 std is misaligned on odd 36-byte vertices (alignment trap).
static inline bool vb_emit(const DcVertex* v, u32 alpha_or, u32 offs)
{
	if (s_vb_used >= s_vb_cap)
		return false;
	volatile float* o = (volatile float*)&s_vb[s_vb_used++];
	u32 c = v->col | alpha_or;
	o[0] = v->x * v->w;
	o[1] = v->y * v->w;
	o[2] = v->w;
	o[3] = 1.0f;
	o[4] = v->u;
	o[5] = v->v;
	// 3j: u1 holds the offset colour as RGBA bytes, 0 (= 0.0f) outside a bump run.
	((volatile u32*)o)[6] = (offs << 8) | (offs >> 24);
	o[7] = 0.0f;
	((volatile u32*)o)[8] = (c << 8) | (c >> 24);
	return true;
}

static inline void run_emit(const DcVertex* v)
{
	const u32 offs = s_run.offs ? s_run.offs[v - s_rl.vtx] : 0;
	if (s_vb)
	{
		if (!vb_emit(v, s_run.alpha_or, offs))
			return;
	}
	else
		emit_vertex(v, s_run.alpha_or, offs);
	s_run.last = v;
	s_run.count++;
}

// 5.12 #2 CHECK: DRY runs the same merge with no GL call and records the runs, for the compare with the SPU.
static SpeRun* s_dry_runs;
static u32 s_dry_nruns, s_dry_strips;

// apply_param's alpha force without the bind: a texture always binds (its sizes cap at 1024).
static inline u32 alpha_rule(const DcParam& p)
{
	return (!p.tsp.UseAlpha || (p.pcw.Texture && p.tsp.ShadInstr == 1)) ? 0xFF000000u : 0;
}

template <bool DRY = false> static void run_close()
{
	if (!s_run.open)
		return;
	if (DRY)
	{
		SpeRun& r = s_dry_runs[s_dry_nruns++];
		r.first = s_run.first;
		r.count = s_run.count;
		r.param = s_run.param;
		r.pad = 0;
		s_run.open = false;
		return;
	}
	const u64 t0 = PERF_TICKS();
	if (s_vb)
		draw_vb(GL_TRIANGLE_STRIP, (u32)sizeof(ps3gl_vertex_t), s_run.first, s_run.count);
	else
		glEnd();
	s_dt[DT_GLEND] += PERF_TICKS() - t0;
	s_run.open = false;
	if (s_run.count != 0)
		s_stat_draws[s_run.list]++;
}

// No GL state may change while a run is open: apply_param only runs after run_close(). DRY needs Strip Merge.
template <bool DRY = false> static void draw_strip(const DcStrip& s, u32 list)
{
	const DcParam& p = s_rl.param[s.param];
	const u32* offs = run_offs(p);
	if (DRY)
		s_dry_strips++;
	else
	{
		s_stat_strips[list]++;
		if (bump_offs(p))
			s_stat_bump_strips++;
	}

	if (!s_merge)
	{
		u32 alpha_or = apply_param(p, list);
		glBegin(GL_TRIANGLE_STRIP);
		const DcVertex* v = &s_rl.vtx[s.first];
		for (u32 k = 0; k < s.count; k++, v++)
			emit_vertex(v, alpha_or, offs ? offs[s.first + k] : 0);
		const u64 t0 = PERF_TICKS();
		glEnd();
		s_dt[DT_GLEND] += PERF_TICKS() - t0;
		s_stat_draws[list]++;
		return;
	}

	bool dupe_next = false;
	if (s_run.open && s_run.count != 0 && s_run.list == list &&
	    s_run.count + s.count * 2u + 4u <= RUN_MAX_VTX &&
	    strip_equivalent(p, s_rl.param[s_run.param]))
	{
		// Repeat the last vertex; one more copy flips the parity when the cull direction differs.
		const DcVertex* last = s_run.last;
		run_emit(last);
		const u32 cull = eff_cull(p);
		if (cull < 2 || cull == s_run.cull)
		{
			if (s_run.reversed)
				run_emit(last);
			s_run.reversed = false;
		}
		else
		{
			if (!s_run.reversed)
				run_emit(last);
			s_run.reversed = true;
		}
		dupe_next = true;
	}
	else
	{
		run_close<DRY>();
		s_run.alpha_or = DRY ? alpha_rule(p) : apply_param(p, list);
		if (!s_vb)
			glBegin(GL_TRIANGLE_STRIP);
		s_run.first = s_vb_used;
		s_run.open = true;
		s_run.reversed = false;
		s_run.cull = (u8)eff_cull(p);
		s_run.param = s.param;
		s_run.list = list;
		s_run.count = 0;
		// The merge key holds the TCW, so every strip of the run is a bump strip or none is.
		s_run.offs = offs;
	}

	const DcVertex* v0 = &s_rl.vtx[s.first];
	const DcVertex* last_good = 0;
	for (u32 i = 0; i < s.count; i++)
	{
		const DcVertex* v = v0 + i;
		if (vertex_inf(v))
		{
			// Skip the inf run; the next good vertex restarts the strip after a degenerate join.
			while (i < s.count - 1u)
			{
				if (!vertex_inf(v0 + i + 1))
				{
					if (last_good)
					{
						run_emit(last_good);
						dupe_next = true;
					}
					break;
				}
				i++;
			}
			continue;
		}
		last_good = v;
		if (dupe_next)
		{
			run_emit(v);
			dupe_next = false;
		}
		// Vertex i must sit at the same parity as in its own strip, or its triangles flip winding.
		if (((i ^ s_run.count) & 1) ^ (u32)s_run.reversed)
			run_emit(v);
		run_emit(v);
	}
}

// Draws one list in submission order: opaque, punch-through, and presorted translucent.
template <bool DRY = false> static void draw_list(u32 list)
{
	for (u32 i = s_strip_lo; i < s_strip_hi; i++)
	{
		const DcStrip& s = s_rl.strip[i];
		if (s.list == list)
			draw_strip<DRY>(s, list);
	}
	run_close<DRY>();
}

// nullDC-v1 sort entry: one per translucent polygon, keyed like PolyParam::zvZ.
struct SortStrip
{
	float key;
	u32 idx;
};
static SortStrip s_order[MAX_STRIPS];

// Autosort pass, nullDC-v1 SortPParams (AlphaSortMode 1): polygons ordered by average 1/W, nearest
// last. The PVR sorts per pixel; this is the approximation the reference ships.
template <bool DRY = false> static void draw_tr_sorted()
{
	const u64 t_sort = PERF_TICKS();
	u32 n = 0;
	for (u32 i = s_strip_lo; i < s_strip_hi; i++)
	{
		const DcStrip& st = s_rl.strip[i];
		if (st.list != ListType_Translucent || st.count == 0)
			continue;
		float sum = 0.0f;
		for (u32 k = 0; k < st.count; k++)
			sum += 1.0f / s_rl.vtx[st.first + k].w;
		s_order[n].key = sum / st.count;
		s_order[n].idx = i;
		n++;
	}
	if (n == 0)
	{
		if (!DRY)
			s_dt[DT_SORT] += PERF_TICKS() - t_sort;
		return;
	}
	std::stable_sort(s_order, s_order + n, [](const SortStrip& a, const SortStrip& b) { return a.key < b.key; });
	if (!DRY)
		s_dt[DT_SORT] += PERF_TICKS() - t_sort;

	// Merging after the sort keeps the draw order, as Flycast's per-strip sort mode does.
	for (u32 j = 0; j < n; j++)
		draw_strip<DRY>(s_rl.strip[s_order[j].idx], ListType_Translucent);
	run_close<DRY>();
}
// spevb_check: end
static_assert(RUN_MAX_VTX == SPEVB_RUN_MAX_VTX && sizeof(ps3gl_vertex_t) == SPEVB_VTX_BYTES, "SPE build layout");

// 3k TR Triangle Sort (Flycast sortTriangles). key: the bits of the triangle's largest W, inverted, so a rising key
// is a rising smallest 1/W (Flycast's key). id: the third corner's vertex | its odd place in the strip << 16 | param << 17.
struct TriKey
{
	u32 key, id;
};
static TriKey s_tri[MAX_VTX], s_tri_tmp[MAX_VTX];

// Every triangle of the pass's translucent strips with no inf corner, in submission order.
static u32 tri_collect()
{
	u32 n = 0;
	for (u32 i = s_strip_lo; i < s_strip_hi; i++)
	{
		const DcStrip& st = s_rl.strip[i];
		if (st.list != ListType_Translucent || st.count < 3)
			continue;
		s_stat_strips[ListType_Translucent]++;
		const DcVertex* v = &s_rl.vtx[st.first];
		bool inf0 = vertex_inf(v), inf1 = vertex_inf(v + 1);
		for (u32 k = 2; k < st.count; k++)
		{
			const bool inf2 = vertex_inf(v + k);
			if (!inf0 && !inf1 && !inf2 && n < MAX_VTX)
			{
				float w = v[k].w;
				if (v[k - 1].w > w) w = v[k - 1].w;
				if (v[k - 2].w > w) w = v[k - 2].w;
				u32 bits;
				memcpy(&bits, &w, 4);
				s_tri[n].key = ~bits;
				s_tri[n].id = (st.first + k) | ((k & 1) << 16) | ((u32)st.param << 17);
				n++;
			}
			inf0 = inf1;
			inf1 = inf2;
		}
	}
	return n;
}

// Flycast's std::stable_sort on the key: a radix sort, least significant byte first, keeps equal keys in order.
static void tri_sort(u32 n)
{
	TriKey* src = s_tri;
	TriKey* dst = s_tri_tmp;
	for (u32 sh = 0; sh < 32; sh += 8)
	{
		u32 hist[256];
		memset(hist, 0, sizeof(hist));
		for (u32 i = 0; i < n; i++)
			hist[(src[i].key >> sh) & 255]++;
		u32 sum = 0;
		for (u32 d = 0; d < 256; d++)
		{
			const u32 c = hist[d];
			hist[d] = sum;
			sum += c;
		}
		for (u32 i = 0; i < n; i++)
			dst[hist[(src[i].key >> sh) & 255]++] = src[i];
		std::swap(src, dst);
	}
}

// The triangles of s_tri from vertex base on, in runs of one state (the merge key). emit writes the corners: an odd
// triangle and the other cull direction of a run each swap the first two. own: only the runs the pass draws.
static void tri_runs(u32 n, u32 base, bool emit, bool own)
{
	const DcParam* rp = 0;
	const u32* offs = 0;
	u32 run_first = 0, run_cull = 0, alpha_or = 0, run_vb = base;
	bool draws = true;
	for (u32 t = 0; t <= n; t++)
	{
		const DcParam* p = t < n ? &s_rl.param[s_tri[t].id >> 17] : 0;
		if (!p || !rp || !strip_equivalent(*p, *rp) || (t - run_first) * 3 + 3 > RUN_MAX_VTX)
		{
			if (t != run_first && draws)
			{
				const u64 t0 = PERF_TICKS();
				draw_vb(GL_TRIANGLES, (u32)sizeof(ps3gl_vertex_t), run_vb, 3 * (t - run_first));
				s_dt[DT_GLEND] += PERF_TICKS() - t0;
				s_stat_draws[ListType_Translucent]++;
			}
			if (!p)
				break;
			// The first pass of + OPAQUE DEPTH draws only the runs that can hide something.
			draws = s_tri_mode != TRI_OPAQUE || tri_first_draws(*p);
			if (s_tri_mode == TRI_OPAQUE)
			{
				s_stat_fp_runs++;
				s_stat_fp_nodepth += !tri_covers(*p) || p->isp.ZWriteDis;
			}
			// TR First Pass: a run that is not drawn has no state to apply; alpha_rule gives its vertex alpha.
			alpha_or = draws || !s_do.first_pass ? apply_param(*p, ListType_Translucent) : alpha_rule(*p);
			rp = p;
			run_first = t;
			run_cull = eff_cull(*p);
			offs = run_offs(*p);
			run_vb = own ? s_vb_used : base + 3 * t;
		}
		if (!emit || (own && !draws))
			continue;
		const u32 c = s_tri[t].id & 0xFFFF, cull = eff_cull(*p);
		const u32 swap = ((s_tri[t].id >> 16) & 1) ^ (u32)(cull >= 2 && cull != run_cull);
		const u32 corner[3] = { swap ? c - 1 : c - 2, swap ? c - 2 : c - 1, c };
		for (u32 k = 0; k < 3; k++)
			vb_emit(&s_rl.vtx[corner[k]], alpha_or, offs ? offs[corner[k]] : 0);
	}
}

static inline u32 vb_spare();

// Bundle 14 TR First Pass GAME ORDER: the first pass writes its own corners, so n triangles take up to 6 n vertices.
// When the scene's block is too short, the rest of the scene goes to a fresh block. False = the sorted first pass.
static bool tri_game_room(u32 n)
{
	const u32 room = s_vb_cap - s_vb_used;
	if (6 * n <= room)
		return true;
	// No room for the triangles once: the caller cuts them, as before.
	if (3 * n > room)
		return false;
	const u32 cap = room + 3 * n;
	u32 off;
	ps3gl_vertex_t* const vb = ps3gl_vring_alloc((int)(cap + vb_spare()), &off);
	if (!vb)
		return false;
	s_vb = vb;
	s_vb_off = off;
	s_vb_cap = cap;
	s_vb_used = 0;
	return true;
}

// 3k: the autosorted translucent list of the pass, far to near. ON draws it once with no depth write (Flycast).
// + DEPTH WRITE keeps the game's depth write. + OPAQUE DEPTH first writes the depth of the near-opaque texels alone.
static void draw_tr_tris()
{
	const u64 t_sort = PERF_TICKS();
	u32 n = tri_collect();
	// The depth the first pass leaves does not depend on its draw order: GAME ORDER sends it before the sort, in
	// submission order, where its runs are as long as the merged strips.
	const bool game = s_do.tri_sort == 3 && s_do.first_pass == 2 && tri_game_room(n);
	if (!game)
		tri_sort(n);
	s_dt[DT_SORT] += PERF_TICKS() - t_sort;
	if (n > (s_vb_cap - s_vb_used) / 3)
		n = (s_vb_cap - s_vb_used) / 3;
	s_stat_tris += n;
	u32 base = s_vb_used;
	reset_param_cache();
	if (s_do.tri_sort == 3)
	{
		// A texel that hides what is behind it does so in any draw order; the rest blend in the sorted order.
		s_tri_mode = TRI_OPAQUE;
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glAlphaFunc(GL_GEQUAL, TRI_OPAQUE_REF / 255.0f);
		tri_runs(n, base, true, game);
		if (game)
		{
			const u64 t_late = PERF_TICKS();
			tri_sort(n);
			s_dt[DT_SORT] += PERF_TICKS() - t_late;
			base = s_vb_used;
		}
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glAlphaFunc(GL_GREATER, 0.0f);
		reset_param_cache();
		s_tri_mode = TRI_NODEPTH;
		tri_runs(n, base, game, false);
	}
	else
	{
		s_tri_mode = s_do.tri_sort == 2 ? TRI_GAME : TRI_NODEPTH;
		tri_runs(n, base, true, false);
	}
	s_tri_mode = TRI_NONE;
	reset_param_cache();
}

// 5.12 #2 ON: one list from the SPU's runs; each run is apply_param and one draw, as run_close does.
// False when the SPU fails; s_spevb_drawn then says whether any run of the list went out.
static u32 s_spevb_drawn;

static bool draw_runs(u32 sec, u32 list)
{
	s_spevb_drawn = 0;
	for (u32 k = ps3spevb_sec_start(sec);; k++)
	{
		const SpeRun* r = ps3spevb_run(k, sec);
		if (!r)
			break;
		const DcParam& p = s_rl.param[r->param];
		const u32 alpha_or = apply_param(p, list);
		ps3spevb_note_alpha(alpha_or == alpha_rule(p));
		if (r->count)
		{
			const u64 t0 = PERF_TICKS();
			draw_vb(GL_TRIANGLE_STRIP, SPEVB_VTX_STRIDE, r->first, r->count);
			s_dt[DT_GLEND] += PERF_TICKS() - t0;
			s_stat_draws[list]++;
		}
		s_spevb_drawn++;
	}
	if (!ps3spevb_mode())
		return false;
	s_stat_strips[list] += ps3spevb_strips(sec);
	return true;
}

// P25 FIRST VERTEX: the shifted colour attribute reads 2 vertices past a run, so each block holds 2 more.
static inline u32 vb_spare()
{
	return s_do.flat == 2 ? 2 : 0;
}

// After a failed SPU build the PPU draws the rest of the scene in a fresh vring block: late SPU puts may
// still land in the old one. A list the SPU had begun is lost for this frame.
static bool spevb_lost(int& build)
{
	build = SPEVB_OFF;
	s_vb = ps3gl_vring_alloc((int)(s_vb_cap + vb_spare()), &s_vb_off);
	s_vb_used = 0;
	return s_spevb_drawn != 0;
}

static void draw_list_or_runs(u32 sec, u32 list, int& build)
{
	if (build == SPEVB_ON && (draw_runs(sec, list) || spevb_lost(build)))
		return;
	draw_list(list);
}

// TR_PPU: past SPEVB_TR_MAX strips (or a W the SPU keys do not cover) the PPU sorts. Its 36-byte vertices
// go to a block of their own, with the cap the SPU left.
static void draw_tr(int& build)
{
	if (build == SPEVB_ON)
	{
		if (!draw_runs(SPEVB_SEC_TR, ListType_Translucent))
		{
			if (spevb_lost(build))
				return;
		}
		else
		{
			ps3spevb_end_frame();
			if (!ps3spevb_tr_ppu())
				return;
			s_vb_cap -= ps3spevb_vb_used();
			s_vb = ps3gl_vring_alloc((int)(s_vb_cap + vb_spare()), &s_vb_off);
			s_vb_used = 0;
		}
	}
	// 3k needs the vertex block; glBegin/glEnd keeps the sort per strip.
	if (s_pass_autosort && s_do.tri_sort && s_vb)
		draw_tr_tris();
	else if (s_pass_autosort)
		draw_tr_sorted();
	else
		draw_list(ListType_Translucent);
}

// Starts the SPU build of this scene. ON writes 48-byte slots into the scene's own vring block, moved to a
// 128-byte line (128 spare bytes cover the move); CHECK writes into the SPU's copy, both sides with one cap.
static u32 s_chk_cap;

static int spevb_begin(int build)
{
	if (!s_vb || build == SPEVB_OFF)
		return SPEVB_OFF;
	void* dst = s_vb;
	u32 cap = s_vb_cap;
	if (build == SPEVB_ON)
	{
		const u32 pad = (u32)(-(uintptr_t)s_vb & 127);
		s_vb = (ps3gl_vertex_t*)((u8*)s_vb + pad);
		s_vb_off += pad;
		dst = s_vb;
	}
	else
	{
		u32 *spu_vb, *dry_vb, ccap;
		SpeRun* dry_runs;
		if (!ps3spevb_check_bufs(&spu_vb, &dry_vb, &dry_runs, &ccap))
			return SPEVB_OFF;
		dst = spu_vb;
		cap = s_chk_cap = s_vb_cap < ccap ? s_vb_cap : ccap;
	}
	const u32 flags = (g_rr.autosort ? SPEVB_AUTOSORT : 0) | (s_mo_sprite ? SPEVB_SPRITE_CULL : 0) |
	                  ((s_rl.opt & TAOPT_TILE_CLIP) ? SPEVB_CLIP_KEY : 0) |
	                  ((s_do.tri_sort && g_rr.autosort) ? SPEVB_NO_TR : 0) | (s_do.offs ? SPEVB_OFFS : 0);
	return ps3spevb_request((u32)s_rl_spe, s_rl.nvtx, s_rl.nstrip, s_rl.nparam, dst, cap, flags)
	     ? build : SPEVB_OFF;
}

static void spevb_report(const char* what, u32 idx, const u32* spu, const u32* ppu, u32 words)
{
	static u32 s_logged;
	if (s_logged++ >= 8)
		return;
	char line[320];
	int n = snprintf(line, sizeof(line), "[spevb] check MISMATCH %s[%u] spu/ppu:", what, idx);
	for (u32 k = 0; k < words && n > 0 && n < (int)sizeof(line); k++)
		n += snprintf(line + n, sizeof(line) - n, " %08X/%08X", spu[k], ppu[k]);
	printf("%s\n", line);
}

// 5.12 #2 CHECK: the PPU's merge again as a dry pass into a cacheable copy, against the SPU's build.
static void spevb_check()
{
	u32 *spu_vb, *dry_vb, ccap;
	SpeRun* dry_runs;
	if (!ps3spevb_check_bufs(&spu_vb, &dry_vb, &dry_runs, &ccap))
		return;
	ps3gl_vertex_t* const vb = s_vb;
	const u32 vb_off = s_vb_off, vb_used = s_vb_used, vb_cap = s_vb_cap;
	s_vb = (ps3gl_vertex_t*)dry_vb;
	s_vb_used = 0;
	s_vb_cap = s_chk_cap;
	s_dry_runs = dry_runs;
	s_dry_nruns = 0;
	static const u32 lists[3] = { ListType_Opaque, ListType_Punch_Through, ListType_Translucent };
	u32 sec_end[3], strips[3], vtx_end[3];
	for (u32 k = 0; k < 3; k++)
	{
		s_dry_strips = 0;
		if (k == SPEVB_SEC_TR && g_rr.autosort)
			draw_tr_sorted<true>();
		else
			draw_list<true>(lists[k]);
		sec_end[k] = s_dry_nruns;
		strips[k] = s_dry_strips;
		vtx_end[k] = s_vb_used;
	}
	s_vb = vb;
	s_vb_off = vb_off;
	s_vb_used = vb_used;
	s_vb_cap = vb_cap;

	const volatile SpeVbEcho* e = ps3spevb_wait_final();
	if (!e)
		return;
	ps3spevb_end_frame();
	const u32 nsec = (e->flags & SPEVB_TR_PPU) ? 2 : 3;
	bool ok = true;
	for (u32 k = 0; k < nsec && ok; k++)
	{
		const u32 hs[] = { e->sec_end[k], e->strips[k] }, hp[] = { sec_end[k], strips[k] };
		if (hs[0] != hp[0] || hs[1] != hp[1])
		{
			spevb_report("sec_end/strips", k, hs, hp, 2);
			ok = false;
		}
	}
	const u32 nv = vtx_end[nsec - 1];
	if (ok && e->vb_used != nv)
	{
		const u32 hs = e->vb_used;
		spevb_report("vb_used", 0, &hs, &nv, 1);
		ok = false;
	}
	const SpeRun* sr = ps3spevb_runs();
	for (u32 i = 0; ok && i < sec_end[nsec - 1]; i++)
		if (memcmp(&sr[i], &dry_runs[i], sizeof(SpeRun)) != 0)
		{
			spevb_report("run", i, (const u32*)&sr[i], (const u32*)&dry_runs[i], 4);
			ok = false;
		}
	// The SPU's slot: the 9 words of ps3gl_vertex_t, then 3 zero words.
	for (u32 i = 0; i < nv && ok; i++)
	{
		const u32* s = spu_vb + i * (SPEVB_VTX_STRIDE / 4);
		if (memcmp(s, dry_vb + i * 9, SPEVB_VTX_BYTES) != 0 || s[9] || s[10] || s[11])
		{
			spevb_report("vtx", i, s, dry_vb + i * 9, 9);
			ok = false;
		}
	}
	ps3spevb_check_result(ok);
}

// Flycast gldraw.cpp SetMVS_Mode. Stencil bit 7 = polygon takes shadows (kept), bit 1 = volume
// parity, bit 0 = inside the summed volume. Inclusion ORs bit 1 into bit 0, exclusion ANDs.
enum { MV_XOR, MV_OR, MV_INCLUSION, MV_EXCLUSION };

static void set_mvs_mode(u32 mode, u32 cull)
{
	if (mode == MV_XOR || mode == MV_OR)
	{
		glEnable(GL_DEPTH_TEST);
		glStencilMask(2);
		if (mode == MV_XOR)
		{
			glStencilFunc(GL_ALWAYS, 0, 2);
			glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
		}
		else
		{
			glStencilFunc(GL_ALWAYS, 2, 2);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		}
		set_cull(cull);
		return;
	}
	// Summing the area is not depth tested.
	glDisable(GL_DEPTH_TEST);
	glStencilMask(3);
	if (mode == MV_INCLUSION)
	{
		glStencilFunc(GL_LEQUAL, 1, 3);
		glStencilOp(GL_ZERO, GL_ZERO, GL_REPLACE);
	}
	else
	{
		glStencilFunc(GL_EQUAL, 1, 3);
		glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
	}
}

static inline void emit_mod_vertex(float x, float y, float inv_w)
{
	float safe = (inv_w >= INV_W_FLOOR) ? inv_w : INV_W_FLOOR;
	float w = 1.0f / safe;
	glVertex3f(x * w, y * w, w);
}

static void draw_mod_tris(u32 first, u32 count)
{
	if (first + count > s_rl.nmtri)
		return;
	glBegin(GL_TRIANGLES);
	for (u32 i = 0; i < count; i++)
	{
		const ModTri& t = s_rl.mtri[first + i];
		emit_mod_vertex(t.x0, t.y0, t.z0);
		emit_mod_vertex(t.x1, t.y1, t.z1);
		emit_mod_vertex(t.x2, t.y2, t.z2);
	}
	glEnd();
}

// P27 (Flycast f2013c652, Xtreme Sports): when a pass has one volume list for opaque and translucent, and the
// translucent list holds more volumes, its volumes are the ones drawn. 0 = the opaque list, 1 = the translucent.
static u32 mv_pick_list()
{
	if (!s_do.tr_vol || !g_rr.mv_shared)
		return 0;
	u32 n[2] = { 0, 0 };
	for (u32 i = 0; i + 1 < s_rl.nmvol; i++)
		if (s_rl.mvol[i + 1].first > s_rl.mvol[i].first)
			n[s_rl.mvol[i].list & 1]++;
	return n[1] > n[0];
}

// Modifier volumes, as Flycast DrawModVols: the volume goes into stencil bits 1:0, then pixels with bit 7 (Shadow)
// and bit 0 are shaded at FPU_SHAD_SCALE. [lo, hi): the pass's volume records; vlist: the list drawn (mv_pick_list).
static void draw_modvols(float cx0, float cy0, float cw, float ch, float quad_w, u32 lo, u32 hi, u32 vlist)
{
	if (s_rl.nmvol < 2 || s_rl.nmtri == 0 || lo >= hi)
		return;
	if (s_do.clip)
	{
		clip_base();
		s_clip_mode = CLIP_NONE;
	}
	s_flat_shift = 0;

	glDisable(GL_TEXTURE_2D);
	// 3a: the volumes and the shade quad draw with no fog.
	if (s_fog)
		ps3gl_dc_fog(PS3GL_FOG_NONE);
	// Bundle 17: and with no offset colour, clamp or weight of the polygon before them.
	if (s_do.offs)
		ps3gl_dc_offs(0);
	if (s_clamp_on || s_do.tril)
		ps3gl_dc_xt(0, 0, 0, 1.0f);
	glDisable(GL_ALPHA_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	// The DC compares 1/W with "greater".
	glDepthFunc(dc_depth_func(DC_GREATER));
	glEnable(GL_STENCIL_TEST);

	s32 mod_base = -1;
	for (u32 i = lo; i < hi && i + 1 < s_rl.nmvol; i++)
	{
		const ModVolParam& p = s_rl.mvol[i];
		u32 count = s_rl.mvol[i + 1].first - p.first;
		if (count == 0 || p.list != vlist)
			continue;
		if (mod_base < 0)
			mod_base = (s32)p.first;

		// An open volume or quad ORs; a closed volume XORs.
		set_mvs_mode((!p.volume_last && p.depth > 0) ? MV_OR : MV_XOR, p.cull);
		draw_mod_tris(p.first, count);

		// DepthMode 1/2 closes the volume: sum its area as inclusion or exclusion.
		if (p.depth == 1 || p.depth == 2)
		{
			set_mvs_mode(p.depth == 1 ? MV_INCLUSION : MV_EXCLUSION, p.cull);
			draw_mod_tris((u32)mod_base, p.first + count - (u32)mod_base);
			mod_base = -1;
		}
	}

	// Shade Shadow polygons inside a volume; the op also clears bits 1:0.
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glStencilFunc(GL_EQUAL, 0x81, 0x81);
	glStencilMask(3);
	glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
	// Flycast: black at alpha 1 - scale/256, so the lit colour keeps scale/256 of itself.
	u32 sf = g_rr.fpu_shad_scale & 0xFF;
	glColor4ub(0, 0, 0, (GLubyte)(255 - (sf * 255 + 128) / 256));
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(cx0 * quad_w, cy0 * quad_w, quad_w);
	glVertex3f((cx0 + cw) * quad_w, cy0 * quad_w, quad_w);
	glVertex3f(cx0 * quad_w, (cy0 + ch) * quad_w, quad_w);
	glVertex3f((cx0 + cw) * quad_w, (cy0 + ch) * quad_w, quad_w);
	glEnd();

	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	reset_param_cache();
}

// P10 List Passes level 2 (Flycast drawSorted with TranslucentPolygonDepthMask, Cosmic Smash): the pass's sorted
// translucent strips again, depth only (apply_param_body), so the next pass tests against them.
static void tr_depth_pass()
{
	const u32 strips = s_stat_strips[ListType_Translucent], draws = s_stat_draws[ListType_Translucent];
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	s_depth_pass = true;
	reset_param_cache();
	draw_tr_sorted();
	s_depth_pass = false;
	reset_param_cache();
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	s_stat_strips[ListType_Translucent] = strips;
	s_stat_draws[ListType_Translucent] = draws;
}

// Resets the GL state the passes below assume; must run before the clear.
static void reset_gl_state()
{
	glDisable(GL_TEXTURE_2D);
	ps3gl_tex_bump(0);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
}

// Per-list dump of the last drawn frame: what the geometry looks like and what
// state it carries. Prints beside the stats line while the SC5 fault is open.
struct ListDiag
{
	u32 strips, verts, zwd, tex, forced, usealpha, ignoretexa;
	float x0, x1, y0, y1, w0, w1;
	u32 dm[8], cull[4], src[8], dst[8], shad[4], fmt[8];
	u32 amin, amax;
	// Shade mode 3 modulates the texture by the vertex color, so an all-black
	// vertex color makes a correctly textured poly render as nothing.
	u32 cmin, cmax, black, ct[4];
	// Strips whose bounding box covers most of the canvas: one of these drawn
	// near, with Z write on, masks everything the later lists put behind it.
	u32 big;
	float bigw0, bigw1;
	// Bundle 4, strips a row acts on: no Gouraud; sprites with a cull direction; user clip inside and outside;
	// blend factors that are not (ONE, ZERO).
	u32 flat, sprcull, clip_in, clip_out, blend;
	// 3a: strips per FogCtrl (table, vertex, none, table as the base colour), and those the vertex fog acts on.
	u32 fog[4], fogv;
	// Bundle 14 (P73's counter): state changes from strip to strip, by the merge key and by the bits the draw reads.
	u32 runs, runs_read;
	// Bundle 17, strips a row acts on: an offset colour; ColorClamp; a trilinear pass and those with a weight under 1;
	// PT strips that ALPHA 1 draws with no blend; sprites whose fourth corner is not the parallelogram's.
	u32 offc, cclamp, tril, tril_w, pt_one, spr_quad;
};
static ListDiag s_diag[3];
static const char* s_diag_name[3] = { "op", "pt", "tr" };

// strip_equivalent without the bits no draw reads: PCW Volume, Gouraud (no Flat Shading), Offset (no vertex fog); a
// sorted or PT list's DepthMode; TSP MipMapD, SuperSample, ColorClamp, Src/DstSelect, FogCtrl (no Fog), FilterMode 1-3.
static bool strip_read_equivalent(const DcParam& a, const DcParam& b, u32 list)
{
	const u32 pcw = s_key_pcw & ~(0x40u | (s_do.flat ? 0u : 0x02u) | (s_fog >= 2 || s_do.offs ? 0u : 0x04u));
	const bool own_depth = list == ListType_Opaque || (list == ListType_Translucent && !g_rr.autosort);
	const u32 isp = own_depth ? s_key_isp : s_key_isp & ~0xE0000000u;
	// Bundle 17: Colour Clamp reads ColorClamp, Trilinear reads MipMapD's low bits and the FilterMode.
	const u32 tsp = ~((s_do.tril ? 0xC00u : 0xF00u) | 0x1000u | (s_do.tril ? 0u : 0x6000u) | (s_clamp_on ? 0u : 0x200000u) |
	                  0x1000000u | 0x2000000u | (s_fog ? 0u : 0xC00000u));
	return ((a.pcw.full ^ b.pcw.full) & pcw) == 0 && ((a.isp.full ^ b.isp.full) & isp) == 0
	    && ((a.tsp.full ^ b.tsp.full) & tsp) == 0 && !a.tsp.FilterMode == !b.tsp.FilterMode && a.tcw.full == b.tcw.full;
}

static void collect_diag()
{
	static const u32 lists[3] = { ListType_Opaque, ListType_Punch_Through, ListType_Translucent };
	for (u32 L = 0; L < 3; L++)
	{
		ListDiag& d = s_diag[L];
		memset(&d, 0, sizeof(d));
		d.x0 = d.y0 = d.w0 = 1e30f;
		d.x1 = d.y1 = -1e30f;
		d.w1 = 0.0f;
		d.amin = 256; d.amax = 0;
		d.cmin = 256; d.cmax = 0;
		d.bigw0 = 1e30f; d.bigw1 = 0.0f;
		const DcParam* prev = 0;
		for (u32 i = 0; i < s_rl.nstrip; i++)
		{
			const DcStrip& st = s_rl.strip[i];
			if (st.list != lists[L])
				continue;
			const DcParam& p = s_rl.param[st.param];
			d.strips++;
			d.runs += !prev || !strip_equivalent(p, *prev);
			d.runs_read += !prev || !strip_read_equivalent(p, *prev, lists[L]);
			prev = &p;
			d.verts += st.count;
			d.dm[p.isp.DepthMode & 7]++;
			d.cull[p.isp.CullMode & 3]++;
			if (p.isp.ZWriteDis) d.zwd++;
			if (p.pcw.Texture) d.tex++;
			d.src[p.tsp.SrcInstr & 7]++;
			d.dst[p.tsp.DstInstr & 7]++;
			d.shad[p.tsp.ShadInstr & 3]++;
			if (p.tsp.UseAlpha) d.usealpha++;
			if (p.tsp.IgnoreTexA) d.ignoretexa++;
			if (p.pcw.Texture) d.fmt[p.tcw.NO_PAL.PixelFmt & 7]++;
			// The same rule apply_param uses, minus the bind result.
			bool force = !p.tsp.UseAlpha || (p.pcw.Texture && p.tsp.ShadInstr == 1);
			if (force) d.forced++;
			d.ct[p.pcw.Col_Type & 3]++;
			if (!p.pcw.Gouraud) d.flat++;
			if ((p.pcw.full >> 29) == 5 && p.isp.CullMode >= 2) d.sprcull++;
			if (((p.pcw.full >> 16) & 3) == 2) d.clip_in++;
			if (((p.pcw.full >> 16) & 3) == 3) d.clip_out++;
			if (p.tsp.SrcInstr != 1 || p.tsp.DstInstr != 0) d.blend++;
			d.fog[p.tsp.FogCtrl & 3]++;
			if (fogv_param(p)) d.fogv++;
			if (offc_param(p)) d.offc++;
			if (p.tsp.ColorClamp) d.cclamp++;
			if (p.pcw.Texture && p.tsp.FilterMode >= 2 && p.tcw.NO_PAL.MipMapped)
			{
				d.tril++;
				d.tril_w += tril_weight(p, lists[L], true) != 1.0f;
			}
			if (L == 1 && (p.tsp.SrcInstr == 1 || p.tsp.SrcInstr == 4) && (p.tsp.DstInstr == 0 || p.tsp.DstInstr == 5))
				d.pt_one++;
			// A sprite is D, C, A, B: its corner D against A + C - B, as spr_plane tests it.
			if ((p.pcw.full >> 29) == 5 && st.count == 4)
			{
				const DcVertex* q = &s_rl.vtx[st.first];
				if ((double)q[0].x - q[2].x != ((double)q[1].x - q[2].x) - ((double)q[3].x - q[2].x) ||
				    (double)q[0].y - q[2].y != ((double)q[1].y - q[2].y) - ((double)q[3].y - q[2].y))
					d.spr_quad++;
			}
			float sx0 = 1e30f, sx1 = -1e30f, sy0 = 1e30f, sy1 = -1e30f;
			float sw0 = 1e30f, sw1 = 0.0f;
			for (u32 k = 0; k < st.count; k++)
			{
				const DcVertex& v = s_rl.vtx[st.first + k];
				if (v.x < sx0) sx0 = v.x;
				if (v.x > sx1) sx1 = v.x;
				if (v.y < sy0) sy0 = v.y;
				if (v.y > sy1) sy1 = v.y;
				if (v.w < sw0) sw0 = v.w;
				if (v.w > sw1) sw1 = v.w;
				u32 a = force ? 255u : (v.col >> 24);
				if (a < d.amin) d.amin = a;
				if (a > d.amax) d.amax = a;
				u32 r = (v.col >> 16) & 0xFF, g = (v.col >> 8) & 0xFF, b = v.col & 0xFF;
				u32 mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
				if (mx < d.cmin) d.cmin = mx;
				if (mx > d.cmax) d.cmax = mx;
				if (mx == 0) d.black++;
				if (v.x < d.x0) d.x0 = v.x;
				if (v.x > d.x1) d.x1 = v.x;
				if (v.y < d.y0) d.y0 = v.y;
				if (v.y > d.y1) d.y1 = v.y;
				if (v.w < d.w0) d.w0 = v.w;
				if (v.w > d.w1) d.w1 = v.w;
			}
			if ((sx1 - sx0) >= 320.0f && (sy1 - sy0) >= 240.0f)
			{
				d.big++;
				if (sw0 < d.bigw0) d.bigw0 = sw0;
				if (sw1 > d.bigw1) d.bigw1 = sw1;
			}
		}
	}
}

static void print_diag()
{
	for (u32 L = 0; L < 3; L++)
	{
		const ListDiag& d = s_diag[L];
		if (d.strips == 0)
			continue;
		printf("[rendz] %s n=%u v=%u x=[%g..%g] y=[%g..%g] w=[%g..%g] "
		       "dm=%u/%u/%u/%u/%u/%u/%u/%u zwd=%u cull=%u/%u/%u/%u tex=%u big=%u bigw=[%g..%g]\n",
		       s_diag_name[L], d.strips, d.verts,
		       (double)d.x0, (double)d.x1, (double)d.y0, (double)d.y1,
		       (double)d.w0, (double)d.w1,
		       d.dm[0], d.dm[1], d.dm[2], d.dm[3], d.dm[4], d.dm[5], d.dm[6], d.dm[7],
		       d.zwd, d.cull[0], d.cull[1], d.cull[2], d.cull[3], d.tex,
		       d.big, (double)d.bigw0, (double)d.bigw1);
		printf("[rendz] %s a=[%u..%u] forced=%u ua=%u ita=%u shad=%u/%u/%u/%u "
		       "src=%u/%u/%u/%u/%u/%u/%u/%u dst=%u/%u/%u/%u/%u/%u/%u/%u "
		       "fmt=%u/%u/%u/%u/%u/%u/%u/%u rgb=[%u..%u] black=%u ct=%u/%u/%u/%u\n",
		       s_diag_name[L], d.amin, d.amax, d.forced, d.usealpha, d.ignoretexa,
		       d.shad[0], d.shad[1], d.shad[2], d.shad[3],
		       d.src[0], d.src[1], d.src[2], d.src[3], d.src[4], d.src[5], d.src[6], d.src[7],
		       d.dst[0], d.dst[1], d.dst[2], d.dst[3], d.dst[4], d.dst[5], d.dst[6], d.dst[7],
		       d.fmt[0], d.fmt[1], d.fmt[2], d.fmt[3], d.fmt[4], d.fmt[5], d.fmt[6], d.fmt[7],
		       d.cmin, d.cmax, d.black, d.ct[0], d.ct[1], d.ct[2], d.ct[3]);
		printf("[rendz] %s flat=%u sprcull=%u clip=%u/%u blend=%u fog=%u/%u/%u/%u fogv=%u\n",
		       s_diag_name[L], d.flat, d.sprcull, d.clip_in, d.clip_out, d.blend,
		       d.fog[0], d.fog[1], d.fog[2], d.fog[3], d.fogv);
		printf("[rendz] %s offs=%u cclamp=%u tril=%u/%u pt_one=%u spr_quad=%u\n",
		       s_diag_name[L], d.offc, d.cclamp, d.tril_w, d.tril, d.pt_one, d.spr_quad);
	}
}

// Draws the render context into the viewport. The canvas rect is the DC area mapped onto it:
// the whole screen for display, the FB clip window for a render to texture.
static void draw_scene(float cx0, float cy0, float cw, float ch)
{
	// ISP_BACKGND_D is the background 1/W (low 4 bits masked, as in nullDC-v1). The register value
	// is a native u32 here, so memcpy gives the float on BE as on LE.
	u32 bgd_bits = g_rr.isp_backgnd_d & ~0xFu;
	float bg_inv_w;
	memcpy(&bg_inv_w, &bgd_bits, 4);
	// Flycast 443d5a2ba: push the plane 1e-6 back so geometry at its depth wins (Xtreme Sports skybox).
	bg_inv_w = std::max(bg_inv_w - 1e-6f, 1e-11f);
	float bg_w = 1.0f / ((bg_inv_w >= INV_W_FLOOR) ? bg_inv_w : INV_W_FLOOR);

	// reicast glesrend.cpp pins the far plane at 1/W = 0, so only the nearest
	// vertex sets the range; tracking the far end let one poly at 1/W <= 0.0001 eat it.
	union { float f; s32 i; } bz, mz;
	bz.f = bg_inv_w;
	mz.f = s_rl.max_inv_w;
	float max_inv_w = (mz.i < bz.i && bz.i < 0x49800000) ? bg_inv_w : mz.f;
	// reicast's sanity fallback: past 1024*1024 the range is garbage, not a scene.
	if (!(max_inv_w > 0.0f) || max_inv_w > 1048576.0f)
		max_inv_w = 10.0f * 1024.0f;
	// reicast's fZ_max *= 1.001: the nearest layer defines the plane it sits on,
	// and float rounding would otherwise clip it away.
	max_inv_w *= 1.001f;
	float n = 1.0f / max_inv_w;
	if (s_rl.min_w < s_stat_w_lo) s_stat_w_lo = s_rl.min_w;
	if (s_rl.max_w > s_stat_w_hi) s_stat_w_hi = s_rl.max_w;
	if (n < s_stat_near_lo) s_stat_near_lo = n;
	s_stat_bg_inv_w = bg_inv_w;
	s_stat_bg_zw = g_rr.bg_param.isp.ZWriteDis;

	// Input (x*W, y*W, W): clip.w = W, so attributes interpolate with perspective. FIXED: z_ndc = 1 - 2n/W,
	// -1 at W = n and +1 at 1/W = 0. FLOAT: z = n/W, unchanged by the depth range (-1, 1).
	float m[16] = { 0 };
	float range_near;
	m[0] = 2.0f / cw;
	m[8] = -1.0f - 2.0f * cx0 / cw;
	m[5] = -2.0f / ch;
	m[9] = 1.0f + 2.0f * cy0 / ch;
	depth_proj(s_zfloat, n, &m[10], &m[14], &range_near);
	m[11] = 1.0f;
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDepthRange(range_near, 1.0);

	s_cv.cx0 = cx0;
	s_cv.cy0 = cy0;
	s_cv.cw = cw;
	s_cv.ch = ch;
	s_clip_base = true;
	s_clip_mode = CLIP_NONE;
	s_flat_shift = 0;
	s_pass = 0;
	s_pass_autosort = g_rr.autosort;
	s_depth_pass = false;

	// nullDC-v1 always shades Gouraud; its winding is judged as seen on screen.
	glShadeModel(GL_SMOOTH);
	glFrontFace(GL_CCW);
	glEnable(GL_DEPTH_TEST);

	// OP/PT write stencil bit 7 from each polygon's Shadow flag (apply_param), as Flycast DrawList.
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	fog_scene_begin();
	xt_scene_begin();

	// Background quad over the whole canvas at the background depth; colors and UV from its plane.
	reset_param_cache();
	u32 bg_alpha_or = apply_param(g_rr.bg_param, ListType_Opaque);
	static const float corner[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
	glBegin(GL_TRIANGLE_STRIP);
	for (int k = 0; k < 4; k++)
	{
		BgVertex bv;
		bg_eval(g_rr.bg_vtx, cx0 + corner[k][0] * cw, cy0 + corner[k][1] * ch, &bv);
		u32 c = bv.col | bg_alpha_or;
		glColor4ub((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
		glTexCoord2f(bv.u, bv.v);
		glVertex3f(bv.x * bg_w, bv.y * bg_w, bg_w);
	}
	glEnd();

	if (s_rl.nstrip != 0)
	{
		s_merge = get_strip_merge_preset() != 0;
		// [rendz] prints only the last frame of a window, and a render job always ends in rth_print_stats.
		if (os_GetSeconds() - s_stat_t0 >= STAT_WINDOW_S)
			collect_diag();
		// P10 List Passes: the marks of the PPU replay; the SPU lists carry none (rth_job_decode).
		const bool multi = s_do.passes && s_rl.npass != 0;
		if (multi)
			s_stat_multi++;
		// Worst case per strip: 3 joins, and per vertex a join copy, a parity copy and itself.
		s_vb = 0;
		int build = SPEVB_OFF;
		if (s_merge && get_vertex_array_preset())
		{
			s_vb_cap = 3 * s_rl.nvtx + 4 * s_rl.nstrip + 16;
			// The TR depth pass writes the translucent strips a second time.
			if (multi && s_do.passes == 2)
				s_vb_cap *= 2;
			s_vb_used = 0;
			build = s_rl_spe >= 0 ? ps3spevb_mode() : SPEVB_OFF;
			if (build == SPEVB_ON)
			{
				s_vb = (ps3gl_vertex_t*)ps3gl_vring_alloc_raw((s_vb_cap + vb_spare()) * SPEVB_VTX_STRIDE + 128, &s_vb_off);
				if (!s_vb)
					build = SPEVB_OFF;
			}
			if (build != SPEVB_ON)
				s_vb = ps3gl_vring_alloc((int)(s_vb_cap + vb_spare()), &s_vb_off);
			build = spevb_begin(build);
		}
		// The clip's extra draws and the colour shift need the vertex block: glBegin/glEnd draws without them.
		if (!s_vb)
		{
			s_do.clip = 0;
			if (s_do.flat == 2)
				s_do.flat = 1;
		}
		// Hardware order: opaque, punch-through, translucent. P10 List Passes: once per pass (Flycast drawStrips);
		// a pass after the first with no strip is not drawn, and its volumes go to the next pass.
		const u32 npass = multi ? s_rl.npass + 1 : 1;
		const u32 vlist = mv_pick_list();
		if (vlist)
			s_stat_trvol++;
		u32 mv_lo = 0;
		for (u32 k = 0; k < npass; k++)
		{
			s_strip_lo = k ? s_rl.pass_strip[k - 1] : 0;
			s_strip_hi = k + 1 < npass ? s_rl.pass_strip[k] : s_rl.nstrip;
			const u32 mv_hi = k + 1 < npass ? s_rl.pass_mvol[k] : s_rl.nmvol;
			if (k != 0)
			{
				if (s_strip_lo == s_strip_hi)
					continue;
				// The state the first pass starts with.
				glDisable(GL_ALPHA_TEST);
				glEnable(GL_STENCIL_TEST);
				glStencilMask(0xFF);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glDepthMask(GL_TRUE);
			}
			s_pass = k;
			s_pass_autosort = g_rr.pass_autosort[k < RR_MAX_PASSES ? k : RR_MAX_PASSES - 1];
			reset_param_cache();
			draw_list_or_runs(SPEVB_SEC_OP, ListType_Opaque, build);

			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GEQUAL, (g_rr.pt_alpha_ref & 0xFF) / 255.0f);
			reset_param_cache();
			draw_list_or_runs(SPEVB_SEC_PT, ListType_Punch_Through, build);

			// Hardware shades the opaque volumes between punch-through and translucent.
			const u64 t_mv = PERF_TICKS();
			draw_modvols(cx0, cy0, cw, ch, bg_w, mv_lo, mv_hi, vlist);
			s_dt[DT_MODVOL] += PERF_TICKS() - t_mv;
			glDisable(GL_STENCIL_TEST);
			mv_lo = mv_hi;

			// apply_param turns the "alpha > 0" test on per translucent poly.
			glDisable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0.0f);
			reset_param_cache();
			draw_tr(build);
			if (s_do.passes == 2 && k + 1 < npass && s_pass_autosort)
				tr_depth_pass();
		}
		s_pass = 0;
		if (build == SPEVB_CHECK)
			spevb_check();
		s_vb = 0;
	}
	if (s_do.clip)
		clip_base();
	s_clip_mode = CLIP_NONE;
	s_flat_shift = 0;
	if (s_do.flat)
		glShadeModel(GL_SMOOTH);
	fog_scene_end();
	if (s_do.offs)
		ps3gl_dc_offs(0);
	if (s_clamp_on || s_do.tril)
		ps3gl_dc_xt(0, 0, 0, 1.0f);
	ps3gl_tex_bump(0);
	ps3gl_tex1_bits(0);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_TRUE);
	// The UI and the framebuffer presents draw with ps3gl's default depth.
	glDepthRange(0.0, 1.0);
	ps3_video_depth_float(0);
}

// P18: the scene's depth format and its clear value (1/W = 0), set before the clear; draw_scene resets them.
static void depth_begin()
{
	static int s_logged = -1;
	s_zfloat = get_depth_float_preset() != 0;
	if ((int)s_zfloat != s_logged)
	{
		s_logged = s_zfloat;
		printf("[rend] depth float %s\n", s_zfloat ? "ON" : "OFF");
	}
	ps3_video_depth_float(s_zfloat);
	glClearDepth(depth_clear(s_zfloat));
}

// -- Bundle 12: the FPS text (P50) and the screenshot (P51) -------------------

// The columns the picture of the frame in progress takes: the screenshot's area.
static int s_pic_x, s_pic_w;
static volatile bool s_shot_want;
// Per stats window: renders drawn wide, WIDE renders their clip kept at 4:3, screenshots.
static u32 s_stat_wide, s_stat_narrow, s_stat_shots;

// The in-game menu asks for a PNG of the next frame that is drawn for the TV.
void ps3rend_screenshot()
{
	s_shot_want = true;
}

// P51: the frame in progress, before the FPS text and the flip; the RSX ends its draws first.
static void shot_take(u32 sh)
{
	s_shot_want = false;
	const double t0 = os_GetSeconds();
	const u32* px = ps3_video_grab((u32)s_pic_x, 0, (u32)s_pic_w, sh);
	REND_PHASE(PH_ENDFRAME);
	char path[96] = "";
	const bool ok = px && ps3shot_save(px, (u32)s_pic_w, sh, path, sizeof(path));
	printf("[shot] %s %s (%dx%u, %s, %.0f ms)\n", ok ? "saved" : "FAILED", path, s_pic_w, sh,
	       px ? "read-back done" : "no read-back", (os_GetSeconds() - t0) * 1000.0);
	s_stat_shots += ok;
}

// P50: the text at the screen's left edge, in the 4:3 picture's side bar. The box under it is opaque, because a
// frame that kept its colour still holds the last text.
static void osd_draw(u32 sw, u32 sh)
{
	if (!ps3ui_overlay_begin())
		return;
	char text[sizeof(s_fps_text[0])], line[OSD_LINES][OSD_LINE_LEN];
	memcpy(text, s_fps_text[s_fps_slot], sizeof(text));
	text[sizeof(text) - 1] = 0;
	const u32 n = osd_split(text, line);
	// The menu's row height and the screen margins, for 720 lines.
	const float row = floorf(26.0f * sh / 720.0f + 0.5f), cap = ps3ui_cap_h(PS3UI_FONT_BODY);
	const float x = floorf(sw * 0.025f), y = floorf(sh * 0.05f);
	float w = 0.0f;
	for (u32 k = 0; k < n; k++)
		w = std::max(w, ps3ui_text_w(PS3UI_FONT_BODY, line[k]));
	if (n)
		ps3ui_rect(x - 8.0f, y - 4.0f, w + 16.0f, n * row + 8.0f, 0xFF000000u);
	for (u32 k = 0; k < n; k++)
		ps3ui_text(x, y + k * row + (row + cap) * 0.5f, PS3UI_FONT_BODY, 0xFFFFFFFFu, line[k]);
	ps3ui_overlay_end();
	reset_param_cache();
}

// Before the end of a frame that is drawn for the TV: the screenshot first, so the PNG has no FPS text.
static void frame_extras()
{
	u32 sw, sh;
	ps3_video_size(&sw, &sh);
	if (s_shot_want)
		shot_take(sh);
	if (g_fps_overlay_preset)
		osd_draw(sw, sh);
	// P37: the VMU screens go over the frame as the FPS text does.
	if (ps3_vmulcd_draw(sw, sh))
		reset_param_cache();
}

static void draw_frame()
{
	u32 sw, sh;
	ps3_video_size(&sw, &sh);

	// draw_scene binds no list param without strips.
	ps3tex_frame_begin(s_rl.param, s_rl.nstrip ? s_rl.nparam : 0);
	reset_gl_state();

	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, sw, sh);
	ps3gl_apply_states();
	glClearColor(0, 0, 0, 1);
	depth_begin();
	glClearStencil(0);

	const float cw = 640.0f * g_rr.fb_scale_x, ch = 480.0f * g_rr.fb_scale_y;
	// Bundle 12 Screen Format: 4:3 with side bars, STRETCH (P69) or WIDE (P53). A WIDE render whose own clip
	// narrows x (Flycast isClipped) has no scene for the sides: it keeps the 4:3 picture.
	const int format = g_screen_format_preset;
	const bool narrow = format == SCREEN_WIDE && g_rr.reg_ok &&
	                    screen_x_clipped(g_rr.fb_x_clip_min, g_rr.fb_x_clip_max, g_rr.reg_x0, g_rr.reg_x1,
	                                     g_rr.glob_clip_x, g_rr.scaler_ctl);
	ScreenView sv;
	screen_view(sw, sh, narrow ? SCREEN_43 : format, cw, &sv);
	const bool wide = sv.cw > cw;
	s_pic_x = sv.x;
	s_pic_w = sv.w;
	s_stat_wide += wide;
	s_stat_narrow += narrow;

	// P60 (Flycast 93523df80, intersectTileAndFBScissor): the PVR draws only the region array's tiles, and the FB
	// clip cuts what the scaler writes, so in TA pixels it is the clip times the scale.
	bool reg = false, cut = false;
	float x0 = 0.0f, y0 = 0.0f, x1 = cw, y1 = ch;
	if (g_region_scissor_preset && g_rr.reg_ok)
	{
		const u32 vs = g_rr.scaler_ctl & 0xFFFF;
		const float xs = ((g_rr.scaler_ctl >> 16) & 1) ? 2.0f : 1.0f;
		const float ys = (vs != 0 && (vs < 1024 || vs > 1025)) ? (float)vs / 1024.0f : 1.0f;
		x0 = std::max(roundf((float)g_rr.fb_x_clip_min * xs), (float)g_rr.reg_x0);
		y0 = std::max(roundf((float)g_rr.fb_y_clip_min * ys), (float)g_rr.reg_y0);
		x1 = std::min(roundf((float)(g_rr.fb_x_clip_max + 1) * xs), (float)g_rr.reg_x1);
		y1 = std::min(roundf((float)(g_rr.fb_y_clip_max + 1) * ys), (float)g_rr.reg_y1);
		reg = true;
		cut = x0 > 0.0f || y0 > 0.0f || x1 < cw || y1 < ch;
		if (cut)
			s_stat_scis++;
		// WIDE: the tiles and the clip take the TA's whole width here, so the scissor leaves the sides open.
		if (wide)
		{
			x0 = sv.cx0;
			x1 = sv.cx0 + sv.cw;
		}
	}

	// P60 (Flycast 3a7a08f3f): over a frame that waits for its flip the colour stays, and the background plane is
	// the clear. A buffer that was flipped holds an old frame, so it is always cleared.
	const bool keep = g_rr.keep && ps3_video_held();
	if (keep)
		s_stat_keep++;
	// Keep Last Frame: the PVR leaves the pixels outside a render's tiles as they are, so a render that the
	// scissor cuts starts from a copy of the last flipped frame, not from black.
	const bool carry = g_keep_frame_preset && cut && !keep && ps3_video_carry();
	if (carry)
		s_stat_carry++;
	glClear(((keep || carry) ? 0 : GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	// A narrow render owns the 4:3 area only: in a frame that kept its colour, the sides may hold a wide frame.
	if (narrow && (keep || carry) && sv.x > 0)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(0, 0, sv.x, sh);
		ps3gl_apply_states();
		glClear(GL_COLOR_BUFFER_BIT);
		glScissor(sv.x + sv.w, 0, sw - sv.x - sv.w, sh);
		ps3gl_apply_states();
		glClear(GL_COLOR_BUFFER_BIT);
	}

	clip_view(sv.x, 0, sv.w, (int)sh, true);
	// No pixel left: no scene.
	if (reg && !clip_view_cut(sv.cx0, 0.0f, sv.cw, ch, x0, y0, x1, y1))
	{
		// What draw_scene leaves for the UI and the 2D presents.
		ps3_video_depth_float(0);
		return;
	}

	glEnable(GL_SCISSOR_TEST);
	glScissor(s_cv.bx, s_cv.by, s_cv.bw, s_cv.bh);
	glViewport(sv.x, 0, sv.w, sh);
	ps3gl_apply_states();

	draw_scene(sv.cx0, 0.0f, sv.cw, ch);
}

// -- Render to texture (FB_W_SOF1 bit 24): draw the clip window at its own size, read it
// back and pack it into VRAM, where the texture cache sees the new hash and re-decodes.
static u32 s_stat_rtt_copies;

static inline u16 pack16(u32 argb, u32 mode, u16 kbit)
{
	u32 a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	switch (mode)
	{
	case 1:  return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	case 2:  return (u16)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
	case 3:  return (u16)(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	default: return (u16)(kbit | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	}
}

// The registers and the geometry of a render to texture: the first ones and each new set, 24 lines at most (P75).
static void rtt_log(const RttIn& in, const RttGeom* g)
{
	static u32 s_sig, s_lines;
	u32 sig = 0x9E3779B9u;
	const u32 w[] = { g_rr.fb_w_sof1, g_rr.fb_w_ctrl, g_rr.fb_w_linestride, in.fx0, in.fx1, in.fy0, in.fy1,
	                  in.tx0, in.ty0, in.tx1, in.ty1, in.scaler_ctl, (u32)in.by_tiles, (u32)in.scaler, (u32)in.fly };
	for (u32 k = 0; k < sizeof(w) / sizeof(w[0]); k++)
		sig = (sig ^ w[k]) * 0x01000193u;
	if (sig == s_sig || s_lines >= 24)
		return;
	s_sig = sig;
	s_lines++;
	printf("[rtt] sof1=%08X ctrl=%08X stride=%u fbclip=%u..%u,%u..%u glob=%ux%u tiles=%u..%u,%u..%u%s scaler=%05X"
	       " | rows size=%d scaler=%d pack=%d readback=%d retry=%d\n",
	       g_rr.fb_w_sof1, g_rr.fb_w_ctrl, (g_rr.fb_w_linestride & 0x1FF) * 8, in.fx0, in.fx1, in.fy0, in.fy1,
	       g_rr.glob_clip_x, g_rr.glob_clip_y, in.tx0, in.tx1, in.ty0, in.ty1, g_rr.reg_ok ? "" : " (not read)",
	       in.scaler_ctl & 0xFFFFF, (int)in.by_tiles, (int)in.scaler, (int)in.fly, g_rtt_readback_preset,
	       g_draw_retry_preset);
	if (g)
		printf("[rtt] area=%g,%g %gx%g target=%ux%u copy=%u..%u,%u..%u at texel %u,%u stride=%u scissor=%d\n",
		       (double)g->sx0, (double)g->sy0, (double)g->sw, (double)g->sh, g->tw, g->th, g->wx0, g->wx1, g->wy0, g->wy1,
		       g->ox, g->oy, g->stride, (int)g->scissor);
	else
		printf("[rtt] not drawn: an empty clip, or a target over %ux%u\n", in.max_w, in.max_h);
}

// One 16-bit texel into the 64-bit texture area: a native word holds two, the left one in its low half.
static inline void rtt_w16(u32 addr, u16 v)
{
	u32* const w = (u32*)&PVRPARAMS.vram[addr & VRAM_MASK & ~3u];
	*w = (addr & 2) ? (*w & 0x0000FFFFu) | ((u32)v << 16) : (*w & 0xFFFF0000u) | v;
}

static void rtt_render()
{
	u32 sw, sh;
	ps3_video_size(&sw, &sh);
	// The rows of the OUTPUT page (all live): RTT Size, RTT Scaler, RTT Pack.
	const bool fly = g_rtt_pack_preset != 0;
	const u32 packmode = g_rr.fb_w_ctrl & 7;
	RttIn in;
	in.fx0 = g_rr.fb_x_clip_min;
	in.fx1 = g_rr.fb_x_clip_max;
	in.fy0 = g_rr.fb_y_clip_min;
	in.fy1 = g_rr.fb_y_clip_max;
	in.tx0 = g_rr.reg_x0;
	in.ty0 = g_rr.reg_y0;
	in.tx1 = g_rr.reg_x1;
	in.ty1 = g_rr.reg_y1;
	in.scaler_ctl = g_rr.scaler_ctl;
	in.stride = (g_rr.fb_w_linestride & 0x1FF) * 8;
	in.max_w = sw < 1024 ? sw : 1024;
	in.max_h = sh < 1024 ? sh : 1024;
	// The old copy's 32-bit modes 6 and 7 keep the old area: the new copy writes 16-bit texels only.
	in.by_tiles = g_rtt_size_preset != 0 && g_rr.reg_ok && (fly || packmode <= 3);
	in.scaler = g_rtt_scaler_preset != 0;
	in.fly = fly;
	RttGeom g;
	if (!rtt_geom(&in, &g))
	{
		rtt_log(in, 0);
		return;
	}
	rtt_log(in, &g);
	const u32 w = g.tw, h = g.th;

	// P76: no reference renders to texture in modes 4-7 (Flycast gltex.cpp BindRTT returns).
	if (fly ? packmode >= 4 : (packmode == 4 || packmode == 5))
	{
		static u32 s_warned;
		if (s_warned++ < 3)
			printf("[ps3rend] RTT pack mode %u not handled\n", packmode);
		return;
	}

	// P60: with the Present Rule a frame can wait in the back buffer, so the RTT draws on its own surface.
	// P76 Draw Retry: else it waits for a free buffer; a return here leaves the game's texture with old data.
	if (!(g_present_rule_preset && ps3_video_begin_rtt()) && !ps3_video_begin_frame())
	{
		if (!g_draw_retry_preset)
			return;
		s_stat_rtt_wait++;
		if (!ps3_video_wait_free(250) || !ps3_video_begin_frame())
			return;
	}
	ps3tex_frame_begin(s_rl.param, s_rl.nstrip ? s_rl.nparam : 0);
	reset_gl_state();
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, w, h);
	ps3gl_apply_states();
	// P76 (Flycast gles.cpp): an RTT target clears to alpha 0; it shows in the 4444 and 1555 packs.
	glClearColor(0, 0, 0, fly ? 0 : 1);
	depth_begin();
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	clip_view(0, 0, (int)w, (int)h, false);
	// Flycast getBaseScissor: with the size from the tiles, the scene draws only inside them.
	if (g.scissor && clip_view_cut(g.sx0, g.sy0, g.sw, g.sh, g.cx0, g.cy0, g.cx1, g.cy1))
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(s_cv.bx, s_cv.by, s_cv.bw, s_cv.bh);
		ps3gl_apply_states();
	}
	draw_scene(g.sx0, g.sy0, g.sw, g.sh);
	ps3_video_end_frame_noflip();

	// P75: the row's value is in the log before the call, because OLD never comes back from it.
	static u32 s_rb_logged;
	const bool say = s_rb_logged < 8;
	if (say)
		printf("[rtt] read-back %ux%u, RTT Read-back %s\n", w, h, g_rtt_readback_preset ? "FIXED" : "OLD");
	const double t_rb = os_GetSeconds();
	const u32* px = ps3_video_readback(0, 0, w, h, g_rtt_readback_preset);
	if (say)
	{
		s_rb_logged++;
		printf("[rtt] read-back %s in %.1f ms\n", px ? "done" : "FAILED", (os_GetSeconds() - t_rb) * 1000.0);
	}
	if (!px)
		return;

	// Bit 24 addresses the linear texture area, so no bank conversion here.
	u32 base = g_rr.fb_w_sof1 & VRAM_MASK & ~3u;
	u16 kbit = (u16)(g_rr.fb_w_ctrl & 0x8000);

	if (!fly && !in.by_tiles)
	{
		// The copy of before bundle 11: every row from the texture's start, pixels in pairs, channels cut.
		u32 stride = in.stride;
		if (stride == 0)
			stride = (packmode >= 6) ? w * 4 : w * 2;
		for (u32 y = 0; y < h; y++)
		{
			const u32* srow = px + (size_t)y * w;
			u32 line = base + y * stride;
			if (packmode <= 3)
			{
				for (u32 x = 0; x < w; x += 2)
				{
					u16 p0 = pack16(srow[x], packmode, kbit);
					u16 p1 = (x + 1 < w) ? pack16(srow[x + 1], packmode, kbit) : 0;
					*(u32*)&PVRPARAMS.vram[(line + x * 2) & VRAM_MASK & ~3u] = ((u32)p1 << 16) | p0;
				}
			}
			else
			{
				for (u32 x = 0; x < w; x++)
				{
					u32 c = srow[x];
					if (packmode == 6)
						c &= 0x00FFFFFF; // KRGB0888: the K bit stays clear
					*(u32*)&PVRPARAMS.vram[(line + x * 4) & VRAM_MASK & ~3u] = c;
				}
			}
		}
	}
	else
	{
		// Flycast writeTexture: each pixel of the copy window goes to its own texel, one 16-bit write each.
		for (u32 y = g.wy0; y < g.wy1; y++)
		{
			const u32* srow = px + (size_t)y * w;
			const u32 line = base + (y + g.oy) * g.stride + g.ox * 2;
			for (u32 x = g.wx0; x < g.wx1; x++)
				rtt_w16(line + x * 2, fly ? rtt_pack16_fly(srow[x], g_rr.fb_w_ctrl) : pack16(srow[x], packmode, kbit));
		}
	}
	s_stat_rtt_copies++;
}

// -- Framebuffer writes. 2D titles draw straight into VRAM with no render; a mark is
// stamped after each render, and if it is gone at vblank another writer refreshed it, so show it.
#define FB_PRESENT_MAGIC 0xDC4F0FB0u
#define FB2D_W 768
#define FB2D_H 576
static u32 s_fb_pix[FB2D_W * FB2D_H];
static GLuint s_fb_tex;
static u32 s_stat_fb_presents;

static void fb_stamp_magic()
{
	vwi(s_snap.fb_w_sof1, FB_PRESENT_MAGIC);
	vwi(s_snap.fb_w_sof2, FB_PRESENT_MAGIC);
}

static bool fb_needs_present()
{
	return vri(FB_R_SOF1 & ~3u) != FB_PRESENT_MAGIC;
}

// TR3: the VRAM read stays on the SH4 thread, so the pixels are the ones the
// guest had written at this vblank; only the quad goes to the render thread.
static u32 s_fb_cpy_w, s_fb_cpy_h;

// 5.12 #5: 3 RSX textures the SPU converts frames into; the RSX may still draw the last 2 frames (ps3gl's vring fence
// covers frame N-2). s_fb_buf is this present's texture, -1 for s_fb_pix.
#define FB_SPU_BUFS 3
static GLuint s_fb_rtex[FB_SPU_BUFS];
static u8* s_fb_rmem[FB_SPU_BUFS];
static bool s_fb_spu_ok;
static u32 s_fb_rnext;
static int s_fb_buf = -1;

// spepix_check: begin fb
static inline u32 e5(u32 v) { return (v << 3) | (v >> 2); }
static inline u32 e6(u32 v) { return (v << 2) | (v >> 4); }

struct FbGeom
{
	u32 sof1, depth, cpy_w, cpy_h, line_bytes;
};

// The framebuffer of the snapped registers; false when there is none or its format is not handled.
static bool fb_geom(FbGeom* fg)
{
	u32 fb_x_size = s_snap.fb_r_size & 0x3FF;
	u32 fb_y_size = (s_snap.fb_r_size >> 10) & 0x3FF;
	u32 fb_modulus = (s_snap.fb_r_size >> 20) & 0x3FF;
	u32 fb_depth = s_snap.fb_r_depth;
	if (fb_x_size == 0 || fb_y_size == 0)
		return false;
	if (fb_depth == 2)
	{
		static u32 s_warned;
		if (s_warned++ < 3)
			printf("[ps3rend] 24-bit framebuffer read not handled\n");
		return false;
	}

	// fb_x_size counts 32-bit words per line, so a 16-bit line holds twice as many pixels.
	bool wide = (fb_depth == 3);
	u32 src_w = wide ? (fb_x_size + 1) : ((fb_x_size + 1) << 1);
	u32 src_h = fb_y_size + 1;
	u32 mod_px = (fb_modulus > 0 ? fb_modulus - 1 : 0) << (wide ? 0 : 1);
	u32 bpp = wide ? 4 : 2;
	fg->sof1 = s_snap.fb_r_sof1;
	fg->depth = fb_depth;
	fg->cpy_w = src_w > FB2D_W ? FB2D_W : src_w;
	fg->cpy_h = src_h > FB2D_H ? FB2D_H : src_h;
	fg->line_bytes = (src_w + mod_px) * bpp;
	return true;
}

// The PPU convert, into s_fb_pix as RGBA.
static void fb_convert_ppu(const FbGeom& fg)
{
	const bool wide = fg.depth == 3;
	const u32 bpp = wide ? 4 : 2;
	u32 addr = fg.sof1;
	for (u32 y = 0; y < fg.cpy_h; y++)
	{
		u32 line = addr;
		u32* out = &s_fb_pix[(size_t)y * fg.cpy_w];
		for (u32 x = 0; x < fg.cpy_w; x++, line += bpp)
		{
			u32 r, g, b;
			if (wide)
			{
				u32 c = vri(line & ~3u);
				r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF;
			}
			else
			{
				u32 c = vri16(line);
				if (fg.depth == 1) { r = e5(c >> 11); g = e6((c >> 5) & 0x3F); b = e5(c & 0x1F); }
				else               { r = e5((c >> 10) & 0x1F); g = e5((c >> 5) & 0x1F); b = e5(c & 0x1F); }
			}
			out[x] = (r << 24) | (g << 16) | (b << 8) | 0xFF;
		}
		addr += fg.line_bytes;
	}
}

// The SPU job for the same frame, into rows of FB2D_W words at dst.
static void fb_job(const FbGeom& fg, u32 dst, SpePixFb* j)
{
	j->sof1 = fg.sof1;
	j->depth = fg.depth;
	j->w = fg.cpy_w;
	j->h = fg.cpy_h;
	j->line_bytes = fg.line_bytes;
	j->dst_ea = dst;
	j->dst_pitch = FB2D_W * 4;
}
// spepix_check: end fb

// 5.12 #5 CHECK: the SPU's copy (A8R8G8B8 rows of FB2D_W words) against s_fb_pix (RGBA).
static void fb_check(const FbGeom& fg)
{
	u32* const c = ps3spepix_fb_check_buf();
	if (!c)
		return;
	SpePixFb j;
	fb_job(fg, (u32)(uintptr_t)c, &j);
	if (!ps3spepix_fb(&j))
		return;
	for (u32 y = 0; y < fg.cpy_h; y++)
		for (u32 x = 0; x < fg.cpy_w; x++)
		{
			const u32 p = s_fb_pix[y * fg.cpy_w + x], want = (p >> 8) | (p << 24);
			if (c[y * FB2D_W + x] != want)
			{
				char what[128];
				snprintf(what, sizeof(what), "depth %u %ux%u sof1 %06X line %u: pixel %u of row %u spu/ppu %08X/%08X",
				         fg.depth, fg.cpy_w, fg.cpy_h, fg.sof1, fg.line_bytes, x, y, c[y * FB2D_W + x], want);
				ps3spepix_fb_check(false, what);
				return;
			}
		}
	ps3spepix_fb_check(true, 0);
}

// P26 FB Reads: the framebuffer as Flycast's ReadFramebuffer takes it (ps3_rtt.h), 24-bit included.
static bool fb_geom_reads(FbGeom* fg)
{
	FbReadIn in;
	in.size = s_snap.fb_r_size;
	in.depth = s_snap.fb_r_depth;
	in.sof1 = s_snap.fb_r_sof1;
	in.sof2 = s_snap.fb_r_sof2;
	in.interlace = s_snap.fb_interlace;
	in.field2 = s_snap.fb_field2;
	in.vclk = s_snap.fb_vclk;
	in.max_w = FB2D_W;
	in.max_h = FB2D_H;
	FbReadGeom g;
	if (!fb_read_geom(&in, &g))
		return false;
	fg->sof1 = g.sof;
	fg->depth = in.depth;
	fg->cpy_w = g.w;
	fg->cpy_h = g.h;
	fg->line_bytes = g.line_bytes;
	return true;
}

// A 24-bit frame into s_fb_pix as RGBA: 3 bytes per pixel, so a pixel can lie over two VRAM words.
static void fb_convert_888(const FbGeom& fg)
{
	for (u32 y = 0; y < fg.cpy_h; y++)
	{
		u32 a = fg.sof1 + y * fg.line_bytes;
		u32* out = &s_fb_pix[(size_t)y * fg.cpy_w];
		for (u32 x = 0; x < fg.cpy_w; x++, a += 3)
			out[x] = (fb_px888(vri(a & ~3u), vri((a & ~3u) + 4), a) << 8) | 0xFF;
	}
}

static bool present_fb_convert()
{
	FbGeom fg;
	if (!(g_fb_reads_preset ? fb_geom_reads(&fg) : fb_geom(&fg)))
		return false;
	s_fb_cpy_w = fg.cpy_w;
	s_fb_cpy_h = fg.cpy_h;
	s_fb_buf = -1;
	// The SPU convert has no 24-bit pixels.
	if (fg.depth == 2)
	{
		fb_convert_888(fg);
		return true;
	}
	const int mode = ps3spepix_fb_mode();
	if (mode == SPEPIX_ON && s_fb_spu_ok)
	{
		SpePixFb j;
		fb_job(fg, (u32)(uintptr_t)s_fb_rmem[s_fb_rnext], &j);
		if (ps3spepix_fb(&j))
		{
			s_fb_buf = (int)s_fb_rnext;
			s_fb_rnext = (s_fb_rnext + 1) % FB_SPU_BUFS;
			return true;
		}
	}
	fb_convert_ppu(fg);
	if (mode == SPEPIX_CHECK)
		fb_check(fg);
	return true;
}

// 5.12 #5: the 3 framebuffer textures, before the render thread starts (ps3gl is not thread-safe).
static void fb_spu_alloc()
{
	if (!ps3spepix_fb_preset() || s_fb_spu_ok)
		return;
	u32 i = 0;
	for (; i < FB_SPU_BUFS; i++)
	{
		u32 pitch;
		glGenTextures(1, &s_fb_rtex[i]);
		glBindTexture(GL_TEXTURE_2D, s_fb_rtex[i]);
		s_fb_rmem[i] = ps3gl_tex_fixed(FB2D_W, FB2D_H, FB2D_W, FB2D_H, &pitch);
		if (!s_fb_rmem[i] || pitch != FB2D_W * 4)
			break;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	s_fb_spu_ok = i == FB_SPU_BUFS;
	if (!s_fb_spu_ok)
		printf("[spepix] framebuffer texture %u alloc failed; the PPU converts\n", i);
}

static void present_fb_draw()
{
	u32 cpy_w = s_fb_cpy_w, cpy_h = s_fb_cpy_h;
	if (!ps3_video_begin_frame())
	{
		// P76 Draw Retry: the vblank has marked this screen as shown; it must try again.
		if (g_draw_retry_preset)
			s_fb_retry = true;
		return;
	}

	u32 sw, sh;
	ps3_video_size(&sw, &sh);
	// Bundle 12 Screen Format: STRETCH (P69) also puts a 2D screen on every column. WIDE has no more picture
	// to show there, so it stays 4:3.
	ScreenView sv;
	screen_view(sw, sh, g_screen_format_preset == SCREEN_STRETCH ? SCREEN_STRETCH : SCREEN_43, 640.0f, &sv);
	const u32 vp_x = (u32)sv.x, vp_w = (u32)sv.w;
	s_pic_x = sv.x;
	s_pic_w = sv.w;

	reset_gl_state();
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, sw, sh);
	ps3gl_apply_states();
	glClearColor(0, 0, 0, 1);
	glClearDepth(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glViewport(vp_x, 0, vp_w, sh);
	ps3gl_apply_states();

	// P76 (Flycast RenderFramebuffer): with the video blanked the screen is the border colour, not the VRAM image.
	if (g_rr.blank)
	{
		const u32 c = g_rr.vo_border_col;
		glEnable(GL_SCISSOR_TEST);
		glScissor(vp_x, 0, vp_w, sh);
		ps3gl_apply_states();
		glClearColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
		frame_extras();
		ps3_video_end_frame();
		reset_param_cache();
		s_stat_fb_presents++;
		s_stat_blank++;
		return;
	}

	glEnable(GL_TEXTURE_2D);
	if (s_fb_buf >= 0)
	{
		// The SPU already wrote the texels; only the size the RSX samples changes.
		u32 pitch;
		glBindTexture(GL_TEXTURE_2D, s_fb_rtex[s_fb_buf]);
		ps3gl_tex_fixed(FB2D_W, FB2D_H, cpy_w, cpy_h, &pitch);
	}
	else
	{
		if (!s_fb_tex)
			glGenTextures(1, &s_fb_tex);
		glBindTexture(GL_TEXTURE_2D, s_fb_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cpy_w, cpy_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_fb_pix);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	// Straight to clip space: the quad covers the whole viewport.
	float m[16] = { 0 };
	m[0] = m[5] = m[10] = m[15] = 1.0f;
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glColor4ub(255, 255, 255, 255);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(0, 0); glVertex3f(-1.0f,  1.0f, 0.0f);
	glTexCoord2f(1, 0); glVertex3f( 1.0f,  1.0f, 0.0f);
	glTexCoord2f(0, 1); glVertex3f(-1.0f, -1.0f, 0.0f);
	glTexCoord2f(1, 1); glVertex3f( 1.0f, -1.0f, 0.0f);
	glEnd();

	glDisable(GL_TEXTURE_2D);
	glDepthMask(GL_TRUE);
	frame_extras();
	ps3_video_end_frame();
	reset_param_cache();
	s_stat_fb_presents++;
}

// -- TR3: the render thread --------------------------------------------------

// rttq_check: begin mark
// Publishes "the replay is done", so the SH4 may touch this context's raw stream again.
static void rth_mark_decoded()
{
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	s_job_decoded = true;
	sysCondBroadcast(s_done_cv);
	sysMutexUnlock(s_rmx);
}
// rttq_check: end mark

static void rth_print_stats()
{
	double now = os_GetSeconds();
	if (now - s_stat_t0 < STAT_WINDOW_S)
		return;
	raw_note_peak(s_ta);   // the stream still being stored
	u32 r = s_stat_renders ? s_stat_renders : 1;
	u32 decodes, live;
	ps3tex_stats(&decodes, &live);
	const double win = (now - s_stat_t0) / 100.0;
	printf("[rend] busy=%.0f%% (dec %.0f draw %.0f end %.0f)\n",
	       (s_stat_t_dec + s_stat_t_draw + s_stat_t_end) / win,
	       s_stat_t_dec / win, s_stat_t_draw / win, s_stat_t_end / win);
	{
		double us[DT_COUNT];
		double sum = 0.0;
		for (int k = 0; k < DT_COUNT; k++)
		{
			us[k] = PERF_TICKS_US(s_dt[k]) / 1e6 / win;
			s_dt[k] = 0;
		}
		sum = us[DT_PARAM] + us[DT_GLEND] + us[DT_SORT] + us[DT_MODVOL];
		// param includes tex; "vtx" is the rest of draw: vertex emit, strip merge, list walks, frame setup.
		printf("[rend] draw split %%: param %.1f (tex %.1f) glEnd %.1f sort %.1f modvol %.1f vtx+other %.1f\n",
		       us[DT_PARAM], us[DT_TEX], us[DT_GLEND], us[DT_SORT], us[DT_MODVOL],
		       s_stat_t_draw / win - sum);
		u64 t_hash, t_decode, t_gl, hash_bytes;
		u32 hashes;
		ps3tex_split(&t_hash, &t_decode, &t_gl, &hashes, &hash_bytes);
		double th = PERF_TICKS_US(t_hash) / 1e6 / win;
		double td = PERF_TICKS_US(t_decode) / 1e6 / win;
		double tg = PERF_TICKS_US(t_gl) / 1e6 / win;
		// "other" is the cache probe and describe(); hash/render is textures checked per frame.
		printf("[rend] tex split %%: hash %.1f decode %.1f gl %.1f other %.1f | hash/render n=%u KB=%u\n",
		       th, td, tg, us[DT_TEX] - th - td - tg, hashes / r, (u32)(hash_bytes / r / 1024));
	}
	s_stat_t_dec = s_stat_t_draw = s_stat_t_end = 0.0;
	printf("[rend] renders=%u drop=%u wait=%ums fbdef=%u rtt=%u/%u fb=%u fbw=%u sort=%s store=%s job_max=%ums avg/render: vtx=%u strips op=%u pt=%u tr=%u draws=%s op=%u pt=%u tr=%u (built op=%u pt=%u tr=%u) mv_tris op=%u tr=%u | w=[%g..%g] near=%g wclamp=%u/%u bg=%g zw=%u | tex decodes=%u live=%u | overflow=%u ta_raw_of=%u peak=%uK/%uK | bump strips=%u ppu=%u\n",
	       s_stat_renders, s_stat_dropped, (u32)(s_stat_wait * 1000.0), s_stat_fb_defer,
	       s_stat_rtt_copies, s_stat_rtt, s_stat_fb_presents, s_stat_fb_watch,
	       g_rr.autosort ? "auto" : "pre", s_ta_fast ? "fsm" : "split",
	       (u32)(s_stat_job_max * 1000.0), s_stat_vtx / r,
	       s_stat_strips[ListType_Opaque] / r, s_stat_strips[ListType_Punch_Through] / r,
	       s_stat_strips[ListType_Translucent] / r,
	       s_merge ? "merge" : "strip",
	       s_stat_draws[ListType_Opaque] / r, s_stat_draws[ListType_Punch_Through] / r,
	       s_stat_draws[ListType_Translucent] / r,
	       s_stat_built[ListType_Opaque] / r, s_stat_built[ListType_Punch_Through] / r,
	       s_stat_built[ListType_Translucent] / r,
	       s_stat_mv[1] / r, s_stat_mv[3] / r,
	       (double)s_stat_w_lo, (double)s_stat_w_hi, (double)s_stat_near_lo,
	       s_stat_wclamp, s_stat_wtotal, (double)s_stat_bg_inv_w, s_stat_bg_zw,
	       decodes, live, s_overflow,
	       s_stat_raw_of, s_stat_raw_peak / 1024, (u32)(TA_RAW_SIZE / 1024), s_stat_bump_strips / r, s_stat_bump_ppu);
	s_stat_bump_strips = s_stat_bump_ppu = 0;
	// Bundle 4: TA_LIST_CONT writes, renders by passes, renders sent to the PPU replay for their passes, renders with
	// the TR volume list, TA records the gate dropped, bad records skipped; 3k: sorted triangles per render.
	printf("[rend4] cont=%u multi=%u pass_ppu=%u trvol=%u gate_drop=%u bad_rec=%u opt=%02X tris=%u\n",
	       s_stat_cont, s_stat_multi, s_stat_pass_ppu, s_stat_trvol, s_stat_gate, g_ta_bad_records, s_rl.opt,
	       s_stat_tris / r);
	s_stat_cont = s_stat_multi = s_stat_pass_ppu = s_stat_trvol = s_stat_gate = g_ta_bad_records = s_stat_tris = 0;
	// Bundle 14: the rows; + OPAQUE DEPTH's first-pass runs per render and those that write no depth; the last
	// render's state changes per list, by the merge key > by the bits the draw reads (P73's counter).
	printf("[cost] bind=%d compare=%d first_pass=%d | fp_runs=%u nodepth=%u | key op=%u>%u pt=%u>%u tr=%u>%u\n",
	       g_tex_bind_table_preset, g_gl_compare_preset, g_tr_first_pass_preset, s_stat_fp_runs / r,
	       s_stat_fp_nodepth / r, s_diag[0].runs, s_diag[0].runs_read, s_diag[1].runs, s_diag[1].runs_read,
	       s_diag[2].runs, s_diag[2].runs_read);
	s_stat_fp_runs = s_stat_fp_nodepth = 0;
	// Bundle 17: the rows; the last lists' options; plane corners of the PPU replay and of the SPU decoder; states
	// applied with an offset colour, a clamp and a weight; the last render's FOG_CLAMP_MIN and MAX.
	{
		static u32 s_spu_planes;
		const u32 spu_planes = ps3spe_spr_planes();
		printf("[rend17] pt_blend=%d corner=%d offs=%d clamp=%d tril=%d | opt=%02X lists_offs=%d planes ppu=%u spu=%u | "
		       "applied offs=%u clamp=%u tril=%u | fog_clamp min=%08X max=%08X\n",
		       g_pt_blend_preset, g_sprite_corner_preset, g_offset_colour_preset, g_colour_clamp_preset,
		       g_trilinear_preset, s_rl.opt, (int)s_do.offs, s_stat_spr_plane, spu_planes - s_spu_planes,
		       s_stat_offc, s_stat_cclamp, s_stat_tril, g_rr.fog_clamp_min, g_rr.fog_clamp_max);
		s_spu_planes = spu_planes;
		s_stat_spr_plane = s_stat_offc = s_stat_cclamp = s_stat_tril = 0;
	}
	// 3a: the Fog level, renders sent to the PPU replay for their vertex fog, table uploads, the last render's registers.
	if (s_fog)
		printf("[fog] level=%u ppu=%u uploads=%u col_ram=%06X col_vert=%06X density=%04X table=%04X %04X .. %04X %04X\n",
		       s_fog, s_stat_fog_ppu, s_stat_fog_up, g_rr.fog_col_ram & 0xFFFFFF, g_rr.fog_col_vert & 0xFFFFFF,
		       g_rr.fog_density & 0xFFFF, g_rr.fog_table[0], g_rr.fog_table[1], g_rr.fog_table[126], g_rr.fog_table[127]);
	s_stat_fog_ppu = s_stat_fog_up = 0;
	// Bundle 11: RTT waits for a buffer; renders that waited for the game, those it showed, those drawn with no
	// colour clear and with a cut scissor; 2D presents tried again and blanked; cut renders over the last frame.
	if (g_present_rule_preset || g_region_scissor_preset || g_draw_retry_preset || g_blank_video_preset ||
	    s_stat_held || s_stat_shown)
		printf("[out] rtt_wait=%u held=%u shown=%u keep=%u scissor=%u retry=%u blank=%u carry=%u\n",
		       s_stat_rtt_wait, s_stat_held, s_stat_shown, s_stat_keep, s_stat_scis, s_stat_retry, s_stat_blank,
		       s_stat_carry);
	s_stat_rtt_wait = s_stat_held = s_stat_shown = s_stat_keep = s_stat_scis = s_stat_retry = s_stat_blank = 0;
	s_stat_carry = 0;
	// P95: the rows; RTTs that waited behind a job and that went at once; the SH4's time in RTT posts; list inits
	// moved to a free context, and those that found none and waited.
	if (g_rtt_async_preset || g_ta_ctx_split_preset || s_stat_rtt)
		printf("[rttq] async=%d split=%d | rtt queued=%u direct=%u sh4 wait %u ms/s | ctx moves=%u full=%u\n",
		       g_rtt_async_preset, g_ta_ctx_split_preset, s_stat_rtt_q, s_stat_rtt_direct,
		       (u32)(s_stat_rtt_sh4 * 1000.0 / (now - s_stat_t0)), s_stat_ctx_moves, s_stat_ctx_full);
	s_stat_rtt_q = s_stat_rtt_direct = s_stat_ctx_moves = s_stat_ctx_full = 0;
	s_stat_rtt_sh4 = 0.0;
	// P87 JIT TA SLOT: flushes that found their record in place, those that took the copy, records compiled since boot.
	if (g_jit_ta_slot_preset)
		printf("[taslot] hit=%u copy=%u records=%u\n", s_stat_slot_hit, s_stat_slot_copy, g_jit_ts_groups);
	s_stat_slot_hit = s_stat_slot_copy = 0;
	// Row Render Low Priority: the windows from a change on run with the new value.
	static int s_low_prio_logged = -1;
	if (g_rend_low_prio_preset != s_low_prio_logged)
	{
		printf("[rprio] Render Low Priority %s\n", g_rend_low_prio_preset ? "ON" : "OFF");
		s_low_prio_logged = g_rend_low_prio_preset;
	}
	// P98 TA Line Zero: the TA buffer lines zeroed ahead of the SH4's writes.
	if (g_ta_line_zero_preset || s_stat_ta_zero)
		printf("[tazero] TA Line Zero %s, lines zeroed %u/s\n", g_ta_line_zero_preset ? "ON" : "OFF",
		       (u32)(s_stat_ta_zero / (now - s_stat_t0)));
	s_stat_ta_zero = 0;
	// Bundle 12: the rows; renders drawn wide and WIDE renders kept at 4:3 by their clip; the last render's canvas.
	if (g_screen_format_preset || g_screen_scaler_preset || g_fps_overlay_preset || s_stat_shots)
		printf("[scr] format=%d scaler=%d fps=%d | wide=%u narrow=%u canvas=%gx%g shots=%u\n", g_screen_format_preset,
		       g_screen_scaler_preset, g_fps_overlay_preset, s_stat_wide, s_stat_narrow,
		       (double)(640.0f * g_rr.fb_scale_x), (double)(480.0f * g_rr.fb_scale_y), s_stat_shots);
	s_stat_wide = s_stat_narrow = s_stat_shots = 0;
	print_diag();
	ps3spe_stats(now - s_stat_t0);
	ps3spevb_stats(now - s_stat_t0);
	ps3spetex_stats(now - s_stat_t0);
	ps3spepix_stats(now - s_stat_t0);
	// A job far longer than a frame is the stall TArender.MD TR3 asks us to watch for.
	if (s_stat_job_max > 0.25)
		printf("[ps3rend] render job stalled for %u ms\n", (u32)(s_stat_job_max * 1000.0));
	memset(s_stat_mv, 0, sizeof(s_stat_mv));
	s_stat_renders = s_stat_rtt = s_stat_rtt_copies = s_stat_fb_presents = s_stat_fb_watch = s_stat_vtx = 0;
	memset(s_stat_strips, 0, sizeof(s_stat_strips));
	memset(s_stat_draws, 0, sizeof(s_stat_draws));
	memset(s_stat_built, 0, sizeof(s_stat_built));
	s_overflow = 0;
	s_stat_raw_of = 0;
	s_stat_raw_peak = 0;
	s_stat_dropped = 0;
	s_stat_fb_defer = 0;
	s_stat_wait = 0.0;
	s_stat_job_max = 0.0;
	s_stat_w_lo = s_stat_near_lo = 1e30f;
	s_stat_w_hi = 0.0f;
	s_stat_wclamp = s_stat_wtotal = 0;
	s_stat_t0 = now;
}

static u32 fbits(float f)
{
	u32 u;
	memcpy(&u, &f, 4);
	return u;
}

// The first 8 mismatches, as spu/ppu word pairs.
static void spe_report(u32 fence, u32 ctx, const char* what, u32 idx, const u32* spu, const u32* ppu, u32 words)
{
	static u32 s_logged;
	if (s_logged++ >= 8)
		return;
	char line[256];
	int n = snprintf(line, sizeof(line), "[spe] fence %u MISMATCH ctx=%u %s[%u] spu/ppu:", fence, ctx, what, idx);
	for (u32 k = 0; k < words && n > 0 && n < (int)sizeof(line); k++)
		n += snprintf(line + n, sizeof(line) - n, " %08X/%08X", spu[k], ppu[k]);
	printf("%s\n", line);
}

// 5.12 S2: the SPU lists for this fence must equal the replay's, bit for bit.
static void spe_compare(TaContext* c, u32 fence, u32 raw_end, u32 cont_end)
{
	const u64 t0 = gettime();
	const SpeFence* r = ps3spe_wait_fence(fence);
	if (!r)
		return;
	const u32 ctx = (u32)(c - s_ctx);
	const SpeOut* o = ps3spe_out(ctx);
	const u32 hs[] = { r->id, r->ctx, r->pos, r->ncont, r->nvtx, r->nstrip, r->nparam, r->nmtri, r->nmvol,
	                   r->max_inv_w, r->min_w, r->max_w,
	                   r->mv_tris[0], r->mv_tris[1], r->mv_tris[2], r->mv_tris[3], r->mv_tris[4], r->opt };
	const u32 hp[] = { fence, ctx, raw_end, cont_end, c->nvtx, c->nstrip, c->nparam, c->nmtri, c->nmvol,
	                   fbits(c->max_inv_w), fbits(c->min_w), fbits(c->max_w),
	                   c->mv_tris[0], c->mv_tris[1], c->mv_tris[2], c->mv_tris[3], c->mv_tris[4], c->opt };
	static const char* const hn[] = { "id", "ctx", "pos", "ncont", "nvtx", "nstrip", "nparam", "nmtri", "nmvol",
	                                  "max_inv_w", "min_w", "max_w", "mv0", "mv1", "mv2", "mv3", "mv4", "opt" };
	bool ok = true;
	for (u32 k = 0; k < sizeof(hs) / sizeof(hs[0]) && ok; k++)
		if (hs[k] != hp[k])
		{
			spe_report(fence, ctx, hn[k], 0, &hs[k], &hp[k], 1);
			ok = false;
		}
	if (ok && memcmp(o->vtx, c->vtx, c->nvtx * sizeof(DcVertex)) != 0)
		for (u32 i = 0; i < c->nvtx && ok; i++)
			if (memcmp(&o->vtx[i], &c->vtx[i], sizeof(DcVertex)) != 0)
			{
				spe_report(fence, ctx, "vtx", i, (const u32*)&o->vtx[i], (const u32*)&c->vtx[i], 6);
				ok = false;
			}
	// 3b: with TAOPT_OFFS both decoders keep one offset colour per vertex.
	if (ok && (c->opt & TAOPT_OFFS) && memcmp(o->offs, c->offs, c->nvtx * sizeof(u32)) != 0)
		for (u32 i = 0; i < c->nvtx && ok; i++)
			if (o->offs[i] != c->offs[i])
			{
				spe_report(fence, ctx, "offs", i, &o->offs[i], &c->offs[i], 1);
				ok = false;
			}
	for (u32 i = 0; i < c->nstrip && ok; i++)
	{
		const SpeStrip& a = o->strip[i];
		const DcStrip& b = c->strip[i];
		if (a.first != b.first || a.count != b.count || a.list != b.list || a.param != b.param)
		{
			const u32 ws[] = { a.first, a.count, a.list, a.param }, wp[] = { b.first, b.count, b.list, b.param };
			spe_report(fence, ctx, "strip", i, ws, wp, 4);
			ok = false;
		}
	}
	if (ok && memcmp(o->param, c->param, c->nparam * sizeof(DcParam)) != 0)
		for (u32 i = 0; i < c->nparam && ok; i++)
			if (memcmp(&o->param[i], &c->param[i], sizeof(DcParam)) != 0)
			{
				spe_report(fence, ctx, "param", i, (const u32*)&o->param[i], (const u32*)&c->param[i], 4);
				ok = false;
			}
	if (ok && memcmp(o->mtri, c->mtri, c->nmtri * sizeof(ModTri)) != 0)
		for (u32 i = 0; i < c->nmtri && ok; i++)
			if (memcmp(&o->mtri[i], &c->mtri[i], sizeof(ModTri)) != 0)
			{
				spe_report(fence, ctx, "mtri", i, (const u32*)&o->mtri[i], (const u32*)&c->mtri[i], 9);
				ok = false;
			}
	for (u32 i = 0; i < c->nmvol && ok; i++)
	{
		const SpeModVol& a = o->mvol[i];
		const ModVolParam& b = c->mvol[i];
		if (a.first != b.first || a.cull != b.cull || a.depth != b.depth || a.volume_last != b.volume_last ||
		    a.list != b.list)
		{
			const u32 ws[] = { a.first, a.cull, a.depth, a.volume_last, a.list };
			const u32 wp[] = { b.first, b.cull, b.depth, b.volume_last, b.list };
			spe_report(fence, ctx, "mvol", i, ws, wp, 5);
			ok = false;
		}
	}
	ps3spe_result(ok, gettime() - t0);
}

static void lists_from_ctx(const TaContext* c)
{
	s_rl_spe = -1;
	s_rl.vtx = c->vtx;
	s_rl.strip = c->strip;
	s_rl.param = c->param;
	s_rl.mtri = c->mtri;
	s_rl.mvol = c->mvol;
	s_rl.offs = c->offs;
	s_rl.nvtx = c->nvtx;
	s_rl.nstrip = c->nstrip;
	s_rl.nparam = c->nparam;
	s_rl.nmtri = c->nmtri;
	s_rl.nmvol = c->nmvol;
	s_rl.min_w = c->min_w;
	s_rl.max_w = c->max_w;
	s_rl.max_inv_w = c->max_inv_w;
	memcpy(s_rl.mv_tris, c->mv_tris, sizeof(s_rl.mv_tris));
	s_rl.opt = c->opt;
	s_rl.npass = c->npass;
	s_rl.pass_strip = c->pass_strip;
	s_rl.pass_mvol = c->pass_mvol;
}

static float bitsf(u32 u)
{
	float f;
	memcpy(&f, &u, 4);
	return f;
}

// 5.12 S3: the SPU lists for this fence, if they cover the same bytes the replay would. The SPU only
// appends past these counts, and its RESET of the context waits for ps3spe_release after the draw.
static bool lists_from_spe(TaContext* c, u32 fence, u32 raw_end, u32 cont_end)
{
	const u64 t0 = gettime();
	const SpeFence* r = ps3spe_wait_fence(fence);
	if (!r)
		return false;
	const u32 ctx = (u32)(c - s_ctx);
	// The SPU decoded with the options its RESET carried: they must be the context's, or the replay takes over.
	if (r->id != fence || r->ctx != ctx || r->pos != raw_end || r->ncont != cont_end || r->opt != c->opt)
	{
		const u32 hs[] = { r->id, r->ctx, r->pos, r->ncont, r->opt }, hp[] = { fence, ctx, raw_end, cont_end, c->opt };
		spe_report(fence, ctx, "id/ctx/pos/ncont/opt", 0, hs, hp, 5);
		ps3spe_result(false, gettime() - t0);
		return false;
	}
	const SpeOut* o = ps3spe_out(ctx);
	s_rl_spe = (int)ctx;
	s_rl.vtx = (const DcVertex*)o->vtx;
	s_rl.strip = (const DcStrip*)o->strip;
	s_rl.param = (const DcParam*)o->param;
	s_rl.mtri = (const ModTri*)o->mtri;
	s_rl.mvol = (const ModVolParam*)o->mvol;
	// 3b: the SPU keeps the offset colours with TAOPT_OFFS; they are read only while the row is on.
	s_rl.offs = (r->opt & TAOPT_OFFS) && s_offc_row ? o->offs : 0;
	s_rl.nvtx = r->nvtx;
	s_rl.nstrip = r->nstrip;
	s_rl.nparam = r->nparam;
	s_rl.nmtri = r->nmtri;
	s_rl.nmvol = r->nmvol;
	s_rl.min_w = bitsf(r->min_w);
	s_rl.max_w = bitsf(r->max_w);
	s_rl.max_inv_w = bitsf(r->max_inv_w);
	memcpy(s_rl.mv_tris, r->mv_tris, sizeof(s_rl.mv_tris));
	s_rl.opt = r->opt;
	s_rl.npass = 0;
	s_rl.pass_strip = s_rl.pass_mvol = 0;
	ps3spe_result(true, gettime() - t0);
	return true;
}

// 3j: a scene with a bump polygon needs offset colours, which only the PPU replay keeps.
static bool has_bump(const DcParam* p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		if (p[i].pcw.Texture && p[i].tcw.NO_PAL.PixelFmt == 4)
			return true;
	return false;
}

// 3a: so does a scene with a vertex-fog polygon, when the lists hold no offset colours (3b).
static bool has_fogv(const DcParam* p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		if (fogv_param(p[i]))
			return true;
	return false;
}

// Stage 1 of a job: the TA replay. It is split from the draw so the inline path
// can still run it under the guest rounding mode, as TR2 did.
static void rth_job_decode(TaContext* c, u32 raw_end, u32 cont_end, u32 fence)
{
	REND_PHASE(PH_TADEC);
	double t0 = os_GetSeconds();
	s_bump_on = g_bump_map_preset != 0;
	s_fog = (u32)g_fog_preset;
	s_offc_row = g_offset_colour_preset != 0;
	// Bundle 4: this job's draw rules (ps3_presets.cpp, all live).
	s_do.pt_depth = g_pt_depth_preset != 0;
	s_do.tr_nodepth = g_tr_sort_depth_preset != 0;
	s_do.pt_blend = (u8)g_pt_blend_preset;
	s_do.clamp = g_colour_clamp_preset != 0;
	s_do.tril = g_trilinear_preset != 0;
	s_do.flat = (u8)g_flat_shade_preset;
	s_do.passes = (u8)g_list_passes_preset;
	s_do.tri_sort = (u8)g_tr_tri_sort_preset;
	s_do.first_pass = (u8)g_tr_first_pass_preset;
	s_mo_sprite = g_sprite_cull_preset != 0;
	// S3 skips the replay; a late or wrong fence replays from raw_decoded, which S3 never moves.
	bool spe = fence && g_spe_draw && lists_from_spe(c, fence, raw_end, cont_end);
	if (spe && s_bump_on && has_bump(s_rl.param, s_rl.nparam))
	{
		spe = false;
		s_stat_bump_ppu++;
	}
	if (spe && s_fog >= 2 && !s_rl.offs && has_fogv(s_rl.param, s_rl.nparam))
	{
		spe = false;
		s_stat_fog_ppu++;
	}
	// P10 List Passes: only the PPU replay marks where each TA_LIST_CONT fell in the lists.
	if (spe && s_do.passes && cont_end != 0)
	{
		spe = false;
		s_stat_pass_ppu++;
	}
	if (!spe)
	{
		decode_pending(c, raw_end, cont_end);
		lists_from_ctx(c);
	}
	// What the lists carry decides the rest: the clip rects in the params (and the merge key), the TR volumes, and
	// (3b) every vertex's offset colour.
	s_do.offs = s_offc_row && (s_rl.opt & TAOPT_OFFS) != 0;
	const bool rects = (s_rl.opt & TAOPT_TILE_CLIP) != 0;
	s_do.clip = rects ? (u8)g_tile_clip_preset : 0;
	s_do.tr_vol = (s_rl.opt & TAOPT_TR_VOL) != 0;
	s_key_pcw = rects ? 0x303CE : 0x300CE;
	s_key_isp = rects ? 0xF40FFFFF : 0xF4000000;
	c->rend_nvtx = s_rl.nvtx;
	s_stat_t_dec += os_GetSeconds() - t0;
	s_rc = c;
	// Compared before the mark: once marked, the SH4 may reset this context and the SPU its lists.
	if (fence && !g_spe_draw)
		spe_compare(c, fence, raw_end, cont_end);
	rth_mark_decoded();
}

// Stage 2: every RSX call of the job. hold: Frame Hold's emulated vblank tick for the flip, 0 = flip at once.
static void rth_job_draw(int type, u64 hold)
{
	double t0 = os_GetSeconds();
	double end_dt = 0.0, held = 0.0;
	// Bundle 14: ps3gl's state path for this job's RSX calls (the rows are live). Tex Recycle: new names skip the scans.
	ps3gl_fast_path((g_tex_bind_table_preset ? PS3GL_FAST_BIND : 0) | (g_gl_compare_preset ? PS3GL_FAST_COMPARE : 0) |
	                (g_tex_recycle_preset ? PS3GL_FAST_GEN : 0));
	if (type == JOB_PRESENT)
	{
		REND_PHASE(PH_PRESENT);
		present_fb_draw();
	}
	else if (type == JOB_RTT)
	{
		REND_PHASE(PH_RTT);
		rtt_render();
	}
	else if (type == JOB_FLIP)
	{
		// P60: the game shows the buffer of the frame that waits.
		REND_PHASE(PH_ENDFRAME);
		ps3_video_flip_held();
	}
	else
	{
		s_stat_renders++;
		s_stat_vtx += s_rl.nvtx;
		VertexCount += s_rl.nvtx;
		for (int k = 0; k < 5; k++)
			s_stat_mv[k] += s_rl.mv_tris[k];

		REND_PHASE(PH_BEGINFRAME);
		if (ps3_video_begin_frame())
		{
			REND_PHASE(PH_DRAW);
			draw_frame();
			frame_extras();
			REND_PHASE(PH_ENDFRAME);
			double t_end = os_GetSeconds();
			// P60 Present Rule: the frame stays in the back buffer until the game's FB_R_SOF write (FbShown).
			if (!g_rr.flip)
			{
				ps3_video_end_frame_hold();
				s_stat_held++;
			}
			else if (hold)
				held = ps3_video_end_frame_at(hold);
			else
				ps3_video_end_frame();
			// The hold is idle time, kept out of busy= and job_max=.
			end_dt = os_GetSeconds() - t_end - held;
		}
	}
	REND_PHASE(PH_IDLE);

	double dt = os_GetSeconds() - t0 - held;
	s_stat_t_end  += end_dt;
	s_stat_t_draw += dt - end_dt;
	if (dt > s_stat_job_max)
		s_stat_job_max = dt;
	// Presents too: a 2D title that never renders still logs its SPE FB Convert window.
	if (type == JOB_RENDER || type == JOB_PRESENT)
		rth_print_stats();
}

// rttq_check: begin thread
static void rth_thread(void* arg)
{
	// The SH4 thread FPSCR follows the guest rounding mode. This thread has its
	// own, set once here, so no draw call has to save and restore it.
	__asm__ volatile("mtfsfi 7,0" ::: "memory");

	sysMutexLock(s_rmx, 0);
	while (!s_rend_quit)
	{
		if (!s_job_pending)
		{
			// Timed wait: a missed signal costs 100 ms, never the whole run.
			sysCondWait(s_job_cv, 100 * 1000);
			continue;
		}
		int type = s_job_type;
		TaContext* c = s_job_ctx;
		u32 raw_end = s_job_raw_end, cont_end = s_job_cont_end, fence = s_job_fence;
		u64 hold = s_job_hold;
		memcpy(&g_rr, &s_job_rr, sizeof(g_rr));
		s_job_pending = false;
		sysMutexUnlock(s_rmx);

		// Row Render Low Priority: the job runs at the PPE's low thread priority (or 1,1,1), then medium again.
		if (g_rend_low_prio_preset) __asm__ volatile("or 1,1,1" ::: "memory");
		if (type == JOB_RENDER || type == JOB_RTT)
			rth_job_decode(c, raw_end, cont_end, fence);
		rth_job_draw(type, hold);
		// Before the job slot frees: the next fence's RESET may wait on this one (5.12 S3).
		ps3spe_release(fence);
		__asm__ volatile("or 2,2,2" ::: "memory");

		sysMutexLock(s_rmx, 0);
		// P60: the game showed the held frame's buffer while this job ran.
		while (s_flip_want)
		{
			s_flip_want = false;
			sysMutexUnlock(s_rmx);
			ps3_video_flip_held();
			sysMutexLock(s_rmx, 0);
		}
		// P95: the RTT that waits takes the slot at once (still busy), so no later job passes it.
		if (s_q_valid)
		{
			s_job_type = JOB_RTT;
			s_job_ctx = s_busy_ctx = s_q_ctx;
			s_job_raw_end = s_q_raw_end;
			s_job_cont_end = s_q_cont_end;
			s_job_fence = s_q_fence;
			s_job_hold = 0;
			memcpy(&s_job_rr, &s_q_rr, sizeof(s_job_rr));
			s_job_decoded = false;
			s_job_pending = true;
			s_q_valid = false;
		}
		else
		{
			s_job_type = JOB_NONE;
			s_job_busy = false;
			s_busy_ctx = 0;
			s_job_decoded = true;
		}
		sysCondBroadcast(s_done_cv);
	}
	sysMutexUnlock(s_rmx);
	sysThreadExit(0);
}

static bool rth_start()
{
	if (!get_render_thread_preset())
	{
		printf("[ps3rend] render thread off, drawing on the SH4 thread\n");
		return false;
	}
	sys_mutex_attr_t mattr;
	sysMutexAttrInitialize(mattr);
	if (sysMutexCreate(&s_rmx, &mattr) != 0)
	{
		printf("[ps3rend] render mutex create failed\n");
		return false;
	}
	// PSL1GHT spells the default cond attr 0x200 (pshared) and the default mutex
	// attr 0x200 (not pshared); if lv2 rejects that pair, retry with it cleared.
	sys_cond_attr_t cattr;
	sysCondAttrInitialize(cattr);
	if (sysCondCreate(&s_job_cv, s_rmx, &cattr) != 0)
	{
		cattr.attr_pshared = 0;
		if (sysCondCreate(&s_job_cv, s_rmx, &cattr) != 0)
		{
			printf("[ps3rend] render cond create failed\n");
			sysMutexDestroy(s_rmx);
			return false;
		}
	}
	if (sysCondCreate(&s_done_cv, s_rmx, &cattr) != 0)
	{
		printf("[ps3rend] render cond create failed\n");
		sysCondDestroy(s_job_cv);
		sysMutexDestroy(s_rmx);
		return false;
	}

	s_job_type = JOB_NONE;
	s_job_pending = s_job_busy = s_q_valid = false;
	s_job_decoded = true;
	s_busy_ctx = 0;
	s_rend_quit = 0;
	// Priority 1000: just above the SH4 thread (1001), below the audio feeder (100).
	s32 ret = sysThreadCreate(&s_rend_tid, rth_thread, NULL, 1000, 0x20000,
	                          THREAD_JOINABLE, (char*)"nulldc_render");
	if (ret != 0)
	{
		printf("[ps3rend] render thread create failed: 0x%08X\n", (unsigned)ret);
		return false;
	}
	s_rend_running = true;
	printf("[ps3rend] render thread up\n");
	return true;
}

static void rth_stop()
{
	if (!s_rend_running)
		return;
	rth_wait_idle();
	sysMutexLock(s_rmx, 0);
	s_rend_quit = 1;
	sysCondBroadcast(s_job_cv);
	sysMutexUnlock(s_rmx);
	u64 rv;
	sysThreadJoin(s_rend_tid, &rv);
	s_rend_running = false;
	sysCondDestroy(s_job_cv);
	sysCondDestroy(s_done_cv);
	sysMutexDestroy(s_rmx);
	printf("[ps3rend] render thread down\n");
}

// Posts one job, or runs it inline with the render thread off (TR2); false = not taken, thread busy.
// RTT never drops (the guest reads it back); renders wait per Frame Drop (TR5); presents never wait.
static bool rth_run(int type, TaContext* c)
{
	if (c)
		c->posted = true;
	if (s_rend_running)
	{
		// P95 RTT Async: an RTT waits behind the job in flight, and the SH4 runs on.
		const bool async = type == JOB_RTT && g_rtt_async_preset;
		bool blocking = (type == JOB_RTT) && !async;
		int pol = (type == JOB_RENDER) ? get_drop_policy_preset() : 0;
		const double t_in = (type == JOB_RTT) ? os_GetSeconds() : 0.0;
		sysMutexLock(s_rmx, 0);
		while (blocking && s_job_busy)
			sysCondWait(s_done_cv, 0);
		// One RTT waits at most: a second one waits until the first takes the slot.
		while (async && s_q_valid)
			sysCondWait(s_done_cv, 0);
		if (async && s_job_busy)
		{
			s_q_ctx = c;
			s_q_raw_end = c ? c->raw_used : 0;
			s_q_cont_end = c ? c->n_cont : 0;
			s_q_fence = spe_fence(c);
			memcpy(&s_q_rr, &s_snap, sizeof(s_q_rr));
			s_q_valid = true;
			s_stat_rtt_q++;
			s_stat_rtt_sh4 += os_GetSeconds() - t_in;
			sysMutexUnlock(s_rmx);
			return true;
		}
		s_stat_rtt_direct += async ? 1 : 0;
		if (s_job_busy && pol != 0)
		{
			// One 59.94 Hz frame. Policy 2 waits with no budget at all.
			const double budget = (pol == 1) ? (1.0 / 59.94) : 0.0;
			double t0 = os_GetSeconds();
			while (s_job_busy)
			{
				if (budget == 0.0)
				{
					sysCondWait(s_done_cv, 0);
					continue;
				}
				double left = budget - (os_GetSeconds() - t0);
				if (left <= 0.0)
					break;
				sysCondWait(s_done_cv, (u64)(left * 1000000.0) + 1);
			}
			s_stat_wait += os_GetSeconds() - t0;
		}
		if (s_job_busy)
		{
			sysMutexUnlock(s_rmx);
			if (type == JOB_PRESENT)
				s_stat_fb_defer++;
			else
				s_stat_dropped++;
			return false;
		}
		s_job_type = type;
		s_job_ctx = c;
		s_busy_ctx = (type == JOB_PRESENT) ? 0 : c;
		s_job_raw_end = c ? c->raw_used : 0;
		s_job_cont_end = c ? c->n_cont : 0;
		s_job_fence = (type == JOB_PRESENT) ? 0 : spe_fence(c);
		s_job_hold = (type == JOB_RENDER) ? s_snap_hold : 0;
		s_job_decoded = (type == JOB_PRESENT);
		memcpy(&s_job_rr, &s_snap, sizeof(s_job_rr));
		s_job_pending = true;
		s_job_busy = true;
		sysCondSignal(s_job_cv);
		while (blocking && s_job_busy)
			sysCondWait(s_done_cv, 0);
		if (type == JOB_RTT)
			s_stat_rtt_sh4 += os_GetSeconds() - t_in;
		sysMutexUnlock(s_rmx);
		return true;
	}

	memcpy(&g_rr, &s_snap, sizeof(g_rr));
	const u32 fence = (type != JOB_PRESENT) ? spe_fence(c) : 0;
	if (type != JOB_PRESENT)
		rth_job_decode(c, c ? c->raw_used : 0, c ? c->n_cont : 0, fence);
	// Host FPSCR follows the SH4 rounding mode (sh4_registers.cpp); draw with round-to-nearest.
	double host_fpscr;
	__asm__ volatile("mffs %0" : "=f"(host_fpscr) :: "memory");
	__asm__ volatile("mtfsfi 7,0" ::: "memory");
	rth_job_draw(type, 0);
	__asm__ volatile("mtfsf 0xff,%0" :: "f"(host_fpscr) : "memory");
	ps3spe_release(fence);
	return true;
}

// P60: flips the frame that waits. The render thread owns the RSX: it flips now, or after the job in flight.
static void rth_flip_held()
{
	if (!s_rend_running)
	{
		ps3_video_flip_held();
		return;
	}
	sysMutexLock(s_rmx, 0);
	if (s_job_busy)
		s_flip_want = true;
	else
	{
		s_job_type = JOB_FLIP;
		s_job_ctx = 0;
		s_busy_ctx = 0;
		s_job_raw_end = s_job_cont_end = s_job_fence = 0;
		s_job_hold = 0;
		s_job_decoded = true;
		s_job_pending = true;
		s_job_busy = true;
		sysCondSignal(s_job_cv);
	}
	sysMutexUnlock(s_rmx);
}
// rttq_check: end thread

// P60, Flycast rend_swap_frame: the game wrote addr (masked) to FB_R_SOF1 or FB_R_SOF2. If the last render waits
// for that buffer, it reaches the TV now.
void FbShown(u32 addr)
{
	if (!s_fb_held || addr != s_fbw_cur)
		return;
	s_fb_held = false;
	s_stat_shown++;
	rth_flip_held();
}

bool InitRenderer()
{
	reset_all_contexts();
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		if (s_ctx[i].raw)
			continue;
		// 128 bytes: the SPE pulls the stream by DMA at full-line alignment.
		s_ctx[i].raw = (u8*)memalign(128, TA_RAW_SIZE);
		if (!s_ctx[i].raw)
			printf("[ps3rend] TA raw buffer %u alloc failed; that context drops records\n", i);
	}
	ta_slot_sync();
	// Keep running headless if the RSX fails: the UDP log still shows why.
	if (!ps3_video_init())
		printf("[ps3rend] RSX init failed, running headless\n");
	else
	{
		fb_spu_alloc();
		// P50: the menu freed its fonts at launch; the FPS text needs them before the render thread starts.
		if (g_fps_overlay_preset)
			ps3ui_init();
		ps3_vmulcd_init();
	}
	if (!TileAccel.Init() || !TaStore.Init())
		return false;
	s_ta_fast = get_ta_fast_preset() != 0;
	s_peak_frame = g_ta_peak_frame_preset != 0;
	ta_fsm_build();
	ta_store_reset();
	ta_gate_set(true);
	rth_start();
	// The SPU decoder starts with the options reset_all_contexts gave the contexts above.
	ps3spe_boot_opt(ta_opts_now());
	ps3spe_init();
	ps3spevb_init();
	ps3spetex_init();
	ps3spepix_init();
	return true;
}

void TermRenderer()
{
	ps3spetex_term();
	ps3spevb_term();
	ps3spe_term();
	rth_stop();
	// After the render thread: it may be waiting on a texture decode.
	ps3spepix_term();
	TileAccel.Term();
	TaStore.Term();
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		free(s_ctx[i].raw);
		s_ctx[i].raw = 0;
	}
	ta_slot_sync();
	ps3tex_reset();
	s_fb_tex = 0;
	memset(s_fb_rtex, 0, sizeof(s_fb_rtex));
	memset(s_fb_rmem, 0, sizeof(s_fb_rmem));
	s_fb_spu_ok = false;
	s_fog_tex = 0;
	s_fog_tex_ok = false;
	ps3_video_term();
}

void ResetRenderer(bool Manual)
{
	rth_wait_idle();
	TileAccel.Reset(Manual);
	TaStore.Reset(Manual);
	ta_store_reset();
	ta_gate_set(true);
	reset_all_contexts();
	ps3tex_reset();
	VertexCount = 0;
	FrameCount = 0;
	// Bundle 11: no render waits for a flip, no buffer address is known (Flycast rend_reset).
	s_fb_held = false;
	s_fbw_cur = s_fbw_hist[0] = s_fbw_hist[1] = 1;
	s_fb_retry = false;
}

bool ThreadStart() { return true; }
void ThreadEnd() {}

// Called at the start of every emulated vblank from SPG.cpp.
void VBlank()
{
	StallScope st_(ST_REND);
	// In-game menu (SELECT): the guest stops here; the menu draws with ps3gl on this thread.
	if (ps3_menu_pending())
	{
		rth_wait_idle();
		ps3_menu_ingame();
		// P60: the menu drew over the back buffer; a frame that waited there is gone.
		s_fb_held = false;
	}
	// P26: a 32-bit write into the shown frame and no render since the last vblank also present it (Flycast).
	const bool watched = g_fb_watch_preset && g_fb_dirty && !s_render_since_vbl;
	s_render_since_vbl = false;
	fb_watch_update();
	if (!FB_R_CTRL.fb_enable)
		return;
	// P76 Draw Retry: the last 2D present found no free buffer, so the mark it left is not true.
	const bool retry = g_draw_retry_preset && s_fb_retry;
	const bool marked = !fb_needs_present();
	if (marked && !watched && !retry)
		return; // the PVR render is still what the screen shows
	if (rth_busy())
	{
		// The next vblank retries; nothing has been marked yet.
		s_stat_fb_defer++;
		return;
	}

	SH4_PHASE(PH_VBLANK);
	snapshot_present_regs();
	// Cleared before the job is posted: the render thread sets it again if this present finds no buffer either.
	s_fb_retry = false;
	if ((s_snap.blank || present_fb_convert()) && rth_run(JOB_PRESENT, 0))
	{
		// Mark what is now on screen; the next write by the game clears the mark.
		vwi(s_snap.fb_r_sof1, FB_PRESENT_MAGIC);
		g_fb_dirty = false;
		if (marked && watched)
			s_stat_fb_watch++;
		if (retry)
			s_stat_retry++;
		// P60: the present draws over the back buffer; a render that waited there is gone.
		s_fb_held = false;
		FrameCount++;
	}
	else if (retry)
		s_fb_retry = true;
	SH4_PHASE(PH_GUEST);
}

double spg_NextVblankTime();

// Plan 5.41 Frame Hold: under 60 fps (4 frames in a row 2+ vblanks apart) a frame flips on the PS3 vsync matched
// to the next emulated vblank, where the DC shows it. A 60 fps game keeps the flip queue, also with dropped frames.
static u64 frame_hold_target()
{
	const u32 e = spg_VblankCountTotal + 1;
	const u32 gap = e - s_hold_last_vbl;
	s_hold_last_vbl = e;
	s_hold_run = gap < 2 ? 0 : s_hold_run < 4 ? s_hold_run + 1 : 4;
	if (!g_frame_hold_preset || !s_rend_running || s_hold_run < 4)
		return 0;
	const double at = spg_NextVblankTime();
	if (at <= 0.0)
		return 0;
	return (u64)((s64)gettime() + (s64)((at - os_GetSeconds()) * (double)ps3_timebase_hz));
}

void StartRender()
{
	StallScope st_(ST_REND);
	SH4_PHASE(PH_RENDER);
	// P26: earlier writes into the shown frame are superseded by this render.
	s_render_since_vbl = true;
	g_fb_dirty = false;
	spe_flush(s_ta);
	snapshot_render_regs();
	TaContext* rc = find_context(s_snap.param_base);
	const bool rc_found = rc != 0;
	if (!rc)
	{
		static u32 s_warned;
		if (s_warned++ < 5)
			printf("[ps3rend] no TA context for PARAM_BASE=%08X, drawing TA_ISP_BASE's\n", (u32)PARAM_BASE);
		rc = s_ta;
	}
	snapshot_region(rc->n_cont < RR_MAX_PASSES ? rc->n_cont : RR_MAX_PASSES - 1);
	// P66 TA Data Gate: the TA's own context goes to the core, so no list init is current from here.
	if (rc == s_ta)
		ta_gate_set(true);

	// Render-done IRQ timing; games pace off these IRQs. This is the previous
	// job's count (TR3 replays later), close enough for a clamped delay.
	u32 vtx = rc->rend_nvtx;
	sys_sync();   // plan 5.36: the countdowns start at the next slice end, as with the update every slice
	const int rdelay = get_render_delay_preset();
	if (rdelay == 1)
	{
		render_isp_pending_cycles = 800000;
		render_tsp_pending_cycles = 850000;
		render_vd_pending_cycles  = 900000;
		render_end_pending_cycles = 0;
	}
	else if (rdelay == 2)
	{
		// Flycast scheduleRenderDone: 450000 cycles + 100 a TA byte, at most 1.5M (10500 bytes); 4096 with no context.
		const u32 bytes = rc->raw_used;
		render_end_pending_cycles = !rc_found ? 4096 : bytes < 10500 ? (s32)(450000 + 100 * bytes) : 1500000;
		g_rdelay_n++;
		g_rdelay_sum += (u64)render_end_pending_cycles;
		if (render_end_pending_cycles > g_rdelay_max)
			g_rdelay_max = render_end_pending_cycles;
		g_rdelay_cap += render_end_pending_cycles == 1500000;
	}
	else
	{
		render_end_pending_cycles = vtx * 15;
		if (render_end_pending_cycles < 50000)
			render_end_pending_cycles = 50000;
	}
	sys_kick();
	raw_note_peak(rc);

	// FB_W_SOF1 bit 24 = render to texture, not a displayed frame.
	if (s_snap.fb_w_sof1 & 0x1000000)
	{
		s_stat_rtt++;
		SH4_PHASE(PH_RTT);
		rth_run(JOB_RTT, rc);
		SH4_PHASE(PH_GUEST);
		return;
	}

	s_snap_hold = frame_hold_target();
	// P60 Present Rule (Flycast DelayFrameSwapping): a render is flipped at its end only if FB_R_SOF1 holds its
	// buffer already; else it waits for the game's FB_R_SOF write (FbShown), and Frame Hold has no flip to place.
	const u32 fbw = s_snap.fb_w_sof1 & 0x00FFFFFC;
	if (g_present_rule_preset)
	{
		s_snap.flip = fbw == (FB_R_SOF1 & 0x00FFFFFC);
		if (!s_snap.flip)
			s_snap_hold = 0;
		// Flycast clearFramebuffer: the colour is cleared for a buffer that is not one of the last two.
		const bool fresh = fbw != s_fbw_hist[0] && fbw != s_fbw_hist[1];
		if (fresh)
		{
			s_fbw_hist[0] = s_fbw_hist[1];
			s_fbw_hist[1] = fbw;
		}
		s_snap.keep = g_present_rule_preset >= 2 && s_fb_held && !fresh;
	}
	else
		s_fb_held = false;
	if (rth_run(JOB_RENDER, rc))
	{
		FrameCount++;
		if (g_present_rule_preset)
		{
			s_fb_held = !s_snap.flip;
			s_fbw_cur = fbw;
		}
	}
	// Mark the framebuffer the PVR just produced, so VBlank knows it is on
	// screen. A dropped frame is marked too: the guest believes it rendered.
	fb_stamp_magic();
	SH4_PHASE(PH_GUEST);
}

void EndRender() {}

// The store stage owns TA_ISP_BASE: it picks the context the raw bytes land in.
// The decoder-side ListInit/ListCont run later, from decode_pending().
void ListCont()
{
	set_ta_context(TA_ISP_BASE);
	ta_gate_set(false);
	s_stat_cont++;
	spe_flush(s_ta);
	if (s_ta->n_cont < MAX_CONT_MARKS)
	{
		s_ta->cont_marks[s_ta->n_cont++] = s_ta->raw_used;
		if (g_spe_on)
			ps3spe_cont((u32)(s_ta - s_ctx), s_ta->raw_used);
	}
	else if (!s_ta->raw_overflow)
	{
		s_ta->raw_overflow = true;
		s_stat_raw_of++;
	}
	// The fast store keeps no per-list-cont state: a continuation reopens the
	// finished flags the splitter tracks, and the table does not use them.
	if (!s_ta_fast)
		TaStore.ListCont();
	// P63 (Flycast ta_vtx_ListInit): a continuation also closes the store's open list.
	else if (g_face_reset_preset)
		ta_store_reset();
}

// rttq_check: begin listinit
void ListInit()
{
	set_ta_context(TA_ISP_BASE);
	ta_gate_set(false);
	// P95 TA Context Split: a job still holds this context, so the new list takes a free one.
	if (g_ta_ctx_split_preset && rth_holds(s_ta))
		ctx_move();
	// reset_raw drops bytes the render thread may still be replaying.
	rth_wait_decode(s_ta);
	reset_raw(s_ta);
	if (s_ta_fast)
		ta_store_reset();
	else
		TaStore.ListInit();
}
// rttq_check: end listinit

// As nullDC-v1: a soft reset keeps the lists and the stored bytes.
void SoftReset()
{
	spe_flush(s_ta);
	if (g_spe_on)
		ps3spe_soft();
	if (s_ta_fast)
		ta_store_reset();
	else
		TaStore.SoftReset();
}
