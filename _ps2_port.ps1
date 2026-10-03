$ErrorActionPreference = 'Stop'
$root = 'E:\Users\Matteo\Desktop\Ferrero\FreshAdventures'

function Write-Text($relative, $content) {
    $path = Join-Path $root $relative
    $parent = Split-Path -Parent $path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText($path, $content, [System.Text.UTF8Encoding]::new($false))
}

function Replace-Once($relative, $old, $new) {
    $path = Join-Path $root $relative
    $text = [System.IO.File]::ReadAllText($path)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $old = $old.Replace("`r`n", "`n").Replace("`n", $nl)
    $new = $new.Replace("`r`n", "`n").Replace("`n", $nl)
    if ($text.Contains($new)) { return }
    if (-not $text.Contains($old)) { throw "Patch anchor missing in $relative" }
    [System.IO.File]::WriteAllText($path, $text.Replace($old, $new), [System.Text.UTF8Encoding]::new($false))
}

function Collapse-Repeats($relative, $block) {
    $path = Join-Path $root $relative
    $text = [System.IO.File]::ReadAllText($path)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $block = $block.Replace("`r`n", "`n").Replace("`n", $nl)
    foreach ($separator in @($nl, $nl + $nl, '')) {
        $pair = $block + $separator + $block
        while ($text.Contains($pair)) { $text = $text.Replace($pair, $block) }
    }
    [System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))
}

function Dedupe-Pattern($relative, $pattern) {
    $path = Join-Path $root $relative
    $text = [System.IO.File]::ReadAllText($path)
    do {
        $before = $text
        $text = [regex]::Replace($text, $pattern, '$1')
    } while ($text -ne $before)
    [System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))
}

Write-Text 'src/platform/fa_backend_ps2.c' @'
/* PS2 backend: software RGB565 game surface presented as one gsKit texture. */
#include "fa/fa_platform.h"
#include "fa/fa_input.h"
#include "fa/fa_audio.h"

#include <audsrv.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <kernel.h>
#include <libpad.h>
#include <loadfile.h>
#include <malloc.h>
#include <sifrpc.h>
#include <string.h>
#include <timer.h>

#define PS2_PAD_PORTS       2
#define PS2_AUDIO_RING      8192
#define PS2_AUDIO_MASK      (PS2_AUDIO_RING - 1)
#define PS2_AUDIO_CHUNK     1024
#define PS2_AUDIO_STACK     (16 * 1024)

extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;

typedef struct {
    GSGLOBAL *gs;
    GSTEXTURE frame;
    int frame_vram;
    uint8_t pad_area[PS2_PAD_PORTS][256] __attribute__((aligned(64)));
    uint8_t pad_open[PS2_PAD_PORTS];
    uint8_t pad_mode_set[PS2_PAD_PORTS];
    volatile uint32_t audio_w;
    volatile uint32_t audio_r;
    int16_t *audio_ring;
    int16_t *audio_chunk;
    int audio_enabled;
    int audio_stop;
    int audio_thread;
    void *audio_stack;
    uint32_t audio_phase;
} ps2_state;

static float ps2_axis(uint8_t raw)
{
    float value = ((float)raw - 128.0f) / 127.0f;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    if (value > -0.15f && value < 0.15f) value = 0.0f;
    return value;
}

