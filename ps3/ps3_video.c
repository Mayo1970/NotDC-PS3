// RSX display bring-up: init, 3 flip buffers and vsync as in xashPS3 vid_ps3.c / ioQuake3-PS3 ps3_glimp.c.
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sysutil/video.h>

#include "ps3gl.h"
#include "ps3_phase.h"
#include "ps3_log.h"
#include "ps3_timebase.h"
#include "ps3_framehold.h"
#include "ps3_video.h"

// 1 MB CB is HW-validated in both ports; the 64 KB rsx.h default failed on HW.
#define PS3_RSX_CB_SIZE   (1 * 1024 * 1024)
#define PS3_RSX_HOST_SIZE (8 * 1024 * 1024)
// 3 buffers: with 2, a late frame waits a full extra vsync.
#define PS3_FB_COUNT      3

static gcmContextData* s_ctx;
static void* s_host_addr;
static u32 s_width = 1280, s_height = 720, s_pitch;
static void* s_color[PS3_FB_COUNT];
static u32 s_color_off[PS3_FB_COUNT];
static void* s_depth;
static u32 s_depth_off;
static int s_cur_fb;
static int s_ready;

// Flip handler, not gcmGetFlipStatus(): polling reported "done" at once on HW.
static volatile u32 s_flip_queued;
static volatile u32 s_flip_done;

// Plan 5.41: refreshes each frame stayed on screen, from the time between flips (whatever order the
// vblank and flip events come in); the vblank count cross-checks it.
static volatile u32 s_held[4];       // 1, 2, 3, 4+ refreshes
static volatile u32 s_held_bad;      // flips where the time and the vblank count disagree
static volatile u32 s_refresh_tb;    // PS3 refresh period, timebase ticks
static u64 s_flip_tb, s_flip_vbl, s_first_tb, s_first_vbl;
static u32 s_held_seen[4], s_held_bad_seen;

// Plan 5.41 (b) Frame Hold: the vsync each queued flip was held for (0 = not held), by flip number & 3;
// at most 2 flips are ever pending.
static volatile u64 s_flip_target[4];
// The vblank handler's tick for each of the last 8 vblanks, by vblank count & 7.
static volatile u64 s_vs_tb[8];
static volatile u64 s_vs_last;
static volatile u32 s_vs_seen;
static volatile u32 s_hold_late, s_hold_early;
static u32 s_hold_late_seen, s_hold_early_seen, s_hold_n;
static double s_hold_wait;
static u64 s_hold_k, s_hold_mark;    // the last held frame's vsync and mark

static u32 s_presented, s_skipped;
static double s_stats_t0;

// Render-to-texture readback staging. CPU reads of RSX local memory run at
// ~16 MB/s, so the RSX blits the rendered rect into main memory instead.
#define PS3_RB_MAX_W 1024
#define PS3_RB_MAX_H 1024
#define PS3_RB_SIZE  (PS3_RB_MAX_W * PS3_RB_MAX_H * 4)
static void* s_rb_addr;
static u32 s_rb_off;

// ps3gl_main.c and ps3gl_vertices.c link against these engine hooks.
void Con_Printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ps3_log_vprintf(fmt, ap);
	va_end(ap);
}

double Platform_DoubleTime(void)
{
	return ps3gl_ticks_to_sec(ps3gl_ticks());
}

// Only for one-shot glReadPixels; CPU reads of GDDR3 run at ~16 MB/s.
void* PS3_RSX_GetColorBuffer(u32* pitch, u32* width, u32* height)
{
	if (!s_ready)
		return NULL;
	rsxFlushBuffer(s_ctx);
	rsxFinish(s_ctx, 1);
	if (pitch)  *pitch = s_pitch;
	if (width)  *width = s_width;
	if (height) *height = s_height;
	return s_color[s_cur_fb];
}

static void flip_handler(const u32 head)
{
	(void)head;
	const u64 tb = gettime();
	const u64 vbl = gcmGetVBlankCount();
	if (s_flip_done == 0)
	{
		s_first_tb = tb;
		s_first_vbl = vbl;
	}
	else
	{
		// Flips land on vblanks, so over 600+ of them the handler's delay does not count.
		if (vbl - s_first_vbl >= 600)
			s_refresh_tb = (u32)((tb - s_first_tb) / (vbl - s_first_vbl));
		const u64 n = (tb - s_flip_tb + s_refresh_tb / 2) / s_refresh_tb;
		if (n > 0)
			s_held[n < 4 ? n - 1 : 3]++;
		if (n == 0 || n != vbl - s_flip_vbl)
			s_held_bad++;
	}
	const u64 k = s_flip_target[s_flip_done & 3];
	if (k && vbl > k)
		s_hold_late++;
	else if (k && vbl < k)
		s_hold_early++;
	s_flip_tb = tb;
	s_flip_vbl = vbl;
	s_flip_done++;
}

