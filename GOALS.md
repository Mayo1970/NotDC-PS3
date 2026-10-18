# nullDC PS3 port - goal list

Base: copy of `nullDC4Wii` (upstream folder stays untouched).
Toolchain: PSL1GHT, Docker image `ps3dev-latest:latest` (ppu-gcc 7.2), run by `./build.sh`.
TITLE_ID: `NOTDC0000`, title NotDC (`NULLDC000` and `/dev_hdd0/data/NullDC/` before 2026-10-06). Data root: `/dev_hdd0/data/NotDC/` (BIOS in `data/`). VMU saves: `/dev_hdd0/savedata/vmu/`. Debug: UDP log only.
References: `ioQuake3-PS3` (RSX, audio, pad, PKG rules), `xashPS3` (vid/snd/in/sys under `engine/platform/ps3`).
Video reuse: RSX init/flip from `xashPS3/engine/platform/ps3/vid_ps3.c` and `ioQuake3-PS3/code/sys/ps3_glimp.c`;
GL 1.1 to RSX layer `ps3gl` (`xashPS3/3rdparty/ps3gl`, `ioQuake3-PS3/code/gl`) as the base for the renderer.
JIT: generated code runs only from a code array in `.text`; RW memory never executes. The first rule here, "no JIT
on PS3", was wrong: the SH4 dynarec and the ARM7 JIT run that way.
Rule: every goal ends with a hardware test. Next goal starts only after SUCCESS.

Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Active goal: G15 (started 2026-10-18, Matt: "Let's go with Goal 15."). G11 (online play) stage 1 is a HW success (2026-10-18, network end DCNet); its stage 2 (V.42) is
not started (Matt: "Do not start it."). G14 (CHD disc images) is done: HW 2026-10-14 (Matt: "The CHD images run well.").
The open goals are G10, G11 (stage 2), G15, G16 and G13 (last). G15 (added 2026-10-16) runs at the end, together with G13. G16 (added
2026-10-17, menu ideas, an open list) is the latest goal; its run order is not set yet.

Speed work and open items after G8: `OPTIMIZATION_PLAN.md` (section 0 has the state; section 7 has the open items
as bundles 10-22 since the re-cut of 2026-10-12 and the new bundle 21 of 2026-10-13, bundles 1-9 are closed) and `OPTIMIZATION_POTENTIAL.md` (P1-P85, six reference reviews).

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

## G9 - SPE offload (optional)  `[x]` (closed 2026-10-12 at the bundle re-cut, Matt: "Yes, do it."; every unit is default ON in the source)
1. Candidates: AICA/DSP mixing, texture decode.
2. HW test per offloaded unit.