static void ps2_feed_pad(fa_input *in, int slot, const struct padButtonStatus *d,
                         int connected)
{
    uint16_t down = connected ? (uint16_t)~d->btns : 0;
    fa_input_set_pad_connected_slot(in, slot, connected);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_A, (down & PAD_CROSS) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_B, (down & PAD_CIRCLE) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_X, (down & PAD_SQUARE) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_Y, (down & PAD_TRIANGLE) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_BACK, (down & PAD_SELECT) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_START, (down & PAD_START) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_LEFTSTICK, (down & PAD_L3) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_RIGHTSTICK, (down & PAD_R3) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_LEFTSHOULDER, (down & PAD_L1) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_RIGHTSHOULDER, (down & PAD_R1) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_DPAD_UP, (down & PAD_UP) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_DPAD_DOWN, (down & PAD_DOWN) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_DPAD_LEFT, (down & PAD_LEFT) != 0);
    fa_input_set_pad_button_slot(in, slot, FA_PAD_DPAD_RIGHT, (down & PAD_RIGHT) != 0);
    fa_input_set_pad_axis_slot(in, slot, FA_PAD_AXIS_LEFT_X,
                               connected ? ps2_axis(d->ljoy_h) : 0.0f);
    fa_input_set_pad_axis_slot(in, slot, FA_PAD_AXIS_LEFT_Y,
                               connected ? -ps2_axis(d->ljoy_v) : 0.0f);
    fa_input_set_pad_axis_slot(in, slot, FA_PAD_AXIS_RIGHT_X,
                               connected ? ps2_axis(d->rjoy_h) : 0.0f);
    fa_input_set_pad_axis_slot(in, slot, FA_PAD_AXIS_RIGHT_Y,
                               connected ? -ps2_axis(d->rjoy_v) : 0.0f);
}

static int ps2_video_init(ps2_state *s)
{
    s->gs = gsKit_init_global();
    if (!s->gs) return -1;

    s->gs->PSM = GS_PSM_CT16;
    s->gs->PSMZ = GS_PSMZ_16S;
    s->gs->ZBuffering = GS_SETTING_OFF;
    s->gs->DoubleBuffering = GS_SETTING_ON;
    s->gs->PrimAlphaEnable = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(s->gs);
    gsKit_mode_switch(s->gs, GS_ONESHOT);

    memset(&s->frame, 0, sizeof s->frame);
    s->frame.Width = FA_FB_W;
    s->frame.Height = FA_FB_H;
    s->frame.PSM = GS_PSM_CT16;
    s->frame.Filter = GS_FILTER_NEAREST;
    s->frame.Vram = gsKit_vram_alloc(s->gs,
        gsKit_texture_size(s->frame.Width, s->frame.Height, s->frame.PSM),
        GSKIT_ALLOC_USERBUFFER);
    if (s->frame.Vram == GSKIT_ALLOC_ERROR) return -1;
    s->frame_vram = (int)s->frame.Vram;
    return 0;
}

