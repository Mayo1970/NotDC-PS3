// RSX display bring-up: init, 3 flip buffers and vsync as in xashPS3 vid_ps3.c / ioQuake3-PS3 ps3_glimp.c.
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sysutil/video.h>

#include "ps3gl.h"
#include "ps3_phase.h"
#include "ps3_log.h"
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
	s_flip_done++;
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
	gcmSetFlipHandler(flip_handler);

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

// Ends a frame that must not reach the screen (render to texture): the draw
// commands still have to be flushed, but no flip is queued and the buffer stays.
void ps3_video_end_frame_noflip(void)
{
	ps3gl_end_frame();
	rsxFlushBuffer(s_ctx);
}

// Copies w*h pixels of the current back buffer into the staging buffer and
// waits for the RSX. x/y are GL window coordinates, so y counts from the bottom.
// Returns the pixels as native A8R8G8B8 rows, or NULL when the copy cannot run.
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
	SH4_PHASE(PH_READBACK);
	rsxFlushBuffer(s_ctx);
	rsxFinish(s_ctx, 1);
	SH4_PHASE(PH_RTT);
	return (const u32*)s_rb_addr;
}

void ps3_video_end_frame(void)
{
	ps3gl_end_frame();
	// WaitFlip before SetFlip: the rsx.h doc order gave a black screen on HW.
	gcmSetWaitFlip(s_ctx);
	gcmSetFlip(s_ctx, s_cur_fb);
	rsxFlushBuffer(s_ctx);

	s_flip_queued++;
	s_cur_fb = (s_cur_fb + 1) % PS3_FB_COUNT;
	s_presented++;

	double now = Platform_DoubleTime();
	if (now - s_stats_t0 >= 5.0)
	{
		printf("[vid] presented=%u skipped=%u flips queued=%u done=%u\n",
		       (unsigned)s_presented, (unsigned)s_skipped, (unsigned)s_flip_queued, (unsigned)s_flip_done);
		s_stats_t0 = now;
	}
}
