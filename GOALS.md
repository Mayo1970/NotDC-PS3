# nullDC PS3 port - goal list

Base: copy of `nullDC4Wii` (upstream folder stays untouched).
Toolchain: PSL1GHT, Docker image `ps3dev/ps3dev:latest` (ppu-gcc 7.2).
TITLE_ID: `NULLDC000`. Data root: `/dev_hdd0/data/NullDC/` (BIOS in `data/`). Debug: UDP log only.
References: `ioQuake3-PS3` (RSX, audio, pad, PKG rules), `xashPS3` (vid/snd/in/sys under `engine/platform/ps3`).
Video reuse: RSX init/flip from `xashPS3/engine/platform/ps3/vid_ps3.c` and `ioQuake3-PS3/code/sys/ps3_glimp.c`;
GL 1.1 to RSX layer `ps3gl` (`xashPS3/3rdparty/ps3gl`, `ioQuake3-PS3/code/gl`) as the base for the renderer.
Rule: no JIT on PS3. Use interpreters only.
Rule: every goal ends with a hardware test. Next goal starts only after SUCCESS.

Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

## G1 - `_PS3` build links and packages  `[x]` (HW 2026-09-18: boots, UDP log OK, clean XMB exit, ~194 MB free heap)
1. Add a PSL1GHT `Makefile` in the project root. Build in Docker with `./build.sh [pkg|self|clean]`.
2. Set up `_PS3` in `config.h` (CPU_PPC_CELL, big endian, `HOST_OS OS_PS3`).
3. Exclude from the build: SH4 dynarec (`dc/sh4/rec_v2`, `wii/dc/sh4/rec_v2`), `arm7_jit.cpp`, `wii_fastmem.cpp`, `libwiidrc`, `libwupc`, `libsicksaxis`.
4. Force the SH4 interpreter and the ARM7 cached interpreter.
5. Replace libogc calls with PS3 code or stubs (`ps3/`, `ps3/include/ogc/` timebase shim).
6. Add `ps3/` platform layer: `SYS_PROCESS_PARAM`, UDP log.
7. HW test: PKG installs, boots, prints a log line, returns to XMB cleanly.

## G2 - Headless BIOS boot  `[x]` (HW 2026-09-18: BIOS runs, PAL 22.5/50 VPS = ~45% speed; XMB exit moved to G3)
1. File layer over `/dev_hdd0/data/NullDC/` (`data/`: dc_boot.bin, dc_flash.bin, fsca-table.bin; VMU saves in the root).
2. Load `dc_boot.bin` and `dc_flash.bin`. Run the SH4 interpreter with no video.
3. Log PC, frame count, and PVR/TA activity (monitor thread `[hb]` line + SPG stats line). XMB exit stops the SH4.
4. HW test: BIOS runs and the log shows progress (frames advance, no crash).

## G3 - RSX video bring-up  `[x]` (HW 2026-09-18: 720p hue ramp at ~24 fps, PS menu + Quit Game OK)
1. Reuse `vid_ps3.c` / `ps3_glimp.c`: `rsxInit`, `videoConfigure`, 3 buffers, flip handler, vsync.
2. Bring in `ps3gl` and its prebuilt `cgcomp` shaders.
3. Drive the flip from the emulated vblank.
4. HW test: solid clear color changes per frame, no tearing, no hang. PS button menu opens and Quit Game returns to the XMB
   (headless G2 build: no menu without video output). Then drop the `[sysutil]` diagnostic line in `ps3/ps3_main.cpp`.

## G4 - Input  `[x]` (HW 2026-09-19: D-pad/A/B drive the BIOS menu, [pad] log OK, clean XMB exit. Fixed DS3 map as the Wii Sixaxis path; user_controls.cfg remap moves to G7)
1. `ioPad` polling, all 7 ports, mapped to the Maple controller.
2. Port `wii/user_controls.cpp` mapping to DS3 buttons and sticks.
3. HW test: button presses show in the log and in the BIOS menu.

## G5 - Audio  `[x]` (HW 2026-09-19: BIOS sound at correct pitch, choppy because the ring starves at ~43% emulation speed; clean XMB exit. Click-free output depends on G8 speed)
1. AICA output (44.1 kHz s16) to a 48 kHz float32 audio port.
2. Dedicated feeder thread, event-queue paced, resample in the mixer.
3. HW test: BIOS boot sound plays with no clicks.

## G6 - RSX renderer (port of `plugs/drkPvr/gxRend.cpp` onto `ps3gl`)  `[~]` (started before G4/G5 at Matt's request)
1. Untextured TA polygons (opaque list). HW test. `[x]` (HW 2026-09-18: BIOS background plane in 4:3, no hang)
2. Textures: decode, twiddle, VQ, palette, hash cache. Also draws the PT list (alpha test) and the TR list (PVR blend, unsorted) so the BIOS swirl can be checked (Matt, 2026-09-18). HW test. `[x]` (HW 2026-09-18: Date/Time + BIOS menu correct, clock from PS3 time via flash syscfg fix. Boot swirl `[x]` HW 2026-09-19: fixed by the GD-ROM no-disc spin-up BUSY status and one TA context per `TA_ISP_BASE` as in nullDC-v1; swirl debug code removed)
3. Blend modes, punch-through, translucent sorting. HW test.
4. Modifier volumes, render-to-texture, framebuffer writes. HW test.
Map GX calls to `ps3gl`. Add `cgcomp` shaders only where `ps3gl` lacks a PVR mode.

## G7 - Game boot and front end  `[ ]`
1. Replace the Wii menu (`wii/main.cpp`) with an RSX disc browser.
2. Load GDI/CDI/ISO from `/dev_hdd0/data/NullDC/` and USB (`/dev_usb000..006`).
3. Port game presets and config.
4. HW test: select a disc and boot a commercial game.

## G8 - SH4 speed: cached interpreter  `[ ]`
1. Build an SH4 cached interpreter (same method as `arm7_cached.cpp`).
2. Measure FPS on hardware only (never RPCS3).
3. HW test: speed gain against the plain interpreter, same output.

## G9 - SPE offload (optional)  `[ ]`
1. Candidates: AICA/DSP mixing, texture decode.
2. HW test per offloaded unit.

## G10 - Release hardening  `[ ]`
1. Memory ceiling probe, allocation checks, pad hot-plug, VMU saves on HDD.
2. Long soak test (command buffer wrap, memory creep).