static void ps2_present(fa_platform *p, const uint16_t *px, int w, int h,
                        size_t pitch)
{
    ps2_state *s = (ps2_state *)p->impl;
    int dst_w, dst_h, dst_x, dst_y;
    (void)pitch;
    if (!s || !s->gs || !px || w != FA_FB_W || h != FA_FB_H) return;

    /* The GS texture upload uses GIF DMA; publish the CPU-rendered frame first. */
    s->frame.Mem = (void *)px;
    FlushCache(0);
    gsKit_texture_upload(s->gs, &s->frame);

    /* Keep the original 4:3 canvas inside the GS's 640x448/512 output. */
    dst_h = s->gs->Height;
    dst_w = dst_h * 4 / 3;
    if (dst_w > s->gs->Width) {
        dst_w = s->gs->Width;
        dst_h = dst_w * 3 / 4;
    }
    dst_x = (s->gs->Width - dst_w) / 2;
    dst_y = (s->gs->Height - dst_h) / 2;
    gsKit_clear(s->gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
    gsKit_prim_sprite_texture(s->gs, &s->frame,
        (float)dst_x, (float)dst_y, 0.0f, 0.0f,
        (float)(dst_x + dst_w), (float)(dst_y + dst_h),
        (float)w, (float)h, 0, 0x80808080);
    gsKit_queue_exec(s->gs);
    gsKit_sync_flip(s->gs);
}

static int ps2_audio_worker(void *arg)
{
    ps2_state *s = (ps2_state *)arg;
    while (!s->audio_stop) {
        uint32_t r = s->audio_r;
        uint32_t w = s->audio_w;
        uint32_t ready = (w - r) & PS2_AUDIO_MASK;
        if (ready == 0) {
            SleepThread();
            continue;
        }

        uint32_t count = ready;
        uint32_t contiguous = PS2_AUDIO_RING - (r & PS2_AUDIO_MASK);
        if (count > contiguous) count = contiguous;
        if (count > PS2_AUDIO_CHUNK) count = PS2_AUDIO_CHUNK;
        int bytes = (int)count * 2 * (int)sizeof(int16_t);
        if (audsrv_wait_audio(bytes) != AUDSRV_ERR_NOERROR) {
            DelayThread(1000);
            continue;
        }

        memcpy(s->audio_chunk, s->audio_ring + (r & PS2_AUDIO_MASK) * 2u,
               (size_t)bytes);
        FlushCache(0);
        int sent = audsrv_play_audio((const char *)s->audio_chunk, bytes);
        if (sent > 0) {
            uint32_t consumed = (uint32_t)sent /
                                (2u * (uint32_t)sizeof(int16_t));
            __sync_synchronize();
            s->audio_r = (r + consumed) & PS2_AUDIO_MASK;
        }
    }
    ExitThread();
    return 0;
}

static int ps2_audio_init(ps2_state *s)
{
    struct audsrv_fmt_t fmt;
    ee_thread_t thread;
    int result = 0;

    if (SifLoadModule("rom0:LIBSD", 0, NULL) < 0) return -1;
    if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &result) < 0 ||
        result < 0)
        return -1;
    if (audsrv_init() != AUDSRV_ERR_NOERROR) return -1;

    fmt.freq = FA_AUDIO_RATE;
    fmt.bits = 16;
    fmt.channels = 2;
    if (audsrv_set_format(&fmt) != AUDSRV_ERR_NOERROR) {
        audsrv_quit();
        return -1;
    }
    audsrv_set_volume(100);

    s->audio_ring = (int16_t *)memalign(64,
        PS2_AUDIO_RING * 2u * sizeof(int16_t));
    s->audio_chunk = (int16_t *)memalign(64,
        PS2_AUDIO_CHUNK * 2u * sizeof(int16_t));
    s->audio_stack = memalign(64, PS2_AUDIO_STACK);
    if (!s->audio_ring || !s->audio_chunk || !s->audio_stack) goto fail;
    memset(s->audio_ring, 0, PS2_AUDIO_RING * 2u * sizeof(int16_t));

    memset(&thread, 0, sizeof thread);
    thread.func = ps2_audio_worker;
    thread.stack = s->audio_stack;
    thread.stack_size = PS2_AUDIO_STACK;
    thread.initial_priority = 40;
    s->audio_thread = CreateThread(&thread);
    if (s->audio_thread < 0) goto fail;
    s->audio_enabled = 1;
    if (StartThread(s->audio_thread, s) < 0) {
        s->audio_enabled = 0;
        DeleteThread(s->audio_thread);
        s->audio_thread = -1;
        goto fail;
    }
    return 0;

fail:
    free(s->audio_ring); s->audio_ring = NULL;
    free(s->audio_chunk); s->audio_chunk = NULL;
    free(s->audio_stack); s->audio_stack = NULL;
    audsrv_quit();
    return -1;
}

static int ps2_audio_want(fa_platform *p)
{
    ps2_state *s = (ps2_state *)p->impl;
    if (!s || !s->audio_enabled) return 0;
    if (s->gs->Mode == GS_MODE_PAL) return FA_AUDIO_RATE / 50;
    /* NTSC field rate is 60000/1001 Hz; alternate 735/736 sample blocks. */
    s->audio_phase += (uint32_t)(FA_AUDIO_RATE * 1001);
    int want = (int)(s->audio_phase / 60000u);
    s->audio_phase %= 60000u;
    return want;
}

