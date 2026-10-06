# nullDC PS3 port - goal list

Base: copy of `nullDC4Wii` (upstream folder stays untouched).
Toolchain: PSL1GHT, Docker image `ps3dev-latest:latest` (ppu-gcc 7.2), run by `./build.sh`.
TITLE_ID: `NULLDC000`. Data root: `/dev_hdd0/data/NullDC/` (BIOS in `data/`). VMU saves: `/dev_hdd0/savedata/vmu/`. Debug: UDP log only.
References: `ioQuake3-PS3` (RSX, audio, pad, PKG rules), `xashPS3` (vid/snd/in/sys under `engine/platform/ps3`).
Video reuse: RSX init/flip from `xashPS3/engine/platform/ps3/vid_ps3.c` and `ioQuake3-PS3/code/sys/ps3_glimp.c`;
GL 1.1 to RSX layer `ps3gl` (`xashPS3/3rdparty/ps3gl`, `ioQuake3-PS3/code/gl`) as the base for the renderer.
JIT: generated code runs only from a code array in `.text`; RW memory never executes. The first rule here, "no JIT
on PS3", was wrong: the SH4 dynarec and the ARM7 JIT run that way.
Rule: every goal ends with a hardware test. Next goal starts only after SUCCESS.

Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Speed work and open items after G8: `OPTIMIZATION_PLAN.md` (section 0 has the state) and
`OPTIMIZATION_POTENTIAL.md` (P1-P15).

## G1 - `_PS3` build links and packages  `[x]` (HW 2026-09-18: boots, UDP log OK, clean XMB exit, ~194 MB free heap)
1. Add a PSL1GHT `Makefile` in the project root. Build in Docker with `./build.sh [pkg|self|clean]`.
2. Set up `_PS3` in `config.h` (CPU_PPC_CELL, big endian, `HOST_OS OS_PS3`).
3. Exclude from the build: SH4 dynarec (`dc/sh4/rec_v2`, `wii/dc/sh4/rec_v2`), `arm7_jit.cpp`, `wii_fastmem.cpp`, `libwiidrc`, `libwupc`, `libsicksaxis`.
4. Force the SH4 interpreter and the ARM7 cached interpreter.
   Steps 3-4 held for G1-G7 only: the SH4 dynarec (G8) and the ARM7 JIT (plan 5.4) were ported later.
5. Replace libogc calls with PS3 code or stubs (`ps3/`, `ps3/include/ogc/` timebase shim).
6. Add `ps3/` platform layer: `SYS_PROCESS_PARAM`, UDP log.
7. HW test: PKG installs, boots, prints a log line, returns to XMB cleanly.

## G2 - Headless BIOS boot  `[x]` (HW 2026-09-18: BIOS runs, PAL 22.5/50 VPS = ~45% speed; XMB exit moved to G3)
1. File layer over `/dev_hdd0/data/NullDC/` (`data/`: dc_boot.bin, dc_flash.bin, fsca-table.bin; VMU saves were in the root until 2026-10-04, now `/dev_hdd0/savedata/vmu/`).
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

Later: the sound runs on SPEs (SPE AICA Sound and SPE ARM7, default ON since 2026-10-05; see G9). Open sound
faults: `OPTIMIZATION_PLAN.md` section 6 (parked).

## G6 - RSX renderer (port of `plugs/drkPvr/gxRend.cpp` onto `ps3gl`)  `[x]` (started before G4/G5 at Matt's request; steps 1-4 HW validated)
1. Untextured TA polygons (opaque list). HW test. `[x]` (HW 2026-09-18: BIOS background plane in 4:3, no hang)
2. Textures: decode, twiddle, VQ, palette, hash cache. Also draws the PT list (alpha test) and the TR list (PVR blend, unsorted) so the BIOS swirl can be checked (Matt, 2026-09-18). HW test. `[x]` (HW 2026-09-18: Date/Time + BIOS menu correct, clock from PS3 time via flash syscfg fix. Boot swirl `[x]` HW 2026-09-19: fixed by the GD-ROM no-disc spin-up BUSY status and one TA context per `TA_ISP_BASE` as in nullDC-v1; swirl debug code removed)
3. Blend modes, punch-through, translucent sorting. HW test. `[x]` (HW 2026-09-20: BIOS menu models correct, ~45% speed.
   Translucent autosort follows nullDC-v1 `SortPParams` (AlphaSortMode 1, its default): whole polygons ordered by the
   average 1/W. Per-triangle sorting (its mode 2) was tried first and drew model back faces over front faces. Added the
   nullDC-v1 `GPstate_cache` to `apply_param`. Blend factor tables, depth compare, PT alpha ref and `IgnoreTexA` were
   checked against nullDC-v1 and already matched, but the BIOS menu is TR-only, so the OP and PT paths run first in G7)
