# Optimization plan - what to take from Flycast

Review of Flycast (`E:\Users\Matteo\Desktop\Dreamcast\flycast`) against our tree,
Reicast and Minicast. Written 2026-09-23. Flycast is an allowed reference from
this date (Matt).

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Flycast is little-endian only. Check every ported line for byte order.

---

## 0. Resume here (updated 2026-10-03)

**Active work: section 5 (speed). Matt wants full speed over accuracy.**

Where it stands (Crazy Taxi, dense traffic):
- All of these are default ON and HW validated: JIT FSCHG, FSQRT, NewOps,
  FPU PIN, T FORWARD, CR0 BRANCH (JIT SPEED page), ARM7 JIT level 5, and
  Vertex Arrays (VIDEO / DEBUG). Build of 20:36 has all of them.
- Matt tests with Dynarec ON, JIT Fastmem ON, SH4 Clock 150 MHz. Dynarec and
  JIT Fastmem are still default OFF, and an install resets the settings.
- Dense VPS 50.7 of 59.95 at 150 MHz; 21% of dense seconds at full speed
  (after 5.10). Audio still gaps when below full speed.
- The render thread is busy 40% of wall on the same PPE core as the SH4
  thread. Dense split, % of wall: TA decode 19, draw 21 = vtx+other 8.5 +
  texture bind 6.3 (gl 2.8, hash 1.9) + glEnd 3 + sort 1 + modvol 1.

Next, in this order (agreed with Matt):
1. 5.9: done, HW 2026-10-03 (busy 57 -> 47%, VPS unchanged).
2. 5.10: done, HW 2026-10-03 (hash 8.2 -> 1.9%, dense VPS 48.5 -> 50.7,
   no stale textures).
3. 5.11: reverted 2026-10-03 (Matt). Moving the vertex math to the decode
   gave no net gain (decode +2.3, draw -2.2 points) and felt worse.

SPE work resumed 2026-10-03 (Matt), after 5.9-5.11. 5.11 was reverted, so
the SPE decoder (5.12 S2) writes the 5.10 vertex format.
- 5.12 S0 (doorbell) is HW validated.
- 5.12 S1 (command ring + raw pull + fence check) is HW validated
  2026-10-03: 6050 of 6050 fences match. Same toggle, default OFF
  (VIDEO / DEBUG -> "SPE Doorbell"). Next: S2. See 5.12.
- 5.13 A0 is not started.

Workflow: `./build.sh pkg`, UDP listener on port 18194 (scratchpad
`udplog.py`), log analysis by windows with vt > 3K. Probes: IFB Probe and
JIT C-Calls (VIDEO / DEBUG) give `[IFB]`/`[CC]`/`[ARMJIT]`; the `[rend] busy=`
`[rend] draw split %:` and `[rend] tex split %:` lines are always on.

Other open items: the XMB exit always takes the "SH4 did not stop in 3 s"
fallback (seen in every run, not new). JitCarry is broken on PS3 (5.2).
Why the ARM7 JIT time saving did not show as SPEED: likely SMT sharing with
the render thread (5.6).

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

- 2026-09-29: see 5.13. The SPE AICA plan runs caique's measured DSP on an
  SPE, which may replace the PPC DSP JIT. Matt's decision.

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
| 5.1d | `fpu_pin` (JIT SPEED page, `JitFpuPin`) | `[x]` HW, no fault, dense SPEED +3.6 points |

| 5.1e | `jit_tfwd` + `jit_cr0` (JIT SPEED page, `JitTfwd` / `JitCr0`) | `[x]` HW, no fault in the log, dense SPEED +3.5 points |

| 5.4 | ARM7 JIT port (JIT SPEED page, `Arm7Jit` 0..5) | `[x]` HW, self-test PASS, sound fine, `arm%` 9.1 -> 5.4 |
| 5.5 | SH4 Clock 150 MHz (existing Emulation option, speed over accuracy) | `[x]` HW, dense VPS +16%, not full speed |
| 5.6 | Render thread busy % (`[rend] busy=` line) + SPEED/VPS target vs effective clock | `[x]` HW, dense busy 61% |
| 5.7 | Draw split (`[rend] draw split %:` line) | `[~]` built, HW pending (runs with 5.8) |
| 5.8 | Vertex arrays for merged strips (VIDEO / DEBUG, `VertexArrays`) | `[x]` HW, dense VPS +6%, busy 61 -> 57%, default ON |
| 5.9 | `collect_diag()` once per stats window | `[x]` HW, busy 57 -> 47%, VPS unchanged |
| 5.10 | `ps3tex_bind` cost (~12% of wall) | `[x]` HW, faster hash: hash 8.2 -> 1.9%, busy 47 -> 40%, no stale textures |
| 5.11 | TA decode writes the final vertex format | REVERTED 2026-10-03 (Matt): no net gain, felt worse |
| 5.12 | SPE offload: TA decode at submit time, then vertex build, texture hash | SHELVED 2026-10-03 (Matt) after S0 `[x]` HW; resume at S1 after 5.9-5.11 |
| 5.13 | AICA on an SPE: control pass on the PPU, sample work + DSP on the SPE (caique model) | SHELVED 2026-10-03 (Matt) until 5.9-5.11 are done; A0 not started |

