# Optimization plan - what to take from Flycast

Review of Flycast (`E:\Users\Matteo\Desktop\Dreamcast\flycast`) against our tree,
Reicast and Minicast. Written 2026-09-23. Flycast is an allowed reference from
this date (Matt).

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Flycast is little-endian only. Check every ported line for byte order.

---

## 0. Resume here (state at end of session 2026-09-23)

| Item | Status |
|---|---|
| 2.1 YUV reset on STARTRENDER | `[x]` HW, no visible change |
| 2.2 AICA EG rate + loop fixes | `[x]` HW, echo unchanged, no regression |
| 2.3 DSP | PARKED: Matt wants a PPC DSP JIT |
| 2.4 Strip merge + inf drop | `[x]` HW, default ON, ~3x fewer draws |
| 2.5 ARM7 clock (`arm_sh4_bias` 20) | PARKED, likely cause of the ChuChu echo |
| 3c modifier volumes (Flycast GL) | `[x]` HW |
| 3d background depth bias | `[x]` HW, no regression |
| 3e background vertex order | dropped, our code has no such fault |
| 3f NaN colour = 255 | `[x]` HW, no regression |
| 3g GD-ROM CD-DA 2352 + zero-fill | `[x]` HW, no regression |
| 3i region / cable from IP.BIN | `[x]` HW, no regression, switch path unexercised |
| 3a fog, 3b offset colour | OPEN: need a PVR Cg shader in `ps3gl` |
| 3h GD-ROM CDDA lead-out / restart rules | OPEN |

Section 3 correctness (3a, 3b, 3h) is SHELVED by Matt on 2026-09-23. Active
work is section 5, SH4-side speed.

All changes are uncommitted in the working tree. Matt commits.

---

## 1. Work order

| # | Item | Size | Gain |
|---|---|---|---|
| 1 | YUV converter reset on STARTRENDER | 1-2 lines | FMV correctness |
| 2 | AICA `EG_EffRate` and loop fixes | small | sound correctness, maybe the ChuChu echo |
| 4 | Strip merging + inf-vertex drop | large | draw calls in dense scenes |
| 3 | DSP - PARKED by Matt, 2026-09-23: wants a PPC DSP JIT | large | DSP effects (reverb, echo) |
| P | ARM7 clock (`arm_sh4_bias`) - PARKED by Matt, 2026-09-23 | small | ChuChu echo, slow SFX |

---

## 2. Items

### 2.1 YUV converter reset on STARTRENDER  `[x]`

- Source: Flycast commit `805d014b5` (2026-02-10), `core/hw/pvr/pvr_regs.cpp`,
  `core/hw/pvr/pvr_mem.cpp`. Commit text: "reset on STARTRENDER on real hw".
- Two parts:
  1. A STARTRENDER write calls `YUV_init()`.
  2. When the last macroblock is done, raise the interrupt only. Do not reset.
     `TA_YUV_TEX_CNT` keeps reading the full count. The reset happens when the
     next YUV data arrives.
- Ours: `dc/pvr/pvr_if.cpp` reset only on `TA_YUV_TEX_BASE` / `TA_YUV_TEX_CTRL`
  writes and at frame end. So `TA_YUV_TEX_CNT` read 0 after a finished frame.
- HW test: an FMV game (Space Channel 5, Crazy Taxi intro). Look for skipped or
  torn frames.
- 2026-09-23: done in `dc/pvr/pvr_if.cpp` and built (`build/nulldc_ps3.pkg`).
  The reset runs when the next block starts, so a finished frame that is
  followed by more data in the same transfer also resets.
- HW 2026-09-23: Space Channel 5 and Crazy Taxi FMVs look the same as before.
  No regression, no visible gain. Kept, because it matches real hardware.

### 2.2 AICA envelope rate and loop fixes  `[x]`

- Source: Flycast `core/hw/aica/sgc_if.cpp`. These fixes are newer than the
  Reicast/Minicast AICA code.
