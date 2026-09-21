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
	float fb_scale_x, fb_scale_y;
	bool autosort;
	// Resolved on the SH4 thread: both read VRAM through PARAM_BASE.
	BgVertex bg_vtx[3];
	DcParam bg_param;
};

extern RenderRegs g_rr;
