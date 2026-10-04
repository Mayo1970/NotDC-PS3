# Optimization potential - what else to take from Reicast and Minicast

Review of Reicast (`E:\Users\Matteo\Desktop\Dreamcast\reicast-emulator`) and
Minicast (`E:\Users\Matteo\Desktop\Dreamcast\minicast`) against our tree,
beyond the JIT items that are already in `OPTIMIZATION_PLAN.md` (5.17 Step 3,
5.20-5.24). Written 2026-10-04 (Matt: "check Minicast and Reicast to see if
there's other helpful stuff"). Review only: nothing is built.

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
One change per HW run, each behind its own toggle, default OFF until HW
validated, SH4 Clock 200.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

- 5.18 (JIT hot-block profile) is not part of this file. It stays the next
  step in `OPTIMIZATION_PLAN.md`.
- Every item was read in both trees. A claim that was not checked in the code
  is marked "unverified".
- Line numbers are from 2026-10-04. If a line moved, search for the function.
- Reicast and Minicast are little-endian only. Check every ported line for
  byte order.

---

## 0. Summary and comparison with the plan

| # | Item | Kind | Helps | Size | Relation to the plan |
|---|---|---|---|---|---|
| P1 | Texture path: palette snapshot, block decoders, 16-bit upload | speed, race | Soul Calibur (render-bound) | step 0 small, steps 1-3 medium | extends the parked Soul Calibur faults (section 0), 5.12 SPE list #4, TArender.MD TR5 item 3 |
| P2 | Frame Drop = DROP A/B, then one pending slot | speed | Soul Calibur SH4 wait (22-28%) | A/B: no code; slot: medium | TArender.MD TR5 item 1: "the A/B was not run" |
| P3 | Async GD-ROM reads + CD-DA read-ahead | hitches | loading, CD-DA games | measure: small; build: ~150 lines | extends RESEARCH.MD 3b |
| P4 | FPSCR.DN -> PPC NI bit | speed (unverified) | unknown | 1 line + option | extends RESEARCH.MD 3e |
| P5 | Small speed items | speed | <= 1% each | small to medium | bind scan is 5.10 (not chosen) |
| P6 | JIT loop never stops | bug | every XMB exit, flash writeback | ~25 lines | explains section 0 "Other open items" (XMB exit) |
| P7 | SB_LMMODE0/1 ignored | bug | SQ and DMA texture uploads | ~40 lines | new |
| P8 | Intensity colour clamp order | bug | brightness (Reicast: Red Dog) | small | new |
| P9 | Hangs on rare input (5 fixes) | bug | robustness | 1-4 lines each | TArender.MD TR4 covers the store stage only |
| P10 | PVR register and list rules (7) | bug | unknown games | small to medium | new |
| P11 | TMU TCNT inside the timeslice | accuracy | Soul Calibur randomness (Flycast) | medium | new |
| P12 | RTC DST adds 24 h | bug | clock under DST | 1 line | new |
| P13 | Puru Puru (rumble) pack | feature | rumble | ~120 lines | new |
| P14 | Fog and offset colour shader reference | reference | 3a, 3b | - | 3a, 3b (open) |
| P15 | Freerunning (Minicast) | pacing option | - | medium | RESEARCH.MD says "not applicable"; close to 5.19 |

Main negative finding: the SH4-side modules (scheduler, TMU, INTC, memory
handlers) take only ~1-2% of the SH4 thread (estimate). The ~10.6% that dense
Crazy Taxi needs is not there. 5.18 stays the way to find it.

---

## 1. Speed

### P1 Soul Calibur texture path  `[ ]`

Facts (plan 5.12 S3 runs 2-3, 5.20 "HW"): in the fight the render thread is
busy 81-99%, texture decode is 82-85% of its wall, ~15 decodes and 4.7 MB
hashed per render, half the renders dropped. The TR list is mostly palette
textures (pal4 144, pal8 512 of 681 strips).

Ours:
- The render thread reads the live `PALETTE_RAM` for every texel
  (`ps3/ps3_tex.cpp` `cvt_pal()` :133-143) and for the hash (`hash_texture()`
  :286-292). The SH4 thread runs at the same time and can write the next
  frame's palette. The snapshot (`ps3/ps3_rend_regs.h` `RenderRegs`,
  `ps3/ps3_rend.cpp:1176`) has `pal_ram_ctrl` only. The palette dirty tracking
  is a TODO (`plugs/drkPvr/regs.cpp:115-124`).