FSCHG, FSQRT, NewOps, FPU PIN, TFWD and CR0 are default ON since 2026-09-23 (Matt).
ARM7 JIT level 5 is the default since 2026-09-23 (Matt).
Vertex Arrays are default ON since 2026-09-23 (Matt: geometry correct).
Matt, 2026-09-23: speed over accuracy. The Flycast cycle model was dropped for
the existing SH4 Clock option. SPEED% reads against 200 MHz, so judge a lower
clock by VPS.
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
- 5.1d audit: no code change. FPRs carry no 64-bit poison, and f14-f29 are
  callee-saved in ELFv1 as on the Wii. Every FP emitter uses the pin-aware
  helpers (`fsrc_or_load`, `fdst_reg`, `ppc_fvec_*`, the int bounce in
  `ppc_sh_load/store`). The PS3 JitFastmem path already carries the
  float-direct shapes (`ColdFrag::fdreg`, `emit_vmem_tier`), and with the Wii
  fastmem off those shapes are active whenever `fpu_pin` is on. Every SHIL op
  has a native case, so the canonical fallback (whose reload would overwrite a
  pinned result) is never reached. The Wii default preset ran `fpu_pin=on`.
- 5.1d HW 2026-09-23, Crazy Taxi, NewOps settings plus FPU PIN. Matt: runs
  better, no visual artifacts. Code cache clears only at boot, as before.

  | Run | dense SPEED | vt 6-9K | vt 9-12K | vt 12K+ |
  |---|---|---|---|---|
  | census (all OFF) | 56.7% | 60.5% | 54.6% | 47.1% |
  | NewOps | 59.3% | 63.6% | 56.6% | 51.1% |
  | + FPU PIN | 62.9% | 67.6% | 59.9% | 51.5% |

  The 12K+ bin of the FPU PIN run carried more load (15.3K vs 13.7K vtx).
  Total so far: dense SPEED +6.2 points (+11%), RPS 33.9 -> 37.7.
  This confirms the lesson above: gains come from inside the emitted code.
- 5.1e audit: no code change. Every compare that feeds T is 32-bit
  (`cmpw`/`cmplw`/`cmpwi`/`cmplwi`), and `tst` uses `andi.` or `and` +
  `cmplwi` since the SC5 fix. The CR0 path only runs when the compare is the
  block's last op, so no Rc=1 op can sit between it and the branch. The
  forwarded T in rarg0 is a clean 0/1 from `rlwinm`.
- 5.1e HW 2026-09-23, Crazy Taxi, FPU PIN settings plus both toggles
  (`[jitcfg] cr0=1 tfwd=1`). 196 s, no fault, clears only at boot.

  | Run | dense SPEED | vt 6-9K | vt 9-12K | vt 12K+ | dense RPS |
  |---|---|---|---|---|---|
  | FPU PIN | 62.9% | 67.6% | 59.9% | 51.5% | 37.7 |
  | + TFWD + CR0 | 66.4% | 71.3% | 63.3% | 56.1% | 39.7 |

  Total since the census: dense SPEED 56.7% -> 66.4% (+17%), RPS 33.9 -> 39.7.
  The XMB exit took the "SH4 did not stop in 3 s" fallback, as in every run
  of this session; not new.
- 5.4 port, 2026-09-23. `plugs/vbaARM/arm7_jit.cpp` and `arm7_selftest.cpp`
  now build (Makefile EXCLUDE), `ps3/ps3_nojit.cpp` (the stubs) is deleted.
  PS3 fixes:
  - Code in its own 2 MB `.text` array; the cached interpreter's block table
    is still borrowed (it is data, not code).
  - C calls through `ppc_call_c` (OPD entry); the entry stub is called through
    an OPD built at stub time; 176-byte ELFv1 frame with `std`/`ld`, LR at 16.
  - `arm_aica_ram` and the block table are 8-byte pointers: `ld`, index * 8.
  - ADD/ADC/CMN C flag = bit 32 of the sum (`rldicl`): 64-bit CA never fires
    for zero-extended operands. SUB-type CA is exact once operands are clean,
    so ASR results and `li` immediates are zero-extended (`e_li32`).
  - The size-4 MMIO test used a wrap mask with Rc=1, which sees the upper half
    on PPC64 (every access would go slow): now `rlwinm` + `cmplwi`.
  - u32 C arguments (addresses, values, opcodes) are zero-extended.
  - The 45-binary ARM7DI self-test runs at boot when the level is > 0 and
    falls back to the cached interpreter on any failure.
- 5.4 HW 2026-09-23, Crazy Taxi, level 5, all JIT toggles ON, probes ON.
  `[ARM7TEST] PASS: all 45 tests passed`, 153 s, no freeze, 0 ARM compiles/s
  in play, 0 slow memory calls.
  - Dense `arm%` 9.1% -> 5.4%. Same-load SPEED unchanged: vt 6-9K 71.3 ->
    71.6%, vt 9-12K 63.3 -> 63.3%. The dense average fell (66.4 -> 64.0%)
    only because this run carried more load (10.7K vs 9.5K vtx).
  - `[ARMHOT]`: 99% of ARM instructions are a 4-instruction idle loop at
    0x34 (`ldr r0,[pc,#12] / add r0,r0,#1 / str r0,[pc,#4] / b 0x34`), a
    counter the sound driver bumps while it waits for the timer FIQ.
  - Render thread `wait=` stayed 0-2 ms, so the SH4 thread is not waiting on
    it; why the freed ARM time did not show as SPEED is still open.