4. Modifier volumes, render-to-texture, framebuffer writes. HW test. `[x]` (HW 2026-09-20: BIOS unchanged, ~45% speed,
   `fb=0` so the framebuffer marker never misfires, `gcmMapMainMemory=0` so the readback buffer maps)
   - Modifier volumes: ported from nullDC-v1 `SetMVS_Mode` (gxRend has empty stubs). Opaque volume list only, stencil bit 1
     counts crossings, bit 0 folds a closed volume, then a flat 50% black quad over the pixels inside.
   - Render to texture: the gxRend approach, not the D3D one. The pass draws at the `FB_X_CLIP`/`FB_Y_CLIP` size without
     flipping, `rsxSetTransferData(LOCAL_TO_MAIN)` reads it back, and it is packed into VRAM at `FB_W_SOF1` so the texture
     cache re-decodes on its hash. `gcmMapMainMemory` needs a 1 MB aligned buffer.
   - Framebuffer writes: gxRend magic word `0xDC4F0FB0` stamped at `FB_W_SOF1`/`FB_W_SOF2` after a render; `VBlank()` shows
     the VRAM image when the mark is gone at `FB_R_SOF1`.
   - Not exercised on HW yet: the BIOS sends no modifier volumes and no bit-24 pass (`mv_tris=0`, `rtt=0/0`). Both get their
     first real test when a game boots in G7. Pack/read modes 24-bit and mode 4/5 log once and skip.
Map GX calls to `ps3gl`. Add `cgcomp` shaders only where `ps3gl` lacks a PVR mode.
Open: no game has used render to texture yet (`rtt=0/0`). Fog and offset colour need a PVR shader
(`OPTIMIZATION_PLAN.md` 3a, 3b, shelved).

## G7 - Game boot and front end  `[x]` (HW 2026-09-20: Crazy Taxi and ChuChu Rocket boot and play correctly, ~45% speed / ~12 RPS)
1. Replace the Wii menu (`wii/main.cpp`) with an RSX disc browser. `[x]`
   - `ps3/ps3_ui.cpp`: text and quad drawing on `ps3gl`, 8x8 bitmap font (`ps3/ps3_font.h`)
     uploaded as one 128x64 RGBA texture. `ps3_video_init()` is idempotent, so the front end
     brings the RSX up before `EmuMain` and `InitRenderer` reuses it.
   - `ps3/ps3_menu.cpp`: browser, 3 option pages and a controls page. Every connected pad
     drives it; each port keeps its own `padData` because `ioPadGetData` does not refresh the
     buffer with no new input (same rule as `ps3_input.cpp`).
2. Load discs from `/dev_hdd0/data/NullDC/discs/` and USB (`/dev_usb000..006`). `[x]`
   `plugs/ImgReader` has three drives: CDI, NRG/MDS/MDF and "ISO reader", but the last one
   only accepts `.gdi` - plain `.iso`, `.bin` and `.cue` reach no reader at all. The browser
   therefore lists `.gdi`, `.cdi`, `.mds`, `.nrg` and `.elf`, and `iso_init` now compares the
   extension case-insensitively so a `.GDI` still loads. The browser only fills
   `selectedFilePath`. Empty path = boot the BIOS with no disc, as in G2-G6.
3. Port game presets and config. `[x]`
   nullDC-v1 has NO per-game preset system: its whole config is `nullDC.cfg` through the
   `cfg*` API (`[nullDC]`, `[nullDC_plugins]`), read by `LoadSettings()` and written by
   `SaveSettings()`. `game_presets.cfg` is a NullDC4Wii invention, so it is NOT ported
   (Matt, 2026-09-20). The option pages read and write `[ps3]` in the same file through
   `cfgLoadInt`/`cfgSaveInt` (`ps3/ps3_presets.cpp`). `cfgOpen()` now returns early on a
   second call, since the front end opens the cfg before `EmuMain` does.
   The DS3 `user_controls.cfg` remap is deferred to after the G7 hardware test (Matt).
