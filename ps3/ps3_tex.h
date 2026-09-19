#pragma once
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"

// PVR texture decode + cache onto ps3gl textures. Runs on the SH4 thread.
void ps3tex_frame_begin();
// Binds the decoded texture on TMU0 and sets its filter/clamp from the TSP.
bool ps3tex_bind(TSP tsp, TCW tcw);
void ps3tex_reset();
void ps3tex_stats(u32* decodes, u32* live);