- 5.5 HW 2026-09-23, Crazy Taxi, SH4 Clock 150 MHz, all JIT options ON.

  | Clock | dense VPS | vt 6-9K | vt 9-12K | vt 12K+ (RPS) | silent audio blocks/s |
  |---|---|---|---|---|---|
  | 200 MHz | 39.4 | 42.9 | 38.0 | 30.9 (30.9) | 53 |
  | 150 MHz | 45.7 | 45.5 | 43.5 | 53.1 (31.2) | 28 |

  - +16% dense VPS, audio gaps halved; no dense second reached full VPS.
  - The gain is well under the 33% the clock cut would give if the SH4 were
    the whole cost. Per-frame work that does not scale with SH4 cycles (the
    render thread on the same PPE core, AICA/ARM per sample) takes the rest.
    This also fits the ARM JIT result.
  - In the heaviest scenes the game now drops its own frames (VPS 53, RPS
    31), which is the intended trade.
  - Cosmetic bug: `SPG.cpp:270` computes the full-rate VPS with a fixed 200,
    so at 150 MHz the stats line shows the NTSC target as 79.93 and SPEED%
    75% at full speed. Fixed in 5.6: both now use `SH4_CLOCK_EFF`.
- 5.6: `ps3/ps3_rend.cpp` prints `[rend] busy=N% (dec D draw W end E)` every
  5 s: wall time the render thread spends in the TA replay, in `draw_frame`
  (texture decode included) and in `ps3_video_end_frame`, as % of the window.
- 5.6 HW 2026-09-23, Crazy Taxi, SH4 Clock 150 MHz, all JIT options ON.
  The stats line now reads the NTSC target as 59.95 at 150 MHz.

  | Scene (vtx/render) | windows | busy | dec | draw | end | draws/render |
  |---|---|---|---|---|---|---|
  | light (< 1.5K) | 8 | 6% | 1 | 5 | 0 | 18 |
  | mid (1.5-5K) | 3 | 13% | 4 | 9 | 0 | 92 |
  | dense (>= 5K) | 17 | 61% | 19 | 41 | 0 | 507 |

  - In dense traffic the render thread works 61% of the time on the same PPE
    core as the SH4 thread (SMT). Its cost is draw 41 + TA replay 19; the
    ps3gl flush/present (`end`) costs nothing.
  - Dense traffic: ~40 renders/s x ~9.9K vertices x ~507 draws. Draw: ~20K
    draws/s and ~400K vertices/s in ~410 ms/s. The per-vertex ps3gl path is
    three small stores, so the per-draw cost (state, texture bind, flush)
    is the likely bulk. Not yet split.
  - Dense VPS at 150 MHz: 48.3 (earlier 150 MHz run 45.7), 10% of dense
    seconds at full VPS.
- 5.7: `[rend] draw split %: param P (tex T) glEnd G sort S modvol M
  vtx+other V`, % of the window. `param` is `apply_param` on a cache miss
  (tex = its `ps3tex_bind` part), `glEnd` is the ps3gl draw submission,
  `sort` the TR autosort keys + sort, `modvol` the modifier volumes, and
  `vtx+other` the rest of `draw_frame` (vertex emit, strip merge, list walks).
- 5.8 (Matt, 2026-09-23): vertex buffers + vertex arrays.
  - `ps3gl_draw_vb(mode, vb_offset, first, count)` (new, `ps3gl_vertices.c`):
    applies the GL state like `glEnd`, binds the four attributes of a
    caller-filled vring block and issues `rsxDrawVertexArray`. No copy.
  - `draw_scene` reserves one vring block per scene (worst case 3 x nvtx +
    4 x nstrip). `run_emit` writes each merged-strip vertex once, straight
    into it in `ps3gl_vertex_t` form (x*W, y*W, W, 1, u, v, RGBA); each run is
    then one `ps3gl_draw_vb` range. Removes, per vertex, three ps3gl calls,
    the write to ps3gl's immediate buffer and the `memcpy` into the vring.
  - Background, modifier volumes and Strip Merge OFF keep glBegin/glEnd. A
    failed vring reserve falls back to glBegin/glEnd for that scene.
  - TA decode is unchanged; writing the final format at decode is a later step.
- 5.7/5.8 HW 2026-09-23, boot A (Vertex Arrays OFF), dense windows, % of wall:
  draw ~42 = vtx+other ~22, param ~13 (tex bind ~12), glEnd ~5, sort ~1,
  modvol ~1; TA decode ~19. `ps3tex_bind` at ~12% is a second target.
- 5.9 built 2026-10-03: `draw_scene()` runs `collect_diag()` only when the
  window is over (`os_GetSeconds() - s_stat_t0 >= STAT_WINDOW_S`). A render
  job always ends in `rth_print_stats()`, so `[rendz]` still shows the last
  drawn frame of the window. HW: `vtx+other` must drop; `[rendz]` unchanged.
