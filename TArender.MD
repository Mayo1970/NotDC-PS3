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

### TR2 - Register and state snapshot  `[x]`

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

Result 2026-09-21: passed on Crazy Taxi and Sonic Adventure, A/B'd in one log
against pre-TR2 runs of the same discs. The nullDC menu windows are identical
(vtx=397/399/258, op=78/79/51). Sonic's dense reference window reproduces TR1
within 0.5% on vertices and 1% on strips (30477/30335 vtx vs 30338/30471;
op 6760/6699 vs 6741/6735; peak 989K/993K vs 987K/993K). `built` equals drawn
in every window, `ta_raw_of=0`, `sort=auto`, zero `[ps3rend]` messages.
Speed is unchanged: 65.5% in both runs at the 30K window; run averages 79.4%
vs 78.7%. Picture confirmed by eye on Crazy Taxi.
Soul Calibur was not re-run. RTT is still uncovered: `rtt=` read 0/0 again, so
the snapshotted `FB_X_CLIP` / `FB_Y_CLIP` / `FB_W_CTRL` / `FB_W_LINESTRIDE`
path is unproven on hardware.

Note for TR3: the build banner cannot tell these phases apart. `__DATE__` /
`__TIME__` live in `ps3_main.cpp`, which an incremental build does not
recompile, so every boot in the log printed the same 08:46:19 stamp.

### TR3 - Render thread  `[x]`

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

#### Closed 2026-09-21 on hardware run 1

Toggle: Options -> VIDEO / DEBUG -> **Render Thread**, `nullDC.cfg [ps3]`
`RenderThread`, **default ON**. OFF runs decode and draw inline on the SH4
thread, which is the TR2 path, so one build A/Bs both. Installing the PKG wipes
`/dev_hdd0/data/NullDC/`, so the toggle comes back ON after every install.

What the code does, where it differs from the list above:

- Thread `nulldc_render`, priority 1000 (SH4 is 1001, audio feeder 100),
  128 KB stack, one `sys_mutex_t` and two `sys_cond_t`. Started at the end of
  `InitRenderer()`, joined at the start of `TermRenderer()`, and
  `ResetRenderer()` waits for idle first.
- Three job types share the one slot: `JOB_RENDER`, `JOB_RTT`, `JOB_PRESENT`.
  `JOB_RTT` blocks the SH4 until the job finishes; the other two are dropped
  when a job is already in flight, and the drop is counted as `drop=` in the
  `[rend]` line.
- **The context is not double-buffered.** The SH4 latches `raw_used` and
  `n_cont` at STARTRENDER and the job replays only up to that point, so the
  guest can keep appending to the same stream while the replay runs. The only
  destructive operation is `reset_raw()` in the exported `ListInit()`, which
  now waits for the job's decode stage (`rth_wait_decode`). The decode stage
  signals completion before the draw starts, so that wait is short.
  `set_ta_context()` also refuses to recycle the slot the render thread holds.
- `render_end_pending_cycles` is still set on the SH4, but the replay has not
  run yet, so it uses that context's **previous** `nvtx`. The value is clamped
  to at least 50000 cycles anyway.
- `present_framebuffer()` is split. The VRAM-to-pixels loop
  (`present_fb_convert`) stays on the SH4 thread so the pixels match the
  guest's writes at that vblank, and the magic word is stamped there too; only
  the textured quad (`present_fb_draw`) is posted. A vblank that finds the
  render thread busy presents nothing and retries at the next one.
- `g_rr` is now the draw side's copy. The SH4 fills `s_snap`, the post copies
  it into `s_job_rr`, and the render thread copies that into `g_rr` when it
  takes the job.
- The TA replay target is `s_dec`, split out of `s_ta` so the store stage and
  the replay never share a cursor.
