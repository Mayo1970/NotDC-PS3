# Optimization plan - what to take from Flycast

Review of Flycast (`E:\Users\Matteo\Desktop\Dreamcast\flycast`) against our tree,
Reicast and Minicast. Written 2026-09-23. Flycast is an allowed reference from
this date (Matt).

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Flycast is little-endian only. Check every ported line for byte order.

---

## 0. Resume here (updated 2026-10-04)

**Section 6 (sound faults) PARKED by Matt 2026-10-04: "We will return to
these later on."** The ChuChu diagnostic build ran on HW 2026-10-04
(results in 6.1). Only the "reg 0x00 reads" row of its decision table
matched, so Matt chose the option "KYONB never cleared" (AUDIO -> "KYONB",
default CLEARED ON END). HW 2026-10-04: FAILURE, F1 unchanged (6.1, "KYONB
option"). On return, F1's next step is a Flycast key trace on the PC.
Start the UDP listener on port 18194 before a HW run (the xashPS3 listener
may hold the port; Matt manages it).

**Then: section 6 (sound faults).** On 2026-10-04 an investigation found the
causes of the ChuChu Rocket echo (sound effects play slower than in
Flycast), the Crazy Taxi crackle with SPE AICA Sound, and the Looney Tunes
silence. It also found two defects that touch every game (6.4 TMU clock,
6.7 JIT FMOV.D). Matt does each item 6.1-6.7 in its own session, then goes
back to the real goals. 6.1 HW tested 2026-10-04: FAILURE (SAFE does not
fix F1). Its `[arm7s]` log shows the driver tick 13% slow at SH4 150 from
the Batch 4 FIQ latency, but with Batch 1 the tick is right and F1 stays,
so the tick is not F1's cause. 6.2-6.7 are not built.

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
  (VIDEO / DEBUG -> "SPE Doorbell").
- 5.12 S2 (TA decoder on the SPU into shadow lists, bit-exact compare at
  each FENCE) is HW validated 2026-10-03: 5362 of 5362 fences match, SPU
  18.9% busy in dense traffic.
- 5.12 S3 (draw from the SPU lists, no PPU replay) is HW tested 2026-10-03
  on Crazy Taxi: all fences ok, busy 40 -> 23%, dense VPS unchanged (50.7).
  No visual change (Matt). Soul Calibur: flicker, malformed background and a
  black texture, also with S3 OFF, so not S3 (5.12 S3 runs 2-3). Soul
  Calibur fight: texture decode 82-85% of wall. Toggle: VIDEO / DEBUG ->
  "SPE Draw" (`SpeDraw`), default OFF. See 5.12.
- 5.13 A0 done on the PC 2026-10-03: `plugs/caiqueAICA/` (not in the PS3
  build yet), `step()` = `step_control()` + `step_render()`, bit-identical
  to caique `5aa2376` on all cases and validators. See 5.13.
- 5.13 A1 HW tested 2026-10-03: caique control twin on the PPU, full model
  on an SPE from the write log, snapshot check every 64 samples. Toggle
  AUDIO -> "SPE AICA Check", default OFF. Run 2 (after bit-exact speed
  work): 96684 checks, bad=0; PPU control pass 1.5-4% of wall. With the
  game's DSP program the SPU falls to ~65% of real time (DSP = 90% of SPU
  wall) and the log-full wait throttles the game.
- 5.13 A1b (DSP recompiler on the SPU) HW validated 2026-10-03, Crazy
  Taxi: self-test PASS, 998770 checked samples with 0 mismatches. The
  game's DSP program now takes 4-6% of the SPU (was 90%, at 65% of the
  rate); the SPU keeps real time and no longer throttles the game. Option
  AUDIO -> "SPE DSP JIT" (OFF / ON / CHECK, default CHECK). Audio still
  crackles: the sound is still nullAICA, starved below full speed (A2 not
  done).
- 5.13 A1c (Matt 2026-10-03: "make PPU control pass cheaper") HW
  validated 2026-10-03 (Matt: SUCCESS). Bit-exact: idle slots that cannot
  move are skipped, and the PPU runs the control pass in batches. In game
  `ppu ctl` 11.2 -> 5.8% of wall, dense VPS 45.5 -> 48.8, bad=0. What is
  left is the playing voices (~7 per sample, ~200 ns each). See 5.13 A1c.
- 5.13 A2 (Matt 2026-10-03: "Go with A2") built 2026-10-03, PC tested, HW
  tested 2026-10-03 on three games. AUDIO -> "SPE AICA Sound"
  (`SpeAicaSound`), default OFF, set before boot. The SPU model plays the
  sound without the DSP (A3 adds it, own toggle); nullAICA's mixer is off;
  0x2810/0x2814 reads come from the PPU control twin; CD-DA sectors go to the
  SPU.
  - Crazy Taxi: correct, `bad=0`; dense SPEED 81.4 -> 82.2%, `aica:` 10.2 ->
    2.9%. Crackles only below full speed (Matt: "ran MUCH better").
  - ChuChu Rocket: 22-25 voices overload the SPU (lag up to 105 ms) and the
    PPU control pass (21% of wall). Echo on the effects, all the time (old).
  - Looney Tunes Space Race: no music, voices or effects, one looped voice.
    OPEN: not yet run with SPE AICA Sound OFF. See 5.13 "A2 HW".
- Next goals (Matt 2026-10-03, after A2; order set by Matt after the menu
  test):
  1. DONE: A2 at high voice counts (5.13 "A2 high voices"), HW validated
     2026-10-04 (Matt: SUCCESS, "very significant performance gain"). The
     model runs slot by slot (`advance()`), bit-exact; new PC SPU simulator
     `misc/spu_sim/` runs the real SPU image. ChuChu stage (26 voices): SPU
     busy 95-101% -> 29%, lag 64, SPEED ~99% (was 45-90%); `ppu ctl`
     20.5-21.9% -> 8.9%. Crazy Taxi dense VPS 49.3 -> 52.5. See 5.13.
     NEW OPEN item: with SPE AICA Sound, Crazy Taxi crackles a little also
     at full speed (main menu: 0 silent blocks, buffer full). Already heard
     in the A2 run. Suspects: stream data overwritten before the SPU reads
     it (SPU ~64-128 samples behind), or clipping at the output stage.
  2. ACTIVE: 5.13 A3 (Matt 2026-10-04: "Go with A3"), the DSP in the SPU
     sound. Built 2026-10-04, PC tested (bit-exact against run() with the
     DSP, also on the real SPU image in the simulator). HW tested 2026-10-04
     (Crazy Taxi and ChuChu in CHECK, Crazy Taxi and Sonic Adventure in ON):
     `bad=0` everywhere, DSP check `bad=0`, `slow=0`; Matt's SUCCESS /
     FAILURE verdict not given yet. AUDIO -> "SPE AICA DSP" (`SpeAicaDsp`),
     default OFF, needs "SPE AICA Sound" ON; "SPE DSP JIT" sets the DSP's
     mode. See 5.13 "A3".
     Matt 2026-10-04: the ChuChu Rocket echo is most likely a sound playing
     much slower than normal, maybe tied to a Quake 3 issue; he has an idea
     of "what we got wrong". He continues in a new session.
     Later on 2026-10-04: Matt cannot recall the Quake 3 issue (dropped).
     Investigated: see section 6.
  3. Moved to section 6: the ARM7 rate option (2.5) is now 6.3, next to
     6.1 (idle skip) and 6.2 (per-sample interleave).