- 5.10 built 2026-10-03, split only, no fix yet. New line after the draw
  split: `[rend] tex split %: hash H decode D gl G other O | hash/render n=N
  KB=K`. `hash` = `hash_texture()` (first bind of each texture per frame),
  `decode` = decode + upload on a miss or a changed hash, `gl` = the final
  `glBindTexture` + 4 `glTexParameteri`, `other` = cache probe + `describe()`.
  Suspects from the code:
  - `hash`: a serial FNV over the whole source of every texture used, every
    frame. Each word is a dependent `xor` + `mullw`; the VRAM reads miss L2.
    KB/render sizes it.
  - `gl`: `ps3gl_texture_find()` is a linear scan of up to 4096 slots
    (~64 B each, 256 KB) on every `glBindTexture`. Each
    `glTexParameteri` also sets `t->dirty`, so every bind re-sends the RSX
    texture commands even for the same texture and state (cost lands in
    `glEnd`).
- 5.9/5.10 HW 2026-10-03, Crazy Taxi, 150 MHz, Dynarec + JIT Fastmem. 16
  dense windows (vtx/render >= 5K, avg 9.3K), 82 dense seconds (vt > 3K).

  | | busy | dec | draw | param (tex) | glEnd | vtx+other | dense VPS | full-VPS secs |
  |---|---|---|---|---|---|---|---|---|
  | 5.8 | 57% | 19 | 38 | 13 (11.9) | 3.0 | 20.0 | 49.0 | 16% |
  | 5.9 | 47% | 19 | 27 | 13.3 (12.4) | 3.1 | 8.6 | 48.5 | 9% |

  - 5.9: `collect_diag()` cost ~11% of wall. `[rendz]` unchanged. Dense VPS
    did not move: as with the ARM7 JIT (5.4), freed render-thread time does
    not show as VPS.
  - 5.10 split, % of wall: hash 8.2, gl 2.6, other 1.0, decode 0.6.
    - hash: 135 textures and 514 KB per render, ~22 MB/s, ~46 cycles per word.
    - gl: ~20 K binds/s at ~1.3 us each, so the ps3gl linear slot scan.
- 5.10 fix (Matt, 2026-10-03): faster hash only, on the PPU. The SPE hash
  waits until 5.11 is done; the ps3gl bind fix (gl 2.6%) is not chosen.
  - `hash_range()` (`ps3/ps3_tex.cpp`): 64-bit loads, 4 independent lanes,
    16 words per pass with `dcbt` 256 bytes ahead. Lane step = Jenkins
    one-at-a-time (`h += w; h += h << 10; h ^= h >> 6`), no multiply in the
    loop (checked in the disassembly: `ld` + `dcbt`, `mulld` only in the
    final mix). The step is a bijection in h and in w, so one changed word
    always changes the 64-bit result, as FNV did; fold to 32 bits at the end.
  - A 4-byte-aligned start (mip offsets) takes one 32-bit head word; a span
    past the end of VRAM keeps the masked word loop.
  - HW test: Crazy Taxi dense, read `tex split` hash (was 8.2%) and check
    for stale textures (FMV, menus, the in-game map).
  - HW 2026-10-03, Crazy Taxi, same settings, 180 s, no errors in the log.
    Same load as the 5.9 run (17 dense windows, 9.3K vtx, 513 KB hashed per
    render).

    | | busy | draw | tex | hash | dense VPS | full-VPS secs | vt 6-9K | vt 9-12K | vt 12K+ |
    |---|---|---|---|---|---|---|---|---|---|
    | 5.9 | 46.6% | 27.1 | 12.4 | 8.2 | 48.5 | 9% | 47.7 | 48.2 | 50.0 |
    | hash | 40.2% | 20.8 | 6.3 | 1.9 | 50.7 | 21% | 48.5 | 50.6 | 53.4 |

    Hash 4.3x faster (~11 cycles per word). Left in tex: gl 2.8, other 1.0,
    decode 0.6. Matt: no stale textures (FMV, menus, arrow, map).