- The hash already covers only the texture's own 16 or 256 palette entries. So
  a texture is already decoded again only when its own bank or its texels
  change.
- `decode()` (:296-387) is one loop per texel. In the disassembly of
  `build/ps3/ps3_tex.o` (`decode+0x608..0x75c`, pal4/pal8 loop): ~45
  instructions per texel, one microcoded `srw` per texel (two for pal4),
  `d.pal4` / `d.addr` / `d.pal_base` reloaded per texel, and a compare chain on
  the palette format per texel.
- Then ps3gl converts every texel a second time (`ps3/ps3gl/ps3gl_textures.c`
  `convert_pixels()` :309-355: 4 byte loads + swizzle table per texel) into a
  staging buffer, then `memcpy` to RSX memory (:626-647). Every texture is
  stored as A8R8G8B8.

Reicast:
- `palette_update()` (`libswirl/rend/TexCache.cpp:86-131`) runs at render
  start on the SH4 thread (`hw/pvr/Renderer_if.cpp:431`), only when a palette
  word changed (`hw/pvr/pvr_sb_regs.cpp:389-392`). It converts the 1024
  entries once and hashes each bank (`pal_hash_16[64]`, `pal_hash_256[4]`).
  A texture keeps its bank hash (`rend/gles/glestex.cpp:281-293`);
  `NeedsUpdate()` (:455-460) compares it.
- Block decoders: `texture_TW` (`rend/TexCache.h:512-536`) with detwiddle
  tables (`TexCache.cpp:31-82`). `convPAL8_TW` / `convPAL4_TW` (:445-487)
  take 8 texels (2x4) or 16 texels (4x4) from one 8-byte read, with the
  palette already converted.
- 16-bit uploads: 565, 1555, 4444 and 16-bit palettes go to GL as 16-bit
  types (`glestex.cpp:57-61`, `PAL_TYPE` :79-80).

PS3 form, in steps:
- Step 0 (diagnostic, one build): split each re-decode into "palette entries
  changed" and "texel data changed" (two hashes per entry, two counters in the
  `tex split` line). This decides the rest:
  - Mostly texel data: steps 2-3 are the lever.
  - Mostly palette: steps 2-3 still help. To stop the decodes themselves, the
    next lever is a palette lookup in the fragment shader (Flycast does this;
    needs a Cg shader and a palette texture; bilinear needs 4 taps).
- Step 1 (race + per-texel switch): set a dirty flag in the palette write
  handler. At STARTRENDER, copy the 1024 words into the render snapshot only
  when dirty (4 KB, SH4 thread). The render thread converts them once per job
  and hashes per bank. Textures read the snapshot, never the live RAM.
- Step 2 (decode speed): block decoders as in Reicast. Read 8 bytes, write
  the RSX swizzled order straight into the ps3gl staging buffer, and skip
  `convert_pixels()`. The PVR twiddle (y bit first) is the RSX swizzle (x bit
  first) with x and y swapped, so a 4x4 PVR block is one 64-byte run in RSX
  order. Needs a ps3gl entry point that takes swizzled data. 5.12 SPE list #4
  already notes "could write the swizzled layout directly".
- Step 3 (half the bytes): RSX has `GCM_TEXTURE_FORMAT_A1R5G5B5`,
  `R5G6B5` and `A4R4G4B4` (PSL1GHT `gcm_sys.h`) with the PVR bit order. Our
  VRAM words hold two texels (low half = even texel), so one `rotlwi 16` per
  word gives the texel order. IgnoreTexA can use the texture remap
  (`GCM_TEXTURE_REMAP_TYPE_ONE` on alpha) instead of a pass. Needs 16-bit
  format support in ps3gl. Check the 16-bit byte order on HW first.
- Expected: today ~45 instructions + 1 microcoded shift per texel, plus ~12 in
  ps3gl. A block decoder needs ~3-4 per texel (estimate, not measured).
- Related, already in the plan: the ps3gl bind scan (5.10, "gl" 2.6-2.8% of
  render wall in Crazy Taxi, not chosen).
