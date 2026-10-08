// Plan 5.41 Frame Hold PC check: the real framehold_vsync() (ps3/ps3_framehold.h) in a model of the pacer's
// emulated vblanks, the vblank handler's grid samples, the render thread's wait and the PS3's vsync flips.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
#include "ps3/ps3_framehold.h"

static const double TB_HZ = 79.8e6;

struct Scenario
{
	const char* name;
	double ps3_hz;      // display refresh
	double emu_hz;      // emulated vblank rate
	int gaps;           // 0: always 2 vblanks, 1: 2 or 3 at random, 2: always 1 (60 fps), 3: 1, 10% 2
	double sr_frac;     // where STARTRENDER falls in the vblank before the frame's own
	double sr_jit;      // +- jitter of that, seconds
	double late_p;      // chance the SH4 is late: STARTRENDER up to 10 ms after the deadline
	double spike_p;     // chance of a 1-6 ms vblank handler delay (else 20-200 us)
	double miss_p;      // chance a vblank gets no handler run
	double minutes;
	int slips;          // drift slips allowed (one per cycle, plus margin)
};

struct Result
{
	long held[5];       // 0 = skipped, 1, 2, 3, 4+
	long late, early, holds, frames, busy;
	double lat_sum, lat_max, wait_sum;
};

// ps3_video.c vsync_ref(): the earliest of the last 8 slots carried to the latest count, within half a refresh
// below their median.
static uint64_t grid_ref(const uint64_t ring[8], uint64_t last, uint64_t per)
{
	uint64_t t[8];
	for (uint32_t j = 0; j < 8; j++)
		t[j] = ring[(last - j) & 7] + j * per;
	std::sort(t, t + 8);
	uint32_t i = 0;
	while (t[i] + per / 2 < t[3])
		i++;
	return t[i];
}

static Result run(const Scenario& s, bool hold, uint32_t seed)
{
	std::mt19937_64 rng(seed);
	std::uniform_real_distribution<double> u01(0.0, 1.0);
	auto uni = [&](double a, double b) { return a + (b - a) * u01(rng); };

	const double T = 1.0 / s.ps3_hz, P = 1.0 / s.emu_hz;
	const double v0 = 0.0037;            // vsync 0 at this time; the emulated clock starts at 0
	auto vt = [&](int64_t k) { return v0 + (double)k * T; };
	const uint32_t per = (uint32_t)llround(T * TB_HZ);

	// The vblank handler, run by run in order: a late run delays the ones behind it. Own generator, so the
	// frame timings match between the "now" and "hold" runs.
	std::mt19937_64 hrng(seed * 31 + 1);
	auto huni = [&](double a, double b) { return a + (b - a) * u01(hrng); };
	uint64_t ring[8] = {};
	uint64_t vs_last = 0;
	int vs_seen = 0;
	int64_t next_vs = 1;
	double h_prev = 0.0, h_next = -1.0;
	bool miss_next = false;
	auto handlers_until = [&](double now) {
		for (;;)
		{
			if (h_next < 0.0)
			{
				const double lam = u01(hrng) < s.spike_p ? huni(0.001, 0.006) : huni(0.00002, 0.0002);
				h_next = std::max(vt(next_vs) + lam, h_prev);
				miss_next = u01(hrng) < s.miss_p;
			}
			if (h_next > now)
				return;
			h_prev = h_next;
			if (!miss_next)
			{
				ring[next_vs & 7] = (uint64_t)llround(h_next * TB_HZ);
				vs_last = (uint64_t)next_vs;
				if (vs_seen < 8)
					vs_seen++;
			}
			next_vs++;
			h_next = -1.0;
		}
	};

	Result r = {};
	std::vector<int64_t> shown;          // vsync each displayed frame flipped on
	uint64_t prev_k = 0, prev_mark = 0;
	int64_t last_flip = -1, prev_flip = -1;
	double rth_free = 0.0;
	int64_t e = 10;
	int run_ge2 = 0;
	const double end = s.minutes * 60.0;

	for (;;)
	{
		int gap;
		if (s.gaps == 0)
			gap = 2;
		else if (s.gaps == 1)
			gap = u01(rng) < 0.6 ? 2 : 3;
		else if (s.gaps == 2)
			gap = 1;
		else
			gap = u01(rng) < 0.1 ? 2 : 1;
		// ps3_rend.cpp frame_hold_target(): 4 frames in a row 2+ vblanks apart.
		run_ge2 = gap < 2 ? 0 : std::min(run_ge2 + 1, 4);
		const bool hold_frame = hold && run_ge2 >= 4;
		e += gap;
		const double d = e * P;          // the deadline of the frame's emulated vblank
		if (d > end)
			break;
		double ts = d - P + s.sr_frac * P + uni(-s.sr_jit, s.sr_jit);
		ts = std::min(std::max(ts, d - P), d);
		if (u01(rng) < s.late_p)
			ts = d + uni(0.0, 0.010);
		r.frames++;

		// The render thread takes the job when free (Frame Drop's default makes the SH4 wait for it).
		double start = ts;
		if (rth_free > ts)
		{
			r.busy++;
			start = rth_free;
		}
		// 3 buffers: with 2 flips still pending at begin_frame the frame is skipped.
		if ((last_flip >= 0 && vt(last_flip) > start) + (prev_flip >= 0 && vt(prev_flip) > start) >= 2)
		{
			r.held[0]++;
			rth_free = start;
			continue;
		}
		const double tf = start + uni(0.002, 0.006);   // draw done on the CPU
		const double tr = tf + uni(0.0003, 0.002);      // the RSX done

		double release = tf;
		int64_t k = 0;
		if (hold_frame)
		{
			handlers_until(tf);
			const uint64_t mark = (uint64_t)llround(d * TB_HZ) + per / 2;
			uint64_t kk = 0, tb0 = 0;
			if (vs_seen >= 8)
			{
				tb0 = grid_ref(ring, vs_last, per);
				kk = framehold_vsync(mark, tb0, vs_last, per, prev_k, prev_mark);
			}
			const int64_t cur = (int64_t)floor((tf - v0) / T);
			if ((int64_t)kk > cur + 5)
				kk = 0;
			if ((int64_t)kk > cur + 1)
			{
				// Wake 1/8 refresh after the grid's vsync k-1, then poll the count.
				const double wake = (double)(tb0 + (kk - 1 - vs_last) * per + per / 8) / TB_HZ + huni(0.0, 0.0005);
				release = std::max(std::max(wake, vt((int64_t)kk - 1) + huni(0.0, 0.0002)), tf);
				r.wait_sum += release - tf;
			}
			if (kk)
				r.holds++;
			prev_k = kk;
			prev_mark = mark;
			k = (int64_t)kk;
		}
		rth_free = release;

		// The RSX reaches the flip at max(release, tr); it runs on the next vsync after the last flip.
		const double at = std::max(release, tr);
		int64_t fk = (int64_t)floor((at - v0) / T) + 1;
		if (fk <= last_flip)
			fk = last_flip + 1;
		prev_flip = last_flip;
		last_flip = fk;
		shown.push_back(fk);
		if (k && fk > k)
			r.late++;
		if (k && fk < k)
			r.early++;
		const double lat = vt(fk) - d;
		r.lat_sum += lat;
		r.lat_max = std::max(r.lat_max, lat);
	}
	for (size_t i = 1; i < shown.size(); i++)
	{
		const int64_t h = shown[i] - shown[i - 1];
		r.held[h >= 4 ? 4 : h]++;
	}
	return r;
}