Done on HW: SPE Draw (the TA decode on an SPU, plan 5.12 S3; default ON), SPE AICA Sound (the AICA sound on an SPU,
plan 5.13 A2) and SPE ARM7 (the ARM7 and the AICA registers on an SPU, plan 5.15); the last two default ON since
2026-10-05. Update 2026-10-11, from `ps3/ps3_presets.cpp`: also default ON in the source are SPE AICA DSP with the
DSP recompiler (plan 5.13 A3), SPE Build (5.12 #2), SPE Tex Hash (#3), SPE Tex Decode (#4), SPE FB Convert (#5) and
SPE AICA Sync at + SAMPLE SYNC (plan 6.6). Plan section 0 has the HW state of each.
What is left of the plan 5.12 list (the YUV decode options) stays in the plan, not in this goal.

## G10 - Release hardening  `[ ]`
1. Memory ceiling probe, allocation checks, pad hot-plug. VMU saves on HDD `[x]` (HW 2026-10-04,
   `/dev_hdd0/savedata/vmu/`).
2. Long soak test (command buffer wrap, memory creep).
3. Open from G7: the DS3 `user_controls.cfg` remap (deferred by Matt). It needs a table of `[ps3]` keys, not a port:
   Reicast's INI form is 212 lines (`OPTIMIZATION_POTENTIAL.md` section 4, sixth review).
4. XMB exit `[x]`: JIT EXIT (`OPTIMIZATION_POTENTIAL.md` P6 = plan 5.35, HW SUCCESS 2026-10-06, default ON) gives a
   clean exit. Left: the 3 s forced path skips the flash save (P85, plan bundle 15). Built 2026-10-13 as the row
   "Exit Flash Save" (FLASH / CLOCK page), default OFF, no HW run of this row yet (plan section 0). Open for the last
   checks before the release (Matt 2026-10-13; bundle 15 itself is a HW success).
5. Stick dead zone: each axis is cut alone below 20, with no rescale. Flycast uses a radial dead zone with a rescale
   (P84, plan bundle 10).

Re-cut 2026-10-12 (plan section 7): steps 3 and 5 are built in bundle 10 (Controllers), and the left part of step 4
in bundle 15 (Flash and clock). Steps 1 and 2 stay in this goal. They run last, together with G13.

Built 2026-10-12 in plan bundle 10 (plan section 0). Bundle 10 is a success by Matt 2026-10-12 ("We can consider this
a success."); these two rows had no HW run:
- Step 3: 15 `[ps3]` keys `MapCross` ... `MapR3` (`ps3/ps3_padmap.h`) and 15 rows on the CONTROLS page. Each DS3 button
  drives one of NONE, A, B, X, Y, START, the D-pad directions, L TRIGGER, R TRIGGER. The defaults are the old fixed map.
  C, Z and D are not offered: the pad still masks them (`0xF901`, G12 step 3).
- Step 5: row CONTROLS > "Stick Dead Zone" (AXIS = the old cut, RADIAL = P84's), default AXIS.

## G11 - Online play (modem)  `[ ]` (added 2026-10-07 at Matt's request; started 2026-10-18, network end DCNet (Matt); stage 1 HW SUCCESS 2026-10-18; stage 2 not started, Matt)
No DreamPi box needed: the emulator replaces the phone line, the modem and the DreamPi in software, as Flycast does.
Reference: Flycast `core/hw/modem/` and `core/network/`.
Order (re-cut 2026-10-12): after plan bundle 15 (Flash and clock), which builds the flash block code for the dial-up
account and the console ID.
Bundle 15 built and HW SUCCESS 2026-10-13 (Matt; plan section 0): the row "Console ID" (default OLD) writes the ID;
Matt's flash has an ID already. Not built
there: Flycast's rule in `alloc_block` that takes a physical block again when a newer copy of its data exists. The
dial-up blocks need it: Matt's flash has its settings block in physical block 224 to 241 of 254.
1. Modem chip, into `plugs/nullExtDev/nullExtDev.cpp` (a stub today), in two stages (2026-10-11,
   `OPTIMIZATION_POTENTIAL.md` P83):
   a. Stage 1, no V.42: the modem of Flycast `df2bdfc9b` (`modem.cpp` 764 lines + `modem_regs.h`; the Reicast
      design). It reports V.23, so the game's driver sends raw PPP bytes. Flycast ran online this way from 2018 to
      2025-12, DCNet included. About 1,460 lines + ~150 of socket code. Not checked: that today's DCNet servers
      still accept a stream without V.42.
   b. Stage 2: `v42.cpp` and `v42bis.cpp` (about 2,700 lines more; today's full set is 4,176). The Next Tetris,
      Bomberman Online, Outtrigger, Gundam Federation vs. Zeon, Propeller Arena, Power Smash and Pro Yakyuu Team
      need it.
   Area 0 `0x600000-0x6007FF` and `RaiseInterrupt` are already wired to the plugin. `libExtDevice_Update()` has no
   caller: call it from MediumUpdate. The build has `-fno-exceptions`: `v42bis.cpp` throws, and the hang-up of
   Flycast `bc3b8c71d` needs a return code.
2. Big endian: Flycast `modem_regs.h` uses `u8` bitfields, which GCC on PPC orders the other way. Give each register
   struct a big-endian layout. More traps (P83): the V.42 frame check and `MEDA` are read as `u16`, two register
   structs do not fill their bytes, and the DCNet finder magic is copied in host order.
3. Network end (Matt picks at goal start):
   a. DCNet: one TCP socket to `dcnet.flyca.st:7654` carries the raw modem bytes (plain pipe, no handshake,
      `core/network/dcnet.cpp`). No IP stack on the PS3. Depends on Flyinghead's free service. The access-point
      finder (UDP port 7655) is optional; port 7656 serves three games (P70).
   b. Local PPP: port `core/network/picoppp.cpp` (picoTCP), DNS `dns.flyca.st`. No outside service, more work
      (per-game port lists, UPnP, Alien Front Online address fix).
   Both: replace asio and `std::thread` with a PPU thread and PSL1GHT sockets. For 3b a BSD-socket base exists
   (Reicast `picoppp.cpp`, 834 lines; picoTCP builds for a big-endian host with `-DPICO_BIGENDIAN`).
   Both: the dial-up account must be in the flash (the ISP blocks 0x80-0xEA, about 80 lines in `dc/flash_fix.cpp`,
   P83). Flycast also writes a random console ID for network games (P85, plan bundle 15).
4. Optional, later: broadband adapter (`core/hw/bba/bba.cpp`, `rtl8139c.cpp`); fewer games support it.
5. HW test: a modem game dials, connects and reaches its revived server.

Started 2026-10-18 (Matt: "Let's go with G11"). Step 3: Matt picked 3a, DCNet. Stage 1 (steps 1a, 2, 3a and the flash
account) is built, package of 04:24 (`build/notdc_ps3.pkg`, 1,796,592 bytes). HW SUCCESS 2026-10-18 on that package
(Matt: "I tested it and it works flawlessly."). Stage 2 (step 1b, V.42): Matt 2026-10-18, "Do not start it." The
row's default is DCNET since 2026-10-18 (Matt: "Keep it DCNET default."); in the source, not in a package.
The three G11 files got their Flycast/Reicast GPL headers on 2026-10-18 at 04:34, after the package.
- Row NETWORK > Modem (`[ps3] Modem`): OFF (the old stub) or DCNET (default). Set before boot: `libExtDevice_Init` reads
  it. OFF keeps every read of 0x600000-0x6007FF at 0 and writes nothing, as before.
- Step 1a: `plugs/nullExtDev/nullExtDev.cpp` is Flycast's `modem.cpp` of df2bdfc9b. Its sh4_sched callback runs on the
  cycles of each MediumUpdate (7,168 in FAST, `dc/sh4/sh4_interpreter.cpp`); Flycast's jitter rule keeps the 143 us
  byte rate. Log: `[modem] reset`, `dialing`, `ringing`, `handshake`, `connected: CONF .. ASYN .. SPEED ..`.
- Step 2: `plugs/nullExtDev/modem_regs.h` lists each byte's bits from bit 0 as Flycast does; `BITS()` reverses them
  on the PPU. reg02 v0/v1 got their 2 missing bits. MEDAL/MEDAM are two bytes; the DSP RAM words are always byte-wise
  (Flycast reads even addresses as `*(u16*)`).
- Step 3a: `ps3/ps3_dcnet.cpp`, Flycast's PPPSocket. At the modem's connection a PPU thread (prio 999) dials
  dcnet.flyca.st:7654; two rings (16 KB in, 4 KB out) join it to the SH4 thread. Bytes from before the link are
  dropped, as in Flycast. A modem reset hangs up. Log: `[dcnet] call N: dialing`, `connected to`, bytes every 5 s,
  `ended`. Not built: the access-point finder (UDP 7655), port 7656 (Power Smash, Pro Yakyuu Team: V.14, stage 2),
  telling the game when DCNet closes the link (Flycast's DCNet does not either).
- Flash: with the row ON, a flash with no block 0x80 gets Flycast's dial-up account (`add_isp_to_nvmem`, user
  `flycast1`, password `password`, number `1234567`), then the BBA block 0xE0. These writes may reuse blocks with a newer
  copy (Flycast's `alloc_block` rule, only on this path). An account that exists stays as it is: Flycast also renames
  its user, and NotDC has no user name row. The account stays in `dc_flash_wb.bin` (`dc_flash.bin` is not written).
  Log: `[flash] dial-up account written|kept|NO ROOM, BBA block ...`.
- TOC: `.got` 0xFF88 (0xFF50 before; the bundle 23 work in the tree adds 2 entries). The two new objects build with
  `-mminimal-toc` (one entry each).
- PC checks: `misc/modem_check/run.sh` (ps3dev image): the layout of all 152 fields on both byte orders is Flycast's;
  300 driver runs in lockstep with Flycast's `modem.cpp` (29 M operations, 251 dials, 153 connections: every read,
  interrupt and byte the same); the
  DCNet rings and thread on Linux sockets with ASan and TSan; 20 source lines. `mutate.py`: 42 of 42 planted bugs fail it.
  `misc/flash_check` test 10: the account is Flycast's on 400 flashes, also Matt's case (242 of 254 blocks used);
  `mutate.py`: 108 of 108 (10 of them for G11).

## G12 - Second analog stick (Full Controller)  `[x]` (added 2026-10-07 at Matt's request; built 2026-10-12; HW 2026-10-12: the right stick works in Quake III Arena, Matt: "Quake 3 double sticks works")
Some games read a 2nd analog stick ("Panther mode", made for the Mad Catz Panther DC flight stick + trackball):
Quake III Arena, Unreal Tournament, Star Wars Episode I Racer, Vigilante 8, KISS Psycho Circus, Railroad Tycoon II.
Today the pad reports one stick (`0xfe060f00` in `dc/maple/maple_devs.cpp`), always sends X2/Y2 as 0x80, and the
DS3 right stick is not read.
Reference: Flycast `FullController` in `core/hw/maple/maple_devs.cpp` (commit 4d5ced1ce, "Panther DC/Full Controller").
The exact edits are in `OPTIMIZATION_POTENTIAL.md` P84 (2026-10-11).
Re-cut 2026-10-12: this goal is built inside plan bundle 10 (Controllers), after the stick dead zone.
1. `ps3/ps3_input.cpp`: read `ANA_R_H`/`ANA_R_V` into per-port X2/Y2 values, with the left stick's dead zone. Build
   P84's radial dead zone first (plan bundle 10), so both sticks get it.
2. `dc/maple/maple_cfg.cpp`: copy them into `pjs.joy[PJAI_X2]`/`pjs.joy[PJAI_Y2]`.
3. `dc/maple/maple_devs.cpp`: add the full controller. It reports `0xffff3f00` (6 axes, all buttons) and the name
   "Dreamcast PantherDC Controller" (Flycast `09b27ddd5`, which fixed KISS: Psycho Circus), and sends X2/Y2
   in the last two condition bytes. Keep the `0xF901` kcode mask in `maple_cfg.cpp`: the triggers also set C/D in
   `ps3_input.cpp`, so without it each trigger press is also a C or D press.
4. CONTROLS > Controller: add "FULL (2 STICKS)". STANDARD PAD stays the default and keeps its capability word, so
   other games see no change. Value 1 is the light gun in `maple_cfg.cpp`, so the new type needs its own value. Give
   it the two VMU slots (Flycast does).
5. HW test: the right stick works in Quake III Arena; a standard-pad game plays as before.

Built 2026-10-12 in plan bundle 10 (plan section 0), steps 1-4; step 5 done on HW the same day (Quake3.cdi with
FULL: `joy2=` lines in the log; Crazy Taxi and Looney Tunes with STANDARD PAD as before). CONTROLS > Controller has
"FULL (2 STICKS)" (`[ps3] Controller` = 1, `get_controller_type()` = 5, `MDT_FullController`); STANDARD PAD is the
default. The right stick is read only for the full controller.

## G14 - CHD disc images  `[x]` (added 2026-10-13 at Matt's request; built 2026-10-13, package of 09:35; HW 2026-10-14, Matt: "The CHD images run well.")
A `.chd` file holds a GD-ROM or CD image compressed in hunks (8 sectors of 2,448 bytes each). No reader takes it today:
`plugs/ImgReader` has CDI, NRG/MDS and GDI. This goal lifts the "Do not take" row of `OPTIMIZATION_PLAN.md` section 4
for CHD; CUE/BIN stays there. Notes: `OPTIMIZATION_POTENTIAL.md` P35. Memory: about 1-1.5 MB.
Sources, all on the PC:
- Library: `reicast-emulator/libswirl/deps/chdr/` (4,643 lines of C; CHD v1-v5; codecs zlib, cdzl, cdlz, cdfl).
  Flycast's `core/deps/libchdr` submodule is not checked out. nullDC-v1's reader (`plugins/ImgReader/chd.cpp` +
  `deps/chdr.cpp`) stops at CHD v4 with zlib only, so it cannot read a file from today's chdman.
- Decoders: `deps/lzma/LzmaDec.c` (1,185 lines), `deps/flac/src/libFLAC` (decoder files only), zlib (`-lz`, linked).
- Track rules: Flycast `core/imgread/chd.cpp` (304 lines).
1. Add the library, `LzmaDec.c` and the FLAC decoder to the Makefile. File access goes through `fopen`/`fread` (the
   Reicast copy uses its `coreio`). `chd.c` calls the LZMA encoder (`LzmaEnc.c` 2,788 lines + `LzFind.c` 1,069) only to
   compute the decoder properties (`chd.c:525-549`): build both, or compute the 5 property bytes by hand.
2. `plugs/ImgReader/chd.cpp`: a 4th `DriveIF` in `drives[]` (`common.cpp`; `InitDrive_` loops `i<3`). TOC and sessions
   come from the track metadata by Flycast's rules: pregap PGTYPE 'V', tracks padded to 4 frames, a GD-ROM has 549,300
   frames, MIL-CD session gap 6750+4500+150. Flycast throws; the build has `-fno-exceptions`, so use return codes.
3. CD-DA: a CHD stores audio samples big-endian and our CD-DA path takes little-endian. Swap each byte pair (Flycast
   `chd.cpp:62-70`; a byte-stream swap, so the same on the PPU).
4. Byte order: the library reads each header and map field byte by byte and picks the FLAC swap at run time
   (`chd.c:753-755`); libFLAC needs `WORDS_BIGENDIAN`. Safe on paper, never run on the PPU.
5. PC check before the HW run (as for CDI): `misc/chd_check` reads every sector of a `.chd` and compares it with the
   GDI or CDI of the same disc. Matt gives the file: no `.chd` is on the PC (2026-10-13).
6. Speed: the decode runs inside `ReadSector`, so on the GD-ROM Async worker when that row is ON. Not measured: the
   LZMA time per hunk on the PPE. Measure it on HW; add a read-ahead buffer only if loads stall (P35: one `fread` per
   hunk = 4 HDD requests per 32-sector fill).
7. Browser: add `.chd` to the extension list (`ps3/ps3_menu.cpp:227`). Disc Swap uses the same reader path.
8. Out: the zstd codec 'cdzs' (not in the Reicast copy; it needs a newer libchdr + zstd) and parent CHDs. A file that
   needs one of them must fail with a log line, not hang.
9. HW test: a GD-ROM `.chd` and a CD (MIL-CD) `.chd` boot and play, CD-DA sounds right, and loads are not slower than
   with the GDI/CDI of the same disc.

Built 2026-10-13, steps 1-4, 7 and 8; package of 09:35 (`build/notdc_ps3.pkg`, 1673008 bytes). Step 9 is open.
- Library (step 1): `deps/chdr`, `deps/lzma` (`LzmaDec.c`) and `deps/flac` (10 decoder files) are Reicast's files,
  unchanged but for one loop of `deps/chdr/flac.c`: a decode of bad FLAC data never returned. Ours: `deps/coreio/coreio.h`
  (stdio), `deps/crypto/*.h` and `deps/zlib/zlib.h` (what `chd.c` includes), `deps/lzma/LzmaEncProps.c` (the 5 property
  bytes by the encoder's own rules, no encoder), `deps/flac/compat/memory.h`. Makefile: `CHD_SRCS`, `CHD_DEFS`,
  `-DWORDS_BIGENDIAN=1` for `deps/`. Link: +125 KB of code, `.got` 0xE9C0 (0xE378 before; the limit is 0x10000), 0 `r2off`.
- Reader (steps 2-3, 7): `plugs/ImgReader/chd.cpp`, the 4th reader of `common.cpp`; `.chd` is in the browser list.
  Tracks, sessions and disc type by Flycast's rules. A GD-ROM gets the TOC fields and the region patch of the GDI reader;
  a CD gets the TOC fields of the CDI reader. A sector of a pregap, of the padding or of a hunk that does not decode
  reads as zeros.
- Step 8: a file with another codec (zstd), with a parent, cut before its map, or with a track type that Flycast does not
  take fails at the open with a `[chd]` line.
- Log: `[chd]` lines at the open (version, codecs, each track, disc kind). While the disc is read, at most once a second:
  `[chd] hunks N: file X ms (K KB), decode Y ms, Z ms per hunk (max), errors E`. This is step 6's measure.
- Step 5, `misc/chd_check` (WSL), PASS. Step 5 said that no `.chd` was on the PC: wrong. Flycast's tests have four chdman
  images, and the Fightcade folder has three Naomi GD-ROM images (v5, cdlz + cdzl + cdfl).
  - Flycast's four images with the values of its `ChdTest.cpp`: disc type, each track, sessions, lead-out, and every
    sector from FAD 140 to past the lead-out.
  - Images written by the check (no codec): a GD-ROM of full size, the old GD-ROM tag (little-endian CD-DA), each track
    type, a v4 file, one data track. TOC of both areas with TOC Fields ON and OFF, the pad, the region patch, 2048, 2340
    and 2352-byte reads.
  - Files that must not open (zstd, parent, bad tag, cut, other extension), a hunk with a changed byte, a disc swap,
    FLAC data that is not FLAC.
  - The three Naomi images (`run.sh file.chd ...`, 3 min): the SHA1 of the decoded data is the header's (1.34 GB each);
    each of the 504450 data sectors says its own FAD in its header; the CD-DA track is little-endian after the swap.
  - The 5 LZMA property bytes are those of the real `LzmaEnc.c` for 70614 sizes (8 frames: `5D 00 60 00 00`).
  - `mutate.py`: 49 of 49 planted bugs fail the check.
- Not done:
  - No disc of Matt's is a `.chd`, so no compare with the GDI or CDI of the same disc. The PC has no chdman.
  - No read-ahead buffer (step 6: only if loads stall on HW).
  - The byte order did not run on a big-endian host (step 4).
- Found: chdr frees no codec buffer at a close, about 135 KB and the packed map for each image that opens. It counts
  only for many disc swaps. `chd.c` is the unchanged file.

## G15 - End-user front end  `[~]` (added 2026-10-16 at Matt's request; started 2026-10-18)
Matt 2026-10-16: "A few more things that I'd like to add/implement in future, as last things". They run at the end,
together with G13. Matt's first item (most rows hidden, an "Advanced mode") is G13, see its note of 2026-10-16.
Each item keeps its own row and HW run on Matt's PS3, SH4 Clock 200.
Matt 2026-10-18: "Let's go with Goal 15." His picks, the same day:
- Item 1: way A (respawn). SELECT menu > START > confirm.
- Item 2: one pair of rows per player (8 rows on CONTROLS), values NONE / VMU / RUMBLE, default VMU (G16 item 4 form).
- Item 3: way A (pages).
- Item 4: Players stays as the most ports (default 4); a port is plugged only when a PS3 pad is there. A port keeps its
  pad while it stays connected, a new pad takes the lowest free port, and ports plug and unplug during a game.
1. Close a game. Matt: "A way to close a game and go back to the disc selection without having to open and close the
   emulator". Today `main()` (`ps3/ps3_main.cpp`) runs the browser (`ps3_menu_run()`) one time, then `EmuMain()`
   until the XMB exit. The SELECT menu goes back to the game or swaps the disc. It cannot stop the game. The core
   was never made to run `EmuMain()` a second time.
   - Way A: an item "Close Game" in the SELECT menu saves the flash and the VMUs, stops the threads as JIT EXIT
     does, then starts our own `EBOOT.BIN` again (PSL1GHT `sysProcessExitSpawn2`). The browser opens as at a start
     from the XMB.
   - Way B: stop and free every unit (SH4, JIT cache, PVR, AICA, SPUs, GD-ROM) and go back to `ps3_menu_run()` in
     the same process. Much larger. Matt picks.
2. Controller slots. Matt: "A way for the emulator to let users choose what both slots in a controller do, by placing
   the VMU as default option and the rumble as second option if the user chooses, or another VMU." Today
   `mcfg_CreateDevices()` (`dc/maple/maple_cfg.cpp`) puts a VMU in slot 1, and in slot 2 a VMU or, with CONTROLS >
   Rumble Pack ON (one row for all players, default OFF), the Puru Puru pack. New: rows "Slot 1" and "Slot 2", each
   VMU (default) or RUMBLE. They replace the Rumble Pack row. Open: one pair of rows for all players, or one per player.
3. VMU space. Matt: "If the VMUs can support a lot more space than the ones it currently has, since I'd like this to
   be compatible with save editors". Facts (2026-10-16):
   - Each card is a raw 128 KB image (`vmu_save_A1.bin`, or `<ID>_vmu_save_A1.bin` with VMU Per Game), the form of
     Flycast's VMU files. So a save editor that opens Flycast's files should open ours. No test yet.
   - The card has 256 blocks of 512 bytes, 200 of them for saves (`vmu_format()` and `MDCF_GetMediaInfo` in
     `dc/maple/maple_devs.cpp`). The FAT is one block of 256 entries. The BIOS formats every card this way, and the
     save editors expect it. So one larger card breaks the save editors.
   - Way A: pages, as the third-party 4X memory cards did (4 pages of 200 blocks, one page plugged at a time). Each
     slot gets N standard files (`vmu_save_A1.bin`, `vmu_save_A1_p2.bin` ...), and a row picks the page.
   - Way B: VMU Per Game (built in plan bundle 16, default OFF because it hides the old saves): one 200-block card per
     disc. Matt picks.
4. Controller detector. Matt: "A controller detector that will let users assign more controllers ONLY if more pads
   are plugged in." Today the row CONTROLS > Players (1-4, default 4) makes the maple ports at boot, pads or not.
   `ps3_find_pad()` (`ps3/ps3_input.cpp`) gives DC port N the Nth connected PS3 pad at each poll, so a port with no
   pad is a plugged controller that sends no input. New: a DC controller (with its slots) is plugged only on the
   ports that have a PS3 pad, and it plugs or unplugs when a pad connects or leaves (also G10 step 1, pad hot-plug).
   Open: the Players row stays as the most ports, or goes.
5. HW test: close a game and boot another one, with no hang and the saves kept; RUMBLE and a second VMU in a slot work
   in a game; a save editor opens a VMU file from the PS3; with one pad the game sees one controller, and a second
   pad adds port B.

Matt 2026-10-18, item 2: "So if Players is set to 1, only show Port A. If players are 2, show both A and B and so on",
slot 1 VMU and slot 2 NONE by default (asked: slot 2 of each port). Built, pkg 16:52 (3550800 bytes, `.got` 0xFFD8):
`controls_shown()` (`ps3/ps3_menu.cpp`) lists a port's two rows only while Players reaches it (`Option.port1`), the
cursor and the scroll go over the rows shown; `[ps3] SlotA2`..`SlotD2` default 0. An install that ran keeps its keys.
HW 2026-10-18, pkg 16:52 (Matt: "Perfect. They work!"): the rows show and hide with Players. Then Matt: "keep Players:
1 by default. So people won't feel overwhelmed": `g_player_count` default 1 (was 4).
Item 1 HW SUCCESS 2026-10-18, pkg 16:32 (Matt: "Perfect. Both went fine!"): Sonic Adventure 2, Close Game, the log's
next boot runs `USRDIR/RELOAD.SELF` and the browser opens; no `[rsxw]` line in that run. Items 2-4: no HW run yet.
HW run 1, 2026-10-18 (pkg 14:44): Close Game ended NotDC cleanly, then the PS3 went to the XMB (Matt). Likely cause:
the PKG's EBOOT.BIN is NPDRM (`make_self_npdrm`) and `sysProcessExitSpawn2` does not start it (RetroArch's PS3 port
calls `sceNpDrmProcessExitSpawn` first, IRISMAN restarts a plain `USRDIR/RELOAD.SELF`; PSL1GHT has no NPDRM spawn).
Items 2-4 did not run (default rows, one pad). Fix, Matt 2026-10-18 ("Both in one package"): the pkg also holds a
plain `USRDIR/RELOAD.SELF` (`make_self`), and Close Game starts RELOAD.SELF in the folder of argv[0]. Package of 16:32
(3549776 bytes); `.got` 0xFFD8 (5 entries left). Also in it, not G15: the RSX stall log (`ps3/ps3_rsxwatch.c`, the
Sonic Adventure 2 freeze of that run: the RSX stopped flipping while the SH4 ran on), and another session's new defaults
of 15:37 (Screen Scaler ON, Native Size ON).
Built 2026-10-18, package `build/notdc_ps3.pkg` of 14:44 (1797616 bytes); not committed.
- Item 1: SELECT menu > START opens "CLOSE GAME" (not in the disc browser); X closes, O goes back. After the presets
  are saved, `ps3_close_game()` (`ps3/ps3_main.cpp`) sets `ps3_exit_requested = 2` (an XMB quit sets 1 and wins). The
  monitor thread stops the SH4 as at an XMB exit; main() ends as at an XMB exit, then calls
  `sysProcessExitSpawn2(argv[0], or PS3_SELF_PATH /dev_hdd0/game/NOTDC0000/USRDIR/EBOOT.BIN)`. If the SH4 does not stop
  in 3 s, the forced exit goes to the XMB, as before. Log: `[ps3] XMB exit requested, stopping SH4 (close game 1)`;
  the next boot line ends with the EBOOT path.
- Item 2: CONTROLS rows "Port A Slot 1" .. "Port D Slot 2", NONE / VMU / RUMBLE, default VMU, set before boot; keys
  `SlotA1` .. `SlotD2` (1 = VMU), in `[padcfg]`. `mcfg_PlugBus()` (`dc/maple/maple_cfg.cpp`) makes each port's
  devices. The Rumble Pack row and key are gone; its old value is not carried over.
- Item 3: CONTROLS row "VMU Page" 1-4 (key `VmuPage`, default 1), set before boot. Page 1 = the old files; page N adds
  `_pN` (`vmu_save_A1_p2.bin`, with VMU Per Game `<ID>_vmu_save_A1_p2.bin`). Each page file is a standard 128 KB card.
  `[vmu] disc ID ...` now prints the page.
- Item 4: `ps3/ps3_padports.h` (a port keeps its pad while it is connected and below Players; a new pad takes the
  lowest free port), `ps3_pad_ports()` (`ps3/ps3_input.cpp`), `mcfg_HotPlug()` at the start of each maple DMA:
  `[maple] port B plugged=1/0`, `[pad] dcN -> ps3 pad M`. At boot only the ports with a pad are plugged. No OLD row.
- TOC: `.got` 0xFF88 -> 0xFFC8 (8 entries; 7 left), no r2off stubs.
- PC checks (WSL): `misc/maple_check` (117 checks, 126 of 126 mutants; its stale `dc.cpp` pair since the bundle 23
  PAL line is now an `order`), `misc/pad_check` (the port rule against a second model, 400000 random steps, 51 of 51
  mutants), new `misc/close_check` (24 source lines, 17 of 17 mutants), and the 18 other run.sh that name a changed
  file: PASS (modem_check in the ps3dev image, as its header says).
- HW steps (SH4 Clock 200; the new keys take their defaults on the install, the old Rumble Pack value is gone):
  1. Close Game: in a game SELECT, START: "CLOSE GAME". O goes back; START, X: the browser after a few seconds. Boot
     a second game. Fault: a black screen, a hang, or the XMB.
  2. Slots: Port A Slot 2 RUMBLE, a game with Jump Pack support: the DS3 motor and `[pad] dc0 rumble` lines (the pack
     never ran on HW). Then Port A Slot 1 NONE: the game sees the VMU in slot 2 only.
  3. VMU Page 2: a blank card in the game (files `vmu_save_A1_p2.bin`, `A2_p2`); save; Close Game, page 1: the old
     saves; page 2: the new one. A save editor on the PC opens `vmu_save_A1_p2.bin`.
  4. Players 4 with one pad: the game sees one controller. A second DS3 on in the game: `[maple] port B plugged=1`;
     off: `plugged=0` and the game's "controller removed".

## G16 - Menu ideas  `[ ]` (added 2026-10-17 at Matt's request; nothing built; an open list, Matt may add items)
Matt 2026-10-17: menu ideas, checked for "doable or not". All are doable. Items 3, 4 and 5 are the newer form of G15
items 4, 2 and 1; item 1 is the newer form of the G13 note of 2026-10-16. Run order: not set yet. Each item keeps its
own row and HW run on Matt's PS3, SH4 Clock 200.
1. Advanced options. Matt: "Hide most JIT/Render/Debug and such features behind an "Advanced options" voice".
   - The menu has 15 pages and 225 rows in `s_opt_pages` (`ps3/ps3_menu.cpp`), and CONTROLS has 19.
   - A flag per page hides a page. A "basic" page can point at the same variables as the advanced rows:
     `for_each_option()` already skips the second row of one variable. The SELECT menu uses the same pages.
   - `MAX_INGAME_OPTS` is 256: a basic page of more than 31 rows needs it raised.
   - Open: which rows are basic.
2. UDP log row. Matt: "the homebrew sending its log files to the UDP port as toggleable option".
   - A flag in `ps3_log_vprintf()` (`ps3/ps3_main.cpp`) skips `vsnprintf` and `sendto`, so OFF can only save time.
   - The log starts before `cfgOpen()`, so the first 3 boot lines go out before the flag is read.
   - The IP is a build constant (`PS3_LOG_IP`, 192.168.178.99), so for other users the log goes nowhere.
   - A new key takes the source default also on Matt's install: with OFF, his logs stop until he sets ON.
   - Open: the default.
3. Controller detector. Matt: "Depending how many controllers are plugged in, the game will detect them and will act
   accordingly (so we can't assign 4 pads, if 2 are connected, for example)".
   - Today CONTROLS > Players (1-4) makes the maple ports at boot, pads or not. `ps3_find_pad()`
     (`ps3/ps3_input.cpp`) gives DC port N the Nth connected PS3 pad.
   - Step A: Players capped at `pad_count()` (`ps3/ps3_menu.cpp`), or a value AUTO (the pads at boot).
   - Step B: plug and unplug during a game. The devices are made one time in `mcfg_CreateDevices()`
     (`dc/maple/maple_cfg.cpp`), and each game reacts in its own way. A later step.
   - Open: cap or AUTO.
4. Slot devices. Matt: "Controllers will be able to pick what's included in slot A and Slot B. So they can choose if
   both slots have a VMU, or one of them has the rumble pack and so on".
   - Each player and slot: NONE, VMU or RUMBLE PACK. Today slot 1 is a VMU, slot 2 a VMU or the pack (one row).
   - VMU files are already per slot (`vmu_save_A1.bin` to `D2`), and the rumble already goes to each player's DS3.
   - Risk: a game can look for the pack in one slot only. Build with item 6.
5. Close game. Matt: "Add a "Close game" feature that will stop the emu so we can pick another game".
   - `EmuMain()` keeps the DC memory, the SPU jobs and the render thread until the process ends (`nullDC.cpp`). So
     G15 way B (a second `EmuMain()` in one process) is large.
   - Way A: save the presets and the flash (VMUs save at each write), `gda_stop()`, then `sysProcessExitSpawn2`
     (PSL1GHT `lv2/process.h`) of our own `EBOOT.BIN`. The browser opens again; a restart takes a few seconds.
   - HW risk: the SPUs and the RSX must be free after the respawn. The forced exit of the monitor thread already ends
     the process with threads running.
6. VMU manager. Matt: "Add a VMU manager that will let us create more than one VMU. Thus acting like in real life where
   we can choose. Then users will choose which VMU to use." Related: G15 item 3.
   - A screen lists the cards in `/dev_hdd0/savedata/vmu/`, makes blank cards (`vmu_format()`), and gives each slot a
     card (a `[ps3]` key per slot). `open_card()` (`dc/maple/maple_devs.cpp`) takes that name, not `vmu_file_name()`.
   - Names: automatic (VMU_01, VMU_02 ...) or the PS3 on-screen keyboard (PSL1GHT `sysutil/osk.h`, not used yet,
     needs a HW bring-up). Optional: list the saves on each card.
   - A swap during a game is a risk: a game can keep the old card's directory, so it needs an unplug first. First
     version: set before boot.
   - Open: the manager's card or VMU Per Game, which wins; the names.
7. HW test: the basic menu shows only the basic rows and Advanced shows all; UDP OFF sends nothing and ON logs as
   today; with 2 pads Players stops at 2; a rumble pack and a second VMU work per slot in a game; Close Game boots
   another game with no hang and the saves kept; a new card from the manager saves in a game, and a second card
   swaps in at boot.

## G13 - Release candidate settings  `[ ]` (added 2026-10-08 at Matt's request; release material, stays the last goal)
An RC build shows only a few option rows. Every other setting runs its source default.
Risk: each launch writes every `[ps3]` key to `nullDC.cfg` (`ps3_presets_save`; `cfgLoadInt` writes an absent key
back), and an install keeps the file. A source default change never reaches an install that ran once, so hidden rows
would lock in an old build's values where nobody can see them.
Re-cut 2026-10-12: it runs last, together with G10 steps 1 and 2. Plan bundles 17, 18 and 20 give the rows that are
OFF their verdicts first (step 5).
1. Build flag: `./build.sh RC=1 pkg` (Makefile `RC=1` -> `-DNOTDC_RC`). Normal builds keep every row.
2. RC: leave the hidden rows out of `s_opt_pages` in `ps3/ps3_menu.cpp`. The SELECT in-game menu uses the same pages.
3. First RC boot: reset `[ps3]` to the source defaults one time (a version key in `nullDC.cfg`).
4. After the reset, the hidden keys still load from `nullDC.cfg`, so a hand edit can turn off a feature that breaks
   one game.
5. Settle the defaults first. Open on 2026-10-08: YUV Block Decode is OFF in the source; SPE Tex Decode and SPE FB
   Convert have one HW run. Also open on 2026-10-11 (P85, plan bundle 15): a new flash boots in Japanese (Flycast:
   English), and Players is 4 where Flycast plugs 1 controller (Players is 1 by default since 2026-10-18, G15, Matt). Since 2026-10-13 the six rows of the FLASH / CLOCK page
   (bundle 15, HW SUCCESS 2026-10-13) have their defaults since 2026-10-13 (Matt: the values of his Rayman 2 run): DISC,
   ENGLISH, FLYCAST, FLYCAST, Exit Flash Save OFF, RTC Local Time ON. So a new flash boots in English now. Open from bundle 15 for these last checks (Matt 2026-10-13: "We'll check out these items as "open"
   for the latest stuff left before release."): Border Down at 60 Hz, Exit Flash Save, Flash Defaults with no flash file.
6. Visible rows (recommended 2026-10-08, Matt confirms at goal start): SH4 Clock, Speed Limiter, Frame Drop, CD Audio,
   and CONTROLS > Players and Controller. G12 adds FULL (2 STICKS) to Controller; keep the G11 online row visible if
   G11 adds one. On 2026-10-08 the menu has 83 option rows on 4 pages and 2 on CONTROLS.
7. HW test: install the RC over a `nullDC.cfg` from an old build. `[jitcfg]` and `[sndcfg]` show the source defaults,
   the menus show only the visible rows, and a game plays as on the last dev build.

Note 2026-10-16 (Matt): "Most options hidden except ones that are EFFECTIVELY useful for the end user, and an
"Advanced mode" that will show the menu as we see it today". So a row "Advanced Mode" (OFF) hides the other rows,
and ON shows the full menu as today. With this form the `RC=1` build flag of steps 1-2 is not needed; Matt confirms at
goal start. Step 6 has the visible rows (recommended 2026-10-08). The rest of G15 runs with this goal.