- Optional, accuracy only: Reicast waits at the render-done IRQ until its frame
  has finished its texture stage (`rend_end_render()`,
  `hw/pvr/Renderer_if.cpp:486-502`; `re` is set after `Process()` :211-217).
  So TArender.MD 3.3 ("libswirl has the same hole") is not accurate. Our
  decode runs inside the draw, so the same sync would bring the SH4 waits
  back. Use it only if the black texture proves to be this race. Plan 6.7
  (JIT FMOV.D) is the other suspect for the black texture.
- Checks: PC test first (every format, mip and VQ case, random VRAM, old
  decode against the new one, bit-exact on the final RSX bytes). HW: Soul
  Calibur fight (`tex split`, busy, RPS, VPS), Crazy Taxi (no stale textures:
  FMV, menus, map).

### P2 Frame Drop A/B, then one pending slot  `[ ]`

Ours:
- Frame Drop defaults to WAIT 1 FRAME (`ps3/ps3_presets.cpp:49`,
  `g_drop_policy_preset = 1`).
- `rth_run()` (`ps3/ps3_rend.cpp:2609-2660`) waits up to 1/59.94 s for the
  job slot (:2618-2636), then drops the frame anyway (:2637-2645). The slot
  stays busy for the whole job: replay, draw with texture decode, end frame.
- At RPS 9-10 a job takes ~90-100 ms, so most STARTRENDERs wait 16.7 ms and
  then drop. That matches the 22-28% SH4 wait and half the frames dropped.
- A second wait: `ListInit()` (:2838-2843) calls `rth_wait_decode()`
  (:825-833) when the TA reuses the context that is being replayed.
- The render-done IRQ does not wait for the render: `StartRender()` arms it
  (max(50000, 15 x vertices) cycles, :2778-2793), `SPG.cpp:373-384` raises it.

Reicast:
- `QueueRender()` (`hw/pvr/ta_ctx.cpp:138-183`) drops the new frame at once
  when one is still queued (:167-172). It waits only with SynchronousRender
  and the SH4 above 120% speed (:159-165).
- `tactx_Pop()` (:283-300) takes the queued context out of the address map,
  so the next ListInit gets a fresh context.

Steps:
- Step 1 (no code): Soul Calibur fight, Frame Drop = DROP against the default.
  Read `wait=`, `drop=`, RPS, VPS. This is the TR5 A/B that never ran. With
  DROP, the SH4 no longer leaves the core to the render thread (SMT), so RPS
  can fall.
- Step 2 (medium, if step 1 shows the trade): one pending slot that the newest
  STARTRENDER overwrites. It needs Reicast's context detach, so a ListInit on
  the pending address takes a free slot (there are 4). Keep the "second
  STARTRENDER redraws" rule (nullDC-v1).

### P3 Async GD-ROM reads + CD-DA read-ahead  `[ ]`

Ours: every disc read runs on the SH4 thread.
- `FillReadBuffer()` (`dc/gdrom/gdromv3.cpp:208-220`) at SPI_CD_READ (:306)
  and when the cache empties mid-DMA (:1128); PIO reads at :296.
- CD-DA: `gdrom_get_cdda()` (:176-181) reads one sector per 588 samples:
  `plugs/nullAICA/sgc_if.cpp` `cdda_refill()` (:1387-1397, called at :1503 and
  :1534). The Sound Thread and SPE ARM7 modes also read it on the SH4 thread
  (`ps3/ps3_sndthr.cpp`, `ps3/ps3_arm_spe.cpp`).
- The CDI reader does one `fseek` + `fread` per sector
  (`plugs/ImgReader/cdi.cpp:54-55`), so a 32-sector fill is 64 calls.

Minicast (commit `d53f4e4`, `libswirl/hw/gdrom/gdromv3.cpp`, option
`gdrom.AsyncDMA`, default ON, `libswirl.cpp:275`):
- A worker thread fills the read buffer; a semaphore plus the atomic
  `fill_pending` (:94-164) hand it over.
- DMA reads kick the worker (:259); the update returns early while a fill is
  in flight; on an empty cache mid-DMA it kicks a refill and breaks out
  (:1266). Bytes are counted as they really move.
- `FillReadBufferSync()` (:138) at SPI_CD_READ (:561) and before save
  states (:1153, :1182). `disc_io_mtx` serializes the worker against PIO and
  CD-DA.
- No read-ahead: every 32-sector fill still holds the DMA.