- `EG_EffRate` (commit `265c5c6c3`, 2025, "from real hw tests"):
  - `effrate = rate*2`.
  - Only if `KRS < 0xF`: add `(FNS >> 9) & 1` and
    `max(0, (KRS + (OCT ^ 8) - 8) * 2)`.
  - Clamp to 0x3F.
  - Ours (`plugs/nullAICA/sgc_if.cpp:648`) adds the OCT term and the FNS bit
    also when `KRS == 0xF`. That changes envelope length.
- Loop rules:
  - `LSA > LEA`: the channel does not stop at LEA. It runs on to LSA, then
    CA = 0 and it stops, also when looping. This replaced the old Legacy of
    Kain hack (LEA = 0xFFFF), which Flycast removed.
  - The interpolation sample after CA wraps to LSA at the loop end.
  - ADPCM stream: LEA aligned up to 4 samples (`alignedLEA`).
  - ADPCM: the decoder state (Q, last sample) is saved at the first pass
    over LSA. Mode 2 restores it on every loop instead of a full reset.
    Mode 3 only saves it (commit `3f70e47ad`, 2026-09-11).
  - CA read-back clamped to `LEA - 3` (VC 2K sports games).
- Channel end: `disable()` sets CA = 0, and a channel that the AEG disables
  skips the rest of its step (`b468c9bda`, `5393235c4`). Flycast reverted
  `a7702c622` ("do not disable at full attenuation"), so it is not taken.
- ALFO random waveform (`8f7e4bf2d`).
- 2026-09-23: all of the above done in `plugs/nullAICA/sgc_if.cpp` and built.
  HW test pending.
- Not taken, because our code does not have the feature:
  - LPF (`bafe07de8`, `807e4a20e`): our FEG and filter are stubs.
  - PLFO waveforms: our pitch step ignores the PLFO value.
- Not in this item, noted for later: log-scale attack and AEG start value
  0x280 (`08d7cc68f`), EG monitor reads 0x1FFF above 0x3BF, ARAM address mask
  on sample reads (`8acadcb66`), ADPCM delta clamp to 0x7FFF.
- HW test: ChuChu Rocket (echo) with "Mute 16-bit PCM" OFF, plus one game with
  music.
- HW 2026-09-23: echo still there. Matt: it feels like timing, short sounds
  play very slowly (menu select). Lead found offline: `arm_sh4_bias` is 20 in
  `plugs/vbaARM/arm7.h` (Wii comment "20 = Nice"), so the ARM7 gets
  4535/20 = 226 ticks per sample. nullDC-v1, nulldc-360, Reicast and Minicast
  use 2 (2267 ticks); Flycast uses 512 (`ARM_CYCLES_PER_SAMPLE`).
  Our sound driver CPU runs 2.3x slower than Flycast and 10x slower than nullDC.

### 2.3 DSP  `[ ]`  PARKED

- Parked by Matt on 2026-09-23. He wants a DSP JIT (PPC), not an interpreter.
  Flycast `dsp_interp.cpp` stays the reference for the semantics; its
  `dsp_x64.cpp` / `dsp_arm64.cpp` show the JIT shape. The code must run from a
  `.text` array on PS3 (see `Dynarec.MD`).

- Semantics notes below hold for the JIT too. Reference: Flycast
  `core/hw/aica/dsp_interp.cpp` (222 lines), not Reicast's. This replaces the
  source named in `RESEARCH.MD` 1.1.
- Reicast/Minicast still have bugs that Flycast fixed in 2019-2020:
  - EFREG cleared every sample (`5905b8c14`).
  - `EFREG += SHIFTED >> 4` where the fix is `= SHIFTED >> 8` (`7058ffeb6`).
  - Y read signed where the fix is unsigned (`7058ffeb6`).
  - PACK/UNPACK and SHIFTER/ACC shifts (`713705a6f`, `a50ed2cc7`).
- The endian and cost notes in `RESEARCH.MD` section 2 still apply: `^2` on the
  ring buffer, `(s16)EFREG`, predecoded MPRO, behind a preset, default OFF.
