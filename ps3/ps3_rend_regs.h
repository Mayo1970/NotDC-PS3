#pragma once
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"

struct DcParam
{
	PCW pcw;
	ISP_TSP isp;
	TSP tsp;
	TCW tcw;
};

struct BgVertex
{
	float x, y, u, v;
	u32 col;
};

// P10 List Passes: the region-array entries (one per pass) a render snapshots.
#define RR_MAX_PASSES 8

// TR2: every PVR register and VRAM-derived value the draw path reads, taken on
// the SH4 thread at STARTRENDER/VBlank so a deferred draw never sees a later write.
struct RenderRegs
{
	u32 param_base;
	u32 isp_backgnd_d;
	u32 pt_alpha_ref;
	u32 fb_w_sof1, fb_w_sof2, fb_w_ctrl, fb_w_linestride;
	u32 fb_x_clip_min, fb_x_clip_max, fb_y_clip_min, fb_y_clip_max;
	u32 fb_r_sof1, fb_r_size, fb_r_depth;
	u32 pal_ram_ctrl, text_control;
	u32 fpu_shad_scale;
	float fb_scale_x, fb_scale_y;
	bool autosort;
	// Bundle 4. pass_autosort[0] is autosort; mv_shared: a pass whose opaque and translucent volume pointers
	// are one list (P27); glob_clip_x/y: the TA_GLOB_TILE_CLIP size in pixels (P10 tile clip).
	bool pass_autosort[RR_MAX_PASSES];
	bool mv_shared;
	u32 glob_clip_x, glob_clip_y;
	// Resolved on the SH4 thread: both read VRAM through PARAM_BASE.
	BgVertex bg_vtx[3];
	DcParam bg_param;
	// 3a Fog, taken only with the Fog row on: FOG_COL_RAM, FOG_COL_VERT, FOG_DENSITY, and FOG_TABLE's low 16 bits.
	u32 fog_col_ram, fog_col_vert, fog_density;
	u16 fog_table[128];
	// Bundle 17 Colour Clamp: FOG_CLAMP_MIN and FOG_CLAMP_MAX (ARGB), taken at every render: the log shows them.
	u32 fog_clamp_min, fog_clamp_max;
	// Bundle 11. reg_*: the region array's tiles in pixels inside TA_GLOB_TILE_CLIP, ends not included (Flycast
	// tileClip); reg_ok = they were read. flip: 0 = the render waits for its FB_R_SOF write (P60); keep: no colour clear.
	u32 scaler_ctl;
	u32 reg_x0, reg_y0, reg_x1, reg_y1;
	bool reg_ok, flip, keep;
	// A 2D present: FB_R_SOF2, the interlace, field and pixel clock bits (P26); a blanked screen and its colour (P76).
	u32 fb_r_sof2, vo_border_col;
	bool fb_interlace, fb_field2, fb_vclk, blank;
};

extern RenderRegs g_rr;