- Diagnostics: the `[hb]` line gained `rend=<phase> seq=<n>` from a second
  phase pair (`REND_PHASE`, phases `idle`, `tadecode`, `present`), so a stuck
  render thread names its own call. The `[rend]` line gained `drop=` and
  `job_max=<ms>`, and a job over 250 ms also prints
  `[ps3rend] render job stalled for N ms`.

**HW run 1, 2026-09-21 (Render Thread ON only):** Crazy Taxi PAL, dynarec +
JitFastmem, 177 s, no freeze and the picture was correct by eye. `drop=1` in the
whole run and `job_max` 9-13 ms in every window but one (43 ms), so the render
thread never fell behind and the 250 ms stall warning never fired. In-game
(150 samples): SPEED avg 75.2% (min 43.7, median 66.7), VPS avg 45.1,
RPS avg 36.8 over the windows that render; 36% of samples sit at the 59.95 cap.
`[vid] presented=5077 skipped=25`.

The run is NOT a valid A/B: the Render Thread OFF pass was not run, and the
scene is far heavier than every earlier measurement - 1146-2538 OP strips and
11-14K vertices per render against G7's 240 strips / 2092 vtx and FASTMEM's 508
strips. `built` equals drawn in every window and `vt:` matches `vtx=`, so the
counts are internally consistent, not a double replay; early gameplay windows
still read `vtx=2075 op=238`, which is the old figure. So the load simply grows
with the scene. Holding 48-78% at 2179 strips/render, where the pre-TR3 build
was pinned at RPS 29.5 with 508, says the draw-call wall is no longer on the
SH4's critical path, but the size of the win is unquantified until the OFF pass
runs on the same spot.

Two other firsts in this run: `mv_tris op=190-271` per render, so the G6.4
modifier volumes finally execute on hardware; and `rtt=0/0` again, so RTT is
still unproven ([[rtt-never-observed]]). The XMB exit again took the ExitFix
fallback ("SH4 did not stop in 3 s"), which predates TR3.

**TR3 CLOSED 2026-09-21 at Matt's request**, on the ON-only run above. It is
correct and stable on hardware, and the Render Thread toggle stays in the build
(default ON) so the OFF path remains available. What was never done, and is the
first thing to redo if TR3 is ever questioned:

- the Render Thread OFF pass on the same spot, so the speed win is still
  unquantified;
- a second game (Sonic Adventure) and a 2D screen for the `JOB_PRESENT` path;
- anything that exercises RTT ([[rtt-never-observed]]).

### TR4 - Fast store path  `[x]`

Only now replace the store splitter with libswirl's table state machine
(`ta_fsm[2049]`, index `(state << 8) | (ParaType << 5) | (obj_ctrl >> 2)`, high
nibble = "must handle"). Build the table from drkPvr's `ta_type_lut`: header
size is `uid >> 30` (`SZ32` / `SZ64`), and the vertex is a two-part 64-byte
type when the data id is 5, 6, 11, 12, 13 or 14.

This is the only phase with a measurable before/after on the SH4 thread, so it
must not be merged into TR1.

**HW test:** same output as TR3, lower `ta%` in the stats line.

#### Built 2026-09-21, not yet hardware tested

Toggle: Options -> VIDEO / DEBUG -> **TA Fast Store**, `nullDC.cfg [ps3]`
`TaFastStore`, **default ON**. OFF keeps the TR3 splitter, so one build A/Bs
both. The `[rend]` line now names the path in use: `store=fsm` or `store=split`.

The table lives in `ps3/ps3_rend.cpp`, next to the splitter it replaces.

- **7 states, not 8.** A state says what the next 32-byte word means:
  no list open, list open with 32-byte vertices, list open with 64-byte
  vertices, a modifier volume list, and one "consume this word" state for each
  of the last three. The whole record walk is one word at a time, so a 64-byte
  record split across two SQ or DMA blocks resumes with no resume pointer.