4. HW test: select a disc and boot a commercial game. `[x]`
   HW 2026-09-20: the browser listed the games; **ChuChu Rocket (CDI V3.5) and Crazy Taxi
   (CDI V3) both boot and play** at ~46% speed. Crazy Taxi switches PAL 480i to NTSC 480i by
   itself, runs from RAM (`pc=0C15A670`), and draws 2092 vertices per render over 240 opaque
   and 283 translucent strips with 52 live textures. Pad input reaches the game (`[pad] dc0`).
   The opaque list ran on hardware for the first time here, and the framebuffer-write present
   fired (`fb=1`). Modifier volumes ran on hardware for the first time later in the same
   session (`mv_tris op=256`, no hang). Render to texture is still unexercised (`rtt=0/0`).
   Crazy Taxi failed on the first attempt. Cause found offline against the real image, with no
   console round trip: `cdi_init` applied the V3.5 header rule to V3. cdirip `CDI_init` stores
   the header as a length back from the end of the file for V3.5 only; V2 and V3 store an
   absolute offset. The V3 file therefore seeked to `size - hdr_offset` = 809, read zeros and
   rejected the image on `session_count == 0`. Fixed in `plugs/ImgReader/cdi.cpp`. The `[CDI]`
   parser logs are now on; the per-sector logs above `cdi_DriveGetTocInfo` stay off so the UDP
   log cannot flood.
5. Texture correctness, found after the first game boot (Matt: glitched textures, wrong tree
   transparency). `[x]` HW 2026-09-20: both fixed, "It looked fine! Even the trees."
   - Cause: mipmapped VQ textures took the codebook from the wrong address. nullDC-v1
     `twidle_tex` keeps the codebook at the texture address and moves only the index map by
     `MipPoint[TexU]`, then by the 2 KB book; `ps3_tex.cpp` advanced the address first and used
     it for both. The block structure survived but every colour AND alpha came from the wrong
     codebook entry, which is why the tree cutouts looked wrong. `hash_texture` had the same
     fault, so a changed codebook never forced a re-decode. Added `TexDesc::vq_cb`.
   - The rest of the decoder was audited against nullDC-v1 `d3dRend.cpp` + `TexCache.h` and
     already matched: the four mipmap offset tables (numerically identical to `MipPoint[]`),
     VQ 2x2 codebook order, PAL4 nibble / PAL8 byte order, palette bank select, YUV422 maths
     and pairing, stride, twiddle (same as `twiddle_razi_`), `IgnoreTexA`, VRAM size and mask.
     The alpha path also matched: PT `GEQUAL PT_ALPHA_REF`, TR `GREATER 0`, and the per-poly
     zero-alpha clip condition `SrcInstr==4 && (DstInstr==1 || DstInstr==5)`.
   - REGRESSION AND REVERT: adding nullDC-v1's mirrored wrap (`SetTexMode`) froze the console
     after ~151 s. 159 of 160 live textures are swizzled, and RSX mirrored wrap needs a linear
     layout; the RSX stalled, the command buffer filled and the SH4 thread blocked in host code
     (pc and vbl frozen, monitor thread still alive). `GL_MIRRORED_REPEAT` stays plumbed in
     `ps3gl` but nothing requests it.
   - RE-ENABLED with the layout fix: `ps3gl` now allocates a mirrored texture linear, so the
     stall condition cannot arise. `ps3_tex` sets the wrap before the first `glTexImage2D`
     (the layout is chosen there and then fixed), and the mirror flag is part of the cache
     key so one texture used both ways gets one entry per layout. The old `mirror=1` reading
     was a cache-fill count, not a usage count; `mirrorbinds` in `[texfmt]` counts per bind.
     Cause of the visible bug: a car body is mapped with `FlipU` about its centreline, so
     plain repeat drew one half right and one half wrong.
   - SECOND DEFECT, the real cause of both freezes: an RSX linear texture needs a pitch that
     is a multiple of 64 bytes, and `ps3gl` used `width * 4` (32 for an 8-texel-wide DC
     texture). It was dormant while everything was swizzled, because swizzled textures
     ignore pitch, and the NPOT fallback that would have hit it is never reached. Enabling
     mirror routed real textures down the linear path and stalled the RSX again, at ~205 s.
     `tex_linear_pitch()` rounds it up and is now used at all four pitch sites (alloc size,
     level offsets, upload stride, GCM descriptor).
   - HARDWARE CONFIRMED 2026-09-20: cars draw correctly, ran to t=290 s with `vbl` advancing
     (~42/s) at 41-42 VPS, past the 222 s best of the pre-mirror build. Measured in traffic:
     `mirrorbinds` ~3000 of ~35000 binds (~9%), and ~25 of ~210 live textures now linear.
     Mirror usage was underreported by three orders of magnitude by the old counter.
     Cost: those ~9% lost the swizzled layout. If the frame rate needs it back, the next
     step is decode-time mirror expansion, which keeps the swizzle.
   - Punch-through settled: Crazy Taxi sends NO PT list (`built pt=0` every frame, counted as
     the TA builds strips, not as they are drawn). Its trees are translucent polygons.
   - Temporary diagnostics still in the tree: `[texfmt]` histogram in `ps3_tex.cpp`
     (`s_fmt_count` / `s_flag_*`) and the `built op/pt/tr` counters in `ps3_rend.cpp`
     (`s_stat_built`). Remove when no longer needed.