- Deferred (Matt 2026-10-03: "get back to this later"): Looney Tunes Space
  Race has no voices or effects, also with nullAICA (not A2). Music works
  since the 3h pause fix. The ARM7 driver writes no AICA register after its
  upload; first check: the ARM7 rate option, then `[ARMHOT]` (JIT C-Calls).
  2026-10-04: now 6.5 (Flycast's Looney Tunes race fixes), with 6.2.
- In-game menu (Matt 2026-10-03) HW validated 2026-10-03 (Sonic Adventure,
  opened and closed 7 times, the game continued each time): SELECT in a game stops
  the guest at the next vblank and shows the option pages (`ps3_menu_ingame`,
  hooked in `VBlank()`); SELECT again resumes. Live options (`k_live_opts`)
  act at once; any other change is saved for the next boot ("*").
- 3h pause rule fixed 2026-10-03 (Matt: "Do it"), HW validated 2026-10-03
  (ChuChu Rocket: the music continues after a pause). SPI_CD_SEEK parameter
  4 keeps the position, so a resume continues the track.

Workflow: `./build.sh pkg`, UDP listener on port 18194 (scratchpad
`udplog.py`), log analysis by windows with vt > 3K. Probes: IFB Probe and
JIT C-Calls (VIDEO / DEBUG) give `[IFB]`/`[CC]`/`[ARMJIT]`; the `[rend] busy=`
`[rend] draw split %:` and `[rend] tex split %:` lines are always on.

Other open items: the XMB exit always takes the "SH4 did not stop in 3 s"
fallback (seen in every run, not new). JitCarry is broken on PS3 (5.2).
Why the ARM7 JIT time saving did not show as SPEED: likely SMT sharing with
the render thread (5.6).

Parked by Matt 2026-10-03 (speed first, return later): Soul Calibur faults,
not caused by S3 (5.12 S3 runs 2-3).
- Background geometry flickers and is malformed, with S3 ON and OFF. It
  flickers more often when the render thread is slower. Not seen on
  2026-09-23 (before 5.8 Vertex Arrays and 5.10). First check: in the same
  fight, turn Vertex Arrays OFF, then Strip Merge OFF (both live).
- One texture went black for about 1 s.
- Fight load: texture decode 82-85% of wall (~15 re-decodes and 4.7 MB
  hashed per render, mostly pal4/pal8), VPS ~26 of 50, half the renders
  dropped.
- S2 compare: the SPU writes min_w = 0 where the PPU keeps a denormal
  (`wden`); stats only, but it stops the compare of those fences.

| Item | Status |
|---|---|
| 2.1 YUV reset on STARTRENDER | `[x]` HW, no visible change |
| 2.2 AICA EG rate + loop fixes | `[x]` HW, echo unchanged, no regression |
| 2.3 DSP | PARKED: Matt wants a PPC DSP JIT |
| 2.4 Strip merge + inf drop | `[x]` HW, default ON, ~3x fewer draws |
| 2.5 ARM7 clock (`arm_sh4_bias` 20) | moved to section 6 (6.1-6.3) on 2026-10-04 |
| 3c modifier volumes (Flycast GL) | `[x]` HW |
| 3d background depth bias | `[x]` HW, no regression |
| 3e background vertex order | dropped, our code has no such fault |
| 3f NaN colour = 255 | `[x]` HW, no regression |
| 3g GD-ROM CD-DA 2352 + zero-fill | `[x]` HW, no regression |
| 3i region / cable from IP.BIN | `[x]` HW, no regression, switch path unexercised |
| 3a fog, 3b offset colour | OPEN: need a PVR Cg shader in `ps3gl` |
| 3h GD-ROM CDDA lead-out / restart rules | pause rule `[x]` HW 2026-10-03; lead-out / restart OPEN |

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
- 2026-10-03 (Matt): the DSP JIT is now 5.13 A1b, a recompiler on the SPU.

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
- 2026-10-03: a next optimization goal (Matt). Cycles per AICA sample:
  Flycast 512 (22.58 MHz, real), Reicast/Minicast 256 (`Run(512 * 32 / 32 /
  2)` per sample), ours `aica_sample_cycles / 20`: 226 at 200 MHz, 170 at
  SH4 Clock 150. The ChuChu echo stays with the caique model (5.13 A2), so
  the mixer is not the cause.
- 2026-10-04: correction. nullDC-v1 gives 256 ticks per sample, not 2267:
  its MediumUpdate runs `UpdateArm(512); UpdateAica(1)` once per sample
  with bias 2. So bias 2 here would be 8.9x nullDC-v1. This item is now
  6.3, with 6.1 and 6.2.

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
| 5.13 | AICA on an SPE: control pass on the PPU, sample work + DSP on the SPE (caique model) | A0 `[x]` PC; A1 HW tested (bad=0, SPU DSP too slow); A1b DSP recompiler `[x]` HW (0 mismatches, DSP 90% -> 4-6% of the SPU); A1c cheaper PPU control pass `[x]` HW (ppu ctl 11.2 -> 5.8% in game, dense VPS 45.5 -> 48.8); A2 SPU sound `[~]` HW tested: Crazy Taxi OK; ChuChu SPU overload at 22-25 voices; Looney Tunes no sound (open); A2 high voices `[x]` HW 2026-10-04 (ChuChu stage SPU 95-101% -> 29%, ppu ctl 21% -> 8.9%, SPEED ~99%; Crazy Taxi dense VPS 49.3 -> 52.5); open: small crackle at full speed with SPE AICA Sound (cause found 2026-10-04, fix is 6.6); A3 DSP in the SPU sound `[~]` built 2026-10-04, PC tested (bit-exact), HW pending |

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
  - S2 built 2026-10-03, HW pending. Same toggle ("SPE Doorbell").
    - `ps3/spu/spe_dec.c`: the FifoSplitter + `Ps3VertexDecoder` in C on the
      SPU. Parse state is global, as on the PPU. Each DATA chunk is decoded
      as it arrives, into per-context shadow lists (`SpeOut`, 4 x 1.97 MB,
      `memalign(128)`, layouts asserted against `DcVertex` etc.).
    - Output: one LS window pair per list (vtx 16 KB, strip 4, param 2, mtri
      4, mvol 1). Only final bytes go out (16-byte block + 8/4-byte tails),
      so overlapping puts always carry the same data.
    - Floats as bits (SPU single math truncates): `clamp_byte` in integers;
      intensity = exact double product + `frds`; sprite D/UV adds in double;
      1/W = `frest` + 2 double Newton steps + midpoint residual sign with
      `dfnms`, so it equals `fdivs`. Offline check (`s2_float_check.py`,
      session scratchpad): 0 mismatches. Disassembly: only DP ops, no `fm`.
    - FENCE answers the counts, `max_inv_w`, `min_w`/`max_w`, `mv_tris`.
      `spe_compare` checks them and every list entry bit for bit, before
      `rth_mark_decoded` (closes the S1 race: the SH4 cannot reset the
      context during the compare). First 8 mismatches print
      `[spe] fence N MISMATCH ctx=c what[idx] spu/ppu: ...`.
    - PPU changes: vertex types 0, 1, 2, 9, 10 write u = v = 0 (were stale
      memory). Raw hash removed. `reset_raw` waits until the SPU has run
      the last DATA of that context (`ps3spe_wait_cmd`, 100 ms -> SPE off).
    - Known differences, counted in the echo: `odd` (non-vertex record in an
      open strip: the PPU reads it as a vertex, the SPU dispatches it),
      `bad` (reserved types: the PPU dies), `pfull` (params past 8K),
      `wden` (1/W below FLT_MIN), `vtx_of`. With the render thread OFF the
      replay runs in the guest rounding mode, so W/colours may differ.
    - SPU: text 19.7 KB, bss 91.5 KB. Link: `.got` 0x9748, 0 `r2off`.
    - Log every 5 s: `[spe] ... | spu busy B% | fences ok bad late wait ...
      check avg max us | vtx_of odd bad pfull wden`.
    - HW test: Crazy Taxi, toggle ON. Pass = `bad=0 late=0`, fences =
      renders, `odd/bad/pfull/wden` 0. Read `spu busy` and `check avg`.
      Then Soul Calibur (sprites, inf vertices, modifier volumes).
    - HW 2026-10-03 run 1, Crazy Taxi, 150 MHz, Dynarec + JIT Fastmem,
      ~3 min: PASS. fences = renders in every window (5362), ok 5362, bad 0,
      late 0, gaps 0, cont_err 0, lag 0-1, odd/bad/pfull/wden/vtx_of 0.
      Fence wait avg 0.2 us: the eager SPU decode is always done first.
    - Dense windows (n=17, 9.5K vtx, 208 renders/window):

      | | busy | dec | vtx+other | dense VPS | full-VPS secs |
      |---|---|---|---|---|---|
      | 5.10 | 40.2% | 19.3 | 8.5 | 50.7 | 21% |
      | S1 ON | 43.3% | 22.0 | 8.8 | 48.5 | 9% |
      | S2 ON | 40.5% | 19.2 | 8.6 | 48.1 | 8% |

      `busy` does not hold the compare: `check avg` 1.67 ms per render
      (~7% of wall on the render thread), which S3 drops.
    - SPU load: 18.9% of one SPE in dense traffic (~400K vtx/s, so ~1500
      SPU cycles per vertex, DMA waits included). Not tuned; 5 SPEs idle.
    - XMB exit: the "SH4 did not stop in 3 s" fallback again (not new).
    - Note: a second listener from an old session took the UDP log (bound
      with SO_REUSEADDR); stop old listeners before a run.
  - S3 built 2026-10-03, HW pending. Toggle: VIDEO / DEBUG -> "SPE Draw"
    (`nullDC.cfg [ps3] SpeDraw`), default OFF, read in `InitRenderer`. It
    starts the SPE by itself; "SPE Doorbell" is not needed. With S3 ON there
    is no PPU replay and no compare (S2 stays on "SPE Doorbell" alone).
    - Draw view: the draw code reads `DrawLists s_rl` (pointers + counts),
      not `s_rc`. `lists_from_ctx` fills it from the PPU lists,
      `lists_from_spe` from `SpeOut` + the fence counts.
    - `rth_job_decode`: wait for the fence, check `id/ctx/pos/ncont` against
      the job, then draw from the SPU lists. A late fence (budget 20 ms in
      S3, was 5 ms) or a wrong header replays on the PPU from `raw_decoded`.
      S3 never moves `raw_decoded`, so that replay starts at 0 and is
      complete. Counted in `fences ok bad late`.
    - Release: after the draw, the render thread stores the job's fence id in
      the bell line (`SpeBell::released`, `ps3spe_release`). The store kills
      the SPU reservation, as a ring does. The SPU keeps the last fence per
      context; a RESET of that context waits until `released` reaches it
      (`wait_release`, sleeps on the LLR event). No deadlock: the next job's
      FENCE is pushed only after this job's release.
    - The SH4 can now wait in `reset_raw` (`ps3spe_wait_cmd`) while the SPU
      waits for a draw: single-buffered games ListInit the context they just
      rendered. The dead-SPU limits (ring full, raw reuse) are 1 s in S3, were
      100 ms.
    - IRQ timing: `StartRender` reads `rend_nvtx` (the last job's count), set
      on both paths. `nvtx` stays the PPU replay's in S3.
    - Not filled in S3 (PPU replay only): `built`, `wclamp`, `overflow`.
    - Log: the `[spe]` line adds `rel waits/s`, `wait %` (SPU time blocked on
      a release, taken out of `spu busy`) and `cmdwait` (SH4 ms in
      `ps3spe_wait_cmd` per window).
    - SPU: text 20.1 KB, bss 89.5 KB. Link: `.got` 0x9798, 0 `r2off`.
    - HW test: Crazy Taxi, 150 MHz, Dynarec + JIT Fastmem, "SPE Draw" ON,
      "SPE Doorbell" OFF. Pass = no visual change, `bad=0 late=0`, fences =
      renders. Read `[rend] busy=`, `dec`, dense VPS, `cmdwait` and
      `rel waits`. Then Soul Calibur (sprites, inf vertices, modifier volumes).
    - HW 2026-10-03 run 1, Crazy Taxi, 150 MHz, Dynarec + JIT Fastmem,
      "SPE Draw" ON, ~3 min: fences = renders in every window, ok = all,
      bad 0, late 0, gaps 0, cont_err 0, lag 0-1, odd/bad/pfull/wden/vtx_of 0.
      `rel waits` 0 and `cmdwait` 0: Crazy Taxi never ListInits a context
      that is still drawing. Fence wait avg 40-440 us, max 1.1 ms (the
      render thread now reaches the fence before the SPU ends the frame).
      SPU busy 18-24% in dense traffic.
    - Dense windows (n=14, 10.0K vtx, 210 renders/window); VPS over the
      dense NTSC seconds (n=81):

      | | busy | dec | vtx+other | dense VPS | full-VPS secs |
      |---|---|---|---|---|---|
      | 5.10 | 40.2% | 19.3 | 8.5 | 50.7 | 21% |
      | S2 ON | 40.5% | 19.2 | 8.6 | 48.1 | 8% |
      | S3 ON | 23.4% | 1.2 | 9.2 | 50.7 | 14% |

      The render thread drops 17 points of wall, but VPS does not move: as
      with the ARM7 JIT (5.4), the freed PPE time does not show as SH4
      speed. The SH4 thread is the limit.
    - XMB exit: the "SH4 did not stop in 3 s" fallback again (not new).
    - HW 2026-10-03 run 2, Soul Calibur, same settings. Matt: background
      geometry flickers, and one texture went black for about 1 s.
      - Fences: all ok, bad 0, late 0, gaps 0. S3 checks only the header
        (id/ctx/pos/ncont), not the list contents. S2 was never run on Soul
        Calibur, so the SPU lists were never compared there.
      - `wden` 284576, all before the fight (constant after). Known
        difference: the SPU W is 0 where the PPU keeps a denormal. Both are
        dropped as inf vertices; the TR sort key can differ (inf against a
        finite 1/W).
      - In the fight: VPS 26 of 50 (PAL), RPS 13, render thread busy 94-99%,
        job 73-136 ms. `tex split` decode 85% of wall: ~15 texture decodes
        per render, 4.7 MB hashed per render. The SPU waits for the draw
        (`rel waits` 13/s, 34% of its time), so the fence wait is 2-4 ms.
        Not known if the texture decode load is new: Soul Calibur was last
        measured on 2026-09-23 (SPEED 68.9%, before 5.10).
      - Next: the same fight with "SPE Draw" OFF and "SPE Doorbell" ON (S2).
        It gives the visual A/B, the bit-exact compare on Soul Calibur and
        the texture load without S3.
    - HW 2026-10-03 run 3, Soul Calibur, "SPE Draw" OFF, "SPE Doorbell" ON
      (S2). Matt: the background still flickers (more often) and its
      geometry is still malformed. So S3 is not the cause.
      - Fight: every fence matches bit for bit (lists, counts, W range).
      - Before the fight: 501 fences differ in `min_w` only, SPU 0 against
        PPU 0x00200000 (a denormal W, the `wden` case). `min_w` feeds only
        the stats line. The compare stops at the first header field, so
        those fences did not compare the lists.
      - Same load as S3: VPS 25.5 of 50, RPS 12.8, busy 96% (dec 5, draw 91),
        texture decode 82% of wall, half the renders dropped. The TR list is
        mostly palette textures (pal4 144, pal8 512 of 681 strips).
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
      - Done 2026-10-03, `[x]` PC. Code: `plugs/caiqueAICA/` (caique-rtl
        `5aa2376` sample-model + `dsp_asm.h`, `dsp_float.h`, MIT `LICENSE`).
        Not in the PS3 build yet. caique-rtl itself is unchanged.
      - Control: timers, KYONEX, CA walk, loops, LPSLNK, one-shot end, slot
        stop, AEG/FEG, LFO, LFSR, MDEC_CT, `samples`, the slot 0..6 fetch
        preview. Render: decode, filter, volume, sends, MIXS, collision
        words, DSP, mix. Render-only slot fields: `s0`, `s1`,
        `adpcm_quant`, `lpf_low`, `lpf_band`; control never reads them.
      - Hand-off: `Frame` per sample. Per slot: LFSR after its step, phase,
        FEG value, AEG level/off, ALFO value, CA before/after, enabled.
        Decode ops `{ca, RESET|DECODE|LAST}`: PCM/noise one op, ADPCM one per
        CA step (max 288 per slot per sample, so `ops` is 64 x 288 x 4 B =
        72 KB). The key-on ADPCM reset is an op of the next sample.
      - Test (WSL gcc 13, harness in the session scratchpad): all 73 cases +
        eg_model, eg_replay x5, tail_cmp x3, feg_validate,
        filt_validate_model, filt_overflow_model, mixsw_check, built against
        the original and the split model. Every output file, exit code and
        validator line is identical; the per-case trace hash (DAC output of
        every sample + every register read, 204 M records) is identical.
        Baseline gates as in tests/SUMMARY: eg_model 97/97, eg_replay 5 x
        4/4, tail_cmp 12/12, feg_validate 9/9, filt_validate 265/265.
      - For A1: the SPE can run control and render per slot to keep the op
        log at one slot (1.2 KB of LS). `step_control()` always fills the
        `Frame`; the PPU-only run can skip that.
    - A1 built 2026-10-03, HW pending. Toggle: AUDIO -> "SPE AICA Check"
      (`nullDC.cfg [ps3] SpeAicaCheck`), default OFF, read in `InitAica`.
      - Model change: `step()` = `run(true)`, control and render per slot
        (ops buffer one slot, 288 x 4 B). `step_control()` = `run(false)`:
        no op log, no slot 0..6 fetch preview, no render. LFSR jump rows
        are constant tables (no static constructors on the SPU). Wave RAM
        goes through hooks with `CAIQUE_RAM_HOOKS`. Re-run of the A0 PC
        test: still bit-identical to caique `5aa2376`.
      - PPU (`ps3/ps3_aica_spe.cpp`): `aica_WriteMem_reg` (SH4 and ARM7)
        feeds every register write to a control-only caique twin and to a
        16384 x 16 B log `{sample, op, off, val, hash}`. A byte write keeps
        nullAICA's other byte; writes to the upper halfword are dropped.
        `libAICA_TimeStep` runs `step_control()`; every 64 samples it keeps
        a `CtlSnap` (CA, phase, AEG, FEG, flags, LFO per slot + LFSR,
        MDEC_CT, eg_cnt; 1296 B) and logs a CHECK with its FNV hash. The
        bell line (5.12 style) is rung at each CHECK and every 1024 records.
        A log full for 100 ms turns the check off.
      - SPU (`ps3/spu/spe_aica.c`, `spe_aica_model.cpp`, `spe_aica_ram.h`):
        the full model, stepped to each record's sample. Wave RAM by a
        16 KB direct-mapped LS cache of `aica_ram` (DC byte a = host byte
        a ^ 3), dropped at each CHECK. DSP ring in LS when RBL <= 32K words,
        else the cache; DSP writes never reach main memory (A1 limit: the
        SPU's sound is not checked). The first 4 mismatches send the SPU
        snapshot; the PPU prints the differing fields.
      - Own SPU thread group `nulldc_aica`; `sysSpuInitialize` is shared
        with 5.12 (`ps3spe_sys_init`). SPU: text 31.7 KB, bss 111 KB. Link:
        `.got` 0x99A0, 0 `r2off`.
      - PC test before HW (WSL, all 73 cases): a control-only twin and the
        SPU code (same cache header, swizzled RAM) fed from the log:
        443432 checks, 0 mismatches. Case outputs still match caique.
      - Log every ~5 s: `[aica-spe] on samples/s ppu= spu= lag= | checks=
        bad= | writes/s= full= | ppu ctl X% N ns/sample | spu busy B%
        misses/s= wakes/s= timeouts/s=`; mismatches print
        `[aica-spe] MISMATCH ...` and per-slot `ppu/spu` lines.
      - HW test: Crazy Taxi + a streamed-music game, toggle ON. Pass =
        `bad=0`, spu samples/s = ppu samples/s, lag small. Read `spu busy`,
        `ppu ctl %` and `aica%` on the SPEED line against a run with the
        toggle OFF.
    - HW 2026-10-03 run 1, Crazy Taxi (PAL), 150 MHz, Dynarec + JIT
      Fastmem, SPE Draw OFF: the SPU thread came up, then after 3.8 s
      `write log full for 100 ms (spu samples 48512, ppu 168868); check
      off`. The SPU ran the full model at ~29% of real time (~3.5x too
      slow). No mismatch count (the off message did not print it).
      - PPU cost with the check on: `aica%` 40-48% (3 windows), against
        5.5% after the check turned off. The control pass took ~38% of wall
        (~8.6 us per sample), not "well under 4.5%" as estimated.
      - Cause, from PC timing (x86, ns per sample): control 740 idle / 804
        with 8 voices; full 3863 / 4790. The model stepped all 64 slots
        (LFO tick + `lfo_calc`, envelope clocks) and all 128 DSP steps
        every sample; idle slots cost as much as playing ones.
    - Speed work 2026-10-03 (Matt: "speed up both"), all bit-exact:
      - LFO ticks lazily (`lfo_sync`, closed form, at PLFO/ALFO use, a
        0x1C write and a snapshot); `lfo_calc` only on a state change.
      - `quiet` mask: a slot with fetch stopped, AEG off at 0x3FF and an
        FEG no step can move (never keyed, held, or at a bound) is skipped
        until a register write or a key event. FEG no-op cases skipped.
      - LFSR jumps 12 steps at a time (`lfsr_adv`); keyed/stop loops only
        when something is armed; `isel_cnt` replaces the per-slot bus flags.
      - Full model: `rsilent` mask skips quiet slots whose output is 0
        (zero input, LPOFF or filter at rest), filter fixed-point skip
        (`lpf_fix`), slot 0..6 preview copies a slot only when it walks.
      - DSP: MPRO decoded on write, `dsp_pack` by clz (all 2^24 inputs
        checked), all-NOP program fast path, `X*Y >> 12` in 32 bits;
        `att_apply` / `send_level` in 32 bits (bounds under 2^28).
      - PC timing after (ns/sample): control 11 idle / 129 with 8 voices;
        full 94 idle; DSP with a program on every step 3624 -> 1588.
      - SPU section timers (`CAIQUE_PROF`): new `[aica-spe] spu % of
        wall: slots (rendered/sample) env preview+bus dsp mix` line. The
        log-full message now also prints the counts and mismatch reports.
      - SPU: text 39.6 KB, bss 124 KB. Link: `.got` 0x99B0, 0 `r2off`.
      - PC tests on the final model: all cases and validators still
        bit-identical to caique `5aa2376`; the A1 compare (quiet control
        twin against the skipping full model) 443432 checks, 0 mismatches.
      - HW run 2: same settings and toggle. Read `ppu ctl %` / `aica%`,
        `spu busy`, the `spu % of wall` split and `bad`.
    - HW 2026-10-03 run 2, Crazy Taxi (NTSC), same settings, 44 windows:
      - Correct: 96684 checks, `bad=0`. The PPU control pass and the full
        SPU model agree on every snapshot.
      - Before the game loads its DSP program: real time (41-44 K
        samples/s), `ppu ctl` 1.5-4% of wall (360-900 ns/sample, was
        ~38% in run 1), `spu busy` 11-20%, DSP 0.4% of SPU wall.
      - With the DSP program: SPU busy 119%, DSP 90.4% of SPU wall (slots
        2.2, env 1.5, preview+bus 2.6, mix 2.4). The SPU makes 28844
        samples/s; the PPU waits on the full log (`full=214`, `ppu ctl`
        90.6%, shown as `arm:` 75-80% on the SPEED line), so the whole
        emulator ran at ~65% SPEED. The 100 ms cut-off never fired.
      - Design fault seen here: a slow SPU throttles the game through the
        log-full wait. The check should stop rather than throttle.
      - Next goal (Matt, 2026-10-03): a DSP recompiler on the SPU (see
        A1b below). The DSP must get at least ~1.6x faster plus margin.
      - XMB exit: the "SH4 did not stop in 3 s" fallback again (not new).
    - A1b (Matt 2026-10-03): DSP recompiler on the SPU. This is 2.3's
      DSP JIT, moved from the PPU to the SPE. `[x]` HW validated
      2026-10-03: see "A1b built" and "A1b HW" below.
      - On an MPRO write the SPU (or the PPU, if LS is short) translates
        the 128 steps into SPU code in LS once; each sample runs that code.
        Fields become constants, most branches go; `clz` for `dsp_pack`,
        3-4 multiply-adds for the 24x13-bit `X*Y`. LS is plain RAM: write,
        `sync`, branch (no cache, no W^X).
      - LS budget: ~164 KB used now, ~75 KB free after a 16 KB stack.
        Estimate (not measured): emitter 8-15 KB, code 10-20 KB. Fallbacks:
        a 32 KB DSP ring (RBL 64K words through the cache), or emit on the
        PPU and DMA the code in (the PPU never runs it: no `.text` issue).
      - Check on HW: run the recompiled DSP beside the caique interpreter
        on the SPU and compare TEMP / MEMS / EFREG / ACC per sample, like
        the A1 snapshot check. Then measure `spu % of wall`.
      - Also: replace the PPU's log-full wait with a stop, so a slow SPU
        never slows the game.
      - A1b built 2026-10-03 (PC tested, HW pending). Option AUDIO ->
        "SPE DSP JIT" (`nullDC.cfg [ps3] SpeDspJit`): OFF (interpreter, as
        A1) / ON / CHECK, default CHECK. Acts only with "SPE AICA Check"
        ON. Set before boot.
        - Emitter `ps3/spu/spe_dsp_jit.c` (portable C, -Os on the SPU): one
          SPU function per program, built on the SPU at the first DSP pass
          after an MPRO or 0x2804 write or a reset. Code buffer 24 KB; a
          program that does not fit runs on the interpreter (`fails=`).
        - Same step order as `dsp_step()`. The read pipeline (odd reads,
          deferred even reads, landing two steps later, the step-127 read
          that lands at step 1 of the next sample) is fixed at compile
          time. Only the collision bits are runtime data.
        - A step builds ACC only if the next step reads it (SHIFTED or B),
          so NOP runs emit nothing.
        - X*Y as two 16x16 `mpy` (X = xh * 4096 + xl), `dsp_pack` with
          `clz`, `dsp_unpack` with `rotma` by the exponent.
        - TEMP, MEMS, EFREG, MIXS_bank, EXTS and coll_* stay in the model
          (TEMP and MIXS_bank 16-aligned in the SPU build only). COEF and
          MADRS go to pre-converted tables, one value per quadword,
          refreshed on each write.
        - LS ring (RBL up to 32K words, DC byte order): quadword
          load/store. TABLE accesses and a 64K-word ring call the
          interpreter's path (`dspjit_rd/wr`, `brasl`). A collision sends
          the store to a spare quadword after the ring.
        - Long-lived values in r80-r108, saved by the prologue as spu-gcc
          does; temporaries r5-r79. Opcodes checked against spu-as.
      - Runtime `ps3/spu/spe_dsp_check.h` (hook, tables, check, self-test,
        ISA probe), shared with the PC test. Model change: `run()` calls
        `caique_dsp_hook()` first and `dsp_step()` is public, both under
        `CAIQUE_DSP_HOOK` (SPU build only). A0 re-run: all cases and
        validators still bit-identical to caique `5aa2376`.
      - CHECK: every 7th sample (odd: both MIXS banks) also runs the
        interpreter from the same state, its ring writes undone after, and
        compares TEMP, MEMS, EFREG, ACC, FRC, Y, ADRS, memval, the read
        pipeline and the LS-ring words written. The recompiled result
        stays. The first 2 mismatches go to the PPU in full (`dspbad` hex
        lines, enough for a PC replay).
      - Boot self-test on the SPU (after RUNNING, before the first record):
        ISA probe (the 35 emitted forms on random operands against a C
        model of the ISA), then 24 random programs x 16 samples against
        the interpreter. A failure turns the recompiler off for the
        session (`dsp jit self-test FAIL`).
      - PPU: a full write log stops the check at once and prints the SPU's
        LS echo (phase: 1 generated code, 2 interpreter, 3 compile, 4
        self-test) and exit status.
      - PC test, `misc/dspjit_check/` (WSL `run.sh`, then
        `objdump_check.sh` in Git Bash): a small SPU simulator, written
        from the ISA, runs the generated code.
        - ISA probe: the simulator matches the C model on every form.
        - Random programs (p = 1/4 per flag): 3000 programs x 32 samples,
          0 mismatches, all fit in 24 KB.
        - All 73 caique cases, recompiler checked on every sample:
          28377429 samples, 0 mismatches, 0 simulator faults (199159
          compiles: the cases write MPRO word by word). A1 still 443432
          checks, 0 mismatches.
        - spu-objdump: every probe form decodes as expected; three whole
          programs (11311 words) decode with no unknown word.
        - Mutation test: 13 of 14 deliberate emitter bugs fail the test;
          the 14th (logical shift for ADRS) is equivalent under the
          address masks.
      - SPU image: text 63.6 KB, bss end 224 KB, ~32 KB left for the stack.
      - Cost in the simulator: ~3570 instructions per sample on random
        programs; the interpreter took ~100K SPU cycles per sample on HW.
      - HW test: settings as run 2 (Dynarec ON, JIT Fastmem ON, SH4 Clock
        150, SPE AICA Check ON), SPE DSP JIT = CHECK, Crazy Taxi into the
        game (DSP program loaded). Pass: `dsp jit self-test PASS`, `dsp jit
        CHECK ... bad=0`, A1 `bad=0`, spu samples/s = ppu samples/s,
        `full=0`. Read `spu % of wall: dsp` and `jit % of wall`. Then one
        boot with ON for the cost without the check.
      - A1b HW 2026-10-03, Crazy Taxi (NTSC), Dynarec + JIT Fastmem, SH4
        Clock 150, SPE AICA Check ON. Two boots, about 3 min each.
        - Boot self-test PASS on both boots (ISA probe + 384 samples).
        - Game DSP program: 1434 words, 79 of 128 steps emit code, no
          slow-path access (ring in LS, no TABLE).
        - CHECK boot: 998770 checked samples, 0 mismatches; A1 109240
          checks, `bad=0`; `full=0` in every window.
        - ON boot: DSP 4.1-6.4% of SPU wall (avg 5.5%) with the program;
          generated code 5.3% at the full 44.1 kHz. Run 2's interpreter
          took 90% of the SPU at 65% of the rate: about 25x faster. SPU
          busy avg 35%, max 64%; the voices (slots 20-32%) and envelopes
          (11-15%) are now the largest SPU costs. A1 `bad=0`, `full=0`,
          SPU samples/s = PPU samples/s.
        - No throttle: `arm:` 4.4% (run 2: 75-80%). Dense windows (vt >
          3K): CHECK SPEED 73.8%, VPS 44.2; ON SPEED 75.8%, VPS 45.5 (the
          check runs on the SPU, so the SH4 thread does the same work).
        - Still on the SH4 thread: the A1 control pass, `ppu ctl` 9-15% of
          wall in dense scenes with 5-12 voices.
        - Audio still crackles (Matt): the sound is still nullAICA (the
          SPU model is only checked until A2). While the game plays, 12%
          of the output blocks are silence; 26% in windows below real time.
    - A1c (Matt 2026-10-03: "make PPU control pass cheaper"). `[x]` HW
      validated 2026-10-03: see "A1c HW" at the end of this block.
      - Cause, from the A1b logs: in game `ppu ctl` took 2900-4600 ns per
        sample, also with only 2.4 slots rendered on the SPU, whose `env`
        was 11-20%. So most of the cost is idle slots (fetch over, never
        quiet), not voices. The old quiet rule needed AEG off at 0x3FF and
        the FEG in release at an exact bound.
      - Model (shared with the SPU), all bit-exact:
        - Quiet = fetch stopped, no flag, both envelopes frozen. Frozen AEG:
          off at 0x3FF, or a rate-0 segment (not decay 1 at DL). Frozen
          FEG: never keyed, rate 0, at 0 / 0x1FFF, or decay 2 / release held
          within the smallest step of the rate.
        - `env_skip`: an idle slot with a slow envelope (R < 44) is visited
          only on clocks where `eg_increment` can step. A write or a key
          event clears it.
        - Effective rates cached per slot (`aeg_R`, `feg_R`), refreshed on
          writes to 0x10/0x14/0x18/0x40/0x44.
        - The envelope pass runs only on a clock or a key event.
        - Control only: the CA loop walks a `fetching` mask; the LFSR
          position is computed only for a noise PLFO.
      - PPU (`ps3_aica_spe.cpp`): per sample only a counter. A write or a
        CHECK (every 64 samples) runs the missed samples back to back. No
        `mftb` per sample.
      - New stats on the first `[aica-spe]` line: `live L (en E aeg A feg
        F)` = slots not quiet per sample; en = fetching or flagged, aeg /
        feg = idle with the AEG / only the FEG moving.
      - PC tests (WSL): all 73 caique cases and validators identical to the
        A1b model; A1 compare 443432 checks, 0 mismatches. Random
        differential test, A1b model against the new one, same writes,
        every sample compared (snapshot; full model also DAC + DSP):
        3000 episodes, 179 M samples, 0 mismatches. 16 deliberate bugs in
        the new rules all fail that test.
      - PC timing, control pass (x86, ns/sample, before -> after): idle
        21 -> 11; 8 voices 133 -> 110; 48 idle slots (D2R 0) + 8 voices
        669 -> 121; 48 idle slots (FEG held in decay 2) + 8 voices
        575 -> 124; 48 idle slots (slow D2R) + 8 voices 734 -> 168.
      - SPU image: text 65.5 KB, bss end 227 KB (~29 KB stack). PPU `.got`
        0x9AA0, 0 `r2off`.
      - HW test: settings as A1b (Dynarec ON, JIT Fastmem ON, SH4 Clock
        150, SPE AICA Check ON, SPE DSP JIT ON), Crazy Taxi into the game.
        Pass: `bad=0`, `full=0`, `dsp jit` bad=0. Read `ppu ctl` % and
        ns/sample against A1b (in game 9-15%, 2900-4600 ns), the `live`
        split, the SPU `env` %, SPEED and VPS.
      - A1c HW 2026-10-03, Crazy Taxi (NTSC), settings as above, ~3 min
        (second boot; the first boot stayed in the menus). Matt: SUCCESS,
        nothing new seen or heard.
        - Correct: `bad=0` in every window, `full=0`, DSP JIT bad=0.
        - In game (windows below real time), A1b -> A1c: `ppu ctl`
          11.2 -> 5.8% of wall, 3511 -> 1715 ns/sample.
        - `live` 6.9 slots per sample in game, all `en` (aeg 0, feg 0): no
          idle slot costs time now. Left: the playing voices, ~200 ns per
          voice per sample.
        - Dense windows (vt > 3K): SPEED 75.8 -> 81.4%, VPS 45.5 -> 48.8.
        - SPU, windows with > 1 rendered slot: env 11.0 -> 3.8%, slots
          19.3 -> 13.7% of SPU wall (same 5.4 rendered/sample).
        - The control pass now runs mostly inside the ARM7's register
          writes, so on the SPEED line it moved from `aica:` to `arm:`
          (aica 19.5 -> 10.2%, arm 4.4 -> 9.0%; sum 23.9 -> 19.2%).
        - XMB exit: the "SH4 did not stop in 3 s" fallback again (not new).
    - A1: the SPE runs the full model from the write log. The PPU runs
      `step_control()` and sends a checksum per block. nullAICA still makes
      the sound. HW: mismatch count, SPE load, PPU cost.
    - A2: nullAICA off. Reads from the PPU control pass, sound from the SPE
      through `ring`. Toggle, default OFF. HW: Crazy Taxi + a streamed-music
      game.
      - A2 built 2026-10-03 (Matt: "Go with A2"), PC tested, HW pending.
        Option AUDIO -> "SPE AICA Sound" (`nullDC.cfg [ps3] SpeAicaSound`),
        default OFF, read in `InitAica`. It starts the SPU thread with or
        without "SPE AICA Check"; the CHECK records and `bad=` stay.
      - nullAICA: `AICA_Sample()` only refills the CD-DA sector; the
        register file, its write path, timers and interrupts are unchanged.
        Reads of 0x2810/0x2811 and 0x2814/0x2815 come from the PPU twin
        (`ps3aica_read_mon`: catch up to the sample, then `read()`). A byte
        read of 0x2810 keeps LP, as nullAICA did. No Flycast `LEA - 3` clamp
        on CA: the model's CA is the hardware's.
      - Model: `no_dsp` flag (A2 sets it on the SPU): no DSP pass, no EFREG
        mix, no collision fetch. `init()` clears it, so the PC tests and A1
        are unchanged.
      - SPU (`spe_aica_snd.h`): per sample EXTS from the CD-DA sector,
        `step()`, then ps3_audio.cpp's 44.1 -> 48 kHz resampler into 256 LS
        frames. At each CHECK the frames go to `ring` by DMA (8 bytes at odd
        ends, quadwords between), then `ring_wr` with a fenced put. Frames
        without room (from `ring_rd`) are dropped, newest first.
      - CD-DA: every 588 samples the PPU copies the sector into one of 8
        slots of `AicaShared::cdda` and logs a CDDA record (a silent sector
        has no data); the SPU DMAs it into LS at that record.
      - Pacing: after each CHECK the SH4 thread waits (AudioBuffers >= 1,
        100 ms cap, in SndWaitTicks) until the frames of all samples so far
        fit the ring: `w0 + ceil(n * 160 / 147) - drops`.
      - A full write log (or a busy CD-DA slot) waits for the SPU instead
        of stopping; no SPU progress for 100 ms stops the SPU, and nullAICA
        plays again.
      - Log: `[aica-spe] sound SPU: frames/s= drops= cdda/s= reads/s= | ppu
        wait % of wall: ring spu` instead of the dsp jit line. `[aud] out=`
        now counts `ring_wr`, so it shows the SPU's frames (`in=` stays 0).
      - PC test of the model change (WSL, all 73 cases, `no_dsp` off):
        every trace (accesses + DAC output), output file and A1/A1b count
        identical to the build before (A1 443432 checks, A1b 28377429).
      - PC test `misc/a2_check/run.sh`: `snd_unit` (fake MFC: DMA size and
        alignment rules, every frame against the PPU resampler, no unread
        frame overwritten, drops, `w0` odd and across 2^32) PASS. Cases: the
        `no_dsp` model against a full one with every EFSDL forced to 0,
        28377429 samples, 0 output and 0 control mismatches.
      - SPU image: text 66.5 KB, bss end 234 KB (~22 KB stack; deepest
        path ~4 KB by `-fstack-usage`). PPU `.got` 0x9B30, 0 `r2off`.
      - HW test: Dynarec ON, JIT Fastmem ON, SH4 Clock 150, SPE AICA Sound
        ON. Crazy Taxi into the game, then a game with streamed or CD-DA
        music. Pass: right sound (pitch, music, effects, no stuck notes),
        `bad=0`, `drops=0`, `frames/s` near 48000 at full speed. Read SPEED,
        VPS, `aica:`/`arm:` and `[aud] silent=` against A1c.
      - A2 HW 2026-10-03, settings as above, AudioBuffers=0 (no pacing).
        CD-DA in Matt's images: ChuChu Rocket 15 audio tracks, Looney Tunes
        Space Race 13; Crazy Taxi, Sonic Adventure, Soul Calibur, Border
        Down, Power Stone 2 and Space Channel 5 none.
        - Every game: `bad=0`, `drops=0`, no fallback to nullAICA, SPU
          samples/s = PPU samples/s. At steady full speed `silent=0`.
        - Crazy Taxi, 207 s: dense SPEED 81.4 -> 82.2%, VPS 48.8 -> 49.3,
          `aica:` 10.2 -> 2.9%, `arm:` 9.0 -> 9.6% (against A1c). SPU 22%
          busy in game, lag at most 320 samples; `ppu ctl` 6.2%, about 1200
          monitor reads/s. Silent blocks: 0.7% at >= 99% SPEED (only right
          after a slow second), 13.4% at 85-95%, 20.9% below 85% (A1c 12.8%,
          24.9%). Matt: small crackles in the music; "ran MUCH better".
        - ChuChu Rocket: 2 voices in the menus (SPU 5-13%). The stage plays
          22-25 voices: SPU 95-101% busy, up to 4659 samples (105 ms) behind;
          the SH4 thread waits for it 12-33% of wall (CD-DA slots, log), so
          SPEED falls to 65-90% there; `ppu ctl` 20.5-21.9% (5.3-6.0 us per
          sample). Matt: echo on the effects, all the time (also with
          nullAICA since 2026-09-23: 2.5 lead). CD-DA did not resume after a
          pause: SPI_CD_SEEK parameter 4 moved to FAD 150 (nullDC4Wii only;
          nullDC-v1, Reicast, Minicast, Flycast keep the position). Fixed.
        - Looney Tunes Space Race: mostly full speed, SPU about 10%. Matt: no
          music, no voices, no effects, one looped voice. Log: after boot
          the AICA writes/s equal the monitor reads/s (about 1270/s, one
          MSLC write and one read per driver tick) and 2 slots stay live:
          the driver keys nothing else. 4 SPI_CD_PLAY, bursts of 11 and 39
          SPI_CD_SEEK. OPEN: same run with SPE AICA Sound OFF; the GD-ROM
          pause fix may change it. Also 8853 log lines of `recSh4: cache
          80% full (warning zone)`.
        - ChuChu Rocket, GD-ROM fix build: the music continues after a pause
          (Matt); the echo stays. Stage: 25-31 voices, SPU 88-98% busy, up
          to 4777 samples behind, SH4 waits 20-41% of wall, SPEED 45-79%.
          After the overload the SPU caught up into a full ring (fill 7910
          of 8192, AudioBuffers=0) and dropped 4765 frames once. On the
          pause screen the twin has 0 live slots, but the SPU renders 13
          slots per sample (25% of the SPU, no wave RAM reads): stopped
          slots whose filter tails never settle (limit cycles) are never
          render-silent.
    - A3: the DSP on, own toggle. Built 2026-10-04: see "A3" after the
      A2 high voices block.
    - A4 (optional): lazy control pass.
    - A2 high voices (NEXT, Matt 2026-10-03). ChuChu Rocket stage, 25-31
      voices (A2 HW, run 2):
      - SPU: 865 ns per rendered slot per sample (about 2770 SPU cycles),
        plus about 180 ns of envelope work per live slot. 31 voices need
        about 32 us per sample against 22.7 us of real time (142%). Target:
        at least 2x faster per slot.
      - PPU control pass: 236-243 ns per live slot per sample, 7.4 us per
        sample at 31 voices (33% of real time). Target: at least 3x faster.
      - Pause screen: 0 live slots, but 13 stopped slots render their
        filter tails (limit cycles never settle): 25% of the SPU.
      - Candidates, SPU first (its overload throttles the game):
        1. Filter tails (speed trade): a stopped slot (fetch off, AEG off at
           0x3FF) outputs at most +-8 after the attenuation; skip its
           render. Control state and the A1 check are not affected.
        2. `lpf_step` without int64 (bit-exact): the SPU has no 64-bit
           multiply; split the products as `mul_shr12` does. Check the
           spu-gcc output for `__muldi3` first.
        3. The rest of the per-slot render path on the SPU (ADPCM decode
           per CA step, interpolation, branches), measured per section.
        4. PPU: closed-form CA walk for a playing slot without PLFO, n
           samples at once between events (loop end, LPSLNK, writes, keys).
           Bit-exact, PC differential test as A1c.
        5. PPU: envelope clocks that cannot move a playing slot (sustain
           with rate 0) skipped, as `env_skip` does for idle slots.
        6. More CD-DA slots and a bigger log, so a short SPU overload does
           not stop the SH4 thread (the 8 CD-DA slots cap the lag at 4704
           samples; lag 4777 was seen).
      - Built 2026-10-04 (Matt: "Let's go with the next goal"), PC tested,
        HW pending. Everything bit-exact except where noted.
      - PC SPU simulator `misc/spu_sim/` (new): runs the real spe_aica image
        (ISA from the spec, MFC DMA, events, decrementer) with an in-order
        dual-issue timing model (CBEA latencies, 18-cycle unhinted branch,
        500-cycle DMA). The harness `run_a2` feeds it like ps3_aica_spe.cpp
        (writes, CHECKs with the x86 twin's hash, CD-DA) and compares every
        48 kHz frame with the base model (ee0def6). Calibration: old code,
        28 voices, 2633 cycles per rendered slot (HW ChuChu: 2770).
        `build_spu.sh` (Git Bash) builds the image with symbols, `run.sh`
        (WSL) the scenarios, `hot.sh FUNC` the hot instructions with source
        lines.
      - Model: `advance(n, render, mix)` runs each slot through a round of up
        to 64 samples (`slot_run`), in run()'s order per sample. A slot's
        sample needs only its own state plus per-sample globals (LFSR,
        envelope clock, KYONEX on the first sample); run()'s cross-slot parts
        are sums, counts or no-ops. `final_mix()` is run()'s output stage.
        - `slot_fast<R>`: steady slots (no key/stop flag, no noise, no
          KYONEX), playing or idle, with walk, decode and filter state in
          locals; branch-free selects and `__builtin_expect` (the SPU has no
          branch prediction); wave RAM through one line pointer
          (`caique_line`). Others: `slot_sample` (generic, run()'s code).
        - `lpf32`: the filter in 32 bits, exact while |low|, |band| < 2^28
          (products split at 2^9); else the int64 path (`lpf64`). No more
          `__muldi3` (was 3 calls per filter step, 8% of the SPU).
        - SPU 16x16 multiplies by intrinsic (`mul16`, `mul32x16`): GCC sees
          through int16 casts and emits the 3-multiply sequence.
        - `decode_adpcm` branch-free; `lfo_tick` (one LFO tick inline);
          timers in closed form; one sample of LFSR as a constexpr GF(2)
          table (`lfsr_adv64`).
        - Candidate 5: `env_skip` also for playing slots (a frozen playing
          envelope gets 0x3FFF); an LPSLNK attack end and a one-shot end
          clear it.
        - Candidate 4 (PPU only): `slot_ctl_fast`, the walk in closed form up
          to the next envelope clock that can move the slot (from the
          env_skip mask); per sample only with PLFO or a loop event in reach.
        - Register file 16 KB -> 8.75 KB (nothing is stored above 0x45BC;
          reads there return 0 as before), for SPU LS.
      - SPU (`spe_aica_model.cpp`): sound mode runs `advance()` in rounds of
        64, then per sample EXTS, `final_mix`, resampler. Build define
        `CAIQUE_ADV_RENDER_ONLY` drops the control-only path from the SPU
        image. SPU image: text 87.1 KB, bss end 0x3D5D0: 10.5 KB stack,
        deepest path ~4.1 KB (`-fstack-usage`).
      - PPU (`ps3_aica_spe.cpp`): `catch_up()` = `advance(n, false, 0)`.
      - Candidate 6: CD-DA slots 8 -> 16 (lag cap 9408 samples), log 16384
        -> 32768 records. PPU RW segment +280 KB, `.got` 0x9BB8, 0 `r2off`.
      - Candidate 1 (filter tail skip, a speed trade) NOT done: the tails now
        cost ~370 cycles per slot-sample (24 tails 12% of real time, was
        ~4x more) and the SPU is no longer the limit. Left as an option.
      - PC tests:
        - `diff_adv` (base run() per sample against advance() in random
          chunks, writes/reads/timer writes between chunks; control
          snapshot every chunk, DAC every sample, SCIPD/MCIPD): 2000
          episodes per change, ~100 M samples, 0 mismatches.
        - `mutate_adv.sh`: 28 of 29 output-relevant bugs caught; the miss is
          equivalent (a non-last ADPCM preview is always overwritten).
        - All 73 caique cases + 13 validators: identical to the base model.
          A1b test: 443432 A1 checks, 28377429 DSP samples, 0 mismatches.
          A2 test: snd_unit PASS, 28377429 samples, 0 mismatches.
        - Simulator, real image (cycles per sample, % of real time):
          | scenario | old | new |
          |---|---|---|
          | 28 voices mixed | 92765 (128%) | 22061 (30%) |
          | 28 ADPCM | 99646 (137%) | 22835 (32%) |
          | 28 PCM16 | 89053 (123%) | 21964 (30%) |
          | 31 voices mixed | - | 24260 (33%) |
          | 24 filter tails | - | 8937 (12%) |
          Every frame bit-exact against the base model, `bad=0`.
        - PPU proxy (x86, `bench_ctl`, ns per voice-sample, base -> new): 28
          voices 19.7 -> 4.5 (4.4x); with 4240 monitor reads/s 19.1 -> 6.0
          (3.2x). Each read catches all slots up: per-slot catch-up is A4.
      - HW test: as A2 (Dynarec ON, JIT Fastmem ON, SH4 Clock 150, SPE AICA
        Sound ON). ChuChu Rocket into a stage, the pause screen, then Crazy
        Taxi. Pass: right sound, `bad=0`, `drops=0`, lag small. Read `spu
        busy` (was 95-101%), `ppu ctl` % and ns/sample (was 20.5-21.9%,
        5.3-6.0 us), `live`, SPEED and VPS. The `spu % of wall` line now
        shows the envelopes inside `slots` and the output stage as `mix`.
      - HW 2026-10-04 (log ps3log_a2hv_run1.txt), settings as above,
        AudioBuffers=0. Matt: SUCCESS, "very significant performance gain";
        the ChuChu echo stays (old, 2.5).
        - Every window: `bad=0`, `drops=0`, lag 64, no fallback to nullAICA.
        - ChuChu stage (windows with > 20 live, 25.9 on average): SPU busy
          29.1% (A2: 95-101% at 22-25 voices; the simulator said 30-33%),
          `ppu ctl` 8.94%, 2028 ns/sample = ~78 ns per live slot (A2:
          236-243 ns, 3.0x). Dense windows: SPEED 98.9%, VPS 59.3, 96% of
          them at full speed (A2 stage: 45-90%).
        - ChuChu pause screen: 0 live, 15 tails rendered, SPU 10.7-11.2%
          (A2: 13 tails = 25% of the SPU).
        - Crazy Taxi dense windows (vt > 3K): SPEED 82.2 -> 87.6%, VPS 49.3
          -> 52.5, `ppu ctl` 6.2 -> 2.1%, `arm:` + `aica:` 12.5 -> 8.4%.
          Silent output blocks only in seconds below 100% and the next one.
        - OPEN: Matt hears small crackles also at full speed, e.g. the main
          menu, where the log shows 0 silent blocks, fill 3300-4500 frames,
          SPU in step. Not from this goal (heard in the A2 run). Suspects:
          music streamed into wave RAM in a fast refill lands before the SPU
          plays the old data (SPU ~64-128 samples behind the PPU), or the
          output stage clips (caique DAC mix levels unverified).
        - Left on the PPU: each write and monitor read (~10K/s in ChuChu)
          catches all slots up. A4 (per-slot catch-up) would cut that.
    - A3 (Matt 2026-10-04: "Go with A3"): the DSP in the SPU sound. Built
      2026-10-04, PC tested, HW pending. Option AUDIO -> "SPE AICA DSP"
      (`nullDC.cfg [ps3] SpeAicaDsp`), default OFF; it acts with "SPE AICA
      Sound" ON and is read in `InitAica`. "SPE DSP JIT" (OFF / ON / CHECK,
      default CHECK) sets the DSP's mode, as in A1b.
      - Model, bit-exact against run() with the DSP:
        - A render round with the DSP is at most `DSP_ROUND` = 32 samples
          (A2 keeps 64; in the simulator 32 cost +0.3% against 64, 16 cost
          +2.7%). Per sample it leaves the SGC bus sums (`adv_bus`) and the
          collision claims and words that dsp_step reads (`adv_cc`,
          `adv_cw`): slots 7..63 from that sample's fetch, slots 0..6 from
          the next sample's (in the round: their own fetch one sample later;
          at the round end: run()'s preview, with this round's registers).
        - `dsp_sample(i)`: run()'s part between the slots and the output
          stage. Bus sums into `MIXS_bank` (20-bit wrap; a bus no slot points
          at keeps its value), the claims into `coll_claim` / `coll_word`,
          then the DSP (recompiler hook, else dsp_step). The caller sets EXTS
          before and runs `final_mix()` after. It leaves the rows zero.
        - Claims only for slots that an even program step with MWT or MRD
          reads (`coll_need`, kept on MPRO writes).
        - `slot_fast` puts V16 and CA in the 2 spare lanes of its mix row (in
          A2's shared rows nothing reads them). With the DSP,
          `slot_fast_dsp` gives it private rows (`adv_row`), then adds l, r
          to the round's mix, makes the bus send (IMXL) from V16 and the
          claims from CA (slot_fetch inline), in 2 branch-free passes. Inside
          slot_fast, slot_quiet() reaches its claim test only after a sample
          with no fetch, where run()'s claim is 0, so it sees 0.
        - First try had the claim and the send inside slot_fast's loop: A2
          (DSP off) got 6% slower from register spills alone. Lanes cost
          +0.5% in A2 now.
        - The generic path (`slot_sample`) makes the claim and the bus send as
          run() does.
        - `final_mix()` skips EXTS and EFREG outputs at level 0 (they add 0).
        - The model keeps the decoded program as `dsp_op_t` (new
          `dsp_op.h`, one byte per field): 3 KB of LS, was 12 KB.
          `dsp_inst_t` stays caique's (`unsigned`): the caique cases include
          their own dsp_asm.h under the same guard, so a byte `dsp_inst_t`
          gave the PC case harnesses two AicaModel layouts (found 2026-10-04:
          adpcm_hi ran away to 7 GB; the PS3 and SPU builds were not affected).
      - SPU: `to()` runs rounds of DSP_ROUND, then per sample EXTS,
        dsp_sample, final_mix and the resampler. The A1 check alone ("SPE
        AICA Check", Sound OFF) runs this path too, without the output stage,
        so run() left the SPU image (7 KB of text). `setup.snd_dsp` selects
        A3.
      - PPU: the boot line says "SOUND from the SPU (DSP on, dsp jit X)"; with
        the DSP the `[aica-spe] dsp jit` line stays beside the sound line.
        The control twin is allocated with memalign (16-aligned members).
      - Limits, as since A1 (not new): the LS ring never syncs with wave RAM.
        Guest writes to the ring area are not seen; DSP writes reach neither
        the guest nor the slots. A 64K-word ring (RBL 3) and TABLE writes go
        through the 16 KB cache path and are lost at each CHECK; `slow=` on
        the dsp jit line counts such accesses of the program (Crazy Taxi: 0).
        A ring moved by a 0x2804 write keeps the old delay-line data.
      - PC tests (WSL):
        - `misc/spu_sim/diff_a3` (new, `BIN=diff_a3 diff_seeds.sh`): base
          run() with the DSP (ee0def6) against advance() + dsp_sample() +
          final_mix() in random chunks. Random programs (dense to sparse,
          TABLE, every ring size), COEF, MADRS, RBP, EFSDL, CPU writes to
          MPRO / TEMP / MEMS / MIXS / EFREG, wave RAM reads and writes; slots
          read below 0x1C0000, the DSP only above it. DAC every sample;
          control, DSP state and the DSP's RAM every chunk: 2000 episodes,
          50.3 M samples, 0 mismatches.
        - `misc/spu_sim/mutate_a3.sh` (new): 26 of 26 output-relevant bugs
          caught; the 27th (a stale claim seen by slot_quiet) is perf only.
        - `misc/a3_check/run.sh` (new): all 73 caique cases, run() with the
          DSP against the A3 order: 28377429 samples, 0 DAC, DSP state,
          control or RAM mismatches (dsp_coll and dsp_mem* included).
        - Unchanged results: diff_adv (A2) 2000 episodes, 99.8 M samples, 0
          mismatches; mutate_adv 28 caught as before; all caique cases + 13
          validators identical to the base; a2_check (snd_unit PASS, 0
          mismatches); dspjit_check (ISA probe ok, 64000 self-test samples,
          28377429 DSP checks, 0 bad). Rerun on the final tree (after the
          dsp_op_t fix): diff_a3, diff_adv, both mutation tests, a3_check
          and a2_check as above; dspjit_check and the caique cases were
          still running when the session ended (log `allpc_final2.log` in
          the scratchpad of session 609fa37c).
        - Simulator, real image (`misc/spu_sim/run_dsp.sh`; new scenarios
          `dsp` and `dsprand` in run_a2, `SND=0` = the A1 check alone): every
          48 kHz frame against the base model with the DSP, bit-exact with
          the recompiler, CHECK and the interpreter; A1 check alone `bad=0`.
          SPU cycles per sample (% of real time):

          | scenario | A2 | A3, DSP JIT ON | A3, CHECK |
          |---|---|---|---|
          | 28 voices, no DSP program (mix) | 22172 (30.6%) | - | - |
          | 28 voices, 1622-word program | - | 34563 (47.6%) | 52645 (72.6%) |
          | 8 voices, 1712-word program | - | 16497 (22.7%) | 34375 (47.4%) |
          | 64 voices, 1952-word program | - | 65796 (90.7%) | 84905 (117.0%) |

          A2 against the A2 high voices build: mix 28 +0.5%, pcm16 28 +0.3%,
          mix 8 -1.2%. The programs are random and dense (2-5 slow TABLE
          reads per sample, half the EFREG outputs on); the DSP code alone
          is 8-9% of real time. Crazy Taxi's program (A1b): 1434 words, 0
          slow.
      - SPU image: text 83.7 KB, bss end 0x3C6B8: 14.3 KB of LS for the stack,
        deepest path ~4.7 KB (`-fstack-usage`). PPU `.got` 0x9BD8, 0 `r2off`.
      - HW test: Dynarec ON, JIT Fastmem ON, SH4 Clock 150, SPE AICA Sound
        ON, SPE AICA DSP ON. Boot 1 with SPE DSP JIT = CHECK: Crazy Taxi into
        the game, then a ChuChu Rocket stage. Boot 2 with ON: the same.
        Pass: the game's reverb and echo are heard where A2 had none, no new
        noise, no stuck notes; `dsp jit self-test PASS`, `dsp jit CHECK ...
        bad=0`, `bad=0`, `drops=0`. Read `spu busy`, `spu % of wall: dsp`,
        `slow=` and SPEED/VPS against A2.
      - HW 2026-10-04 (build of 03:55, log `ps3log_a3_run1.txt` in the
        scratchpad of session 609fa37c), AudioBuffers=0. Matt's verdict
        (SUCCESS / FAILURE) is not given yet. Log, every boot: SPU thread up
        with the DSP, `dsp jit self-test PASS`, `bad=0` in every window.
        - Crazy Taxi, CHECK: game program 1434 words, 79 live steps,
          `slow=0`; DSP check `bad=0` (~31500 checks per window); `drops=0`;
          SPU busy 31-40%. Dense (vt > 3K): SPEED 88.4%, VPS 53.0 (A2 high
          voices: 87.6%, 52.5). Matt: the main menu still crackles; the menu
          seconds have 0 silent blocks and fill 3400-4400 (as in A2).
        - ChuChu Rocket, CHECK (short): program 2132 words, `slow=0`, check
          `bad=0`, SPU busy 61.5% at 21 voices. Matt: the echo is still
          there (old, also with nullAICA and A2).
        - Crazy Taxi, ON: SPU busy 12-16%; dense SPEED 87.4%, VPS 52.4.
        - Sonic Adventure, ON, ~17 min: program 2768 words, 127 of 128 steps
          live, `slow=0`; SPU busy avg 22.6%, max 35.4%; DSP 11% of the SPU.
        - `drops` in both ON boots (566105 and 902061 frames) only while
          Matt had Speed Limiter OFF in the in-game menu (250-365% speed:
          the SPU makes audio faster than real time, the full ring drops it).
        - Matt 2026-10-04, after the run: the ChuChu echo is most likely a
          sound that plays much slower than normal, maybe tied to a Quake 3
          issue; he has an idea of "what we got wrong". Next session.
        - 2026-10-04, later: the echo, the menu crackle and Looney Tunes
          were investigated. See section 6.
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

## 6. Sound faults (investigation 2026-10-04)  PARKED 2026-10-04 (Matt)

Matt, 2026-10-04: do each item below in its own session, then go back to the
real goals. Each item: build (`./build.sh pkg`), the PC checks it lists, a
HW test on Matt's PS3, then Matt's verdict. A new option goes in nullDC.cfg
[ps3] (cfg* table, `ps3/ps3_presets.cpp`) and on the AUDIO page; a live one
also goes in `k_live_opts` (`ps3/ps3_menu.cpp:780-784`). Main-menu changes
print nothing today, so the first item to build adds a `[sndcfg]` line (all
sound and ARM7 settings, at boot and on each change). Defaults keep today's
behaviour unless the item says otherwise.

Source: a read-only investigation (7 lenses, each checked by an adversarial
reviewer, then a synthesis and a critic). Full proposal with every log line:
`proposal_final.md` in the scratchpad of session b712e835. Logs used:
ps3log_ctl_run1 (9b40b305), ps3log_a2_run2 (43d8f55c), ps3log_a2hv_run1
(f83a8fb2), ps3log_a3_run1 (609fa37c), ps3log_a1b_run1 (3534ff6d), and the
ARM census logs s3_run1.log / s3_sc.log (16a921ca).

| Item | Fault | Status |
|---|---|---|
| 6.1 ARM7 Idle Skip SAFE | F1 ChuChu | FAILURE on HW 2026-10-04: SAFE does not fix F1; the logs work. Diagnostic build HW run done; KYONB option FAILURE on HW 2026-10-04 |
| 6.2 ARM7 Interleave SAMPLE | F1 ChuChu, F3 Looney Tunes | `[ ]` |
| 6.3 ARM7 Rate (capped) | F1 ChuChu | `[ ]` |
| 6.4 TMU Clock EFF | every game at SH4 150 | `[ ]` |
| 6.5 AICA Race Fix | F3 Looney Tunes | `[ ]` |
| 6.6 SPE AICA Sync | F2 Crazy Taxi crackle | `[ ]` |
| 6.7 JIT FMOV.D ABI fix | every game | `[ ]` |
| 6.8 Verified, not scheduled | various | - |

### 6.0 Facts set on 2026-10-04

- F1 ChuChu Rocket: Matt compared it with Flycast on a PC. Our sound effects
  play slower. That is the "echo".
- The 2026-09-23 echo tests ran at SH4 Clock 200 MHz (the 150 MHz option
  came later that day). So F1 is not only an underclock effect.
- ChuChu's main PCM16 instrument (ctl:11766): `SA=0x02A04E LSA=17453
  LEA=30266 loop OCT=15 FNS=349 ... D1R=0 D2R=0 DL=0 RR=12`. It plays at full
  level until KEY_OFF, with a 12813-sample loop (433 ms). A late KEY_OFF
  makes the note ring on.
- F2 Crazy Taxi: Matt, SPE AICA Sound OFF (nullAICA) is clean in the menu
  at full speed; ON crackles. The fault is in the SPE path.
- F3 Looney Tunes: the twin and nullAICA boots match for 11 log windows,
  then split. With the twin the driver loops one MSLC write + one monitor
  read per 34.76 samples (1269/s); with nullAICA all AICA writes stop.
- Quake 3: Matt cannot recall the issue. No Quake 3 PS3 audio bug makes a
  sound slower. Dropped.
- ARM7 cycles per AICA sample: ours 170 at SH4 150, 226.7 at 200; Reicast
  256, nullDC-v1 256, Flycast 512. All three run the ARM once per sample.
- Ruled out, with proof:
  - Output path: `late=0` and `dropped=0` in all 7084 `[aud]` lines; rate
    exact (48009 frames per 44107 samples); resampler, DMA and ring order
    correct.
  - Mixer and DSP (echo with nullAICA, A2 and A3); sample clock (+0.01%);
    PCM16 pitch and note life exact in emulated time (`life=650ms`,
    ctl:3402/3422).
  - ARAM and register byte order on every path (non-uniform values); timer
    and interrupt bitfields on BE; CD-DA; SPU against the twin (`bad=0`,
    `full=0` in 1249 windows).

### 6.1 ARM7 Idle Skip SAFE  FAILURE (HW 2026-10-04)

- Defect: a backward branch to its own block in a store-free block costs
  +100 ticks (`plugs/vbaARM/arm7_jit.cpp:1652-1653`
  `e_ticks(selfLoop ? 103 : 3)`; `arm7_cached.cpp:737-746`, rule at
  `:947-963`). Only stores, SWP, MSR and LDM/STM clear `blockSafe`; register
  writes and LDR writeback do not. So a counted delay loop
  (`subs rN,rN,#1; bne`) or a scan loop costs 121 ticks per pass, not 21:
  4.2-5.8x slower in emulated time. Soul Calibur: 22-23.5% of the ARM
  budget, true cost 4.4-4.7% (s3_sc:3277). Flycast, Reicast and nullDC-v1
  have no idle skip. Present since 5a32b94.
- Change: option "ARM7 Idle Skip" OLD / SAFE (`Arm7IdleSkip`), default OLD,
  live (flush the ARM code caches, as the JIT C-Calls toggle does with
  `arm_jit_on_reset()`, plus the cached engine's decode cache). SAFE keeps
  the +100 only for a true wait loop: no register the block writes is a
  source of the same or an earlier instruction (so `subs r3,r3,#1` counts),
  and no load uses writeback or post-index.
- Logs (also used by 6.2, 6.3, 6.5):
  - `[arm7s]` once per second: ARM runs/s, ticks/s, FIQs/s. Per timer A/B/C:
    last TIMx/md and period P = (256-TIMx)<<md samples, overflows/s,
    merges/s (an overflow while its SCIPD bit is still 1), reload latency.
    SCIEB, SCIPD, SCILV0-2, MCIEB. Monitor reads/s and MSLC writes/s, split
    by ARM and SH4.
  - `[sndcfg]` (see the section head).
  - `[pcm16s]` note aggregates, nullAICA sound only (the diag clock does not
    run in SPE sound): most-keyed SA, key-ons, key-offs, ignored key-ons,
    held mean/max in samples, age of the oldest active note, histogram of
    the gap to the next key-on of the same SA.
- Check: `[ARM7TEST] PASS: all 45 tests passed.` on HW.
- HW test: ChuChu Rocket, SPE AICA Sound OFF. Title/menu, confirm 10 times
  per setting, OLD then SAFE; then 60 s of a stage each. Compare by ear with
  Flycast. Read `[arm7s]`, `[pcm16s]` (held, oldest), `arm:`. Then check
  Crazy Taxi and Soul Calibur speed with SAFE.
- 2026-10-04: built (`./build.sh pkg`), PC tested, HW test pending.
  - Option AUDIO -> "ARM7 Idle Skip" OLD / SAFE (`Arm7IdleSkip`), default
    OLD, live. On a change the in-game menu calls `arm_FlushCode()`
    (`arm7.cpp`): the cached decode cache plus `arm_jit_on_reset()`.
  - One rule for both engines: `plugs/vbaARM/arm7_idle.h` (`Arm7IdleScan`).
    SAFE = the old `blockSafe` and `scan.ok`, over the block from its start.
    The JIT block of a loop starts at the loop target after the first pass;
    the cached engine scans its decoded block (more conservative when a loop
    branches into the middle of a block).
  - `[sndcfg] boot:` at launch, `[sndcfg] menu:` after an in-game change of
    a sound setting (running values, cfg key names).
  - `[arm7s]`, once per wall second, rates per emulated second (AICA samples
    / 44100): ARM runs, ticks granted, FIQs; per timer the last TIMx/md
    written, P, overflows, merges, reload latency avg/max in samples; SCIEB,
    SCIPD, SCILV0-2, MCIEB at print time; monitor reads and MSLC writes split
    SH4 / ARM7 (`g_arm7_in_run` around `arm_Run`). Both sound back ends.
  - `[pcm16s]` adds `oldest=` and the most-keyed SA of the second: kon, koff,
    kon-ignored, held mean/max (n) in samples, active, oldest, pair gaps
    <64/<256/<1024/<2048 and their median. `[pcm16] off` and `end` show SA.
  - PC check (WSL; the real `arm7.cpp` + `arm7_cached.cpp`; scratchpad of
    session e184b2b5, `idletest/run.sh`): 15 scan cases pass. Counted loop
    `subs r3,r3,#1 / bne`: OLD 111 ticks per pass, SAFE 15. Post-index scan
    loop: 48 against 240 words in 6000 ticks. A store loop counts the same
    in both modes. The JIT uses the same scan; only HW tests it.
  - ELF: `.got` 0x9D30, 0 r2off stubs.
  - Comments longer than 2 lines in the touched files were trimmed (Matt's
    rule), no code change in those edits.
- HW 2026-10-04, ChuChu Rocket, SH4 150, Batch 4, SPE AICA Sound ON (the
  `[sndcfg]` line showed it; `[pcm16s]` therefore did not print). Self-test
  PASS, full speed. Matt: the slow sound is the same with OLD and SAFE.
  FAILURE: the idle-skip over-charge is not F1. Option kept, default OLD.
  The SAFE part has no log (the listener stopped at 12:29:57).
- `[arm7s]` found the likely F1 cause (6.2). The driver sets its timer to
  TIMx 0xD4, md 0: P = 44 samples, 1002 Hz on hardware. Measured 872/s,
  reload latency 6.6 samples (max 7), in the menu (timer A) and in the next
  boot (timer B). The FIQ waits for the next Batch 4 burst (every 8.43
  samples at SH4 150), so the period locks to ceil(44 / 8.43) x 8.43 = 50.6
  samples: the sound driver runs 13% slow. Same rule: Batch 1 at 150 gives
  about 996/s, Batch 4 at 200 MHz about 996/s (bursts of 6.32 samples).
- Debug step 1, HW 2026-10-04: ChuChu, SH4 150, Batch 1, SAFE, SPE AICA
  Sound ON. Timer A 996-997/s, latency 0.3 samples (max 1): the driver tick
  is now right (HW 1002/s). Matt: the slow sound is still there. So the
  tick rate (and with it the Batch 4 FIQ delay) is NOT the cause of F1.
- Debug step 3, HW 2026-10-04: SH4 200, Batch 4 (Matt also tried 8, 2, 3
  live), SAFE, SPE AICA Sound ON. Timer A 996-997/s, ARM7 226.7 ticks per
  sample (was 170). Matt: still slow. Ruled out for F1: Idle Skip (6.1), the
  tick rate and FIQ delay (the point of 6.2), an ARM7 budget of 170 -> 227
  (most of 6.3's cap of 256), and the TMU (6.4, exact at 200 MHz).
- Matt 2026-10-04: against Flycast the pitch is the same; the sound is only
  longer and slower. So the sample rate is right; the key events come late
  or the notes are held too long.
- nullDC4Wii (checked 2026-10-04, Matt asked): it never fixed F1. Commit
  cc192ac: "SA=0x2A04E (the problematic SFX chord) sounds echoey/slow-motion
  ... plays correctly on real hardware. Root cause unresolved"; its only
  answer is the mute_pcm16 preset (our AUDIO "Mute 16-bit PCM"). Its README
  keeps "Fix Chuchu Rocket 16Bit PCM bug" open. Its other attempt, the
  KRS=0xF rule in AEG_EffRate, is already in our code (Flycast's form).
  Our envelope tables, rates and attenuation already match Flycast's.
- HW 2026-10-04, ARM7 Speed test (decision table row b): SH4 150, Batch 1.
  Boot 1 HALF (85 ARM7 ticks per sample), boot 2 FULL (170), then live
  HALF, FULL, EIGHTH (21; the tick then drops to 910/s), Mute 16-bit PCM,
  AICA Fast, CD Audio. Matt: HALF and FULL sound the same. ARM7 time is
  ruled out for F1. The table's next step: `[g2dma]`, `[mem64]`,
  `[montrace]` (6.5, 6.7), then a key-on/key-off command trace.
- HW 2026-10-04, `[pcm16s]` run: SPE AICA Sound OFF (nullAICA), SH4 150,
  Batch 1, FULL, SAFE; ChuChu menu, select confirmed ~10 times. Only SA
  0x02A04E plays as PCM16 (FNS 349 and 705, plus OCT 0 FNS 4).
  - 11-16 of these notes are active at once; the oldest is up to 392304
    samples (8.9 s). Held key-on to key-off: 0.7-6.2 s (mean per second
    112000-272000 samples).
  - Key-offs come in batches: 5 slots at the same moment, every 1.7-2.4 s
    (t=30050, 31969, 33973, 35755, 37590, 39577, 41311, 43746 ms). Key-ons
    come one by one. Each released note then fades for 2.8 s (RR 12).
  - kon-ignored 13-492/s: each KYONEX write re-keys every held slot
    (KYONB=1), as on hardware and in Flycast; not a fault by itself.
  - With the same key timing, Flycast would sound the same (same key, rate
    and envelope rules), so the key-on/key-off timing itself must differ.
    No reference trace exists yet.
- caique-rtl checked 2026-10-04 (Matt asked; `E:\...\Dreamcast\caique-rtl`,
  console-measured AICA). Its slot side is what SPE AICA Sound already
  plays, and F1 is there too: the slots are not the cause. What the CPUs
  see is still nullAICA's in both modes, and caique measured these
  hardware rules that nullAICA breaks (NOTES "Timers and interrupts",
  "Loops and key events", `aica_model.cpp` read()/chan_mask()):
  - KYONB is never cleared by the hardware; nullAICA clears it on release.
    KYONEX and slot reg 0x00 bits 13:11 read 0.
  - TIMA/B/C, SCILV0-2, SCIRE, MCIRE read 0 (nullAICA: live or stored).
    0x2800 reads 0x0010 (VER 1), 0x2804 0, 0x2808 0x0900.
  - One timer prescaler, never restarted by a write (nullAICA restarts it).
  - MCIPD gets bit 10 (sample done) like SCIPD (nullAICA: SCIPD only).
  - FIQ (wren7 handshake): level 0 never interrupts, reading L releases
    nFIQ, writing M bit 0 re-arms (ours: released only by the M write).
  - Flycast also has the AICA internal DMA (0x288C DEXE: clear or copy
    between wave RAM and registers, DMA-end interrupt, 15a99aeeb); neither
    nullAICA nor caique has it.
- **ChuChu diagnostic build** (Matt 2026-10-04: built now, HW run in the
  next session). Logs only, no behaviour change, `.got` 0x9DE8, 0 r2off.
  - `[aicarw]` once per second, per emulated second, split SH4 / ARM:
    KYONEX writes, DEXE writes (0x288C bit 0; counted, not carried out),
    slot register reads and reg 0x00 (KYONB) reads, timer reads,
    write-only register reads (SCIRE, SCILV0-2, MCIRE: 0 on hardware),
    irq register reads. With any DEXE: the last DMA's gate, dir, dlg,
    dmea, drga. (`aica.cpp`, `aica_mem.cpp`)
  - `[key]` per KYONEX write: sample time, CPU, the writing slot, the
    KYONB mask of all 64 slots, and (nullAICA sound only) the slots it
    started and released. At most 120 lines per second; drops show on
    `[aicarw]`. (`sgc_if.cpp` RegWrite 0x01)
  - `[montrace]` every ~5 s while there were monitor reads: the last 32
    reads of 0x2810/0x2814 with time, CPU, slot, byte or word, the value
    returned, and the slot's KYONB, LPCTL, LSA-LEA. (`sgc_if.cpp`)
  - HW procedure: SPE AICA Sound OFF (so `[key]` start/rel and `[pcm16s]`
    are valid), SH4 150, ARM7 Batch 1, ARM7 Speed FULL. ChuChu Rocket menu:
    confirm the select sound ~10 times; then 60 s of a stage.
  - Decision: DEXE > 0 -> port Flycast's internal DMA (15a99aeeb) as an
    option. Reg 0x00 reads > 0 -> option for "KYONB never cleared". Timer
    or write-only reads > 0 -> option "read as 0". `[key]` cpu=SH4 -> the
    game keys the notes from the SH4: look at the SH4 side. `[montrace]`
    shows the value the driver polls while the notes hang. If none of
    these shows anything: a Flycast key trace on the PC (VS 2022 is
    installed) as the reference.
- HW 2026-10-04, ChuChu diagnostic build: SPE AICA Sound OFF, SH4 150,
  Batch 1, FULL, SAFE; menu (~10 confirms), then 60 s of a stage. Speed
  ~100% in the menu, timer A 996/s. Log `ps3log_diag_run1.txt` (scratchpad
  of session 558e9945).
  - DEXE 0. Timer reads 0, write-only reads 0, irq reads 0. KYONEX 5343, all
    from the ARM. Reg 0x00 reads 4826, all ARM: the read-modify-write of a
    KYONEX write (title: ~210/s on slot 63, no key change; menu: the slot
    being keyed). Only the "reg 0x00 reads" row matched.
  - `[montrace]`: the ARM reads 0x2810 of all 64 slots, ~6000/s (MSLC
    writes ~5000/s). Held notes read 0x4000, idle slots 0x63FF.
  - KYONB went 1 -> 0 without a key-off: 0 times in the menu, 66 in the
    stage (slots that ended by themselves). With SPE AICA Sound ON the
    nullAICA channels do not step, so KYONB already stays as written, and
    the monitor reads come from the caique twin; F1 was there in those runs.
  - The key-offs come late. Every menu note is looped and stops only on a
    key-off. Menu: 100 key-offs, median hold 205,000 samples (4.6 s), max
    408,102 (9.3 s). SA 0x02A04E notes stop at a cap of 271,484-273,000
    samples (6.16-6.19 s) unless a batch releases them first; in 16 of 20
    cases the next key-on comes 38-72 samples after that key-off. Other
    key-offs come in batches of 6 slots within 6 samples. Probably the
    select sound: a slot pair (loop 6296-10237) keyed every 2-3 s, each
    pair held 4.8-9.3 s. The 0x02A04E notes form an arpeggio: each note
    repeats 3 steps later on the other side, quieter; a step is ~22,550
    samples (0.51 s).
- **KYONB option** (Matt 2026-10-04: "Let's do the KYONB never cleared
  option"). Built 2026-10-04 14:04, `.got` 0x9DF8, 0 r2off. HW: FAILURE.
  - AUDIO -> "KYONB" CLEARED ON END / NEVER CLEARED (`AicaKyonb`), default
    CLEARED ON END (today's nullAICA), live; on `[sndcfg]`.
  - NEVER CLEARED: `SetAegState(EG_Release)` no longer clears KYONB
    (`plugs/nullAICA/sgc_if.cpp`). A per-slot `keyed` latch (set by a key-on,
    cleared only by a key-off) keeps the hardware key rule: a slot that ended
    by itself ignores key-ons until a key-off. Without the latch, every
    KYONEX would restart such a slot.
  - Back to CLEARED ON END in the in-game menu: `sgc_KyonbSync()` clears
    KYONB on slots that ended by themselves, as the old rule would have.
  - It acts on nullAICA only: with SPE AICA Sound ON it changes nothing.
  - HW test: same settings as the diagnostic run (SPE AICA Sound OFF), KYONB
    NEVER CLEARED before boot. ChuChu menu ~10 confirms, 60 s of a stage;
    then SELECT, CLEARED ON END, ~10 confirms (A/B by ear). Log check: in
    the stage, no KYONB 1 -> 0 without a key-off; menu hold times.
  - HW 2026-10-04 (`ps3log_kyonb_run1.txt`, scratchpad of session
    558e9945): FAILURE. Matt: still the same. With NEVER CLEARED, no KYONB
    bit went 1 -> 0 without a key-off; after the switch to CLEARED ON END
    (86.9 s), 25 did, all in the stage. The NEVER CLEARED part covered the
    menu (24-55 s) and a silent screen (56-86 s, 0 voices, no KYONEX); the
    stage ran from ~118 s, with CLEARED ON END. Menu holds unchanged (0x02A04E
    cap 271,434-271,484 samples). After the switch, on another screen
    (87-107 s), each confirm keyed one 0x02A04E note: held 344 ms, ended
    513 ms after its key-on (the driver sets a faster release there).

### 6.2 ARM7 Interleave SAMPLE  `[ ]`

- Fact: with Arm7Batch 4 (default, `ps3_presets.cpp:20`) and accuracy FAST
  (7168 SH4 cycles per MediumUpdate), the ARM7 runs one burst of 1433 ticks
  every 8.43 AICA samples at SH4 150 (191 us) (`arm_aica.cpp:59-77`). The
  AICA steps after the burst (`:94-106`). So:
  - at most one FIQ per source per burst, up to 8-9 samples late;
  - inside a burst the ARM sees no AICA progress (a KEY_OFF, wait, KEY_ON
    on one slot; a poll of the monitor or SAMPLE_DONE);
  - Batch 1 still freezes 2.1 samples per run.
- Measured: the Crazy Taxi / BIOS driver tick runs 435.7 per emulated second
  at SH4 150 (12 bursts) and 498.3 at 200 (14 bursts): `fiq 1744/s`
  (s3_run1:1325) against `fiq 1990/s` (s3_run1:36932). On HW it is fixed.
  The tick is quantized to bursts.
- Change: option "ARM7 Interleave" SLICE / SAMPLE (`Arm7Interleave`),
  default SLICE, live. SAMPLE: for each whole sample due, `arm_Run`
  (per-sample ticks, remainder carried), then `libAICA_TimeStep()`, as
  Flycast does; Batch is ignored. Each run's overshoot is carried into the
  next run (`arm_jit_run` / `arm_Run_Cached` return it); with 170-tick runs,
  dropping it would grant up to 64% extra on idle branches.
- Risk: `IC_DEMOTE_SLICES` (`arm7_jit.cpp:440`) counts runs, so its window
  gets 8.4x shorter.
- Cost: 44.1K ARM runs/s instead of 5.2K, estimated 0.5-2% of wall; measure
  it on the stats line.
- HW test: as 6.1, SLICE against SAMPLE (Batch 1 as a control). Looney
  Tunes (SPE AICA Sound ON): does the 34.76-sample loop change, do voices
  come back.

### 6.3 ARM7 Rate (capped)  `[ ]`

- Fact: ticks = Cycles / 20 (`arm_aica.cpp:61,71`, `arm7.h:20`). The budget
  follows SH4 Clock: 170 per sample at 150, 226.7 at 200. Engines charge 6+
  ticks per instruction (ours about 8.3 on average), so ours runs about 20.5
  instructions per sample at 150; Reicast's dynarec about 28.4.
- Change: option "ARM7 Rate" LEGACY / 256 (`Arm7Rate`), default LEGACY,
  live: ticks = (banked SH4 cycles x rate + remainder) / aica_sample_cycles,
  remainder carried; the ARM7 Speed shift still applies. Move the
  `aica_sample_cycles` refresh above its first use.
- Matt, 2026-10-04: yes to a cap (256 was the example). 384 and 512 only as
  test steps if 256 is not enough.
- Note: F1 was heard at 200 MHz (226.7 ticks), and 256 is only 1.13x that.
  6.1 and 6.2 are more likely to decide F1.
- Cost (linear model from a2hv; in SPE sound the twin catch-up of each
  monitor read sits inside the ARM bracket, so measure it): `arm:` x1.51 at
  256: Crazy Taxi dense 6.0 -> about 9%, ChuChu stage 9-12 -> about 14-18%.
  At 512: about 18% and 27-36%.
- HW test: as 6.1.

### 6.4 TMU Clock EFF  `[ ]`

- Defect: `UpdateTMU(s_timeslice)` gets raw SH4 cycles
  (`dc/sh4/sh4_interpreter.cpp:313, 334`), and `dc/sh4/tmu.cpp:145-147`
  assumes 4 SH4 cycles per Pclk at 200 MHz. Every other time base uses
  `SH4_CLOCK_EFF`: RTC (`sh4_interpreter.cpp:281`), VBlank (`SPG.cpp:71`),
  AICA (`arm_aica.cpp:90`), G2 DMA (`aica_if.cpp:313`), maple, GD-ROM. At SH4
  150 the TMU gives 37.5M Pclk per emulated second, not 50M: every TMU
  period is 1.33x too long against video and sound, in every game.
- Not F1's root cause (F1 exists at 200 MHz, where the TMU is exact); it may
  add to it at 150.
- Change: option "TMU Clock" SH4 / EFF (`TmuClock`), default SH4, live. EFF
  feeds UpdateTMU `Cycles x 200 / SH4 MHz`, remainder carried. At 200 MHz
  both are the same. Cost: one multiply per timeslice (<0.05%).
- HW test: ChuChu menu at SH4 150, SH4 against EFF; Crazy Taxi and Soul
  Calibur for regressions.

### 6.5 AICA Race Fix (Looney Tunes)  `[ ]`

- Reference: Flycast a920ae590 (2021-02-14), "aica: fix race conditions
  between sh4/arm - Fix sound issue in Test Drive Le Mans and Looney Tunes".
  Ours differs:
  - `Read_SB_ADST` never runs: `REG_READ_DATA` (`aica_if.cpp:435`) makes
    `sb.cpp:354-361` return the raw register first.
  - No busy guard: a start while SB_ADST&1 is accepted
    (`aica_if.cpp:272-278`).
  - ADSTAR/ADSTAG advance at the start (`:301-302`); Flycast and Reicast
    advance them at completion.
  - End rule `SB_ADEN = bit31 ? 1 : 0` (`:217`), the opposite of Flycast
    a44619931 (Reicast, nullDC-v1 and Minicast keep ours). A later start
    with ADEN=0 is dropped silently (`:277-278`).
  - SB_ADSUSP bit 4 is never maintained.
  - No avoidRaceCondition: when the ARM raises the SH4 line (MCIPD bit 5 or
    MCIEB), Flycast runs the ARM at least 50 more ticks (`arm7.cpp:77-80`);
    ours only calls `UpdateSh4Ints` (`aica.cpp:208-215`).
  - SCIEB/MCIEB writes take the default path, with no
    `update_arm_interrupts()` / `UpdateSh4Ints()` (`aica.cpp:187-241`);
    Flycast updates both at once.
- Not shown: that Looney Tunes uses G2 DMA.
- Change: option "AICA Race Fix" OFF / ON (`AicaRaceFix`), default OFF,
  boot only (the ADEN rule changes DMA state). ON: all the points above.
- Logs:
  - `[g2dma]` when non-zero: starts/s, KB/s, `noaden` (starts dropped by
    ADEN=0), `busy`, `dup` (same STAR/STAG/LEN), `irqrd` (SB_ADST reads while
    the SPU DMA IRQ is pending), bit 31 counts, `bell` (+50-tick runs).
  - `[montrace]` every 5 s while monitor reads > 0: the last 32 monitor
    accesses (CPU, MSLC, register, size, value, slot state). It shows the
    value a stuck driver waits for, so no second diagnostic build is needed.
- HW test: Looney Tunes with SPE AICA Sound ON (the 34.76-sample loop exists
  only with the twin), one race, 60 s, OFF then ON (next boot). Voices and
  effects? Also live: Batch 1 moves 34.76 to about 31-32 = timer reload
  after FIQ latency; ARM7 Speed HALF moves it to about 70 = ARM-bound loop;
  neither = paced by the SH4 or a monitor value (`[montrace]`).

### 6.6 SPE AICA Sync (Crazy Taxi crackle)  `[ ]`

- Cause (Matt: SPE AICA Sound OFF clean, ON crackles):
  - The PPU publishes records at each 64-sample CHECK
    (`ps3/ps3_aica_spe.cpp:172-173`). The SPU renders a block only after the
    guest has passed its end, and it fetches ARAM live
    (`ps3/spu/spe_aica_model.cpp:15-20`; cache dropped in `am_check`,
    `:163-170`).
  - Guest ARAM writes land at once (`dc/mem/sh4_area0.cpp:229-233`; G2 DMA
    copies at the start, `aica_if.cpp:295-299`; ARM JIT stores inline), and
    the guest reads the exact CA from the twin (`sgc_if.cpp:1162-1181`).
  - So a refill 0-63 samples behind CA (+8-26 samples of SPU render time,
    more in A3 CHECK) plays next-lap data: a click. nullAICA is at most 2.1
    samples off.
  - Crazy Taxi music: looped PCM16 rings `SA=0x1D7E80` / `0x1DBEC0`,
    `LEA=8192` (ctl:2521-2522). `bad=0` cannot see it: CtlSnap holds
    control state only.
- Second, smaller suspect: clipping. The SPU path is 0.92 dB (even MVOL) to
  1.43 dB (odd) louder than nullAICA: nullAICA applies Volume=90
  (`nullAICA.cpp:71`, `sgc_if.cpp:1350,1356`) and 0.707 at odd steps
  (`:1129`); caique uses 0.75 and no Volume factor
  (`aica_model.cpp:726-731, 1089-1105`).
- Change: option "SPE AICA Sync" OFF / ON (`SpeAicaSync`), default OFF,
  live, SPE sound only.
  - New record `AICA_OP_SYNC` (`ps3/spu/spe_aica.h`): the SPU runs
    `to(sample)`, then drops its ARAM cache.
  - Trigger: a guest ARAM write inside a live slot's unrendered span (behind
    CA within `(s_now - echo()->samples) x step` + 8 samples). At most one
    per `s_now` and one per 64-sample block (the rest counted as `miss`).
    Push SYNC, publish, spin (no usleep) until `echo()->tail` reaches it;
    100 ms cap -> the existing failure path. Then the write lands.
  - Always on: invalidate the SPU ARAM cache at the start of each SPU batch
    (0 cost today, one batch = one CHECK).
- Logs: `[aram-hz]` (256-byte page bitmap near each live slot's play point,
  rebuilt per CHECK: hazards/s by sh4/dma/arm, minimum distance, syncs/s,
  miss/s, wait us); `clip=L/R`/s on the `sound SPU` line (in `final_mix`,
  outside `slot_fast`'s loop); nullAICA clip16 hits.
- Cost: bounded by the cap: <=3.9% of wall (A2), <=15% (A3 DSP JIT ON),
  <=38% (A3 CHECK); expected 0.01-0.3% in A2. Test in A2 or with DSP JIT ON.
- PC checks before the pkg: `misc/spu_sim` (`run.sh`,
  `BIN=diff_a3 diff_seeds.sh`), `misc/a2_check`, `misc/a3_check/run.sh`
  stay bit-exact.
- HW test: Crazy Taxi main menu with music at SPEED >= 99.5%, SPE AICA Sound
  ON, DSP OFF: 60 s Sync OFF, 60 s ON, 30 s OFF; count the crackles per
  10 s. If ON still crackles with hazards = 0 and clip > 0: next step is an
  SPU output gain.

### 6.7 JIT FMOV.D ABI fix  `[ ]`

- Defect: the PS3 JIT cold path for a 64-bit access to a handler page uses
  the 32-bit EABI layout.
  - Write (kind 3) leaves the data in r5:r6 and calls `WriteMem64(u32,u64)`
    (`ps3/ps3_driver.cpp:2586-2598`); ELF64 ELFv1 passes the u64 in r4,
    which holds no data.
  - Read (kind 1) takes r3:r4 (`:2549-2562`); ELFv1 returns the u64 in r3
    alone. So FR[n] = word[a+4] and FR[n+1] = junk.
  - The interpreter, SH4 Cached and JIT Safe Mode are correct.
- Pages: area 0 (AICA regs, ARAM, Holly/G2), the 0x05 VRAM window, area 5,
  P4 0xE4-0xFF. Store queues are not affected (block-mapped). May relate to
  the Soul Calibur black texture.
- Change: kind 1, after the call: `rldicl r4,r3,0,32; rldicl r3,r3,32,32`.
  Kind 3, before the call: `rldicl r4,r6,0,32; rldimi r4,r5,32,0` (safe with
  poisoned upper halves). Check the `c.fuct` path too. Option "JIT FMOV.D
  Fix" (`JitFmovdFix`), boot only (SH4 code cache), default ON (Matt
  2026-10-04). It changes every game.
- Log: `[mem64]` when non-zero: JIT cold-path 64-bit reads/writes per area
  (aram, aica, other area 0, 0x05, p4).
- HW test: ChuChu, Looney Tunes, Soul Calibur (black texture), Crazy Taxi:
  any change; `[mem64]` counts.

### 6.8 Verified, not scheduled

- Byte writes to timer and interrupt registers: the cases match exact
  addresses only (`aica.cpp:190-241`). A byte write to 0x2891/95/99 skips
  RegisterWrite; 0x28A1/0x28B9 store raw pending bits; 0x28A5/0x28BD clear
  nothing.
- `arm_Reset` (`arm7.cpp:355-395`) does not clear aica_interr, aica_reg_L,
  e68k_out, e68k_reg_L/M (`arm_mem.cpp:6-11`); Flycast does.
- An unaligned ARM word store scatters 4 bytes (`arm_mem.cpp:129-145`); the
  ARM7 and Flycast write the aligned word.
- AICA internal DMA (0x288C DEXE) exists in neither nullAICA nor caique.
- Sample-rate surplus: floor(150e6/44100) = 3401 gives 44104.7 Hz
  (+106 ppm); with AudioBuffers 0 the ring fill rises about 5 frames/s.
- nullAICA idle-slot EG readback is 0x3FF; caique and Flycast give 0x1FFF.
- ARM7 self-test: no STR/STRB, unaligned STR or register-window cases.
- `_vmem` handler ids: only ids 0-7 fit (id*4 under HANDLER_MAX 0x1F,
  `dc/mem/_vmem.cpp:16-17`); 8 are registered, so a 9th would break.

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