static void print(const char* tag, const Result& r)
{
	printf("  %-5s held 1=%ld 2=%ld 3=%ld 4+=%ld skip=%ld | hold=%ld late=%ld early=%ld busy=%ld | latency avg %.1f max "
	       "%.1f ms, wait avg %.1f ms\n",
	       tag, r.held[1], r.held[2], r.held[3], r.held[4], r.held[0], r.holds, r.late, r.early, r.busy,
	       r.frames ? r.lat_sum * 1000.0 / r.frames : 0.0, r.lat_max * 1000.0,
	       r.holds ? r.wait_sum * 1000.0 / r.holds : 0.0);
}

int main()
{
	const double emu = 200e6 / 3336375.0, ntsc = 60000.0 / 1001.0;
	const Scenario sc[] = {
		// Sonic: 30 fps, STARTRENDER mid-frame; ~3 drift cycles in 10 minutes.
		{ "sonic 30fps", ntsc, emu, 0, 0.5, 0.004, 0.0, 0.0, 0.0, 10, 5 },
		{ "sonic, SH4 late 1% of frames", ntsc, emu, 0, 0.5, 0.004, 0.01, 0.0, 0.0, 10, 5 },
		{ "sonic, render late in frame", ntsc, emu, 0, 0.9, 0.003, 0.0, 0.0, 0.0, 10, 5 },
		{ "sonic, handler spikes 5%", ntsc, emu, 0, 0.5, 0.004, 0.0, 0.05, 0.0, 10, 5 },
		{ "sonic, handler spikes 30% + misses", ntsc, emu, 0, 0.5, 0.004, 0.0, 0.3, 0.01, 10, 5 },
		// Looney Tunes: 2 or 3 vblanks per frame.
		{ "looney 2/3 vblanks", ntsc, emu, 1, 0.5, 0.004, 0.0, 0.0, 0.0, 10, 5 },
		// Crazy Taxi: 60 fps, Frame Hold leaves it alone; with 10% of frames dropped, nearly alone.
		{ "crazy taxi 60fps", ntsc, emu, 2, 0.5, 0.004, 0.0, 0.0, 0.0, 5, 0 },
		{ "60fps, 10% frames dropped", ntsc, emu, 3, 0.5, 0.004, 0.0, 0.0, 0.0, 5, 0 },
		// A 60.00 Hz display: the drift runs the other way, a slip every ~18 s.
		{ "sonic on 60.00 Hz", 60.0, emu, 0, 0.5, 0.004, 0.0, 0.0, 0.0, 10, 40 },
	};
	int fails = 0;
	for (const Scenario& s : sc)
	{
		const Result base = run(s, false, 7), held = run(s, true, 7);
		printf("%s (%.0f min)\n", s.name, s.minutes);
		print("now", base);
		print("hold", held);
		bool ok = held.early == 0;
		if (s.gaps == 2)
			// 60 fps: no frame is held, so nothing may change.
			ok = ok && held.holds == 0 && held.held[1] == base.held[1] && held.held[0] == base.held[0];
		else if (s.gaps == 3)
			// Dropped frames in a 60 fps game: almost never 4 in a row, so almost nothing may change.
			ok = ok && held.holds <= 5 && held.busy <= base.busy + 2 * held.holds &&
			     held.held[0] <= base.held[0] + 2 * held.holds;
		else
		{
			// Under 60 fps: the game's own steps, one slip per drift cycle, and a 1/3 pair per late frame.
			const long odd = s.gaps == 1 ? held.held[1] : held.held[1] + held.held[3];
			const long late_frames = (long)(s.late_p * held.frames * 1.5) + 2;
			ok = ok && held.late <= late_frames && odd <= s.slips + 2 * held.late;
		}
		printf("  %s\n", ok ? "PASS" : "FAIL");
		fails += !ok;
	}
	printf(fails ? "FAIL: %d scenario(s)\n" : "ALL PASS\n", fails);
	return fails ? 1 : 0;
}