- 5.11 built 2026-10-03 (`ps3/ps3_rend.cpp`).
  - `DcVertex` is now the draw form, 24 bytes: x*W, y*W, W, u, v, RGBA.
    `vb_emit` is a 6-word copy plus the 3 constant words of
    `ps3gl_vertex_t` (w = 1, no TC1); no float math left in the draw.
  - Alpha force (apply_param's rule) is applied at decode from the current
    param (`s_alpha_or`). A texture always binds (sizes cap at 1024), so the
    rule needs no bind result.
  - Inf vertices (Flycast `is_vertex_inf`) are dropped at decode: the strip
    is split there and each piece keeps the index parity of its first
    vertex (`DcStrip::parity`). `draw_strip` has no per-vertex inf test.
    Strip Merge OFF leads an odd piece with a copy of its first vertex, and
    now drops inf vertices too. `[rendz]` x/y no longer show inf values.
  - Decode hot path: the 1/W range test uses float compares (the int union
    was a store + reload, a load-hit-store on the PPE); 16-bit UVs store
    their bits straight (no int-to-float round trip).
  - Checked offline (`split_sim2.py`, session scratchpad): 30000 random
    strip sets with inf vertices and cull modes give the same triangles as
    the old draw-time skip, culling taken into account; dropping the parity
    term fails 781 of 2000; Strip Merge OFF pieces 0 of 20000 wrong.
  - HW test: Crazy Taxi dense (`dec`, `vtx+other`, VPS) and Soul Calibur
    (the inf path moved). Look for holes, wrong colours or alpha.
  - HW 2026-10-03, first build, Crazy Taxi, 196 s, no faults, drop 0,
    overflow 0. 15 dense windows, 8.6K vtx and 48 renders/s (last run:
    9.3K, 42/s), so read it per render:

    | per render | dec | vtx+other | busy | dense VPS | full-VPS secs |
    |---|---|---|---|---|---|
    | 5.10 | 4.60 ms | 2.02 ms | 9.57 ms | 50.7 | 21% |
    | 5.11 a | 4.57 ms | 1.34 ms | 8.86 ms | 48.3 | 3% |

    Per vertex the decode got ~8% slower (493 -> 531 ns). Cause in the
    disassembly: `strip_close()` was an out-of-line call inside the vertex
    loop, so GCC stopped keeping the decode counters in registers (load +
    store per vertex). Fixed: `strip_close()` is always_inline; the loop
    now has no call. VPS is not comparable: other scenes and load.
  - HW 2026-10-03, build b (inline), Crazy Taxi, 182 s, no faults. Same
    load as the 5.10 run (16 dense windows, 9.4K vtx, 214 renders/window).
    Matt: the game felt worse.

    | | busy | dec | vtx+other | dense VPS | full-VPS secs | vt 6-9K | vt 9-12K | vt 12K+ |
    |---|---|---|---|---|---|---|---|---|
    | 5.10 | 40.2% | 19.3 | 8.5 | 50.7 | 21% | 48.5 | 50.6 | 53.4 |
    | 5.11 b | 40.5% | 21.6 | 6.3 | 49.9 | 14% | 49.3 | 49.5 | 49.9 |

    The work moved from the draw to the decode with no net gain (decode
    535 ns per vertex, was 493). The inline did not help. ~1600 cycles per
    vertex is far above the math, so memory stalls are the likely bulk
    (raw stream read back after the SH4 wrote it, vertex stores without
    a prefetch). Not measured.
  - REVERTED by Matt, 2026-10-03: `ps3/ps3_rend.cpp` is back to the 5.10
    code (checked: the type-3 decode loop disassembles as in the 5.10
    build). Not tried: `dcbt` prefetch of the raw stream and `dcbz` on the
    vertex lines, to test the memory-stall guess.
- 5.12 (design agreed with Matt, 2026-09-29): SPE offload. Decode the TA
  stream on an SPE while the SH4 submits it, not at STARTRENDER.
  - Why: the PS3 has one PPE core with 2 hardware threads. The SH4 thread and
    the render thread share it. TA decode (~19% of wall) and `vtx+other` (~20%)
    are streaming work: raw 32-byte records in, vertices out. 6 SPEs are free.
  - Toolchain checked: `ps3dev-latest:latest` has `spu-gcc`/`spu-g++` 7.2,
    `spu_rules`, `libsputhread`, `libspudma`, `libspuatomic`, `libspumars`,
    `libspurs`. Build pattern: PSL1GHT `samples/spu` (`spu/` sub-Makefile,
    `spu.bin` linked in with `bin2o`, `sysSpuImageImport`). No code calls
    `sysSpuInitialize` today.
  - Samples to follow: `sputhread` (one thread that stays alive), `spudma`
    (`mfc_get`/`mfc_put` + tag wait), `spuparallel` (fenced done word with
    `mfc_putf`). Not used: raw SPU (`sputest`), SPU-to-SPU DMA (`spuchain`),
    MARS, SPURS.
  - Why it fits: the SH4 thread already copies every record into `c->raw`
    (`ps3ta_store_sq` / `ps3ta_store_dma`); only the store FSM runs there.
    `Ps3VertexDecoder` reads only the stream and its own state (`s_face_*`,
    `s_sprite_col`, `s_cur_list`) and only appends to its output arrays.
  - Notify: a lock-line reservation doorbell, not a syscall and not a PPU DMA.
    The SPU reads a 128-byte doorbell line with `mfc_getllar` and sleeps on
    `SPU_RdEventStat` (`MFC_LLR_LOST_EVENT`). A plain PPU store to the line
    wakes it. The SPU then pulls the new raw range itself in chunks of 16 KB
    or less, with double buffering.
    - The PPU publishes in batches (~2 KB) and always at list end, LIST_INIT,
      LIST_CONT, SOFTRESET and STARTRENDER. Cost per SQ record: one compare.
      Cost per batch: one `lwsync` + one store.
    - Rejected: `sysSpuThreadWriteSignal` per record (one lv2 syscall, and
      there are ~371 K `sqw` calls/s); SPU DMA polling (line ping-pong).
  - Design:
    - Command ring in main memory: `{DATA_TO pos, LIST_INIT, LIST_CONT,
      SOFT_RESET, FENCE}` + context. It carries what `cont_marks` and
      `set_ta_context` sequence today.
    - At each FENCE (STARTRENDER) the SPE writes the counts at that point
      (`nvtx, nstrip, nparam, nmtri, nmvol, max_inv_w`). The arrays only
      grow, so the render thread draws `[0, count)` while the SPE appends.
    - LIST_INIT on a context that is still drawing: the SPE waits at that ring
      entry. The SH4 no longer blocks in `ListInit` -> `rth_wait_decode`.
    - Output: the SPE stages in LS, writes with `mfc_put`, then a done line
      with `mfc_putf`. The render thread uses `lwsync` after it reads it.
    - Raw buffers to 128-byte alignment (now `memalign(32)`).
    - SPU float: no NaN, no Inf, no denormals, truncating rounding.
      `clamp_byte`'s `f != f` and `set_pos`'s `inv_w >= INV_W_FLOOR` must test
      the integer bits (`track_inv_w` already does). W may differ by 1 ULP.
    - Side effect: `StartRender` sets the render-end IRQ delay from
      `rc->nvtx`, which is the previous frame's count today. With early decode
      it is the current count, so the IRQ timing changes.
    - The store FSM stays on the PPU (list-end IRQ timing).
  - Steps, each ends with a HW test:
    - S0: doorbell ping. The PPU bumps a counter, the SPU echoes it. Measure
      wake latency and PPU cost.
    - S1: command ring + raw pull. The SPE returns a count + checksum per
      FENCE; the PPU checks them against its own decode. No visible change.
    - S2: decoder ported to the SPU into shadow arrays. Compare counts and
      hashes against the PPU decode at each FENCE.
    - S3: draw from the SPE output. Toggle, default OFF. Measure
      `[rend] busy=` and VPS.
  - SPE candidates, in order (from the dense Crazy Taxi split):

    | # | Job | Cost now | Fit |
    |---|---|---|---|
    | 1 | TA decode (S0-S3) | 19% of wall, render thread | good |
    | 2 | Strip merge + final vertex build (`run_emit`, = 5.11 on the SPE) | most of vtx+other ~20%, render thread | good; RSX vring by SPE DMA unproven, else main memory. `collect_diag` does not move (5.9) |
    | 3 | Texture hash (`hash_texture`, serial FNV over each texture on its first bind per frame) | part of `ps3tex_bind` ~12% | good; the SPE sees every TCW/TSP and can SIMD-hash at FENCE. Split the 12% first (hash / decode / `glTexParameteri`) |
    | 4 | Texture decode on a miss (twiddle, VQ, palette) | FMV / loading spikes | good; could write the swizzled layout directly |
    | 5 | `present_fb_convert` | SH4 thread, each vblank, direct-FB games only | good |
    | 6 | AICA DSP | not emulated (2.3 parked) | moved to 5.13 |
    | 7 | AICA channel mix (`GenerateAllFast`) | 4-4.5%, SH4 thread | moved to 5.13 (control/render split solves the readback) |

    Left out by Matt: the YUV FMV converter (latency-bound, less important).
    Stay on the PPE: SH4 JIT, ARM7 JIT, MMU, memory handlers, the TA store
    FSM, maple, GD-ROM, RSX/ps3gl calls.
  - S0 built 2026-10-03, HW pending. Toggle: VIDEO / DEBUG -> "SPE Doorbell"
    (`nullDC.cfg [ps3] SpeDoorbell`), default OFF, read in `InitRenderer`.
    - `ps3/spu/spe_bell.c` (spu-gcc, embedded with `bin2s`, `sysSpuImageImport`):
      one SPU thread, `mfc_getllar` on the bell line, sleeps on
      `MFC_LLR_LOST_EVENT`, echoes the seq with `mfc_put`. A 1 ms decrementer
      event is a fallback; a new seq found by it alone counts as `rescued`
      (a lost reservation event).
    - `ps3/ps3_spe.cpp`: boot test, 1000 pings (round-trip min/avg/max) and
      100000 back-to-back rings against a plain store + `lwsync`. Log `[spe]`.
    - In play: rings every 2 KB of TA data and at list end, LIST_INIT,
      LIST_CONT, SOFTRESET, STARTRENDER (`ps3spe_note_bytes` /
      `ps3spe_ring_event`). `[spe] rings/s handled/s wakes/s timeouts/s
      rescued lag` follows the `[rend]` lines every 5 s.
    - Link: `.got` 0x9630, 0 `r2off`.
  - S0 HW 2026-10-03, Crazy Taxi, 150 MHz, Dynarec + JIT Fastmem, 3 min.
    - Boot: `sysSpuInitialize(6,0)` = 0. Ping x1000: round trip min 0.49,
      avg 0.52, max 8.82 us, lost 0. Ring back to back: 32.6 ns, against
      20.0 ns for a plain store + `lwsync`.
    - Play: 4-7 K rings/s in dense traffic, handled/s = rings/s in every
      window (no coalescing), lag 0. PPU cost ~5 K x 33 ns = ~0.02% of a thread.
    - Dense (vt > 3K, n=88): VPS 48.6, 13% of seconds at full VPS. The 5.8
      run read 49.0 / 16%: within noise.
    - `rescued` rose to 30 (~0.3/s, grows with the ring rate). Likely not
      lost events: a ring that lands between the decrementer wake and the
      next `getllar` also counts, and its own LLR event follows. S1 checks
      the pending LLR bit (`spu_readchcnt(SPU_RdEventStat)`) before counting.
    - XMB exit: "SH4 did not stop in 3 s" fallback as in every run, so
      `TermRenderer` / `ps3spe_term` did not run; the process exit took the
      SPU group down.
  - Gain: 1-3 are ~45-50% of wall on the shared PPE core. The VPS gain is
    unknown until HW: freed ARM7 time did not show as SPEED (5.4).
  - S1 built 2026-10-03, HW pending. Same toggle ("SPE Doorbell").
    - Ring: 4096 x 16-byte `SpeCmd` in main memory; the bell line now holds
      the ring head. Ops: DATA (ctx, ea, bytes, to), RESET (ctx), CONT (ctx,
      mark), SOFT, FENCE (ctx, id, raw end), NOP. The SPU fetches up to 64
      entries per DMA and puts its tail in the echo after each batch. A full
      ring makes the PPU wait (`full=`); full for 100 ms turns the SPE off.
    - PPU hooks (`ps3_rend.cpp`): `spe_note` sends DATA every 2 KB per
      context (`TaContext::spe_pub`); `spe_flush` at list end, context
      switch, LIST_CONT (then CONT with the mark), SOFTRESET (then SOFT) and
      STARTRENDER; `reset_raw` sends RESET. `rth_run` sends a FENCE only for
      a job it takes (dropped renders get none).
    - SPU: DATA pulls the range in 16 KB chunks, double buffered, and adds
      it to a per-context hash: 4 word lanes, `a += w; b += a` (one quadword
      per step). FENCE puts a 64-byte `SpeFence` (id, pos, ncont, gaps,
      hash), then the echo with `mfc_putf`.
    - Check: `decode_pending` hashes `[raw_decoded, raw_end)` the same way on
      the PPU (scalar, before the replay). After `rth_mark_decoded`,
      `ps3spe_check` waits up to 5 ms for the fence and compares pos, ncont
      and the 8 hash words. The PPU hash adds to `dec` while the toggle is ON.
    - `rescued` fix: a new head found after a decrementer wake counts only if
      no LLR lost event is pending (`spu_stat_event_status`).
    - Raw buffers now `memalign(128)`.
    - Log: `[spe] ping`/`push` at boot; every 5 s `[spe] cmds/s data KB/s
      wakes/s timeouts/s rescued lag full gaps cont_err | fences ok bad late
      wait avg max`. The first 8 mismatches print `[spe] fence N MISMATCH`.
    - Known race: if the SPU lags past the replay and a LIST_INIT on that
      context, it pulls the new bytes and reports a mismatch. Expected never
      (the SPU runs µs behind, the replay ms). S2 must close it.
    - Link: `.got` 0x96D8, 0 `r2off`.
    - HW 2026-10-03 runs 1-2: the SPU hung at the first FENCE. The LS dump
      (`[spe] STALL`, `dump_ls_echo`) showed the result put to EA 0x40: the
      SPU `main` gets only 3 thread arguments, `arg3` arrives as 0. Fixed:
      ring and results share one block (`SpeShared`), passed in `arg2`.
      The LS dump stays (first late fence or full ring, at most 3).
    - HW test: Crazy Taxi, toggle ON. Pass = `bad=0 late=0 gaps=0
      cont_err=0`, `fences` = renders, no VPS loss against the S0 run.
    - HW 2026-10-03 run 3 (fixed build), Crazy Taxi, 150 MHz, Dynarec + JIT
      Fastmem, 189 s: PASS. 29 windows, fences = renders in every window
      (6050), ok 6050, bad 0, late 0, gaps 0, cont_err 0, lag 0, rescued 0
      (the S0 count was 30: the fix holds). Fence wait avg 0.2-0.3 us, max
      0.7 us: the SPU is always done before the replay ends. Peak 7253
      cmds/s and 16.8 MB/s of TA data.
    - Cost of the check (PPU hash in `dec`), 16 dense windows, same load as
      5.10 (9.2K vtx, 216 renders/window):

      | | busy | dec | vtx+other | dense VPS | full-VPS secs |
      |---|---|---|---|---|---|
      | 5.10 | 40.2% | 19.3 | 8.5 | 50.7 | 21% |
      | S1 ON | 43.3% | 22.0 | 8.8 | 48.5 | 9% |

      S1 is a check only, so this is overhead, not the S2/S3 result.
    - `full=` over-counts: `s_tail_seen` only refreshes when the ring looks
      full, so each ring wrap counts once (120 in this run, lag stayed 0).
      Count only real waits if it matters.
    - XMB exit: the "SH4 did not stop in 3 s" fallback again (not new).
  - Flycast reference: `BaseTAParser` in `flycast/core/hw/pvr/ta_vtx.cpp`
    (parser class, not read in detail yet).
- 5.13 (design agreed with Matt, 2026-09-29): AICA on an SPE, split like the
  TA. A light control pass stays on the emulation thread; the SPE does the
  sample work, the DSP and the mix.
  - Base: caique-rtl `model/sample-model/aica_model.{h,cpp}` (~1100 lines,
    MIT, Stefanos Kornilios Mitsis Poiitidis; keep the notice). 64 slots + DSP
    + mixer, `write()`/`read()`/`step()` = one 44.1 kHz sample. Integer only,
    so the SPU float limits do not apply. Checked against the console (4.29 M
    filter samples, 89/89 envelope streams, the DSP cases).
  - Split of `step()`. The control side never reads decoded sample data:
    - `step_control()`, PPU: key events (KYONEX/KYONB latch), CA phase step
      with pitch and PLFO, loop start/end, LPSLNK, one-shot end, AEG/FEG
      clocks, slot stop at 0x3C0, LFO state, the noise LFSR (fixed steps per
      sample; `LfsrJump` exists). Timers and interrupts stay on the PPU too.
    - `step_render()`, SPE: wave RAM fetch, ADPCM decode, interpolation,
      filter (`lpf_step`, `int64`), ALFO attenuation, sends, pan, MIXS, DSP,
      EXTS/CD-DA, final mix, 44.1 -> 48 kHz into the `ring` of
      `ps3_audio.cpp` (the feeder thread is unchanged).
  - The SPE runs `step_control()` again from the same write log
    (`{sample_no, off, val}` + the lock-line doorbell of 5.12); the PPU does
    not ship per-sample state (~34 MB/s). Same integer code, both GCC 7.2, so
    the state is identical. The PPU sends a CA/AEG checksum per block, the
    SPE checks it.
  - Result: 0x2810 (EG, state, LP) and 0x2814 (CA) are answered on the PPU at
    the exact sample. No IRQ depends on the render pass (the AICA has no
    channel-end IRQ). Only DSP-side state lags (TEMP, MEMS, MIXS, EFREG, the
    DSP ring in wave RAM); the guest rarely reads it.
  - Wave RAM stays in main memory. The SPE keeps a per-slot line cache
    (~16 KB LS), refetched every block with DMA lists; that also picks up new
    stream data from the ARM7/SH4. The DSP ring stays in LS when RBL <= 32K
    words, else through the cache. CD-DA sectors go through a PPU ring.
  - Cost: PPU control pass runs only for active slots (a phase add and a loop
    compare per sample, an envelope step every other sample); estimate well
    under today's `aica` 4-4.5%, not measured. SPE: the model steps all 64
    slots and the full DSP per sample; estimate 40-70% of one SPE, not
    measured; skip idle slots if needed.
  - Later option: lazy control. Nothing in it raises an IRQ, so a slot only
    needs to be caught up on a guest read (MSLC), a register write to it, or a
    key event. The PPU cost then follows guest reads/writes, not the rate.
  - Risk: the model has `int64` math (SPU emulates it, slower). Speed is still
    paced by the SH4 clock, so audio still gaps below full speed.
  - Steps, each ends with a test:
    - A0: split `step()` into `step_control()` + `step_render()`. On the PC,
      caique's own validators must give output bit-identical to `step()`.
    - A1: the SPE runs the full model from the write log. The PPU runs
      `step_control()` and sends a checksum per block. nullAICA still makes
      the sound. HW: mismatch count, SPE load, PPU cost.
    - A2: nullAICA off. Reads from the PPU control pass, sound from the SPE
      through `ring`. Toggle, default OFF. HW: Crazy Taxi + a streamed-music
      game.
    - A3: the DSP on, own toggle.
    - A4 (optional): lazy control pass.
- 5.8 HW 2026-09-23, boot B (ON): BIOS swirl invisible, then the console
  crashed. The log stops rendering at `rend=draw seq=8` (RPS 0) while the SH4
  runs on: the render thread died at its first vertex-array draw. Cause: GCC
  merged the zero `u1`/`v1` of the packed 36-byte vertex into one `std`,
  misaligned on every odd vertex, and RSX memory is cache-inhibited, so the
  store raised an alignment exception. Fixed: `vb_emit` writes through
  `volatile float*` / `volatile u32*` (checked in the disassembly: only
  `stfs`/`stw` to the vertex, and the stfs->lwz bounce for x*W/y*W is gone).
- 5.8 HW 2026-09-23, fixed build, Vertex Arrays ON: swirl back, 199 s of
  Crazy Taxi, no freeze, `overflow=0`, `ta_raw_of=0`. Against boot A (OFF):

  | | busy | draw | glEnd | vtx+other | tex | dense VPS | full-VPS secs |
  |---|---|---|---|---|---|---|---|
  | OFF | 61% | 42 | 5.1 | 22.2 | 12.2 | 46.1 | 5% |
  | ON | 57% | 38 | 3.0 | 20.0 | 11.9 | 49.0 | 16% |

  vt 9-12K bin: VPS 43.4 -> 48.6. `vtx+other` stays at 20%: it also holds
  `collect_diag()`, a per-frame diagnostic for the `[rendz]` lines that walks
  every vertex of every frame, and three passes over all strips per frame.
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