PS3 form:
- Measure first: a `[gdr]` stats line (calls, sectors, us/s and max us, per
  caller: DMA fill, PIO, CD-DA). Build the async path only if it shows more
  than ~1% of wall or a max above ~2 ms.
- Then: `sysThreadCreate` worker (I/O-bound), `sysSemaphore`, the atomics
  pattern of `ps3_sndthr.cpp`. Wait for the fill at SPI_CD_READ and at the
  GD-ROM reset. One `sysLwMutex` around every `libGDR_ReadSector` caller: the
  readers share FILE positions and static buffers. Add a CD-DA read-ahead
  keyed by FAD (Minicast has none).
- Risk: medium. DMA completion time depends on the host.

### P4 FPSCR.DN -> PPC NI bit  `[ ]`

- Ours: `SetFloatStatusReg()` (`dc/sh4/sh4_registers.cpp:89-96`) sets only the
  rounding mode (`mtfsfi 7,RM`).
- Reicast (`hw/sh4/sh4_core_regs.cpp:91-108`): DN=1 sets host flush-to-zero
  (x86 MXCSR bit 15, ARM FZ bit 24).
- New fact for RESEARCH.MD 3e: SH4 DN=1 means denormals are zero, and the
  reset FPSCR (0x00040001) has DN=1. The PPC form is
  `mtfsfi 7,(DN?4:0)|RM` (field 7 = XE, NI, RN). The render thread keeps its
  own FPSCR (`ps3/ps3_rend.cpp:2500`, :2667-2672), so it is not affected.
- Value: unverified. A gain only if the PPE stalls on denormals; check what NI
  does on the PPE before use. One line, behind an option.

### P5 Small speed items  `[ ]`

- Deadline scheduler (Reicast `hw/sh4/SuperH4_impl.cpp:15-28`,
  `sh4_sched.cpp`): one decrement per timeslice. Ours runs the whole
  peripheral update every slice (`dc/sh4/sh4_interpreter.cpp:326-343`: TMU,
  PVR counters, AICA DMA, the INTC check). Estimate: 1% or less, medium
  refactor. Not before 5.18.
- `raw_note_peak()` (`ps3/ps3_rend.cpp:159-163`) stores to a global on every
  32-byte TA record (:909, :1014, :1032). Update the peak at reset and
  STARTRENDER instead. Gain unverified (estimate 0.1-0.5% of the SH4 thread).
- ps3gl bind scan: see 5.10 in the plan (not chosen). It weighs more in the
  render-bound Soul Calibur than in Crazy Taxi.

---

## 2. Bugs

### P6 The JIT loop never stops (XMB exit)  `[ ]`

- Ours: `ngen_mainloop()` (`ps3/ps3_driver.cpp:4368-4387`) never reads the run
  flag, so `recSh4_Run` never returns. Every XMB exit takes the forced path
  ("SH4 did not stop in 3 s", `ps3/ps3_main.cpp:178-181`). So `EMU_TERM`
  (`dc/dc.cpp:116-128`) never runs, and `mem_Term()` never writes
  `dc_flash_wb.bin` (`dc/mem/sh4_mem.cpp:168-178`). This is the plan's open
  item in section 0 ("Other open items").
- Reicast: `UpdateSystem()` returns `interrupt_pend | !bCpuRun`
  (`hw/sh4/SuperH4_impl.cpp:25-27`). The backends test the run flag only on
  that cold path.
- Fix: `UpdateSystem_no_event()` returns nonzero when the CPU is stopped.
  After `UpdateSystem_handle_event`, test the flag and jump to an emitted
  epilogue (flush the pinned GPRs and FPRs, restore the saved registers and
  LR, `blr`). No cost on the hot path. About 25 lines.
- HW check: XMB exit without the 3 s fallback; `dc_flash_wb.bin` is written;
  the in-game menu (SELECT) still works.

### P7 SB_LMMODE0/1 ignored  `[ ]`

- Ours: `SB_LMMODE0/1` are registered (`dc/mem/sb.cpp:56-59`, :637-648) but
  never read.
  - Store-queue writes to 0x11/0x13 always take the 32-bit interleave
    (`dc/pvr/pvr_if.cpp` `TAWriteSQ()` :346-363). Its comment says the old
    linear copy scrambled Rez.
  - ch2 DMA to 0x11 is always 64-bit linear (`dc/sh4/dmac.cpp:111-138`); 0x13
    is a stub that moves no data (:139-145).
