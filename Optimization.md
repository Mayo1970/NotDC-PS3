# Optimization — where the missing frames are

Audit of the three reference emulators against our tree, and a ranked work list.
Written 2026-09-21. No code was changed.

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Sources read:
- `nullDC-v1/nulldc/nullDC/dc/sh4/rec_v1/` (analyser, basicblock, blockmanager)
- `nullDCe/dc/sh4/rec_v2/` + `nullDCe/wii/dc/sh4/rec_v2/wii_driver.cpp`
- `reicast-emulator/libswirl/hw/sh4/dyna/` + `libswirl/jit/backend/`
- `reicast-emulator/docs/Dynarec Architecture.md`
- our tree: `dc/sh4/rec_v2/`, `ps3/ps3_driver.cpp`, `ps3/ps3_rend.cpp`

---

## 0. Baseline

Crazy Taxi, HW 2026-09-21, dynarec + JitFastmem + threaded render (TR4):
SPEED 47-65%, VPS 28-39. Light scenes reach 43 RPS. Dense traffic does not.

Two budgets share ONE PPE execution pipe:

| Thread | Work |
|---|---|
| SH4 | guest execution, TA store stage |
| render | TA decode, `draw_frame`, present |

SMT gives roughly 1.3x, not 2x. The PPE is in-order with a large mispredict
penalty, so its real IPC at 3.2 GHz is closer to a mid ARM core than to a
desktop CPU. Extra RAM and idle SPEs do not change that.

The RSX is not the limit. Command submission on the PPE is.

---

## 1. Where our rec_v2 already stands

Our core is **nullDC4Wii's** rec_v2, not nullDCe's. Byte-identical on import.

| File | nullDCe | ours | reicast |
|---|---|---|---|
| `driver.cpp` | 5.1 K | 24.5 K | 11.9 K |
| `shil.cpp` | 2.5 K | 17.8 K | 23.8 K |
| `blockmanager.cpp` | 1.7 K | 8.9 K | 8.3 K + 10 K extras |
| `decoder.cpp` | 17.9 K | 38.0 K | 25.5 K |
| backend | 19 K (wii) | 197 K (ps3) | 20-80 K |

**nullDCe is a downgrade in every part.** It is the pre-Wii rec_v2. Nothing to
take from it. Keep using it only as a core-behaviour reference, per the existing
house rule.

Already present in our PS3 backend, so do NOT re-port:

| Feature | Where | reicast's name |
|---|---|---|
| Static block linking | `ngen_LinkBlock_Static_stub` | `rdv_LinkBlock` |
| Dynamic link + inline cache | `dyn_ic` preset | — |
| Fastmem, trampoline back-patch | `fastmem` preset | `Rewrite()` |
| Idle-loop detection | `decoder.cpp:1418` | `settings.dynarec.idleskip` |
| Dead code elim (WAW) | `AnalyseBlock`, `shil.cpp:161` | `srt_waw` |
| Compare fused into branch | `jit_cr0` / `jit_tfwd` presets | `enjcond` |
| Flat dispatch cache | `bcache` preset | — |
| FP register pinning | `fpu_pin` preset | part of `regalloc.h` |
| Bulk-write memcpy path | `g_blockcopy_preset`, `sh4_mem.cpp:268` | — |

---

## 2. What reicast has that we do not

### 2.1 Per-block invalidation via a block graph  `[ ]`  — the only large one

`rdv_BlockCheckFail` (`driver.cpp:527`) does a **full cache clear** on every SMC
hit. The comment there states the reason honestly: `ngen_LinkBlock_Static`
patches a caller's exit into a direct `b <target>`, so dropping one map entry
would leave patched branches pointing at stale code.

reicast solves this with a block graph in `blockmanager-extras.cpp`: it records
who links to whom, so one block can be dropped and its callers unlinked.

Cost to us: every self-modifying game flushes the whole 6 MB cache and
recompiles from nothing. This shows up as stutter more than as average speed.

PS3 has the RAM for a block graph. The Wii did not. This is the one place where
"PS3 is more powerful" actually buys something.

### 2.2 Staging + steady-state code buffers  `[ ]`

reicast compiles into a scratch circular buffer, then copies blocks that reach a
steady state into a final buffer, with relocation and GC (see the `ngen.h`
header comment). It removes cache-clear churn and is the prerequisite for 2.1.

Caveat for us: our code cache must live in a `.text` array — RW memory never
executes on GameOS. Two buffers means two `.text` arrays and a larger ELF.
`CODE_SIZE` is 6 MB today; reicast uses 10 MB.

**Do 2.1 and 2.2 as one job.** Splitting them does not work.

### 2.3 `SmcCheckLevel`  `[ ]`

reicast has `NoCheck` / `FastCheck` / `FullCheck` (`types.h:446`). We always
emit the full entry guard in `ngen_Begin`. A per-game NoCheck is free speed on
any game that never writes code. Cheap to add, and it pairs with 2.1: fewer
checks means fewer full clears.