static void vblank_handler(const u32 head)
{
	(void)head;
	const u64 tb = gettime();
	const u64 n = gcmGetVBlankCount();
	s_vs_tb[n & 7] = tb;
	__asm__ volatile("lwsync" ::: "memory");
	s_vs_last = n;
	if (s_vs_seen < 8)
		s_vs_seen++;
}

// One point of the PS3 vsync grid: the latest vblank count and the earliest of the last 8 handler ticks carried
// to it, within half a refresh below their median (a missed vblank leaves an entry 8 refreshes old).
static int vsync_ref(u64* tb, u64* n)
{
	if (s_vs_seen < 8)
		return 0;
	const u64 last = s_vs_last;
	__asm__ volatile("lwsync" ::: "memory");
	const u64 per = s_refresh_tb;
	u64 t[8];
	for (u32 j = 0; j < 8; j++)
	{
		u64 v = s_vs_tb[(last - j) & 7] + j * per;
		u32 i = j;
		for (; i > 0 && t[i - 1] > v; i--)
			t[i] = t[i - 1];
		t[i] = v;
	}
	u32 i = 0;
	while (t[i] + per / 2 < t[3])
		i++;
	*tb = t[i];
	*n = last;
	return 1;
}

static void set_render_target(int index)
{
	gcmSurface sf;
	memset(&sf, 0, sizeof(sf));
	sf.colorFormat = GCM_SURFACE_A8R8G8B8;
	sf.colorTarget = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0] = s_color_off[index];
	sf.colorPitch[0] = s_pitch;
	// MRT slots 1-3 must be filled even when unused, else black screen on HW.
	for (int i = 1; i < 4; i++)
	{
		sf.colorLocation[i] = GCM_LOCATION_RSX;
		sf.colorOffset[i] = s_color_off[index];
		sf.colorPitch[i] = 64;
	}
	sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
	sf.depthLocation = GCM_LOCATION_RSX;
	sf.depthOffset = s_depth_off;
	sf.depthPitch = s_width * 4;
	sf.type = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;
	sf.width = s_width;
	sf.height = s_height;
	rsxSetSurface(s_ctx, &sf);
}