6. Measured speed (HW 2026-09-20, Crazy Taxi, 246 samples): emulation 45.3% (26.6-56.6),
   VPS 26.4 of NTSC 59.95, **RPS 12.0 average** (14.4 excluding idle windows), RSX present
   13.7 fps. Dense driving is RSX bound at 143-152 ms per frame (~7 fps) with ~1390 draw calls
   of ~8 vertices each: one `glBegin`/`glEnd` per PVR strip. Two independent limits - the SH4
   interpreter (G8 targets it) and the renderer draw-call count (G8 will NOT help; batching
   strips that share polygon state would, and `GPstate_cache` already knows when state is
   unchanged).

## G8 - SH4 speed  `[x]` (reached with the SH4 dynarec, not the cached interpreter)
Status 2026-10-05: the SH4 dynarec (`dc/sh4/rec_v2` + `ps3/ps3_driver.cpp`) runs dense Crazy Taxi at 99.5% SPEED
and Sonic Adventure at 99.8% (SH4 Clock 200). The TA decode and the RSX draw run on a render thread. Dynarec and
JIT Fastmem are ON by default since 2026-10-05. Details: `Dynarec.MD`, `TArender.MD`, `OPTIMIZATION_PLAN.md`
section 5. The steps below are the cached interpreter (OFF by default), kept as history.

1. Build an SH4 cached interpreter (same method as `arm7_cached.cpp`).  `[x]` (HW in step 2)
   - `dc/sh4/sh4_cached.cpp`: uop pool (256 K x 8 B) + direct-mapped tagged entrypoint table
     (256 K x 8 B, indexed by `(pc>>1)`, so every RAM mirror shares a slot). A block is decoded
     once into `{ &&label, opcode }` slots; the hot loop is `goto *pc_op->dispatch` with a
     `pc_op++` step, which drops the per-instruction `_vmem_ReadMem16` fetch, the `OpPtr[]`
     load and the full-ABI handler call.
   - No opcode semantics are duplicated. The same body headers that `sh4_cpu.cpp` and
     `sh4_fpu.cpp` compile into functions are re-included with `sh4op()` redefined as
     `ADVANCE(); lbl_<name>:`, so control falls out of one body into the next body's advance.
     That needed a mechanical split: `sh4_cpu_prelude.h` / `sh4_cpu_misc.h` out of `sh4_cpu.cpp`,
     `sh4_fpu_prelude.h` / `sh4_fpu_ops.h` out of `sh4_fpu.cpp`, the two `branch_target_*`
     helpers and the `do_sqw` template moved into the prelude (a template cannot be declared at
     block scope), and the two early `return`s in fldi0/fldi1 rewritten as an `if`.
     `sh4_cached_ops.inc` lists the 213 handler names so each function pointer can be paired
     with its label; an opcode missing from that list falls back to a plain `OpPtr[op](op)`.
   - PC model unchanged: while the instruction at A runs, `next_pc == A+2`. Branch handlers still
     run their own delay slot through `ExecuteDelayslot()`, so timing and semantics match the
     plain interpreter exactly (same `l -= cpu_ratio` per instruction, no charge for delay slots).
   - A block ends on `WritesPC` or `WritesSR` (`ldc Rn,SR` and `ldc.l @Rn+,SR` can dispatch an
     interrupt through `UpdateINTC()`), or at 128 instructions.
   - Invalidation: entrypoint tags are cleared on `sh4_cpu.ResetCache()` (CCR write, ELF load)
     and on every bulk write into system RAM (`WriteMemBlock_*` in `dc/mem/sh4_mem.cpp`, which
     covers GD-ROM DMA and the store queue). The uop pool is only reset inside the decoder, so a
     flush raised from inside a running block can never pull that block's slots away.
   - Off by default. `nullDC.cfg [ps3] SH4Cached`, Options -> Emulation -> "SH4 Cached";
     latched in `Get_Sh4Interpreter`, so it applies at the next boot.
