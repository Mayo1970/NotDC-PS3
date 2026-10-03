#pragma once
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"

// PVR texture decode + cache onto ps3gl textures. Runs on the SH4 thread.
void ps3tex_frame_begin();
// Binds the decoded texture on TMU0 and sets its filter/clamp from the TSP.
bool ps3tex_bind(TSP tsp, TCW tcw);
void ps3tex_reset();
void ps3tex_stats(u32* decodes, u32* live);
// Read and clear the window's bind split (timebase ticks) and hash volume.
void ps3tex_split(u64* t_hash, u64* t_decode, u64* t_gl, u32* hashes, u64* hash_bytes);