### 2.4 `constlink` and `sq_pref`  `[ ]`

Two SHIL passes we lack (reicast `dyna/shil.cpp:926`). `sq_pref` is gated behind
`settings.dynarec.unstable_opt` and touches the store-queue path, which is OUR
TA submission path (`CC_SQW` in the census). Worth a measurement.

Note: reicast's own `constprop` is commented out at both call sites, with
"crashes on ip". Do not port that one from reicast.

### 2.5 `regalloc.h` — DO NOT PORT

reicast's 23 KB allocator exists because ARM32 and x86 are register-poor. It
does live-range allocation with per-block preload and writeback.

We pin SH4 `r0..r15` to PPC `r14..r28` for the **whole JIT run**, loaded once in
the mainloop prologue (`ps3_driver.cpp:467`). There is no per-block reload at
all. On a 32-GPR host that is strictly better than what `regalloc.h` produces.
Porting it would be a regression.

---

## 3. What nullDC-v1 offers

`rec_v1` is the fast one — skmp measured a 1:1.2 SH4:x86 cycle ratio on a
Core 2. But its IL is modelled on x86 flags (`SaveT` / `LoadT` with SetCC
conditions), which the architecture doc calls out as the thing that makes
porting to flagless hosts "much much much much harder". The compiler itself
does not port.

Two settings do:

| Setting | Where | Note |
|---|---|---|
| `UnderclockFpu` | `rec_v1/analyser.cpp:44` | divides FP issue cycles by 3. A speed hack, one config line. |
| `CPpass` (constant propagation) | `shil/shil_ce.cpp:400` | nullDC-v1's version, NOT reicast's disabled one. |

`rec_v1/analyser.cpp` is otherwise a SH4 pipeline model for cycle **accuracy**,
not speed. Skip it.

---

## 4. The evidence that limits all of section 2 and 3

`dc/sh4/rec_v2/ccall_census.h:19`, written from our own hardware runs:

> Every change argued from static instruction counts — superblocks, bcache,
> jit_align, hot-block clustering, dyn_ic, jit_cr0 — has landed between 0% and 3%.

The only JIT change that ever moved a game was `jit_fschg` (+10% in ChuChu), and
a counter found it, not a code reading. Interpreter fallback is already measured
at **2 calls per second**. Memory dispatcher traffic was chased down to 95.6%
block transfers from plain C, and closed with `g_blockcopy_preset`.

**The SH4 JIT is not where the missing frames are.** Expect section 2 to buy
smoothness (fewer full cache clears) rather than average speed.

---

## 5. The larger target: draw submission

`draw_list` in `ps3/ps3_rend.cpp:1137` issues one `glBegin` / `glEnd` per PVR
strip. Dense Crazy Taxi: **~1300 draws of ~8 vertices each**, every one walking
the ps3gl immediate-mode path into its own `rsxDrawVertexArray`.

G7 measured this at 143-152 ms/frame before the render thread existed, and G8
measured 78.5 ms/frame after the cached interpreter. TR3 moved it off the SH4
thread. Nothing has yet reduced the work itself.

The fix: build one vertex buffer for the whole frame during the decode stage,
group strips by polygon state, then submit one draw per state block using an
index array or degenerate-triangle joins. `GPstate_cache` (`ps3_rend.cpp:1052`)
already knows when state is unchanged.

Second, smaller: `apply_param` sets PVR state through the ps3gl GL state
machine, so every blend / depth / texture change re-walks dirty flags. Writing
the PVR state set straight into the GCM command buffer removes that layer.

---

## 6. Ranked work list

| # | Item | Effort | Expected |
|---|---|---|---|
| 1 | Strip batching + frame vertex buffer (§5) | large | the big one; dense scenes |
| 2 | PVR state straight to GCM (§5) | medium | follows from 1 |
| 3 | Block graph + staging buffer (§2.1, §2.2) | large | stutter, not average speed |
| 4 | `SmcCheckLevel` per game (§2.3) | small | free on non-SMC games |
| 5 | `UnderclockFpu` (§3) | trivial | FP-heavy games only |
| 6 | `constlink`, `sq_pref`, `CPpass` (§2.4, §3) | small each | measure, expect 0-3% |
| 7 | SPE offload: AICA mixing, then texture decode (G9) | large | frees PPE time directly |
| — | `regalloc.h` | — | DO NOT PORT (§2.5) |

Do 1 before 3. The evidence in §4 says so.

---

## 7. Realistic ceiling

- 2D and puzzle games: full speed reachable with item 1 alone.
- Mid-weight 3D (Crazy Taxi, ChuChu, Sonic Adventure): full speed is a realistic
  target after items 1-3.
- Heavy titles (Shenmue, Soul Calibur in crowds, Jet Set Radio): expect 70-90%.
  Getting those to 100% needs item 7 plus JIT work not yet scoped.

There is no single missing trick. We pay for a slow in-order CPU carrying both
emulation and draw submission. Item 1 removes the draw half.
