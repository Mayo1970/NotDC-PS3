# TARender - deferred TA decode and threaded render

Port of the reicast/libswirl two-stage PVR pipeline into the PS3 renderer.

Reference: `reicast-emulator/libswirl/hw/pvr/` — `ta.cpp` (store stage),
`ta_ctx.cpp` (context handoff), `ta_vtx.cpp` `ta_parse_vdrc()` (parse stage),
`Renderer_if.cpp` (frame dispatch).

Rule: every phase ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

---

## 1. What the port changes

### Today

`plugs/drkPvr/ta.cpp` `libPvr_TaSQ()` / `libPvr_TaDMA()` call `TaCmd`, which is
`FifoSplitter<Ps3VertexDecoder>::ta_main`. Every TA record is decoded into a
`TaContext` the moment the guest writes it. `StartRender()` then runs
`draw_frame()` inline. `VBlank()` runs `present_framebuffer()` inline.

All of it sits on the SH4 thread:

| Work | Thread today |
|---|---|
| TA record decode | SH4 |
| RSX draw submission (up to 78.5 ms in dense scenes, G8) | SH4 |
| Framebuffer present | SH4 |

### After

| Stage | Thread | Work |
|---|---|---|
| Store | SH4 | copy 32 bytes, track list state, schedule list-end IRQ |
| Decode | render | replay stored bytes through the existing splitter |
| Draw | render | `draw_frame()` / `present_framebuffer()` |

The store stage is libswirl's "lie to the game": the guest gets its
`holly_OPAQUE` / `holly_TRANS` / `holly_PUNCHTHRU` interrupts on time, and no
vertex has been touched.

### Why this is the right target

G8 measured the RSX at 78.5 ms/frame for ~1306 draw calls in dense Crazy Taxi
traffic. That time blocks the SH4 today. Moving it off the SH4 thread is worth
more than the TA decode saving on its own.

---

## 2. What makes the PS3 port easier than libswirl

1. The decoded output is already a self-contained struct. `TaContext` in
   `ps3/ps3_rend.cpp` holds vtx / strip / param / modvol arrays with no
   renderer globals leaking in. The Wii `gxRend.cpp` decoder was the opposite
   (30+ cursor globals), and it is excluded from the build.
2. There are already 4 TA contexts keyed by address (`set_ta_context`,
   `find_context`), which is nullDC-v1's `SetCurrentTARC` / `SetCurrentPVRRC`.
3. Textures are not touched at decode time. `add_param()` copies only
   pcw/isp/tsp/tcw; `ps3_tex.cpp` runs at draw time. So deferring the decode
   moves no texture work and changes no texture timing.

## 3. What makes it harder

1. **RSX has one command buffer.** `draw_frame()`, `rtt_render()` and
   `present_framebuffer()` all write it. If the draw moves to a second thread,
   every RSX caller must move with it or serialise behind the same lock.
2. **Live register reads.** `draw_frame()` and friends read `PARAM_BASE`,
   `ISP_BACKGND_T`, `FPU_PARAM_CFG`, `ISP_FEED_CFG`, `FPU_SHAD_SCALE`,
   `REGION_BASE`, `FB_W_SOF1`, `FB_W_CTRL`, `FB_W_LINESTRIDE`, `FB_X_CLIP`,
   `FB_Y_CLIP`, `FB_R_CTRL`, `FB_R_SOF1`; `ps3_tex.cpp` reads `PAL_RAM_CTRL`
   and `TEXT_CONTROL`. A deferred draw must read a snapshot, not the live regs.
3. **VRAM and palette race.** Textures are read at draw time from live VRAM.
   A late draw can sample a texture the SH4 already overwrote. libswirl has the
   same hole and accepts it.
4. **RTT must stay synchronous.** The guest reads the rendered texture straight
   back out of VRAM. libswirl blocks in `rend_end_render()` for exactly this.
5. **`TaCmd` is one global.** `plugs/drkPvr/ta.h` keeps the split-record resume
   pointer in `TASplitter::TaCmd`. Two splitter instances (store and decode)
   would corrupt each other through it.