int ps3_video_init(void)
{
	if (s_ready)
		return 1;

	s_host_addr = memalign(1024 * 1024, PS3_RSX_HOST_SIZE);
	if (!s_host_addr)
	{
		printf("[vid] host memalign failed\n");
		return 0;
	}

	s32 ret = rsxInit(&s_ctx, PS3_RSX_CB_SIZE, PS3_RSX_HOST_SIZE, s_host_addr);
	printf("[vid] rsxInit ret=%d ctx=%p\n", (int)ret, (void*)s_ctx);
	if (ret != 0 || !s_ctx)
		return 0;

	// 720p: 1080p was fill-rate bound in xashPS3. Fall back to the TV mode if absent.
	s32 vid_res = VIDEO_RESOLUTION_720;
	u8 vid_aspect = VIDEO_ASPECT_16_9;
	if (!videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_16_9, 0))
	{
		videoState state;
		if (videoGetState(0, 0, &state) != 0)
		{
			printf("[vid] videoGetState failed\n");
			return 0;
		}
		vid_res = state.displayMode.resolution;
		vid_aspect = state.displayMode.aspect;
	}

	videoResolution res;
	if (videoGetResolution(vid_res, &res) != 0)
	{
		printf("[vid] videoGetResolution failed\n");
		return 0;
	}
	s_width = res.width;
	s_height = res.height;
	s_pitch = s_width * 4;

	videoConfiguration vconfig;
	memset(&vconfig, 0, sizeof(vconfig));
	vconfig.resolution = vid_res;
	vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch = s_pitch;
	vconfig.aspect = vid_aspect;
	ret = videoConfigure(0, &vconfig, NULL, 0);
	printf("[vid] %ux%u res_id=%d aspect=%u videoConfigure=%d\n",
	       (unsigned)s_width, (unsigned)s_height, (int)vid_res, (unsigned)vid_aspect, (int)ret);

	for (int i = 0; i < PS3_FB_COUNT; i++)
	{
		s_color[i] = rsxMemalign(64, s_pitch * s_height);
		if (!s_color[i])
		{
			printf("[vid] rsxMemalign color[%d] failed\n", i);
			return 0;
		}
		rsxAddressToOffset(s_color[i], &s_color_off[i]);
		ret = gcmSetDisplayBuffer(i, s_color_off[i], s_pitch, s_width, s_height);
		printf("[vid] display buffer %d offset=0x%X ret=%d\n", i, (unsigned)s_color_off[i], (int)ret);
	}

	s_depth = rsxMemalign(64, s_width * s_height * 4);
	if (!s_depth)
	{
		printf("[vid] rsxMemalign depth failed\n");
		return 0;
	}
	rsxAddressToOffset(s_depth, &s_depth_off);

	// The RSX IO map works in 1 MB pages, so the staging buffer matches that.
	s_rb_addr = memalign(1024 * 1024, PS3_RB_SIZE);
	if (s_rb_addr)
	{
		s32 rb_ret = gcmMapMainMemory(s_rb_addr, PS3_RB_SIZE, &s_rb_off);
		printf("[vid] readback %u KB at %p gcmMapMainMemory=%d offset=0x%X\n",
		       (unsigned)(PS3_RB_SIZE / 1024), s_rb_addr, (int)rb_ret, (unsigned)s_rb_off);
		if (rb_ret != 0)
		{
			free(s_rb_addr);
			s_rb_addr = NULL;
		}
	}
	if (!s_rb_addr)
		printf("[vid] readback buffer unavailable, render to texture disabled\n");

	gcmSetFlipMode(GCM_FLIP_VSYNC);
	s_flip_queued = 0;
	s_flip_done = 0;
	memset((void*)s_flip_target, 0, sizeof(s_flip_target));
	s_hold_k = 0;
	s_vs_seen = 0;
	// 59.94 Hz until the flip handler has measured the real period.
	s_refresh_tb = (u32)(ps3_timebase_hz * 1001 / 60000);
	gcmSetFlipHandler(flip_handler);
	gcmSetVBlankHandler(vblank_handler);

	if (!ps3gl_init(s_ctx, s_width, s_height))
	{
		printf("[vid] ps3gl_init failed\n");
		return 0;
	}

	s_cur_fb = 0;
	s_presented = s_skipped = 0;
	s_stats_t0 = Platform_DoubleTime();
	s_ready = 1;
	printf("[vid] ready\n");
	return 1;
}

void ps3_video_term(void)
{
	if (s_ready)
	{
		// Drain the RSX before freeing memory it may still read, else a hard freeze.
		rsxFinish(s_ctx, 1);
		// A flip IRQ after teardown would run on freed state.
		gcmSetFlipHandler(NULL);
		gcmSetVBlankHandler(NULL);
		ps3gl_shutdown();
		for (int i = 0; i < PS3_FB_COUNT; i++)
		{
			if (s_color[i])
				rsxFree(s_color[i]);
			s_color[i] = NULL;
		}
		if (s_depth)
			rsxFree(s_depth);
		s_depth = NULL;
		if (s_rb_addr)
			free(s_rb_addr);
		s_rb_addr = NULL;
		s_ready = 0;
		printf("[vid] shut down\n");
	}
	if (s_host_addr)
	{
		free(s_host_addr);
		s_host_addr = NULL;
	}
}

void ps3_video_size(u32* w, u32* h)
{
	*w = s_width;
	*h = s_height;
}

int ps3_video_begin_frame(void)
{
	if (!s_ready)
		return 0;
	// Never block the SH4 on the display: skip when the free buffer is still queued.
	if ((int)(s_flip_queued - s_flip_done) > PS3_FB_COUNT - 2)
	{
		s_skipped++;
		return 0;
	}
	set_render_target(s_cur_fb);
	ps3gl_begin_frame();
	return 1;
}

// P18: CONTROL0's depth format. FLOAT reads the Z24 surface as f32 bits >> 7 (RPCS3's d24f); a FIFO
// write, so a deferred ps3gl GL_POLYGON batch goes out first.
void ps3_video_depth_float(int on)
{
	if (!s_ready)
		return;
	ps3gl_flush_polygon_batch();
	rsxSetDepthFormat(s_ctx, on ? GCM_DEPTH_FORMAT_FLOAT : GCM_DEPTH_FORMAT_FIXED);
}

// Ends a frame that must not reach the screen (render to texture): the draw
// commands still have to be flushed, but no flip is queued and the buffer stays.
void ps3_video_end_frame_noflip(void)
{
	ps3gl_end_frame();
	rsxFlushBuffer(s_ctx);
}