- **The key masks `obj_ctrl` to 5 bits**, `(obj_ctrl >> 2) & 31`. UV_16bit,
  Gouraud and Shadow never change a record's size, and without the mask
  ParaType 5 (sprite, 64-byte vertices) would share a slot with a 16-bit-UV
  polygon header, whose vertices can be 32 bytes. Table size is 8 * 256 bytes.
- **The slow path owns three cases only:** END-OF-LIST, and the polygon or
  sprite header that opens a list. Those need the live list state, which the
  key cannot carry. Everything else - every vertex, every header inside an open
  list, every tile clip - is one table load. In the TR3 measurements headers are
  about one record in six, so the table answers roughly 85% of the stream.
- **Object_List_Set and the two reserved parameter types log once and skip.**
  The splitter calls `die()` there, which on PS3 is a msgbox that does not
  exist followed by an endless loop: a freeze with no log. Neither path is
  reachable from a valid stream.
- **A soft reset now also drops a half-consumed 64-byte record.** The splitter
  kept its `TaCmd` resume pointer across `SoftReset()` and only cleared the
  list. One state covers both here, and a mid-record resume across a TA soft
  reset is meaningless anyway.
- **Known corner, not modelled:** vertices with no END-OF-LIST, then
  `TA_LIST_INIT`, then a vertex parameter before any header. The splitter still
  holds the previous strip's vertex handler and would consume 64 bytes; the
  table is in the "no list" state and consumes 32. `TA_LIST_INIT` is always
  followed by a header in a real stream.

**Host cross-check, 8.4 M records:** `misc/tr4_check/run.sh` extracts the live
store-stage code out of `ps3_rend.cpp`, links it against the real `ta.h`
splitter, and runs both walkers over 200000 generated streams - polygons of
every valid `obj_ctrl`, sprites, modifier volumes, tile clips, junk ListType on
every header after the first, stray vertices with no list open - cut into
random 1-to-4 record blocks. The second word of every 64-byte record carries a
planted END-OF-LIST, so a walker that loses record alignment fires an interrupt
no one else fires. Both walkers reproduced the intended interrupt sequence
exactly, under AddressSanitizer. The generator skips Volume 1 with Col_Type 1:
that encoding is invalid, and the splitter indexes `ta_poly_data_lut[0xFF]` out
of range on it.

#### The stats line could not measure this at first

`ta%` read 0.0% on the first PS3 run, with `ta_kps` at 36 k/s. `PERF_TICKS()`
in `plugs/drkPvr/Renderer_if.h` was guarded `#if HOST_OS == OS_WII` and fell
back to a constant 0 everywhere else, so **every** percentage in the stats
line - `ta%`, `rnd%`, `arm%`, `aica%`, `vox%`, `snd%` - has read 0.0% on PS3
since the Wii purge. `ps3/include/ogc/lwp_watchdog.h` already provides the
same `gettime()` / `ticks_to_microsecs()` on the PPE time base, so the guard
now also accepts `OS_PS3`. Timebase is 79.8 MHz, 12.5 ns per tick.

The same `#if HOST_OS == OS_WII` guard is repeated in `plugs/nullAICA/sgc_if.cpp`
and `plugs/vbaARM/arm_aica.cpp`, which own `aica%`, `vox%` and `arm%`. Those
were patched too, so the whole line is live now. Cost is two `mftb` reads per
audio sample, which the headers already budgeted at under 0.1%. `rnd%` stays
at 0.0% on purpose: TR3 moved the draw out of `DoRender()`, which is what that
bracket measures.

#### First measured run, 2026-09-21, Crazy Taxi, dense traffic

This is the TR3 run-1 spot. `vtx=` reaches 14463 per render with 2595 OP
strips and `mv_tris op=248-300`, against TR3 run 1's 11-14K vertices,
1146-2538 strips and `mv_tris op=190-271`. Matt reports the spot cannot be
reproduced exactly, so treat these as the same scene, not the same frame.

Health, 28 windows, whole boot:

- `store=fsm` in every window, `drop=1` in the entire run, `ta_raw_of=0`,
  `overflow=0`, raw peak 577K of 4096K.
- `built` equals drawn in every dense window.
- `job_max` 8-17 ms, one window at 42 ms. The 250 ms stall warning never fired.
- No `[ps3rend]` message except `render thread up`.

**Store stage cost, 87 dense windows:**

| | value |
|---|---|
| avg `ta%` | 3.86% |
| avg TA calls | 284 k/s |
| avg vertices/frame | 9.56K |
| avg VPS | 35.4 |
| peak `ta%` | 5.5% at 467 k calls/s, 16.21K vtx/frame |
| cost per TA call | 0.136 us |

In the light scene the same figures are 0.8-0.9% at 43-44 k calls/s, or
0.21 us per call. The per-call cost falls as the scene gets denser, because
the DMA batches get longer and the fixed per-call work amortises.

**TR4 CLOSED 2026-09-21 at Matt's request**, on the ON-only run above. The
table store stage is correct on hardware and the build stays on it; the TA
Fast Store toggle stays in (default ON) so the splitter remains available.

What was never done, and is the first thing to redo if TR4 is ever questioned:

- the `store=split` pass, so the **saving is unquantified**. 3.86% is the cost
  of the table store stage, not the win over the splitter. TR3 run 1 cannot
  supply the baseline either - its `ta%` read 0.0% because the perf brackets
  were dead. One OFF window on any repeatable spot closes this; it does not
  need the dense scene, only the same scene twice.
- a second game, and anything that exercises RTT ([[rtt-never-observed]]).
- the corner in the list above: `TA_LIST_INIT` followed by a vertex parameter
  with no header. The table consumes 32 bytes there, the splitter would
  consume the previous strip's vertex size.

### TR5 - Tuning  `[~]`

1. Frame drop policy and its interaction with the existing frameskip presets.
2. Raw buffer size against real games. Measured peak is 993 KB (Sonic
   Adventure, 30k vtx/frame), so 2 MB is the smallest safe shrink, not 1 MB.
3. Decide whether the VRAM race (section 3.3) needs a guard in any real game.

#### Built 2026-09-21, not yet hardware tested. Item 3 not started.

**There are no frameskip presets on PS3.** `get_frameskip_preset()` is defined
only in `wii/main.cpp`, and its one caller `plugs/drkPvr/gxRend.cpp` is excluded
by `Makefile` line 42. So item 1 has nothing to interact with: the TR3
single-slot queue is the only frame-drop mechanism in the build.

**What the drop policy was.** Three different rules, only one of them counted:

| job | rule | counted |
|---|---|---|
| `JOB_RENDER` | drop at once when busy | yes, `drop=` |
| `JOB_PRESENT` | `VBlank()` returns early on `rth_busy()` | **no** |
| `JOB_RTT` | block until free | n/a |

**What it is now.** Options -> VIDEO / DEBUG -> **Frame Drop**, `nullDC.cfg
[ps3]` `DropPolicy`, **default WAIT 1 FRAME**:

- `0` DROP - the TR3 baseline, so one build A/Bs the change.
- `1` WAIT 1 FRAME - the SH4 waits up to 1/59.94 s for the slot, then drops.
- `2` WAIT - the SH4 waits with no budget, as `JOB_RTT` already does.

The budget is what makes 1 the default rather than 2: a render thread that hangs
can never hang the SH4 through this wait. TR3 run 1 and the TR4 run each read
`drop=1` for the whole run, so the expected cost is near zero and the expected
win is one kept frame per run - the policy earns its place only if a heavier
game drops more, which is what the new counters are for.

`JOB_PRESENT` is deliberately left on DROP at every policy. A deferred present
loses nothing: the framebuffer is not marked, so the next vblank presents it.

**Counters added to the `[rend]` line:**