static int ps2_audio_push(fa_platform *p, const int16_t *frames, int n)
{
    ps2_state *s = (ps2_state *)p->impl;
    if (!s || !s->audio_enabled || !frames || n <= 0) return 0;
    uint32_t w = s->audio_w;
    uint32_t r = s->audio_r;
    int written = 0;
    while (written < n) {
        uint32_t next = (w + 1u) & PS2_AUDIO_MASK;
        if (next == r) break;
        s->audio_ring[(w & PS2_AUDIO_MASK) * 2u] = frames[written * 2];
        s->audio_ring[(w & PS2_AUDIO_MASK) * 2u + 1u] = frames[written * 2 + 1];
        w = next;
        written++;
    }
    __sync_synchronize();
    s->audio_w = w;
    if (written) iWakeupThread(s->audio_thread);
    return written;
}

static int ps2_pump(fa_platform *p, struct fa_input *in)
{
    ps2_state *s = (ps2_state *)p->impl;
    for (int port = 0; port < PS2_PAD_PORTS; port++) {
        struct padButtonStatus data;
        int state;
        int slot = port;
        if (!s->pad_open[port])
            s->pad_open[port] = padPortOpen(port, 0, s->pad_area[port]) != 0;
        if (!s->pad_open[port]) {
            ps2_feed_pad(in, slot, NULL, 0);
            continue;
        }

        state = padGetState(port, 0);
        if (state != PAD_STATE_STABLE || !padRead(port, 0, &data)) {
            ps2_feed_pad(in, slot, NULL, 0);
            continue;
        }
        if (!s->pad_mode_set[port]) {
            padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
            s->pad_mode_set[port] = 1;
        }
        ps2_feed_pad(in, slot, &data, 1);
        /* Start + Select is the platform quit chord in PCSX2 and ps2link. */
        if ((uint16_t)~data.btns & PAD_START &&
            (uint16_t)~data.btns & PAD_SELECT)
            return 1;
    }
    return 0;
}

static uint64_t ps2_now_ns(fa_platform *p)
{
    (void)p;
    return GetTimerSystemTime() * 1000ull;
}

/* gsKit_sync_flip already blocks on field sync; do not sleep a second time. */
static void ps2_wait(fa_platform *p, uint64_t ns)
{
    (void)p;
    (void)ns;
}

static void ps2_shutdown(fa_platform *p)
{
    ps2_state *s;
    if (!p || !p->impl) return;
    s = (ps2_state *)p->impl;
    if (s->audio_enabled) {
        s->audio_stop = 1;
        iWakeupThread(s->audio_thread);
        WaitThread(s->audio_thread, NULL);
        DeleteThread(s->audio_thread);
        audsrv_quit();
    }
    for (int port = 0; port < PS2_PAD_PORTS; port++)
        if (s->pad_open[port]) padPortClose(port, 0);
    padEnd();
    free(s->audio_ring);
    free(s->audio_chunk);
    free(s->audio_stack);
    if (s->gs) gsKit_deinit_global(s->gs);
    free(s);
    p->impl = NULL;
}

static const fa_platform_vtbl PS2_VT = {
    ps2_pump,
    ps2_present,
    ps2_audio_want,
    ps2_audio_push,
    ps2_now_ns,
    ps2_wait,
    ps2_shutdown
};

int fa_backend_ps2_create(fa_platform *p, const fa_platform_cfg *cfg)
{
    if (!p) return -1;
    ps2_state *s = (ps2_state *)calloc(1, sizeof *s);
    if (!s) return -1;
    s->audio_thread = -1;

    SifInitRpc(0);
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0 ||
        SifLoadModule("rom0:PADMAN", 0, NULL) < 0 || padInit(0) == 0) {
        free(s);
        return -1;
    }
    for (int port = 0; port < PS2_PAD_PORTS; port++)
        s->pad_open[port] = padPortOpen(port, 0, s->pad_area[port]) != 0;

    if (ps2_video_init(s) != 0) {
        if (s->gs) gsKit_deinit_global(s->gs);
        free(s);
        return -1;
    }
    if (!cfg || cfg->want_audio) {
        if (ps2_audio_init(s) != 0)
            printf("ps2: audsrv unavailable; continuing without audio\n");
    }

    memset(p, 0, sizeof *p);
    p->vt = &PS2_VT;
    p->impl = s;
    p->width = FA_FB_W;
    p->height = FA_FB_H;
    p->audio_rate = FA_AUDIO_RATE;
    p->audio_channels = 2;
    p->name = "ps2-gskit";
    printf("ps2: gsKit %dx%d framebuffer, audio=%s\n", s->gs->Width,
           s->gs->Height, s->audio_enabled ? "on" : "off");
    return 0;
}
'@