- Reicast: `hw/pvr/pvr_mem.cpp:153-170` (SQ) and `hw/pvr/pvr_sb_regs.cpp:88-129`
  (DMA, also `SB_C2DSTAT += len`) take the 64-bit path when LMMODE0 is 0.
  Flycast uses LMMODE1 for 0x13 (`core/hw/pvr/pvr_mem.cpp:268-269`,
  `core/hw/sh4/modules/dmac.cpp:66`; commit `0d0fd212f`, Giana's Return).
- Our texture decoder reads VRAM in the 64-bit layout (`ps3/ps3_tex.cpp:79-82`),
  so a texture sent through the wrong path lands scrambled.
- Steps: log LMMODE0/1 once per change in a few games first (Rez must stay
  right). Then the Flycast rule in both paths: 64-bit = a 32-byte copy into
  VRAM, 32-bit = `pvr_write_area1_32`. About 40 lines, no speed cost.

### P8 Intensity colour clamp order  `[ ]`

- Reicast (`hw/pvr/ta_vtx.cpp:1001-1019`, comment "Fixes red dog"): the face
  colour is stored as saturated bytes at the header (`poly_float_color`,
  :855 and :869), the intensity is saturated to a byte, then
  `col = face * int / 256`.
- Ours (`ps3/ps3_rend.cpp` `intensity_argb()` :335-338, face colour :446-461):
  float face x intensity, clamp after.
- Results differ when the face colour or the intensity is outside 0..1.
  Bonus: 1 float-to-int conversion per vertex instead of 4.
- The SPU decoder must change the same way (`ps3/spu/spe_dec.c`
  `intensity_argb()` :141-143), or the S2 compare fails.

### P9 Hangs on rare input  `[ ]`

None of these messages appears in the logs of past sessions (461 scratchpad
text files checked). Each fix is 1-4 lines.
- TA replay: the splitter calls `die()` on Object_List_Set and on the reserved
  types 3 and 6 (`plugs/drkPvr/ta.h:356-362`, :449-455, and the second copy
  :1123, :1232). `die()` ends in `for(;;)` (`types.h:186-188`). The store FSM
  skips these records (TArender.MD TR4), but they are still copied into the
  raw buffer, and the replay (`ps3/ps3_rend.cpp:863`) reaches `die()`. The
  render thread then loops forever, and the SH4 hangs in `ListInit()`.
  Reicast skips the record (`hw/pvr/ta_vtx.cpp:434-441`, used by Naomi
  Virtual On).
- GD-ROM DMA longer than the data read: `verify(read_params.remaining_sectors>0)`
  (`dc/gdrom/gdromv3.cpp:1126`) and `verify(!(SB_GDST&1))` (:1177) hang.
  Reicast clamps the length (`hw/gdrom/gdromv3.cpp:1117-1141`).
- Maple vblank (DDT) DMA: `maple_DoDma()` runs with SB_MDST still 0
  (`dc/maple/maple_if.cpp:47`), so its `verify(SB_MDST &1)` (:95) hangs.
  Reicast sets `SB_MDST = 1` first (`hw/maple/maple_if.cpp:268`).
- AICA G2 DMA with SB_ADDIR=1 (wave RAM to system RAM): ours prints a message
  and returns (`dc/aica/aica_if.cpp:280-286`): no copy, no IRQ, the guest
  waits forever. Reicast swaps source and destination
  (`hw/aica/aica_mmio.cpp:459-468`). Both RAMs hold native u32 words, so a
  word copy is byte-order safe.
- 2340-byte sector reads (head + subheader + data, `dc/gdrom/gdromv3.cpp:555-562`)
  return zeros: `plugs/ImgReader/common.cpp` converts only to 2048 and 2352
  (:81-125, the default case prints per sector), and `ImgReader.cpp:113`
  zero-fills. Reicast `ConvertSector` (`imgread/imgread.h:246-281`) gives 2340
  from 2352 (`&in[12]`).

### P10 PVR register and list rules  `[ ]`

Games affected: unknown for each.
- TA_LIST_INIT sets TA_ISP_CURRENT = TA_ISP_BASE (Reicast
  `hw/pvr/pvr_sb_regs.cpp:315-319`, needed by KOS for its background
  polygon). Ours (`plugs/drkPvr/regs.cpp:51-57`) never writes it.
- SOFTRESET bit 1 cancels a pending render-done (Minicast `9970b0e`,
  `dcbeb5d`). Ours ignores bit 1 (`regs.cpp:59-71`), and `SPG.cpp:373-384`
  still raises the IRQ.
- Region array: Reicast skips an empty first entry and decides autosort per
  pass (`hw/pvr/ta_vtx.cpp:1755-1786`). Ours reads entry 0 only
  (`ps3/ps3_rend.cpp` `using_autosort()` :1089-1094). About 10 lines.
- LIST_CONT passes: Reicast records counts per pass (`ta_vtx.cpp:1539-1563`)
  and draws OP, PT, MV, TR for each pass (`rend/gles/glesdraw.cpp:1120-1154`).
  Ours splits only the replay at the marks (`decode_pending()` :868-892), then
  draws all OP, all PT, MV, all TR (:1901-1924). Medium.
- Screen clip and scaler: Reicast uses FB_X_CLIP/FB_Y_CLIP as a scissor and
  SCALER_CTL.vscalefactor / pixel double (`rend/gles/glesrend.cpp:892-913`,
  :1145-1197). Ours uses the clip only in `rtt_render()` (:1979-1983).
  Small.
- User tile clip: ours is empty (`SetTileClip()` / `TileClipMode()`,
  :585-586). Reicast: `ta_vtx.cpp:747-761`, `glesdraw.cpp:83-160`, shader
  `glesrend.cpp:229-240`. "Inside" can be a scissor per run; "outside" needs
  stencil or Cg. Medium (DcParam and the SPE layout change).
- Empty frame: Reicast neither draws nor presents a frame with no polygon of
  3+ vertices (`ta_vtx.cpp:1564-1578`, `Renderer_if.cpp:209-222`). Ours always
  draws the background and presents. Heuristic: a screen that is only a
  background would freeze. Low.

### P11 TMU TCNT inside the timeslice  `[ ]`

- Flycast `b31f92f8e` ("sh4: increase timers resolution", issue #2093: lack
  of randomness in Soul Calibur) reads TCNT with the cycles spent inside the
  current timeslice.
- Ours updates TCNT once per slice (`dc/sh4/tmu.cpp` `UpdateTMU_chan()`
  :62-...). The JIT keeps the cycle count in a register (`ppc_cycles`), so a
  handler cannot see the time inside the slice: the JIT would need to store it
  before a cold memory call. Medium, minor gain.

### P12 RTC under DST  `[ ]`

- `GetRTC_now()` adds 24 * 3600 when `tm_isdst > 0`
  (`dc/aica/aica_if.cpp:66-67`); it should be 3600. Reicast uses the
  localtime - gmtime offset (`hw/aica/aica_mmio.cpp:46-59`). On PS3 `isdst` is
  probably 0, so it may never fire (unverified). One line.

---

## 3. Features and references

### P13 Puru Puru (rumble) pack  `[ ]`

- Reicast `hw/maple/maple_devs.cpp:1023-1155` decodes SetCondition into power
  and duration. Minicast `a656d98` adds only the host side.
- Ours has no vibration device (slot 2 holds a second VMU). PS3: the DS3
  motors (`ioPadSetActDirect`, API not checked in this tree). About 120
  lines. No speed or fault value.

### P14 Fog and offset colour reference (plan 3a, 3b)  `[ ]`

Reicast GLES shader, the reference for a PVR Cg shader:
- shader text `rend/gles/glesrend.cpp:138-323`: `fog_mode2()` :208-216,
  offset add :289-292, fog modes :297-307, tile clip :229-240;
- uniforms :997-1028; fog table texture `UpdateFogTexture()` :702-728;
- vertex offset colour `hw/pvr/ta_vtx.cpp:1014-1019`.

### P15 Freerunning (Minicast)  `[ ]`

- Commits `2874978`, `02f4ab3`, `b40f51b` (`hw/sh4/SuperH4_impl.cpp`):
  `UpdateSystem` advances the scheduler by wall-clock time (the A9 global
  timer, scaled for overclock), in 448-cycle chunks, at most 1/60 s per call.
  Time stops while the audio FIFO is nearly full.
- It ports (PPE timebase via `mftb`, the Q24 scale of `02f4ab3`), but it is
  not a speedup: when the SH4 thread is short, the guest gets a lower
  effective clock and drops its own frames. In practice it is a dynamic form
  of the SH4 Clock option (5.5, 5.19).
- Costs: when ahead, the SH4 spins in idle loops and takes SMT slots from the
  render thread (needs a sleep); VPS stops measuring speed; runs are no longer
  deterministic. Matt's call, as a pacing option.

---

## 4. Checked, nothing to take

SH4 core and system:
- The C boundary is small: `UpdateSystem_no_event` ~63-100 K calls/s,
  `getcode` ~75 K/s, area-0 writes from JIT code ~49 K/s, together ~1-2% of
  the SH4 thread (estimate from the 5.0 census).
- INTC, `Do_Interrupt` / `Do_Exception`, the `sleep` loop, SB_FFST, area 0 and
  area 7 dispatch, block cycle accounting: same design as Reicast.
- Idle skip: ours (x30) is stronger than Reicast's (x3; its hash list covers
  only the BIOS, Soul Calibur, House of the Dead 2 and Ikaruga loops).
