#pragma once
// OPTIMIZATION_PLAN 5.41 (b), VIDEO / DEBUG "Frame Hold": the PS3 vsync a held frame flips on. Pure math,
// shared with the PC check in misc/framehold_check.
#include <stdint.h>

// mark: timebase tick of the frame's emulated vblank plus half a refresh. tb0/n0: tick and vblank count of one
// completed vsync; per: refresh period in ticks. prev_k/prev_mark: the last held frame (prev_k 0 = none).
static inline uint64_t framehold_vsync(uint64_t mark, uint64_t tb0, uint64_t n0, uint32_t per,
                                       uint64_t prev_k, uint64_t prev_mark)
{
	const int64_t p = per;
	const int64_t m = (int64_t)(mark - tb0);
	// First vsync at or after the mark.
	const int64_t j = m > 0 ? (m + p - 1) / p : -((-m) / p);
	uint64_t k = n0 + (uint64_t)j;
	if (prev_k)
	{
		// Same step as the marks while that vsync stays near [mark, mark + T): the drift then moves one frame
		// per cycle, and timing noise near a vsync edge cannot flip frames between two vsyncs.
		const int64_t dm = (int64_t)(mark - prev_mark);
		const int64_t steps = dm >= 0 ? (dm + p / 2) / p : -((-dm + p / 2) / p);
		const uint64_t kr = prev_k + (uint64_t)steps;
		const int64_t e = (int64_t)(kr - n0) * p - m;
		if (e >= -p / 8 && e < p + p / 8)
			k = kr;
	}
	return k;
}