Write-Text 'src/platform/fa_paths_ps2.c' @'
/* PS2 writable user-data location. PCSX2/ps2link use hostfs; hardware builds
 * default to USB mass storage. */
#include "fa/fa_vfs.h"
#include <stdio.h>

int fa_user_dir(const char *app, char *out, size_t out_sz)
{
    int n;
    if (!app || !app[0] || !out || out_sz == 0) return -1;
#if defined(FA_PS2_STORAGE_HOSTFS)
    n = snprintf(out, out_sz, "host:%s", app);
#else
    n = snprintf(out, out_sz, "mass0:/%s", app);
#endif
    if (n < 0 || (size_t)n >= out_sz) return -1;
    return fa_vfs_mkdirs(out);
}
'@

Write-Text 'platform/ps2/source/main.c' @'
/* PS2 executable entry. Keep the game entry point shared with the Switch/PS3
 * builds so game behavior remains platform independent. */
#include "../../../tools/fa_slice.c"
'@

Write-Text 'platform/ps2/Makefile' @'
# Fresh Adventures PS2 homebrew target (ps2sdk + gsKit).
PS2DEV ?= /usr/local/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT  ?= $(PS2DEV)/gsKit
EE_CC  ?= $(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-gcc
BIN2S  ?= $(PS2SDK)/bin/bin2s

ROOT := ../..
BUILD := build
TARGET := $(BUILD)/freshadventures.elf
HOSTFS ?= 1

VPATH := source $(ROOT)/src/app $(ROOT)/src/core $(ROOT)/src/game $(ROOT)/src/platform
SOURCES := main.c \
  fa_app.c fa_loop.c fa_surface.c fa_script.c fa_aom.c fa_res.c fa_vfs.c \
  fa_input.c fa_w01.c fa_w02.c fa_map.c fa_render.c fa_entity.c fa_bmp.c \
  fa_menu.c fa_hiscore.c fa_options.c fa_save.c fa_wav.c fa_audio.c fa_rng.c \
  fa_player.c fa_charspr.c fa_collide.c fa_beh.c fa_hud.c fa_death.c fa_credits.c \
  fa_platform.c fa_backend_null.c fa_backend_sdl2.c fa_backend_ps2.c fa_paths_ps2.c
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o)) $(BUILD)/audsrv.o

EE_FLAGS := -O2 -G0 -Wall -Wextra -std=gnu11 -D_EE -D__PS2__ -DFA_PS2_BUILD \
  -march=r5900 -mhard-float -msingle-float -mno-shared -mno-llsc -mplt \
  -fno-common -fsingle-precision-constant \
  -I$(ROOT)/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include \
  -I$(PS2SDK)/ports/include -I$(GSKIT)/include \
  -I$(GSKIT)/ee/gs/include -I$(GSKIT)/ee/dma/include
ifeq ($(HOSTFS),1)
EE_FLAGS += -DFA_PS2_STORAGE_HOSTFS
endif
EE_LIBS := -L$(PS2SDK)/ee/lib -L$(PS2SDK)/ports/lib -L$(GSKIT)/lib \
  -lgskit -ldmakit -lpad -laudsrv -lpatches -lm
EE_LDFLAGS := -T$(PS2SDK)/ee/startup/linkfile -Wl,-zmax-page-size=128

.PHONY: all clean smoke
all: $(TARGET)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(EE_CC) $(EE_FLAGS) -c $< -o $@

$(BUILD)/audsrv.s: $(PS2SDK)/iop/irx/audsrv.irx | $(BUILD)
	$(BIN2S) $< $@ audsrv_irx