// Copies w*h pixels of the back buffer into the staging buffer and waits for the RSX. x/y are GL window
// coordinates (y from the bottom). Returns native A8R8G8B8 rows, or NULL when the copy cannot run.
const u32* ps3_video_readback(u32 x, u32 y, u32 w, u32 h)
{
	if (!s_ready || !s_rb_addr || w == 0 || h == 0)
		return NULL;
	if (w > PS3_RB_MAX_W || h > PS3_RB_MAX_H || x + w > s_width || y + h > s_height)
		return NULL;

	u32 src_y = s_height - (y + h);
	rsxSetTransferData(s_ctx, GCM_TRANSFER_LOCAL_TO_MAIN, s_rb_off, w * 4,
	                   s_color_off[s_cur_fb] + src_y * s_pitch + x * 4, s_pitch,
	                   w * 4, h);
	REND_PHASE(PH_READBACK);
	rsxFlushBuffer(s_ctx);
	rsxFinish(s_ctx, 1);
	REND_PHASE(PH_RTT);
	return (const u32*)s_rb_addr;
}

// Queues the flip of the drawn buffer; k = the vsync it was held for, 0 = not held.
static void queue_flip(u64 k)
{
	s_flip_target[s_flip_queued & 3] = k;
	// WaitFlip before SetFlip: the rsx.h doc order gave a black screen on HW.
	gcmSetWaitFlip(s_ctx);
	gcmSetFlip(s_ctx, s_cur_fb);
	rsxFlushBuffer(s_ctx);

	s_flip_queued++;
	s_cur_fb = (s_cur_fb + 1) % PS3_FB_COUNT;
	s_presented++;
}

static void vid_stats(void)
{
	double now = Platform_DoubleTime();
	if (now - s_stats_t0 < 5.0)
		return;
	u32 held[4];
	for (int i = 0; i < 4; i++)
	{
		const u32 c = s_held[i];
		held[i] = c - s_held_seen[i];
		s_held_seen[i] = c;
	}
	const u32 bad = s_held_bad, late = s_hold_late, early = s_hold_early;
	printf("[vid] presented=%u skipped=%u flips queued=%u done=%u | refresh %.3f Hz, held 1=%u 2=%u 3=%u 4+=%u bad=%u"
	       " | hold=%u late=%u early=%u wait=%.1fms\n",
	       (unsigned)s_presented, (unsigned)s_skipped, (unsigned)s_flip_queued, (unsigned)s_flip_done,
	       (double)ps3_timebase_hz / s_refresh_tb, (unsigned)held[0], (unsigned)held[1], (unsigned)held[2],
	       (unsigned)held[3], (unsigned)(bad - s_held_bad_seen), (unsigned)s_hold_n,
	       (unsigned)(late - s_hold_late_seen), (unsigned)(early - s_hold_early_seen),
	       s_hold_n ? s_hold_wait * 1000.0 / s_hold_n : 0.0);
	s_held_bad_seen = bad;
	s_hold_late_seen = late;
	s_hold_early_seen = early;
	s_hold_n = 0;
	s_hold_wait = 0.0;
	s_stats_t0 = now;
}

void ps3_video_end_frame(void)
{
	ps3gl_end_frame();
	queue_flip(0);
	vid_stats();
}

double ps3_video_end_frame_at(u64 target_tb)
{
	ps3gl_end_frame();
	// The RSX draws while this thread waits for the flip's turn.
	rsxFlushBuffer(s_ctx);
	const u32 per = s_refresh_tb;
	const u64 mark = target_tb + per / 2;
	u64 tb0 = 0, n0 = 0;
	u64 k = vsync_ref(&tb0, &n0) ? framehold_vsync(mark, tb0, n0, per, s_hold_k, s_hold_mark) : 0;
	const u64 cur = gcmGetVBlankCount();
	// Further than any paced vblank can be: flip now, not counted as held.
	if (k > cur + 5)
		k = 0;
	double waited = 0.0;
	if (k > cur + 1)
	{
		const u64 t0 = gettime();
		// Wake 1/8 refresh after vsync k-1; the vblank count, not the grid, ends the wait.
		const s64 dt = (s64)(tb0 + (k - 1 - n0) * per + per / 8 - t0);
		if (dt > 0)
			usleep((unsigned)(dt * 1000000 / (s64)ps3_timebase_hz));
		for (int i = 0; i < 200 && gcmGetVBlankCount() < k - 1; i++)
			usleep(200);
		waited = (double)(gettime() - t0) / (double)ps3_timebase_hz;
	}
	if (k)
	{
		s_hold_n++;
		s_hold_wait += waited;
	}
	s_hold_k = k;
	s_hold_mark = mark;
	queue_flip(k);
	vid_stats();
	return waited;
}