2. Measure FPS on hardware only (never RPCS3).  `[x]`
   HW 2026-09-20, Crazy Taxi, cached ON: emulation 67-73% (was 45.3%), VPS 40-43.7 (was 26.4),
   RPS 43.2-43.7 in light scenes (was ~12), RPS 10.8-14.0 in dense driving (was 12.0).
   SH4 throughput is up ~1.5x. Dense driving did not move: the RSX still needs 78.5 ms/frame
   for ~1306 draw calls of ~8 vertices, exactly the renderer limit G6/G7 predicted.
3. HW test: speed gain against the plain interpreter, same output.  `[!]` SHELVED 2026-09-20 at Matt's request.
   Superseded by the dynarec before this freeze was attributed.
   The speed goal is met, but the run FROZE at t=120 s in dense traffic: `pc=0C159AEA` and
   `vbl=4190` stopped advancing, `[aud] in=0/s`, monitor thread still alive. That is the
   "SH4 thread blocked in host code" signature, the same shape as the mirrored-wrap RSX stall.
   Not yet attributed. Facts gathered:
   - No vring fence-timeout warning, so it is NOT the bounded fence wait in `ps3gl_begin_frame`.
   - `ps3_video_begin_frame` skips instead of blocking, and `ps3_video_end_frame` does not block,
     so it is not a flip wait. The open suspect is a command-buffer-full stall inside
     `rsxFlushBuffer`, which a 1.5x faster SH4 makes much easier to hit.
   - The draw load had just doubled to 1306 flushes/frame in the window before the freeze.
   - `Sh4: cached pool reset #8` printed ~1 s before the freeze; resets 1-8 span the whole run,
     so the rate was already climbing. Probably unrelated, but not ruled out. Invalidated blocks
     are never reclaimed until a pool reset, so the 256 K uop pool churns. Raising it is a
     tuning option, not a fix.
   Planned next at the time:
   a. Run the phase-marker build (already built and packaged). `[hb]` now prints
      `phase=<name> seq=<n>` from `ps3/ps3_phase.h`; the phase names the stuck host call
      (guest / decode / render / beginframe / draw / endframe / rtt / readback / tex / vblank).
   b. Run the same scene with `SH4 Cached` OFF for 3+ minutes. If it also freezes, the cached
      interpreter is not the cause and this belongs to the renderer, not G8.
   c. Only then decide the fix. Do NOT patch before (a) and (b) answer.
   The phase markers are diagnostic scaffolding; remove them once the freeze is closed.

## G9 - SPE offload (optional)  `[~]`
1. Candidates: AICA/DSP mixing, texture decode.
2. HW test per offloaded unit.

Done on HW: SPE Draw (the TA decode on an SPU, plan 5.12 S3; default ON), SPE AICA Sound (the AICA sound on an SPU,
plan 5.13 A2) and SPE ARM7 (the ARM7 and the AICA registers on an SPU, plan 5.15); the last two default ON since
2026-10-05. Open: SPE AICA DSP (plan 5.13 A3, HW tested, Matt's verdict pending); texture decode and the rest of
the plan 5.12 list (shelved).

## G10 - Release hardening  `[ ]`
1. Memory ceiling probe, allocation checks, pad hot-plug. VMU saves on HDD `[x]` (HW 2026-10-04,
   `/dev_hdd0/savedata/vmu/`).
2. Long soak test (command buffer wrap, memory creep).
3. Open from G7: the DS3 `user_controls.cfg` remap (deferred by Matt).
4. Every exit to the XMB takes the 3 s fallback (`OPTIMIZATION_POTENTIAL.md` P6).