$(BUILD)/audsrv.o: $(BUILD)/audsrv.s
	$(EE_CC) $(EE_FLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(EE_CC) $(EE_FLAGS) $(EE_LDFLAGS) -Wl,-Map,$(BUILD)/freshadventures.map \
	  -o $@ $^ $(EE_LIBS)

# Run 120 frames in PCSX2 against the user's mounted GData and BIOS directories.
PCSX2 ?= /opt/pcsx2/AppRun
smoke: $(TARGET)
	test -f $(BUILD)/GData/Scripts/EngineInit.jrs
	test -n "$$(find /opt/pcsx2/bios -type f -iname '*.bin' -print -quit)"
	QT_QPA_PLATFORM=offscreen $(PCSX2) -portable -nogui -fastboot \
	  -elf $(abspath $(TARGET)) -gameargs "--gdata host:GData --frames 120" \
	  -logfile $(abspath $(BUILD))/pcsx2-smoke.log

clean:
	rm -f $(OBJECTS) $(BUILD)/audsrv.s $(TARGET) $(BUILD)/freshadventures.map
'@

Write-Text 'platform/ps2/Dockerfile' @'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive \
    PS2DEV=/usr/local/ps2dev \
    PS2SDK=/usr/local/ps2dev/ps2sdk \
    GSKIT=/usr/local/ps2dev/gsKit

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates curl file jq make python3 tar xz-utils \
    libfuse2t64 libfuse3-3 libgl1 libpulse0 libxkbcommon0 libxkbcommon-x11-0 \
    libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 \
    libxcb-xkb1 libxcb-randr0 libx11-6 libxext6 && \
    rm -rf /var/lib/apt/lists/*

# Official ps2dev prebuilt Linux toolchain, ps2sdk, ps2sdk-ports and gsKit.
RUN mkdir -p "$PS2DEV" && \
    curl -fL https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-latest.tar.gz \
      | tar -xz --strip-components=1 -C "$PS2DEV"

# Use the official PCSX2 Linux AppImage, extracted so FUSE is not needed at runtime.
RUN python3 - <<'PY'
import json, os, urllib.request
api = 'https://api.github.com/repos/PCSX2/pcsx2/releases/latest'
req = urllib.request.Request(api, headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'openfa-ps2-image'})
with urllib.request.urlopen(req) as r:
    release = json.load(r)
assets = [a for a in release['assets'] if a['name'].lower().endswith('.appimage') and 'linux' in a['name'].lower() and 'qt' in a['name'].lower()]
if not assets:
    raise SystemExit('PCSX2 release has no Linux Qt AppImage')
asset = assets[0]
urllib.request.urlretrieve(asset['browser_download_url'], '/tmp/pcsx2.AppImage')
PY
RUN chmod +x /tmp/pcsx2.AppImage && cd /opt && \
    /tmp/pcsx2.AppImage --appimage-extract >/dev/null && \
    mv /opt/squashfs-root /opt/pcsx2 && rm /tmp/pcsx2.AppImage

ENV PATH="$PATH:/usr/local/ps2dev/bin:/usr/local/ps2dev/ee/bin:/usr/local/ps2dev/iop/bin:/usr/local/ps2dev/dvp/bin:/usr/local/ps2dev/ps2sdk/bin" \
    QT_QPA_PLATFORM=offscreen
WORKDIR /src
ENTRYPOINT ["/usr/local/bin/openfa-ps2"]
CMD ["shell"]

RUN cat > /usr/local/bin/openfa-ps2 <<'SH' && chmod +x /usr/local/bin/openfa-ps2
#!/bin/sh
set -eu
case "${1:-shell}" in
  build)
    shift
    exec make -C /src/platform/ps2 "$@"
    ;;
  smoke)
    shift
    exec make -C /src/platform/ps2 smoke "$@"
    ;;
  pcsx2)
    shift
    exec /opt/pcsx2/AppRun -portable "$@"
    ;;
  shell)
    exec /bin/bash
    ;;
  *)
    exec "$@"
    ;;
esac
SH

RUN useradd --create-home --uid 1000 dev && \
    mkdir -p /home/dev/.config/PCSX2/bios && \
    chown -R dev:dev /opt/pcsx2 /home/dev
USER dev
'@

Write-Text 'platform/ps2/README.md' @'
# Fresh Adventures for PlayStation 2

The PS2 target keeps the shared 800x600 RGB565 software renderer. gsKit uploads
the completed frame as one CT16 texture and scales it into a centered 4:3 GS
display area. libpad maps two PS2 controller ports; audsrv streams the existing
44.1 kHz mixer on a worker thread so IOP audio RPCs do not stall rendering.

## Build and PCSX2 smoke run (Windows + Docker Desktop)

From the repository root, build with the toolchain image:

```powershell
docker build -f platform/ps2/Dockerfile -t openfa-ps2 platform/ps2
New-Item -ItemType Directory -Force platform/ps2/build | Out-Null
docker run --rm -v "${PWD}:/src" openfa-ps2 build
```

Then mount the user's BIOS directory read-only and GData beside the ELF. The
image never copies either into its layers:

```powershell
$bios = 'E:\Users\Matteo\Desktop\Emulatori\PCSX2 1.6.0\bios'
$gdata = (Resolve-Path GData).Path
docker run --rm `
  -v "${PWD}:/src" `
  -v "${gdata}:/src/platform/ps2/build/GData:ro" `
  -v "${bios}:/opt/pcsx2/bios:ro" `
  openfa-ps2 smoke
```

The smoke target boots the ELF in PCSX2 for 120 rendered frames using
`host:GData`, then exits. `HOSTFS=0` selects `mass0:/FreshAdventures` for a
hardware build instead of PCSX2/ps2link host storage. The output is
`platform/ps2/build/freshadventures.elf`.

PCSX2's headless smoke run checks startup and asset loading; it is not a PS2
hardware frame-time measurement. The current renderer remains CPU-bound and
needs on-console profiling before stable hardware frame pacing can be claimed.
'@

Replace-Once 'include/fa/fa_platform.h' @'
/* PlayStation 3/PSL1GHT backend. Returns 0 on success, -1 on init failure. */
int fa_backend_ps3_create(fa_platform *p, const fa_platform_cfg *cfg);
'@ @'
/* PlayStation 3/PSL1GHT backend. Returns 0 on success, -1 on init failure. */
int fa_backend_ps3_create(fa_platform *p, const fa_platform_cfg *cfg);

/* PlayStation 2/ps2sdk + gsKit backend. */
int fa_backend_ps2_create(fa_platform *p, const fa_platform_cfg *cfg);
'@

Replace-Once 'src/platform/fa_platform.c' @'
/* fa_platform.c - backend selection: Switch and PS3 init their own display path;
 * desktop builds try SDL2, then fall back to the null backend. */
'@ @'
/* fa_platform.c - select the platform display/input/audio backend. */
'@
Replace-Once 'src/platform/fa_platform.c' @'
#elif defined(__PPU__)
    return fa_backend_ps3_create(p, cfg);
#else
'@ @'
#elif defined(__PPU__)
    return fa_backend_ps3_create(p, cfg);
#elif defined(__PS2__)
    return fa_backend_ps2_create(p, cfg);
#else
'@

Replace-Once 'tools/fa_slice.c' @'
#elif defined(__PPU__)
#else
#  include <unistd.h>   /* readlink - exe-dir GData probe on POSIX */
'@ @'
#elif defined(__PPU__)
#elif defined(__PS2__)
#else
#  include <unistd.h>   /* readlink - exe-dir GData probe on POSIX */
'@
Replace-Once 'tools/fa_slice.c' @'
#elif defined(__PPU__)
    /* argv[0] is .../USRDIR/EBOOT.BIN: GData beside it, then USB roots */
'@ @'
#elif defined(__PS2__)
    /* PCSX2 and ps2link provide hostfs beside the ELF; retail storage uses USB. */
    {
        const char *cands[] = {
            "host:GData", "mass0:/GData", "mass1:/GData",
            "cdfs:/GData", "cdrom0:/GData", "mc0:/GData"
        };
        for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
            if (dir_has_gdata(cands[i])) {
                snprintf(out, cap, "%s", cands[i]);
                return;
            }
        }
    }