6. **FPSCR.** The host FPSCR tracks the SH4 rounding mode. Today
   `StartRender()` saves it, forces round-to-nearest, and restores it. A render
   thread sets its own once and never touches the SH4's.

---

## 4. Phases

### TR1 - Store and replay, still single-threaded  `[x]`

No speed goal. This phase only proves that record-and-replay is identical to
decoding inline.

1. Give `TaCmd` per-instantiation lifetime: add `static TaListFP* TaCmd;` as a
   member of `FifoSplitter` in `plugs/drkPvr/ta.h`, plus its out-of-class
   definition. Member lookup wins inside the class, so no call site inside the
   template changes. `FifoSplitterEx` is never instantiated; leave it on the
   namespace global.
2. Add the raw stream to `TaContext`: `u8* raw`, `raw_used`, `raw_decoded`,
   `cont_marks[]`, `n_cont`, `raw_overflow`.
3. Add a second splitter `FifoSplitter<TaStoreDecoder> TaStore` whose decoder
   methods are all empty. It keeps list state and fires `spg_SchedListEndIrq`
   through the unchanged `ta_main`, and decodes nothing.
4. Rewrite the entry points. `libPvr_TaSQ` / `libPvr_TaDMA` in
   `plugs/drkPvr/ta.cpp` become thin forwarders to `ps3ta_store_sq()` /
   `ps3ta_store_dma()` in `ps3_rend.cpp`, keeping the `PERF_TICKS` brackets.
   Each store call copies the 32-byte records into `s_ta->raw`, then runs
   `TaStore`'s `TaCmd` over the copy.
5. Move context selection out of the decoder. `Ps3VertexDecoder::ListInit()`
   drops `set_ta_context(TA_ISP_BASE)` and keeps `reset_lists(s_ta)`;
   `Ps3VertexDecoder::ListCont()` becomes empty. The exported `ListInit()` /
   `ListCont()` do the `set_ta_context(TA_ISP_BASE)`, because at replay time
   `TA_ISP_BASE` may already point somewhere else.
   - `ListInit()`: select context, `reset_lists`, clear `raw_used`,
     `raw_decoded`, `n_cont`, then `TaStore.ListInit()`.
   - `ListCont()`: select context, append `raw_used` to `cont_marks`, then
     `TaStore.ListCont()`.
   - `SoftReset()`: `TaStore.SoftReset()` only. Lists and raw bytes are kept,
     as nullDC-v1 does.
6. Add `decode_pending(TaContext* c)`: bind `s_ta = c`, and if
   `raw_decoded == 0` run `TileAccel.ListInit()` first. Then walk
   `[raw_decoded, raw_used)`, splitting at each `cont_marks` entry and calling
   `TileAccel.ListCont()` on each boundary. Finish with
   `raw_decoded = raw_used`.
7. `StartRender()` calls `decode_pending(s_rc)` right after `find_context()`,
   before it reads `s_rc->nvtx`.

Semantics this preserves, on purpose:
- A second `STARTRENDER` with no new TA data decodes nothing and redraws the
  same list (nullDC-v1 keeps the tile arrays until `TA_LIST_INIT`).
- Data submitted after a render and before `TA_LIST_INIT` appends to the same
  list, exactly as the inline decoder does.
- `render_end_pending_cycles` is still set from `nvtx`, so it must be computed
  after the decode.

Buffer sizing: `TA_RAW_SIZE` 4 MB per context, 4 contexts, 32-byte aligned,
allocated in `InitRenderer()`. 16 MB against ~194 MB free (G1). On overflow,
stop appending, keep firing interrupts, and count it in the `[rend]` stats line.

**HW test:** Crazy Taxi and one more game boot and play. Geometry, sprites,
modifier volumes and RTT look the same as the current build. `[rend]` stats
show the same strip and vertex counts. No new overflow.