- `wait=<ms>` - total SH4 time spent waiting for the job slot in the window.
  This is the price of the policy, and it reads 0 when nothing ever collides.
- `fbdef=<n>` - vblank presents the render thread was too busy to take. This
  was silent before; it is a retry, not a lost frame.
- `peak=<n>K/<cap>K` - the raw high-water now prints its buffer size with it.

**The old peak figure was a floor, not a high-water.** `s_stat_raw_peak` was
written only in `decode_pending()`, from `raw_end`. `raw_end` is the `raw_used`
latched at STARTRENDER, so bytes stored after that latch, and any stream that is
never drawn at all, never reached the peak. It is now taken in
`ps3ta_store_sq()` / `ps3ta_store_dma()` on every append, so it covers the whole
stream. **Expect the reported peak to rise**; the 993 KB and 577 KB figures in
TR1 and TR4 are not comparable with it.

**Item 2 shipped on the old figure.** `TA_RAW_SIZE` is 2 MB, so the four
contexts hold 8 MB instead of 16 MB. That is the shrink the plan asks for, but
it rests on a peak that was measured with the understating counter above. The
hardware test must read the new `peak=` before this is considered settled; on
overflow the store stage keeps firing interrupts and `ta_raw_of` counts, so the
failure mode is lost geometry, not a crash.

**HW test:** Crazy Taxi in dense traffic and one 2D-heavy screen, 3+ minutes.
1. Read `peak=` against the 2048K cap and confirm `ta_raw_of=0`.
2. Read `wait=` and `drop=` at the default policy.
3. Run the same spot at Frame Drop = DROP and compare `drop=`, `wait=` and
   SPEED. That pass is the A/B the policy needs.
4. Confirm `fbdef=` on a 2D screen, where `JOB_PRESENT` actually runs.

#### HW run 1, 2026-09-21 (Frame Drop = WAIT 1 FRAME only)

Crazy Taxi NTSC, dynarec + JitFastmem, 224 s, 33 `[rend]` windows, menu through
dense traffic. No freeze. `[hb]` read `rend=idle` at every sample and no
`[ps3rend]` message fired, so the render thread never stalled.

**Item 2 passes.** `peak=` reached **602K of 2048K** at 10-12K vtx/frame and
~1050 OP strips, with `ta_raw_of=0` and `overflow=0` in all 33 windows. That is
the fixed store-time counter, so it is a real high-water: 2 MB keeps 3.4x
headroom, and the TR1/TR4 figures it replaces were understating by roughly 1.6x
(993 KB reported for a denser Sonic scene than 602 KB here).

**Item 1 fired twice and cost nothing.** `drop=0` in **every** window. Two
windows read `wait=`: 11 ms over 235 renders (that window's `job_max` was 34 ms,
the worst of the run) and 2 ms. Every other window read 0 ms. So a collision that
the TR3 build would have counted as a lost frame became a sub-0.05 ms/render
wait, twice in 224 s.

Health: `job_max` 1-34 ms, the 250 ms warning never fired. SPEED 47-65%,
VPS 28-39. `ta:` 3.4-4.8% at 253-359 k calls/s, in line with TR4's 3.86%.
`[vid] presented=4084 skipped=30`.

Not covered by this run:

- **The A/B was not run.** Frame Drop = DROP on the same spot is what turns
  "two collisions" into a dropped-frame count and a SPEED comparison. Until it
  runs, the policy's win is observed, not measured.
- `fbdef=0` everywhere and `fb=0` after the menu window, so `JOB_PRESENT` never
  ran in game. The deferred-present counter is unexercised.
- `rtt=0/0` again ([[rtt-never-observed]]).
- Item 3, the VRAM race, was not started.

---

## 5. Order of work

TR1 is the only phase that can change rendering by accident, so it ships alone
and gets its own hardware test. TR2 is a refactor with no behaviour change.
TR3 is where the speed arrives. TR4 is an optimisation on top. Do not reorder.