- JIT FPU semantics: `fmac` already uses the fused `fmadds`; `ftrc` saturates
  through `fctiwz`. Flycast's double-precision `fipr` is accuracy only.
- Minicast Ikaruga fix (`c7c54e5`): its bug was one global SQ target; we pick
  QACR0/1 per flush (`dc/sh4/sh4_sqw.h:25`).
- Minicast thread pinning / WFE (`18a2c18`, `c4e2c10`): the PPE has one core
  with 2 SMT threads, so there is no affinity choice. SMT priority nops
  (`or 1,1,1`) are the nearest idea (unverified on GameOS).
- Minicast clrs/sets decode fix (`fc8ceff`): ours is already right
  (`dc/sh4/sh4_opcode_list.cpp:141`, :144).

PVR and renderer:
- Minicast multithreaded TA ring and decoupled contexts (`83c2fb0`,
  `c68bdf2`, `cbb6e76`, `0998288`): our raw store + replay + SPE decode covers
  it; their drain at end of list would add waits.
- lxdream tacore fixes (`c74485c`, `82afa24`): tile-list internals that our
  decoder does not have.
- Reicast TA state machine: same as our TR4 table.
- Render-done delay (Reicast 60 cycles per vertex + 1.5 M; Minicast
  200 M / FPSTarget): Soul Calibur renders at most once per vblank, so a longer
  delay saves nothing.