- HW test: a game with DSP reverb, then the PPE cost on the stats line.

### 2.4 Strip merging and inf-vertex drop  `[x]`

- This is `Optimization.md` item 1. Reicast and Minicast do not have it.
- Source: Flycast `core/hw/pvr/ta_util.cpp`:
  - `makeIndex()`: joins strips with degenerate triangles. Handles the
    cull-direction parity when two joined strips cull the opposite way.
  - `PolyParam::equivalentIgnoreCullingDirection()` (`ta_ctx.h:86`): the
    merge key (PCW, ISP, TSP, TCW, tile clip).
  - `is_vertex_inf()`: drops NaN, |x| or |y| > 1e25, z > 3.4e37, and splits
    the strip there.
- Result: one `glDrawElements` per state block, not one `glBegin`/`glEnd` per
  PVR strip. `ps3gl` already maps it to `rsxDrawIndexArray`. No restart index
  needed.
- The inf drop closes the Soul Calibur item: inf vertices are still drawn with
  clip.w = 0 today.
- HW test: Crazy Taxi dense traffic (draw count, SPEED, RPS), Soul Calibur.
- 2026-09-23: built. `draw_strip()` in `ps3/ps3_rend.cpp` ports `makeIndex()`.
  - Same-state strips in a row go out as one `glBegin(GL_TRIANGLE_STRIP)` /
    `glEnd` run, not `glDrawElements`: the `ps3gl` client-array
    `glDrawElements` forces `GL_TRIANGLES` and inline indices. The draw-call
    count drops the same way.
  - Runs are capped at 16000 vertices (`ps3gl` drops past 16384).
  - Translucent autosort merges after the sort, as Flycast per-strip mode.
  - Toggle: Options -> VIDEO / DEBUG -> "Strip Merge" (`nullDC.cfg [ps3]
    StripMerge`), default ON, live. OFF is the old one-strip-per-draw path.
  - `[rend]` line: new `draws=merge|strip op= pt= tr=` next to `strips`.
  - Checked offline: `merge_sim.py` (session scratchpad) mirrors the merge
    path; 20000 random cases with cull directions and inf vertices gave the
    same triangles and winding as the source strips. Mutating the parity or
    the cull reversal makes it fail, so the check has teeth.
  - Known gap: a NaN 1/W is not dropped. `set_pos` floors NaN to W = 1e9
    before the draw sees it; Flycast drops it. +inf and > 3.4e37 are dropped.
  - HW 2026-09-23, Crazy Taxi, one boot ON and one boot OFF. Matt: "ran
    better and looked alright" with it ON. No holes or culling faults.
    - Draws per render, dense traffic: 3345 op + 295 tr strips went out as
      539 + 207 draws. Menu: 240 op strips as 1 draw.
    - ps3gl flushes per frame at 6-18K vtx: ON 370-860, OFF 1280-3090.
    - Emulation SPEED in dense scenes did not move: ON 49.3% / RPS 29.5
      (22 windows), OFF 48.9% / RPS 29.3 (13 windows). The ON run carried
      more load (up to 17.8K vtx against 15.1K). The dense-scene limit is
      now the SH4 side, not draw submission.
    - `drop=0`, `overflow=0`, `ta_raw_of=0` in every window of both runs.
  - Kept default ON.
  - HW 2026-09-23, Soul Calibur, Strip Merge ON: Matt saw no malformations.
    `[rendz]` still shows the inf input (`op x=[-inf..inf] y=[-inf..inf]`),
    so the drop path ran. In a fight, 2889 op strips went out as 29 draws.
    SPEED 68.9% average over 72 s. The Soul Calibur inf item in
    `dc-depth-range-findings` is closed.

### 2.5 ARM7 clock  `[ ]`  PARKED

- Parked by Matt on 2026-09-23. Do not build until he asks.
- Lead: see the 2.2 HW result. `arm_sh4_bias` = 20 (`plugs/vbaARM/arm7.h:20`)
  is a Wii speed trade-off. It gives the ARM7 226 cycles per sample: Flycast
  gives 512, and nullDC-v1, Reicast and Minicast give 2267.