#elif defined(__PPU__)
    /* argv[0] is .../USRDIR/EBOOT.BIN: GData beside it, then USB roots */
'@

Replace-Once 'src/core/fa_vfs.c' @'
#if defined(__PPU__)
/* PS3: LV2 mkdir on an existing mount root (/dev_hdd0, /dev_usb000) may not report
 * EEXIST, which failed every Data/ path; skip directories that already exist */
'@ @'
#if defined(__PS2__)
/* Device roots such as mass0: and host: are mount points, not directories. */
static int ps2_device_root(const char *path)
{
    const char *colon = strchr(path, ':');
    return colon && colon[1] == '\0';
}
#endif

#if defined(__PPU__)
/* PS3: LV2 mkdir on an existing mount root (/dev_hdd0, /dev_usb000) may not report
 * EEXIST, which failed every Data/ path; skip directories that already exist */
'@
Replace-Once 'src/core/fa_vfs.c' @'
            if (buf[0] && !(p - buf == 2 && buf[1] == ':')) {
#if defined(__PPU__)
'@ @'
            if (buf[0] && !(p - buf == 2 && buf[1] == ':') &&
#if defined(__PS2__)
                !ps2_device_root(buf)
#else
                1
#endif
            ) {
#if defined(__PPU__)
'@
Replace-Once 'src/core/fa_vfs.c' @'
#if defined(__PPU__)
    if (ps3_mkdir(buf) != 0) return -1;
#else
    if (FA_MKDIR(buf) != 0 && errno != EEXIST) return -1;
#endif
    return 0;
'@ @'
#if defined(__PPU__)
    if (ps3_mkdir(buf) != 0) return -1;
#elif defined(__PS2__)
    if (!ps2_device_root(buf) && FA_MKDIR(buf) != 0 && errno != EEXIST) return -1;
#else
    if (FA_MKDIR(buf) != 0 && errno != EEXIST) return -1;
#endif
    return 0;
'@

Collapse-Repeats 'include/fa/fa_platform.h' @'
/* PlayStation 2/ps2sdk + gsKit backend. */
int fa_backend_ps2_create(fa_platform *p, const fa_platform_cfg *cfg);
'@
Collapse-Repeats 'src/core/fa_vfs.c' @'
#if defined(__PS2__)
/* Device roots such as mass0: and host: are mount points, not directories. */
static int ps2_device_root(const char *path)
{
    const char *colon = strchr(path, ':');
    return colon && colon[1] == '\0';
}
#endif
'@
Collapse-Repeats 'tools/fa_slice.c' @'
#elif defined(__PS2__)
    /* PCSX2 and ps2link provide hostfs beside the ELF; retail storage uses USB. */
    {
        const char *cands[] = {
            "host:GData", "mass0:/GData", "mass1:/GData",
            "cdfs:/GData", "cdrom0:/GData", "mc0:/GData"
        };
        for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
            if (dir_has_gdata(cands[i])) {
                snprintf(out, cap, "%s", cands[i]);
                return;
            }
        }
    }
'@
Dedupe-Pattern 'include/fa/fa_platform.h' '(?m)(/\* PlayStation 2/ps2sdk \+ gsKit backend\. \*/\r?\nint fa_backend_ps2_create\(fa_platform \*p, const fa_platform_cfg \*cfg\);\r?\n)(?:\r?\n)\1'
Dedupe-Pattern 'src/core/fa_vfs.c' '(?s)(#if defined\(__PS2__\)\r?\n/\* Device roots.*?#endif)(?:\r?\n){1,2}\1'
Dedupe-Pattern 'tools/fa_slice.c' '(?s)(#elif defined\(__PS2__\)\r?\n    /\* PCSX2 and ps2link.*?        \}\r?\n    \}\r?\n)(?:\1)+'

Write-Host 'PS2 source files staged.'