- Sort DMA: ours follows the fixed rule (link value 2 ends the chain).
- ShadInstr, PT alpha, wrap and filter, modifier volumes, vertex buffers,
  strip merge: equivalent, or already taken from Flycast. Per-triangle sort
  stays rejected (Matt).
- Minicast YUV texel fix (`47c0ec2`): refsw only; our pairing is already right
  (`yuv_pair()` takes horizontal neighbours in twiddled, planar and VQ).
- Reicast YUV "avoid copy": FMV only, ~384 bytes per macroblock.

GD-ROM, maple, sound:
- GD-ROM DMA pacing (Reicast ~10 KB per 1 M cycles): ours is faster; no game
  is known to need pacing.
- Sense data after a disc change: no disc swap on PS3.
- GET_SCD, REQ_STAT, REQ_SES: ours is tuned for the BIOS CD player and the
  ChuChu pause fix.
- Maple DMA timing: same formula. VMU: same protocol, fflush on every write.
  Lightgun: moot (the PS3 controller type is fixed).
- G2 DMA bulk copy: under 0.1% of wall (estimate).
- ARM7 / AICA: no new lead for the ChuChu echo (F1) or the Looney Tunes
  silence. Reicast's differences are already in section 6 (6.2 interleave,
  6.3 256 ticks per sample, 6.8 reset state).
- Audio pacing: Reicast (4096-frame ring, 1 ms sleeps) and Minicast `b40f51b`
  (wait for the FIFO) add nothing over our vblank pacer, 8192-frame ring and
  `ring_wait_space`.

---

## 5. Suggested order

1. P2 step 1 (no code) and P1 step 0 (counter), in one Soul Calibur run.
2. P1 steps 1-2 (PC test first), then step 3.
3. P6 (cheap, closes an open item).
4. P9 (one build with the five fixes).
5. The rest when Matt picks them.