Result 2026-09-21: passed on Crazy Taxi, Soul Calibur and Sonic Adventure,
A/B'd against a pre-TR1 baseline build. Vertex and strip counts match window
for window (30338 vs 30407 vtx, 6741 vs 6751 op strips). Raw peak 993 KB of
4096 KB, `ta_raw_of=0`. Cost is 1-10 points of emulation speed, as expected
from the extra memcpy and second splitter pass.
RTT was NOT covered: `rtt=` read 0/0 in all 149 samples across the three games.

### TR2 - Register and state snapshot  `[ ]`

1. Add `struct RenderRegs` holding every PVR register the draw path reads
   (list in section 3.2), plus `s_fb_scale_x/y` and the `using_autosort()`
   result.
2. Fill it in `StartRender()` on the SH4 thread. Resolve `background_plane()`
   there too: it reads VRAM through `PARAM_BASE`, so it must be snapshotted in
   order, the same way libswirl calls `FillBGP()` before `QueueRender()`.
3. Change `draw_frame()`, `rtt_render()` and the texture path to read the
   snapshot, not the live registers.

Still single-threaded. This phase is a pure refactor and must not change any
pixel.

**HW test:** same scenes as TR1, same output.

### TR3 - Render thread  `[ ]`

1. Create one PPU thread in `InitRenderer()` (`sysThreadCreate`, as
   `ps3/ps3_audio.cpp` does). It owns the RSX from then on: `ps3_video_*`,
   `draw_frame`, `present_framebuffer`, `rtt_render`, `ps3_tex.cpp`.
2. Hand off one frame at a time, as libswirl does: a single-slot queue, a
   mutex, and two events. No second pending frame; drop instead of stalling
   and count the drop.
3. `StartRender()` on the SH4 thread now does: pick `s_rc`, snapshot the
   registers, set `render_end_pending_cycles`, publish, return. The decode
   moves to the render thread, in front of the draw.
4. Double-buffer the decoded context. The SH4 thread may keep storing into the
   next stream while the render thread decodes the previous one, so the raw
   buffer being replayed must not be the one being appended to. Pick the
   context slot with the same "not the one in use" rule `set_ta_context()`
   already applies to `s_rc`.
5. RTT blocks. `EndRender()` waits for the render thread, mirroring
   `rend_end_render()` in libswirl.
6. `VBlank()` must not touch the RSX directly any more. It posts the present to
   the render thread.
7. The render thread sets round-to-nearest once at start. Remove the per-call
   `mffs` / `mtfsf` pairs from the moved functions and keep the SH4 FPSCR
   untouched.

Open risk to watch: G8 left an unattributed freeze whose open suspect is a
command-buffer-full stall inside `rsxFlushBuffer`. Moving RSX submission to its
own thread stops that from freezing the SH4, which may hide the bug rather than
fix it. Log a warning if the render thread blocks longer than one frame.

**HW test:** 3+ minute soak in dense traffic. FPS, VPS and RPS against the TR2
build. No freeze, no torn or stale frame, RTT still correct.

### TR4 - Fast store path  `[ ]`

Only now replace the store splitter with libswirl's table state machine
(`ta_fsm[2049]`, index `(state << 8) | (ParaType << 5) | (obj_ctrl >> 2)`, high
nibble = "must handle"). Build the table from drkPvr's `ta_type_lut`: header
size is `uid >> 30` (`SZ32` / `SZ64`), and the vertex is a two-part 64-byte
type when the data id is 5, 6, 11, 12, 13 or 14.

This is the only phase with a measurable before/after on the SH4 thread, so it
must not be merged into TR1.

**HW test:** same output as TR3, lower `ta%` in the stats line.

### TR5 - Tuning  `[ ]`

1. Frame drop policy and its interaction with the existing frameskip presets.
2. Raw buffer size against real games. Measured peak is 993 KB (Sonic
   Adventure, 30k vtx/frame), so 2 MB is the smallest safe shrink, not 1 MB.
3. Decide whether the VRAM race (section 3.3) needs a guard in any real game.

---

## 5. Order of work

TR1 is the only phase that can change rendering by accident, so it ships alone
and gets its own hardware test. TR2 is a refactor with no behaviour change.
TR3 is where the speed arrives. TR4 is an optimisation on top. Do not reorder.