- The ARM7 runs the sound driver. Too few cycles delay every note step, which
  fits the ChuChu echo and the slow menu-select sound.
- Proposed test: make the bias an option (Options -> Emulation). The choices
  are 20 (today), 9 (Flycast rate, 504 cycles) and 2 (nullDC rate).
- HW test: ChuChu Rocket at each setting. Compare the echo, the menu-select
  sound and SPEED. Read the ARM time on the stats line for the PPE cost.

---

## 3. Smaller items  `[ ]`

| # | Item | What we gain | Cost | Flycast source |
|---|---|---|---|---|
| 3a | Fog (table and per-vertex) | Distance fog in every game that uses it. Today it is ignored, so far geometry pops in sharp and hazy scenes look clear. | New Cg vertex + fragment program in `ps3gl` (cgcomp in Docker), fog table as a 128x1 texture, snapshot of FOG_TABLE / FOG_COL_* / FOG_DENSITY. Medium. | `core/rend/dx9/d3d_shaders.cpp` `fog_mode2` |
| 3b | Offset (specular) colour | Highlights and glints on models (cars, fighters). Today `pcw.Offset` is dropped, so lit models look flat and darker. | The decoder must keep the offset colour per vertex (`DcVertex` has one colour today) and the shader must add it. Shares the shader work of 3a. Medium. | same file, `pp_Offset` |
| 3c | Correct modifier volumes | Shadows only on polygons with the Shadow bit, at the game's intensity (`FPU_SHAD_SCALE`), plus inclusion/exclusion volumes. Today every pixel inside a volume gets a flat 50% black, HUD and sky included. Crazy Taxi sends ~250 volume triangles per frame. | Stencil bit 0x80 from each shadowed polygon, new final pass. The two-parameter (param1 colour) mode needs the shader of 3a. Medium. | `d3d_renderer.cpp` `setMVS_Mode`, `drawModVols` |
| 3d | Background plane: depth bias | Fixes black screens in Windows CE FMVs (4x4 EVO, Armada, Caesar's Palace, Giant Killers, PBA Bowling, Starlancer, Tomb Raider, Wild Metal, Who Wants to Beat Up...), the Xtreme Sports skybox, the Blue Stinger (JP) intro. | Small. | `443d5a2ba` |
| 3e | Background plane: vertex order, V flip | Lovebery select screen, Donald Duck Goin' Quackers backgrounds. | Small, but check against our nullDC-v1 `background_plane()` first. | `c63f1a999`, `692ca5218` |
| 3f | NaN intensity = max | Gaiamaster Kessen! black texture. | One line. | `c0f0a758d` |
| 3g | GD-ROM: blank sector on FAD miss | NBA Hoopz voice samples. | Small. | `e94b26f1d` |
| 3h | GD-ROM: CDDA stop at lead-out, restart/pause rules | CD audio that loops or runs past its track. Only matters with the Cdda preset ON. | Medium (touches the image readers). | `9ea0563a7`, `e5344d37d` |
| 3i | Region and cable from IP.BIN | A game boots in a region and video mode it supports, with no manual setting. | Small. Check it does not clash with Matt's "no game presets" rule: it reads the disc header, not a list. | `core/emulator.cpp` `loadSpecialSettings()` |

Status 2026-09-23 (all big-endian checked):
- 3d `[x]` built. `draw_scene()` subtracts 1e-6 from the background 1/W, floor
  1e-11, as Flycast `FillBGP`. We keep nullDC-v1's 28-bit mask; Flycast dropped
  it, the difference is below the bias. The register is a native u32 on BE, so
  the `memcpy` to float is exact.
  CORRECTION to the gain above: the Windows CE games need full SH4 MMU
  emulation, and ours has none (`dc/mem/mmu.cpp` only remaps the store queue).
  They cannot boot here yet, so their FMV fix does not apply. What is left:
  Xtreme Sports skybox, Blue Stinger (JP) intro.
- 3e dropped. Our `background_plane()` + `bg_eval()` evaluates the plane from
  the 3 real vertices (position, colour, UV) at the canvas corners. That is
  independent of vertex order and never rebuilds UVs, which is what both
  Flycast fixes repair. UV16 order already matches (U low half, V high half).
- 3f `[x]` built. `clamp_byte()` returns 255 for NaN. On PPC `fctiwz` turns NaN
  into 0x80000000, which our clamp made 0, so a NaN intensity drew black,
  exactly the Gaiamaster Kessen! symptom.
- HW test: any game for regressions on the background (Crazy Taxi, Soul
  Calibur, BIOS). Xtreme Sports / Blue Stinger (JP) / Gaiamaster Kessen! only
  if Matt has them.
- HW 2026-09-23: no regression (Matt). The three target games were not run.
- 3c `[x]` built. Port of Flycast `gldraw.cpp` (GL path, no shader needed):
  - `apply_param()` sets `glStencilFunc(GL_ALWAYS, Shadow ? 0x80 : 0, 0xFF)`;
    `draw_scene()` enables stencil REPLACE for the background, OP and PT, so
    stencil bit 7 marks pixels whose visible polygon has the Shadow bit. The
    background takes `ISP_BACKGND_T` bit 27. Shadow is in the strip-merge key.
  - Volumes: an open volume (DepthMode > 0 without pcw.Volume) ORs bit 1, a
    closed one XORs it; DepthMode 1/2 then sums the area since the last close
    into bit 0 as inclusion / exclusion (`set_mvs_mode`, Flycast stencil
    funcs and ops). Replaces the nullDC-v1 per-triangle loop.
  - Final quad: stencil EQUAL 0x81 / mask 0x81, black at alpha
    1 - (FPU_SHAD_SCALE & 0xFF) / 256 (was a flat 50%), clears bits 1:0.
    `FPU_SHAD_SCALE` is snapshotted in `RenderRegs` (TR2 rule).
  - Not ported: the two-parameter mode (FPU_SHAD_SCALE bit 8, param1 colours),
    which Flycast's GL path does not do either; translucent volumes.
  - HW test: Crazy Taxi car shadows (~250 volume tris per frame): shadow on
    the road only, not on the HUD or sky, at the game's strength. Soul Calibur.
  - HW 2026-09-23: Matt, "Shadows look fine" (Crazy Taxi, Soul Calibur).

- 3g `[x]` built (big-endian checked).
  - `dc/gdrom/gdromv3.cpp` SPI_CD_READ: `other` or expected data type 1
    (CD-DA) now reads raw 2352-byte sectors. The `GDReadBlock` bitfields already
    have a BE branch, and the packet bytes are stored LE (`HOST_TO_LE16`), so
    the flags decode the same as on the DC. Buffers fit: DMA cache 32 x 2352,
    PIO 27 x 2352 < 64 KB, reader temp buffers >= 2352.
  - `libGDR_ReadSector()` zero-fills before the drive reads, so a FAD no track
    covers returns zeros. CDI already did; GDI and MDS left stale bytes.
- 3i `[x]` built (byte reads only, endian-neutral).
  - `dc/dc.cpp` `apply_disc_meta()` reads IP.BIN after the flash loads (GD-ROM
    FAD 45150, CD = start of the last session), checks "SEGA SEGAKATANA ".
  - Region: `dc/flash_fix.cpp` `dc_flash_set_region()` keeps the flash's region
    if the disc lists it, else picks one it lists (Flycast order), and writes
    both factory copies (0x1a002, 0x1a0a2). `dc_flash_restore_region()` puts
    the original bytes back in `mem_Term()` before `dc_flash_wb.bin` is saved,
    so the console's own region never drifts. GDI discs already read as "JUE"
    (nullDC `PatchRegion_0`), so they never switch.
  - Cable: if `Dreamcast.Cable` is VGA (0/1) and the disc has no VGA flag, the
    boot uses composite through `g_cable_override` (`bsc.cpp`). Not saved to
    `nullDC.cfg`. No per-game lists (Matt's rule).
  - Broadcast is not touched, as in Flycast.
  - Log: `[flash] region X fits` / `[flash] region X -> Y for disc area '...'`.
- HW test 3g/3i: boot Crazy Taxi and Soul Calibur. Read the `[flash]` line.
  Both must still boot. NBA Hoopz only if Matt has it.
- HW 2026-09-23: "still boots correctly" (Matt). Log: Crazy Taxi read
  `[flash] region 1 fits disc area 'JUE     '`. So Matt's flash region is USA
  (1), not EU as assumed earlier. No switch happened on that disc, so the
  switch path itself is still unexercised on hardware. NBA Hoopz not run.

Already covered, drop from the list: "do not clip negative-depth vertices"
(`4b0d377e6`). Our `vertex_inf()` never treats a negative 1/W as inf.

None of section 3 is a speed gain. 3a-3c share one piece of work: a real PVR
shader for `ps3gl`, which replaces the fixed-function texture modes.

RTT test discs (for `rtt=0/0`): Flycast `core/emulator.cpp:64-157` lists games
that need RTT copy to VRAM - Tony Hawk 1/2, Shenmue, Jet Set Radio, Skies of
Arcadia, Grandia II, Worms World Party, Silent Scope.

---

## 5. SH4-side speed (Matt, 2026-09-23)

The 2.4 run showed the dense-scene limit is the SH4 thread (`slept=0`). Split
in Crazy Taxi dense traffic: arm 7.4-9%, aica 4-4.5%, ta 3-4.3%. The other ~80%
is JIT code and its C calls, not split yet. The PS3 JIT runs with almost every
speed feature OFF. Matt allowed the Wii-era JIT code for this work.

| # | Item | Status |
|---|---|---|
| 5.0 | Census: `IFB Probe` + `JIT C-Calls` toggles (VIDEO / DEBUG) | `[x]` HW, results below |
| 5.1a | `jit_fschg` (JIT SPEED page, `JitFschg`) | `[x]` HW, no fault, speed within noise |
| 5.1b | `jit_fsqrt` as inline `fsqrts` (JIT SPEED page, `JitFsqrt`) | `[x]` HW, no fault, dense SPEED +2.5 points |
| 5.1c | `JitNewOps` (rotcl/rotcr, SR ops), Emulation page | `[x]` HW, no fault, speed within noise |
| 5.1d | `fpu_pin` | `[ ]` |

FSCHG and FSQRT are default ON since 2026-09-23 (Matt).
| 5.2 | `JitCarry`: fix, then test | `[ ]` BROKEN on PS3, see below |
| 5.3 | `JitMac`, `jit_cr0`, `jit_tfwd`, `dyn_ic`, `bcache` | `[ ]` |
| 5.4 | ARM7 JIT port (`arm7_jit.cpp` is Wii-gated) | `[ ]` |

- 5.0: `nullDC.cfg [ps3] IfbProbe` / `JitCCalls`. IFB Probe is read at compile
  time, so set it before boot. The log gets `[IFB]` and `[CC]` lines once a
  second.
- 5.0 HW 2026-09-23, Crazy Taxi, Dynarec + JIT Fastmem, 102 dense windows
  (vt > 3K, SPEED < 90%): SPEED 56.7%, RPS 33.9, 1.20 M C calls/s,
  115 K IFB/s.
  - C calls/s: `sync_fpscr` 442 K, `sqw` 371 K, `fsqrt` 165 K, `getcode`
    75 K, `writemem` 67 K (49 K from JIT code, mostly a0 regs and a7),
    `timeslice` 63 K, `fsrra` 7 K.
  - IFB/s: rotcl 45.6 K, rotcr 21.8 K, addc 18.8 K, subc 15.3 K, div1 5.0 K,
    stc SR 3.6 K, ldc SR 2.6 K, lds FPSCR 1.4 K.
  - Light scenes (SPEED ~100%) have 3.8x fewer IFB and 16x fewer
    `sync_fpscr`, so the T&L code of dense scenes is where these costs are.
  - Cell note: the PPE has a large load-hit-store penalty, which the Wii CPU
    did not have. Features that keep values out of memory (`fpu_pin`,
    `jit_tfwd`, `jit_cr0`) can gain more here than their 0-3% on the Wii.
- 5.1a: no code change, the Wii path was already in `ps3_driver.cpp`. It skips
  the C call and its `mtfsfi`.
- 5.1b: the Wii path was a Newton-Raphson for a CPU without `fsqrt`. The PPU has
  `fsqrts`, and GCC already compiles `rec_fsqrt`'s `sqrtf` to it, so inline
  `fsqrts` is bit-identical. `fsrra` = `fsqrts` + `fdivs`, as `1.0f/sqrtf`.
- 5.1 HW 2026-09-23, Crazy Taxi, both probes ON in all three runs. Dense =
  vt > 3K and SPEED < 90%.

  | Run | dense n | SPEED | RPS | `sync_fpscr`/s | `fsqrt`/s |
  |---|---|---|---|---|---|
  | census (both OFF) | 102 | 56.7% | 33.9 | 442 K | 165 K |
  | boot 1 (FSCHG) | 85 | 56.4% | 33.8 | 111 K | 161 K |
  | boot 2 (FSCHG + FSQRT) | 98 | 59.0% | 35.4 | 107 K | 0 |

  - Same-load bins agree: vt 6-9K 60.5 / 60.1 / 63.1%, vt 12K+ 47.1 / 48.4 /
    51.2%. Matt: boot 2 looked fine and felt faster.
  - FSCHG removed 331 K C calls/s with no speed change, so a plain C call is
    cheap here. FSQRT gained more with half the calls removed.
  - The 107 K `sync_fpscr`/s left are not `fschg` (likely `frchg`).
- 5.1c audit: no code change. Every SHIL op the NewOps decoders emit is already
  used by base opcodes on PS3: `mov32`, shifts by an immediate (`rlwinm`), `and`
  with an immediate (`andi.`), `or`, `seteq`, byte and `@(disp,Rn)` memory ops,
  `sync_sr` (`rte`), and the full `sync_fpscr` (`frchg`). None of them use
  carry, `addis` or Rc=1 on a pinned register.
- 5.1c HW 2026-09-23, Crazy Taxi, same settings as boot 2 plus NewOps: dense
  SPEED 59.3% (boot 2: 59.0%), vt 6-9K 63.6% (63.1%), vt 12K+ 51.1% (51.2%).
  IFB 120 K/s -> 41 K/s; only addc, subc, div1 are left. `interrupt` rose
  0.5 K -> 3 K/s: the `ldc SR` block ends call `UpdateINTC`, as designed.
- Lesson from 5.1a and 5.1c: removing 330 K C calls/s or 80 K IFB/s does not
  move SPEED. The time is inside the emitted code, not at its C boundary.
- 5.2: `adde`/`subfe`/`subfze`/`addo`/`subfo` set CA and OV from 64 bits on the
  PPU. Pinned regs have garbage upper halves (`jit-64bit-poison`), so T is
  wrong. Fix: zero-extend the operands and take the carry from bit 32.
- `jit_fmov` does not apply: it needs the Wii DSI fastmem, which is off.
- Dropped: `SmcCheckLevel` (the guard is 2 halfword loads), `regalloc`, SSA.

---

## 4. Do not take

| Item | Reason |
|---|---|
| x64/ARM JIT backends, DSP JITs | no PPC |
| OIT / per-pixel sorting, Vulkan, GL4 | RSX cannot run them |
| VRAM lock, SMC page tracking | need mprotect |
| Naomi, network, savestates | out of scope |
| SSA optimizer (`dyna/ssa.cpp`) | JIT work measured at 0-3% (`Optimization.md` 4) |
| CHD/CUE, reios | Reicast and Minicast have them too |
