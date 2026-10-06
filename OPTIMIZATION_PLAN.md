# Optimization plan - what to take from Flycast

Review of Flycast (`E:\Users\Matteo\Desktop\Dreamcast\flycast`) against our tree,
Reicast and Minicast. Written 2026-09-23. Flycast is an allowed reference from
this date (Matt).

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Flycast is little-endian only. Check every ported line for byte order.

---

## 0. Resume here (updated 2026-10-05)

**Active: Crazy Taxi at full speed (Matt 2026-10-05: "I'd like to get Crazy
Taxi in full speed").** Dense Crazy Taxi (`run528b.txt`): VPS 59.2, SPEED
98.7%, 68% of dense windows at full speed; 12K+ vertices 95.8%. The SH4
thread is the limit. Done: 5.16 SPE Draw (same load +0.5%, render busy 56
-> 32%; default ON since the package of 20:13, Matt) and SPE Wait Yield
(no gain, default OFF). 5.29 JIT FRCHG and 5.30 JIT PREFETCH (VIDEO /
DEBUG, default OFF, package of 20:40): HW in one boot, correct (full
sync_fpscr calls 187K/s -> 2.4K/s), same load -0.6% (noise), no gain;
see section 5 after 5.30. 5.31 JIT GUARD REUSE (VIDEO / DEBUG, default
OFF, package of 21:50): HW correct, same load +0.1% (noise), no gain.
Dense Crazy Taxi now: SPEED 99.2-99.4%, 77-80% of dense windows at full
speed. 5.32 stall probe HW (package of 22:22): the deepest dip of each
run (VPS 40-44, one moment) is one disc read of 330 ms on the SH4 thread,
and dense play spends ~61 ms/s in disc reads (one read of 70-96 ms in 11
of 56 dense seconds). Cause: the CDI reader did one seek and one read per
sector. 5.33 CDI batched reads (Matt: "Just build the fix right away"),
package of 22:38: one seek and one read per run of up to 32 sectors. HW
2026-10-05: correct (Crazy Taxi, Sonic Adventure), small gain: read time
per request -7%; the 330 ms read stays (the first read after seconds with
no reads). Matt: "We can park them for now. It's okay.": 5.33 stays in;
the unbuffered read test and P3 (async reads) are parked. See section 5
after 5.31.
Left from the list: the TA per-record counters and the deadline
scheduler (P5). Not useful now: a lower SH4 Clock (JIT IDLE SKIP makes the wait time
almost free) and frame skip (the SH4 never waits for the render thread).
UDP: the 5.33 listener of session 8112eaf0 (`ps3log_533_run1.txt`) was
stopped at Matt's request at 22:57; port 18194 is free.
Defaults (Matt 2026-10-05): Dynarec, JIT Fastmem, TA SQ Direct, JIT SQ
STORES, JIT FTRV, JIT CONSTPROP, SPE AICA Sound and SPE ARM7 are ON by
default since the package of 23:22 (no warning, `.got` 0xA790, 0 `r2off`).

**Done: 5.28, a larger SH4 code cache (Matt 2026-10-05: "We gotta move
to the next goal", then picked "Larger SH4 code cache"), default 24 MB
since the package of 19:08 (Matt: "Let's keep it 24 as default then").**
EMULATION -> "JIT Code Cache" (`JitCodeCache`): 6 MB / 12 MB / 24 MB, set
before boot. The `.text` array is 24 MB (`CODE_SIZE`); the option sets
how much of it the JIT uses. The row is on the EMULATION page: the JIT
SPEED page has 19 rows and already reaches the footer at 720p. HW
2026-10-05 at 24 MB (package of 17:08): Sonic Adventure clean, no full
clear in 157 s of play (`run53c`: 3), the cache grew to 7.8 MB and slowed
to 10-20 KB/s, dense SPEED 99.7% (99.6%); Crazy Taxi clean, same-load VPS
-0.2% against `run53b` (noise). Matt asked about 12 MB: the option alone
frees no memory (the array stays 24 MB), and Sonic would reach its 11 MB
flush mark after ~6-7 min in one area; he kept 24 MB. Next: Matt picks
(left from the list: the XMB exit 3 s fallback P6). The 5.28 listener
(PID 24244, `ps3log_528_run1.txt` in the scratchpad of session 849b7627)
was stopped at Matt's request; port 18194 is free.

**Done: 5.3 DYN_IC (Matt 2026-10-05: "Go with DYN_IC (5.3) this
session"), default JSR/JMP since the package of 16:42.** Matt after the HW
runs: "It kind of felt the same across both runs? We could mark it as done
anyway", then "Place it on" (level JSR/JMP, his pick: same speed as + RTS,
fewer misses, less code). An install resets the settings to it. HW 2026-10-05 (5.3
"HW"): all three boots clean; Crazy Taxi same-load VPS +1.0% at JSR/JMP and
at + RTS against `run524a.txt` (noise level, the levels not apart); Sonic
Adventure equal within noise; code +3-4%. Rts sites miss ~0.86M/s in dense
Crazy Taxi. Our listener (PID 32812) was stopped from outside at ~16:15,
after the Sonic run, when the Halo-PS3 session took port 18194 back (PID
23760). Next: Matt picks (left from the earlier list: a larger SH4 code
cache, the XMB exit 3 s fallback P6). Package of 14:19: JIT SPEED
-> "JIT DYN IC" (`JitDynIc`): OFF / JSR/JMP / + RTS, default OFF, set before
boot. Each dynamic block end (jsr/bsrf, jmp/braf; rts at + RTS) gets a
4-word site, and its first target is written over it as a guard (`xoris`,
`cmplwi`, `bne`, `b target`): a repeat of that target skips the lookup's 3
dependent loads, its `lookups` store and the `bctr`. Misses take the old
lookup. PS3 fix: the fill stub now zero-extends the pc it passes to C. PC:
`misc/dynic_check/run.sh` PASS (637371 checks), `mutate.py` 25 of 25
planted bugs fail. HW steps and results in 5.3.

**Done: 5.24, `mfcr` -> `mfocrf` and the FPUL moves (Matt 2026-10-05:
"Go with 5.24 in the Optimization plan"), default ON.** Built and PC
tested, package of 13:01: JIT SPEED -> "JIT MFOCRF" (`JitMfocrf`) and "JIT
FPUL MOVES" (`JitFpulMoves`), set before boot (default ON since 13:49). PC:
`misc/ppe_check/run.sh` PASS (68 words against ppu-as, a PPC64 model with
bit-exact lfs/stfs/fctiwz, new forms against old); `mutate.py` 22 of 22
planted bugs fail. The package also holds 5.27 (JIT SQ MISS, default OFF)
from the same tree. HW boot 1 2026-10-05 (Crazy Taxi, with JIT SQ MISS = +
LOOK-AHEAD as in `run527b.txt`): log clean, `[ppe]` sites in all five
forms, dense VPS 58.4 at SPEED 97.4% (vt 9.2K), 51% of dense windows at
full speed; same-load VPS +1.2% against `run527b.txt` (about the noise of
one run), +2.0% against `run526a.txt`. HW boot 2 (Sonic Adventure): log
clean, speed equal within noise against `run527c.txt` (SPEED 99.7%, 95%
of dense windows at full speed). Both steps done. Matt: "I want them ON by
default": `JitMfocrf` and `JitFpulMoves` default to 1 since the package of
13:49. The listener of session 468f5b68 was stopped (Matt). Since 13:46 a
Halo-PS3 session's listener (PID 16436, `ps3log_g3_run1.txt`) holds port
18194; not ours, left running. Start a listener only when Matt says so.
Next: Matt picks (the other candidates are in the block below: 5.3
DYN_IC, a larger code cache, the XMB exit fallback P6).

**Earlier next: Matt picks (2026-10-05, after 5.27: "What's next?"); Matt
picked 5.24.** Candidates by
evidence: 5.24 `mfcr` -> `mfocrf` (3.5M/s microcoded `mfcr`, one
instruction swapped); 5.3 DYN_IC (code exists on the PS3, preset fixed at
0); a larger SH4 code cache (`CODE_SIZE` 6 MB in `dc/sh4/rec_v2/ngen.h`,
a Wii size: Sonic Adventure fills it every ~55 s, each clear a short dip;
it also hides 5.27's +8% code); not speed: the XMB exit 3 s fallback
(P6).

**Done: 5.27, the store-queue stores JIT SQ STORES misses (Matt
2026-10-05: "Work on 5.27 then", option "both, 3 levels").** Built and PC
tested, package of 11:19: JIT SPEED -> "JIT SQ MISS" (`JitSqMiss`), OFF / SLOW PATH / +
LOOK-AHEAD, default OFF, set before boot; LOOK-AHEAD needs JIT SQ STORES
ON. HW 2026-10-05 (Crazy Taxi, one boot per level, logs clean):
same-load VPS +1.7% with SLOW PATH and +0.9% with + LOOK-AHEAD against
`run526a.txt` (JIT SQ MISS OFF, scratchpad of session 3f596c0b); the
levels are not apart in one run each; + LOOK-AHEAD best in heavy scenes
(Matt: "Boot 2 run much better"). Sonic Adventure (+ LOOK-AHEAD, Matt:
"It ran worse?"): gameplay speed equal within noise, but the code is ~8%
larger (the SLOW PATH test), so the cache fills sooner: 4 cache-full
clears in 271 s against 3 in 216 s, two followed by a 1 s dip to 64-73%
SPEED (5.27 "HW", boot 3). No Soul Calibur for these tests (Matt). Default
ON is Matt's call. The listener of session 468f5b68 stopped at 13:44
(killed from outside); its `ps3log_527_run1.txt` also holds two later
boots of another session's build (`mfocrf=1 fpul=1`): Crazy Taxi from line
36567, Sonic Adventure from line 44391. Before it (Matt asked "which
ones will give a gain?"): no candidate is measured; 5.27 has the best
evidence (5.17 Step 3), then `mfcr` -> `mfocrf` (5.24), then DYN_IC (5.3,
its PS3 code exists, `get_dyn_ic_preset()` is fixed at 0). Texture decode
is 0.8% of the render thread's wall in dense Crazy Taxi (`run526a.txt`)
and the SH4 thread waits ~0 for it, so work there does not help Crazy
Taxi.

**Done: 5.26, T for `bt.s`/`bf.s` (Matt 2026-10-05: "Let's start with
5.26", option a).** JIT SPEED -> "JIT T COPY" (`JitTCopy`), set before
boot, default ON since the package of 10:13 (Matt: "And make it default
ON"). The last T writer before the jcond also leaves T in r31, so the
jcond skips its reload. PC: `misc/tcopy_check` PASS, 18 of 18 planted
bugs fail it. HW 2026-10-05 (package of 09:28): Crazy Taxi log clean, no
reload at 2107 of 2124 jcond sites, miss=0, same-load VPS -0.1% against
`run525b.txt` (no measurable gain); Sonic Adventure and Soul Calibur
clean. The profiler boot was not run. Lesson: the 5.18 static
load-hit-store count overstates the cost of these reloads, so the
remaining load-hit-store items (5.24) may gain little too. Next: Matt
picks. The UDP listener of session 3f596c0b was stopped (Matt); port
18194 is free. Logs: `run526a/b/c.txt` in its scratchpad.

**Earlier next: 5.26, T for `bt.s`/`bf.s` (the jcond load-hit-store), from
the 5.18 ranking.** Matt 2026-10-05: "Can you update the OPTIMIZATION plan
so it resumes from there?"
- Candidates after 5.25, in the order of the 5.18 ranking (item 1, the
  wait loop, is done):
  1. Load-hit-stores, ~6M/s real stalls of ~40 cycles each:
     - T for `bt.s`/`bf.s`, ~1.9M/s: 5.26, built (option a), HW pending.
     - FPUL and `jit_scratch` ~1.2M/s (`lds`, `float`, `ftrc`, `flds`,
       `fsts` through memory): 5.24.
     - Pinned FR values through the context ~1M/s; FPSCR 0.22M/s.
  2. Dynamic ends (`rts`, `jsr @Rn`, `braf`) 2.6M/s, each ~15
     instructions and an indirect branch: DYN_IC, 5.3 (forced OFF on the
     PS3).
  3. `mfcr` -> `mfocrf`, 3.5M/s: 5.24.
  4. Store-queue stores JIT SQ STORES misses, 0.88M/s: 5.27 (new).
  5. Small: sync_fpscr full path 158K/s, variable shifts 0.45M/s.
- Lesson from 5.25: the profiler counts instructions, not time. The wait
  loop was 12.4% of the JIT instructions but only ~3% of the time (+3.0%
  VPS). Stalls (items 1-3) cost far more than their instruction count, so
  rank by stalls.
- Dense Crazy Taxi now (SH4 200, JIT IDLE SKIP ON, no profiler): VPS 57.1
  of 59.95 at vt 9.3K, 35% of dense windows at full speed. Full speed
  needs ~5% more.
- Reference log for the next same-load comparison: `run525b.txt` in the
  scratchpad of session 1a0d7654 (with `vtbins.py`, `bins.py`,
  `idlepair.py`, `jcond518.py`). Its settings (an install resets them):
  Dynarec ON, JIT Fastmem ON, SH4 Clock 200; ARM7 Idle Skip SAFE, SPE AICA
  Sound ON, SPE ARM7 ON; TA SQ Direct ON, IFB Probe ON, JIT C-Calls ON,
  JIT HOTBLOCKS OFF; JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD
  VALUES ON, JIT CYCLE CHECK LOOPS ONLY and JIT IDLE SKIP ON (both
  defaults).
- Open, not speed: the Sonic Adventure check of the 5.25 fix (not run);
  the XMB exit 3 s fallback (`OPTIMIZATION_POTENTIAL.md` P6, ~25 lines);
  section 6 (sound faults) and the Soul Calibur faults, both parked.
- UDP: no listener runs; port 18194 is free. Start one only when Matt
  says so.

**Done: 5.25, the wait-loop idle skip (Matt 2026-10-05: "Let's go with
the wait loop", option "a + safety").** Built and PC tested 2026-10-05,
package of 00:45: JIT SPEED -> "JIT IDLE SKIP" (`JitIdleSkip`), default
OFF, set before boot. HW 2026-10-05: Crazy Taxi +3.0% VPS at the same
load, but Sonic Adventure drew 20 frames/s where it drew 30 (a skipped
loop did work). Fix (Matt: "Fix build"): a register-hash check, package of
01:13. HW 2026-10-05: Crazy Taxi +3.0% VPS at the same load against the
no-probe 5.18 run (dense VPS 57.1, 35% of dense windows at full speed);
work loops no longer skip. The Sonic check of the fix was not run (Matt:
"Sonic Adventure already runs well"). Default ON since the package of
01:27 (Matt: "Turn it on by default").
Matt 2026-10-05: do not start the UDP listener until he says so. This
session's listener (file `ps3log_525_run1.txt` in its scratchpad) was
stopped at the end (Matt); port 18194 is free.

**Earlier next: 5.25, the wait-loop idle skip (from the 5.18 ranking).**
Matt picks the option (5.25 a-d) before any build.
- 5.18 (Matt 2026-10-04: "We start from 5.18") is measured: profiler VIDEO
  / DEBUG -> "JIT HOTBLOCKS" (default OFF, set before boot), package of
  23:13; HW run 3 is complete. Ranking in 5.18 "HW": 1. wait loop (12.4% of
  the JIT instructions), 2. real load-hit-stores ~6M/s (T for `bt.s`/
  `bf.s` 1.9M/s, FPUL and float conversions 1.2M/s), 3. dynamic ends
  2.6M/s (DYN_IC, 5.3), 4. `mfcr` 3.5M/s (5.24), 5. missed store-queue
  stores 0.88M/s, 6. sync_fpscr full path 158K/s.
- The 23:13 package also holds the VMU session's maple changes (same
  working tree). The 5.18 logs are in the scratchpad of session 29bdd1c1.

**Earlier next (Matt 2026-10-04: "5.18 for measurements, and we'll think from
there"), in a new session: 5.18, the JIT hot-block profile.** It changes no
speed by itself: it measures where the SH4 thread's JIT time goes, then Matt
picks the fix. Steps in 5.18. Candidates it ranks: 5.24 (`mfcr` ->
`mfocrf`; the load-hit-stores of `float`, `ftrc`, `flds`, `fsts`), 5.23
(`fmov.s @Rn+` pairs) and whatever the dumps show.
- State at the end of the 2026-10-04 evening session (a08941d5):
  - 5.21 HW: correct (Matt), same-load VPS +0.2% (noise). "JIT CONSTPROP"
    and "JIT DEAD VALUES" stay default OFF.
  - 5.22 HW: LOOPS ONLY +1.9% at the same load against 5.21; Matt: "boot B
    played normally". "JIT CYCLE CHECK" defaults to LOOPS ONLY since the
    package of 22:44 (Matt: "Set LOOPS only as default").
  - Since the 5.17 Step 1 run: +13.5% at the same load (dense VPS 48.4 at
    vt 9.5K -> 54.2 at vt 9.6K). Full speed (59.95) needs ~10.6% more.
  - Settings of the last runs (an install resets them): Dynarec ON, JIT
    Fastmem ON, SH4 Clock 200 (default), VIDEO / DEBUG -> TA SQ Direct ON,
    JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD VALUES ON,
    JIT CYCLE CHECK LOOPS ONLY (default). The five ON options are still
    default OFF: Matt's call.
  - Reference log for 5.18: `run522b2.txt` (the LOOPS ONLY boot) in the
    scratchpad of session a08941d5, with `bins.py`, `vtbins.py` and
    `longcomments.py` (the comment-length check).
  - UDP: the listener of session daecf347 (PID 5656, msys2 python) still
    holds port 18194 and appends to `ps3log_517_run3.txt` in that session's
    scratchpad (3539212 bytes at the end of this session). Stop it and start
    a new one, or read that file after the offset.
  - Every Crazy Taxi boot shows ~33 s at RPS 0 and VPS 60 before gameplay
    (after the intro). It is normal, not a hang.

**Active: 5.15 SPE ARM7 (Matt 2026-10-04, after 5.14 boot 3: "It sounded
the same. Let's apply 2.").** The ARM7, nullAICA's registers and the caique
control twin run on an SPU; the SH4 thread only queues commands. Built
2026-10-04, PC tested (B0, B1, B2 checks and mutation tests, see 5.15).
AUDIO -> "SPE ARM7" (`SpeArm7`), default OFF, set before boot, needs SPE
AICA Sound ON. HW 2026-10-04 (package of 17:34): Crazy Taxi dense VPS 52.9
-> 54.7, ChuChu stage at full speed, logs clean; Matt heard popping in Crazy
Taxi, also at full speed. Tests 2026-10-04 (lag cap 32, then the PPU path's
order exactly, then SPE ARM7 OFF): the old PPU path pops the same with SPE
AICA Sound ON, so the popping is plan 6.6's crackle, not 5.15. Lag cap back
to 256, test option removed: package of 19:03 (HW: dense VPS 54.8, driving
49.5 fps against 42.6 before 5.15; Matt: popping lower, still there).

**5.21 (Matt 2026-10-04: "It's time for 5.21"), HW tested, no gain.** Minicast M3 +
M4, constant propagation + dead value elimination on SHIL. Built and PC
tested 2026-10-04, package of 22:06: JIT SPEED -> "JIT CONSTPROP" and "JIT
DEAD VALUES", both default OFF, set before boot. PC: `misc/constprop_check/`
(real headers, SHIL interpreter, PPC-form validator, fuzzer) PASS on 1.6 M
blocks; 31 of 31 planted bugs fail it. HW 2026-10-04 (Crazy Taxi, both ON):
log clean, SHIL ops -2.9%, same-load VPS +0.2% (within noise); Matt:
"geometry and sound looked right". See 5.21 "HW".

**5.22 (Matt 2026-10-04: "Yes, do it"), HW validated, LOOPS ONLY default.** Block-entry cycle check:
COLD EXIT puts the exit out of line, LOOPS ONLY also drops the poll on
blocks whose end cannot close a loop. Built 2026-10-04, package of 22:27,
JIT SPEED -> "JIT CYCLE CHECK" (default OLD, set before boot). HW
2026-10-04 (Crazy Taxi, one boot per mode): logs clean; same-load VPS
+0.8% with COLD EXIT and +1.9% with LOOPS ONLY against the 5.21 run
(+13.5% since the 5.17 Step 1 run). Matt: "boot B played normally". LOOPS
ONLY is the default since the package of 22:44. See 5.22 "HW".

**Earlier next (Matt 2026-10-04: "Save the 2nd option ... and we get back
to it in a cleaner session"): 5.21 in a new session.** 5.21 = Minicast M3 +
M4, constant propagation + dead value elimination. Build its PC fuzzer
first (port Minicast's `tests/constprop_test.cpp` to WSL as
`misc/constprop_check/`, with a PPC-form validator); see 5.21 for the PS3
rules. Then 5.22.
- State at the end of the 2026-10-04 session: 5.17 Steps 1-3 and 5.20 are
  built and HW tested on Crazy Taxi (dense VPS 48.4 -> 53.5, +11.1% at the
  same load, SH4 200). Three options, all still default OFF: VIDEO / DEBUG
  -> "TA SQ Direct" (live), JIT SPEED -> "JIT SQ STORES" and "JIT FTRV"
  (both set before boot). Sonic Adventure with the first two ON: looked
  right (Matt). Soul Calibur with all three ON: log clean, still
  render-bound (texture decode), visual check by Matt pending (5.20 "HW").
  Default ON: Matt's call.
- Tools from that session (scratchpad of session daecf347): `split517.py`
  (`[ta-split]` by vt bin and `direct=`), `vtbins.py` (same-load VPS of two
  logs, 1K vt bins), logs `ps3log_517_run1.txt` (Step 1), `run2` (Step 2),
  `run3` (boot 1 Step 3, boot 2 Sonic Adventure, boot 3 5.20).

**Earlier next (Matt 2026-10-04, in a new session): 5.17 TA store off the
SH4 thread and 5.18 JIT hot-block profile.** Dense Crazy Taxi needs ~9%
more SH4-thread speed for 60 VPS (54.8 -> 59.95). Also saved: 5.16 (SPE
Draw check, one boot) and 5.19 (SH4 Clock below 150, last resort). Plan 6.6
when Matt says.

**Active: 5.17 (Matt 2026-10-04: "Let's start with 5.17 TA").** Step 1
(`[ta-split]` probe, package of 19:33) HW done 2026-10-04: the C side of
the SQ path costs ~252 ns per TA flush, 9.5% of wall in dense Crazy Taxi
(SH4 200); the call chain is 47% of it. See 5.17 "Step 1 HW". Step 2
(option 1, Matt: "let's do option 1") built and PC tested 2026-10-04,
package of 20:20: VIDEO / DEBUG -> "TA SQ Direct", live, default OFF. HW
2026-10-04 (Crazy Taxi, ON): no wrong geometry, no hangs; dense VPS 48.4
-> 50.5, +3.2% at the same load. See 5.17 "Step 2 HW". Step 3 (Matt: "Do
the Step 3 then") built and PC tested 2026-10-04, package of 20:49: JIT
SPEED -> "JIT SQ STORES", default OFF, set before boot. HW 2026-10-04
(Crazy Taxi, both ON): log clean, dense VPS 51.5, +4.2% at the same load
against Step 2 and +6.7% against Step 1; Matt's verdict pending. See 5.17
"Step 3 HW".

**Order after the Minicast JIT review (2026-10-04; Matt: "Can you update
the OPTIMIZATION PLAN, then?").** Review notes: 5.20-5.24.
1. 5.17 Step 2, option 1: the call-chain fix (one flat TA store function
   that the JIT's PREF calls directly). HW 2026-10-04: +3.2% VPS, clean.
2. 5.17 Step 3 (Minicast M2): the JIT side of the SQ path (the guest's
   stores before each PREF). Built + PC tested 2026-10-04 (JIT SPEED ->
   "JIT SQ STORES", default OFF), package of 20:49. HW: +4.2% VPS, clean log.
3. 5.20 (Minicast M1): ftrv with known w, interleaved MAC chains. Built +
   PC tested 2026-10-04 (JIT SPEED -> "JIT FTRV", default OFF; the known-w
   pass is not built), package of 21:05. HW: +3.8% VPS (same load), log clean.
4. 5.21 (Minicast M3 + M4): constant propagation + dead value elimination.
   Built + PC tested 2026-10-04 (JIT SPEED -> "JIT CONSTPROP", "JIT DEAD
   VALUES", default OFF), package of 22:06. HW: log clean, same-load VPS
   +0.2% (noise).
5. 5.22 (Minicast M5): cycle check out of line, then backward-only.
   Built 2026-10-04 (JIT SPEED -> "JIT CYCLE CHECK"), package of 22:27.
   HW: COLD EXIT +0.8%, LOOPS ONLY +1.9% (same load, against 5.21).
   LOOPS ONLY is the default since 2026-10-04 (Matt).
6. 5.18 (hot-block profile): next, in a new session (Matt 2026-10-04). It
   ranks 5.23, 5.24 and other patterns before anything else is built.
One change per HW run, each behind its own toggle, default OFF until HW
validated, at SH4 200. Reference: the 5.17 Step 1 run (dense SPEED 80.7%,
VPS 48.4). 5.18 (hot-block profile) stays open; it ranks 5.21 and 5.22.

**Rule (Matt 2026-10-04): HW tests run with SH4 Clock 200 MHz (the
default), since most games won't like the 150 MHz underclock.** Do not
compare VPS with runs at 150 MHz (e.g. the 19:03 run's dense VPS 54.8).

5.14 Sound thread (second PPU thread) works but is a FAILURE on speed: dense
VPS 52.9 -> 51.6, the SH4 thread ~10% slower from SMT (see 5.14 "Boot 3").

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

| 5.14 | Sound thread: ARM7 + AICA on a second PPU thread (AUDIO "Sound Thread", `SoundThread`) | `[!]` HW 2026-10-04: works, but dense VPS 52.9 -> 51.6 (SMT slows the SH4 ~10%); Matt chose 5.15 |
| 5.15 | SPE ARM7: ARM7 + nullAICA registers + caique control twin on an SPU (AUDIO "SPE ARM7", `SpeArm7`) | `[~]` HW 2026-10-04: Crazy Taxi dense VPS 52.9 -> 54.7, ChuChu stage 100%, Sonic Adventure fine; the Crazy Taxi popping is 6.6 (old path pops too); clean-up package 19:03 |
| 5.16 | SPE Draw check with SPE ARM7 ON (existing option, one boot) | `[x]` HW 2026-10-05: same load +0.5% (10K+ bins +2.4% to +5.0%), render busy 56 -> 32%, 80% of dense windows at full speed; SPE Draw default ON since the package of 20:13 (Matt); SPE Wait Yield (db16cyc in the fence spin) HW: no gain, default OFF |
| 5.17 | TA store off the SH4 thread (`ta` 6.0% of wall in dense Crazy Taxi) | `[~]` Step 1 probe HW 2026-10-04: SQ path ~252 ns per TA flush, 9.5% of wall (dense, SH4 200), call chain 47%; Step 2 option 1 ("TA SQ Direct", call-chain fix) HW 2026-10-04: dense VPS 48.4 -> 50.5 (+3.2% same load), no faults, default OFF; Step 3 (Minicast M2, "JIT SQ STORES") HW 2026-10-04: dense VPS 51.5, +4.2% same load vs Step 2 (+6.7% vs Step 1), log clean, default OFF |
| 5.18 | JIT hot-block profile in dense Crazy Taxi, then fix the hottest generated code | `[x]` measured: Step 1 built + PC tested 2026-10-04 (VIDEO / DEBUG "JIT HOTBLOCKS", default OFF), package of 23:13; Step 2 HW 2026-10-04: ranking in 5.18 "HW" (wait loop 12.4% of JIT instructions, real load-hit-stores ~6M/s, dynamic ends 2.6M/s, `mfcr` 3.5M/s, missed SQ stores 0.88M/s); fixes are their own items (5.25 first) |
| 5.19 | SH4 Clock below 150 MHz (speed trade, last resort) | `[ ]` |
| 5.20 | Minicast M1: ftrv with known w + interleaved MAC chains | `[~]` HW 2026-10-04 ("JIT FTRV", bit-exact form, no known-w pass): Crazy Taxi dense VPS 53.5, +3.8% same load vs 5.17 Step 3, log clean; Soul Calibur pending |
| 5.21 | Minicast M3 + M4: constant propagation (no memory baking) + dead value elimination | `[x]` HW, correct, no gain, default OFF; built + PC tested 2026-10-04 ("JIT CONSTPROP", "JIT DEAD VALUES", default OFF; fuzzer PASS 1.6 M blocks, 31/31 mutants fail), package of 22:06; HW 2026-10-04: Crazy Taxi log clean, SHIL ops -2.9%, same-load VPS +0.2% (noise); geometry and sound right (Matt) |
| 5.22 | Minicast M5: cycle check out of line, then only on backward branches | `[x]` HW, LOOPS ONLY default since 2026-10-04 (Matt); built 2026-10-04 ("JIT CYCLE CHECK": OLD / COLD EXIT / LOOPS ONLY, default OLD), package of 22:27; HW 2026-10-04: logs clean, same-load VPS +0.8% (COLD EXIT), +1.9% (LOOPS ONLY) against 5.21 |
| 5.23 | Minicast M6: `fmov.s @Rn+` pair fusion | `[ ]` not scheduled (low) |
| 5.24 | PPE codegen seen in the review (not Minicast): `mfcr` -> `mfocrf`, int/float moves through memory | `[x]` HW, default ON since the package of 13:49 (Matt); built + PC tested 2026-10-05 (Matt: "Go with 5.24"): JIT SPEED "JIT MFOCRF" and "JIT FPUL MOVES"; ppe_check PASS, 22/22 mutants fail; package of 13:01; HW 2026-10-05 (JIT SQ MISS + LOOK-AHEAD ON too): Crazy Taxi log clean, same-load VPS +1.2% vs `run527b` (same settings without 5.24, about the noise); Sonic Adventure clean, speed equal within noise (95% of dense windows at full speed) |
| 5.25 | Wait-loop idle skip (Crazy Taxi 0C15A658-0C15A67E, from 5.18) | `[x]` option a + safety (Matt 2026-10-05), JIT SPEED "JIT IDLE SKIP", default OFF; HW 2026-10-05 (package 00:45): Crazy Taxi +3.0% same-load VPS, Sonic Adventure 30 -> 20 frames/s (work loop skipped); fix with a register-hash check, package of 01:13: HW Crazy Taxi +3.0% same-load VPS (dense 57.1), work loops no longer skip; Sonic not rechecked; default ON since the package of 01:27 (Matt) |
| 5.26 | T for `bt.s`/`bf.s` kept in a register (the jcond load-hit-store, ~1.9M/s, from 5.18) | `[~]` option a (Matt 2026-10-05), JIT SPEED "JIT T COPY", default OFF; built + PC tested 2026-10-05 (tcopy_check PASS, 18/18 mutants fail), package of 09:28; HW boot 1 2026-10-05 (Crazy Taxi): clean, miss=0, same-load VPS -0.1% (no measurable gain); Sonic Adventure clean (30 fps outside heavy scenes); Soul Calibur clean (render-bound as before); default ON since the package of 10:13 (Matt) |
| 5.27 | Store-queue stores JIT SQ STORES misses (0.88M slow-path entries/s, from 5.18) | `[x]` HW, default OFF (Matt 2026-10-05: "It's fine for now"); both options (Matt 2026-10-05): JIT SPEED "JIT SQ MISS" OFF / SLOW PATH / + LOOK-AHEAD, default OFF; built + PC tested 2026-10-05 (sqstore_check and sqla_check PASS; 12/12 and 26/26 mutants fail), package of 11:19; HW 2026-10-05 (Crazy Taxi): logs clean, same-load VPS +1.7% (SLOW PATH), +0.9% (+ LOOK-AHEAD, best in heavy scenes; Matt: "much better") against `run526a` |
| 5.28 | Larger SH4 code cache (6 MB was a Wii size; Sonic Adventure refills it every 44-55 s) | `[x]` HW, default 24 MB since the package of 19:08 (Matt); built 2026-10-05 (Matt picked it): EMULATION "JIT Code Cache" 6 MB / 12 MB / 24 MB, array 24 MB, `bl` reach 27549 KB of 32768; package of 17:08; HW 2026-10-05 at 24 MB: Sonic Adventure no full clear in 157 s of play (6 MB: 3), cache 7.8 MB, dense SPEED 99.7% (99.6%); Crazy Taxi same-load VPS -0.2% (noise) |
| 5.29 | JIT FRCHG: an FPSCR sync that only flips FR swaps the pinned bank inline (~140K full syncs/s in dense Crazy Taxi) | `[x]` HW, no gain, default OFF; built + PC tested 2026-10-05 (frpref_check PASS, 23/23 mutants fail), VIDEO / DEBUG "JIT FRCHG"; package of 20:40; HW 2026-10-05 (one boot with 5.30): clean, sync_fpscr 187K/s -> 2.4K/s, same load -0.6% against `run516` (noise) |
| 5.30 | JIT PREFETCH: a `pref` outside the store queue prefetches its RAM line with `dcbt` | `[x]` HW, no gain, default OFF; built + PC tested 2026-10-05 (same check), VIDEO / DEBUG "JIT PREFETCH"; package of 20:40; HW with 5.29 (one boot): clean, 174 sites, no gain; whether it costs a little is open |
| 5.31 | JIT GUARD REUSE: an access skips the RAM or SQ check its base register passed earlier in the block (a CR flag) | `[x]` HW, no gain, default OFF; built + PC tested 2026-10-05 (greuse_check PASS, 25/25 mutants fail), VIDEO / DEBUG "JIT GUARD REUSE"; package of 21:50; HW 2026-10-05: clean, 20994 reuse sites, same load +0.1% against `run516` (noise) |
| 5.32 | Stall probe: the SH4 thread's slowest frame each second, split by cause (rides on JIT C-Calls) | `[x]` HW 2026-10-05 (package of 22:22, `runstall_ct.txt`): the Crazy Taxi dip is one 330 ms disc read; dense play ~61 ms/s in disc reads |
| 5.33 | CDI batched reads: one seek and one read per run of up to 32 sectors (was one per sector) | `[x]` HW, small gain, kept (Matt: "It's okay"), follow-ups parked; built + PC tested 2026-10-05 (cdi_check PASS, 14/14 mutants fail), package of 22:38; HW 2026-10-05: Crazy Taxi and Sonic Adventure clean; Crazy Taxi read time per request -7%, dense 60.9 -> 49.9 ms/s, the 330 ms read stays; same load +0.7% (noise) |

FSCHG, FSQRT, NewOps, FPU PIN, TFWD and CR0 are default ON since 2026-09-23 (Matt).
Dynarec, JIT Fastmem, TA SQ Direct, JIT SQ STORES, JIT FTRV, JIT CONSTPROP, SPE AICA Sound and SPE ARM7 are
default ON since the package of 23:22 on 2026-10-05 (Matt).
ARM7 JIT level 5 is the default since 2026-09-23 (Matt).
Vertex Arrays are default ON since 2026-09-23 (Matt: geometry correct).
Matt, 2026-09-23: speed over accuracy. The Flycast cycle model was dropped for
the existing SH4 Clock option. SPEED% reads against 200 MHz, so judge a lower
clock by VPS.
| 5.2 | `JitCarry`: fix, then test | `[ ]` BROKEN on PS3, see below |
| 5.3 | `JitMac`, `jit_cr0`, `jit_tfwd`, `dyn_ic`, `bcache` | `dyn_ic` `[x]` HW, done (Matt 2026-10-05), default JSR/JMP since the package of 16:42 (Matt); built + PC tested 2026-10-05 (Matt: "Go with DYN_IC"): JIT SPEED "JIT DYN IC" OFF / JSR/JMP / + RTS, default OFF; dynic_check PASS, 25/25 mutants fail; package of 14:19; HW 2026-10-05: clean, Crazy Taxi same-load VPS +1.0% at both levels (noise), Sonic equal, code +3-4%. `jit_cr0` + `jit_tfwd` done in 5.1e. `JitMac`, `bcache` `[ ]` |
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
- 5.14 (Matt 2026-10-04: "let's go with ARM7+AICA off the SH4 thread"):
  the ARM7 and the AICA run on a second PPU thread. Option AUDIO -> "Sound
  Thread" (`nullDC.cfg [ps3] SoundThread`), default OFF, set before boot,
  on the `[sndcfg]` line.
  - Why: dense Crazy Taxi at 150 MHz (A3 run) is limited by the SH4 thread
    (VPS 52.3-53.0 of 59.95, the SH4 never waits for the render thread).
    Sound costs the SH4 thread `arm` 6.0% + `aica` 2.4% of wall; a ChuChu
    stage 14%. Full speed needs ~12% less SH4-thread time. The render thread
    is busy 42%, so the second PPE hardware thread is idle more than half the
    time. In the ChuChu logs the SH4 reads no AICA register; it shares only
    ARAM with the sound driver.
  - Model: each MediumUpdate is one queue entry {SH4 cycles, AICA samples}.
    The SH4 thread counts the samples (`aica_samples_due`, the old
    accumulator) and queues the entry (`snd_push`). The token holder runs
    `arm_aica_run()` per entry: the ARM7 slice (Batch rules unchanged), then
    one `libAICA_TimeStep()` per sample. Same code and order as before.
    OFF: `armUpdateARM` calls `arm_aica_run()` directly, as before.
  - Token: only its holder runs sound code. The worker takes it when the
    queue is not empty. The SH4 thread takes it (`snd_lock`) for AICA
    register reads and writes (area 0, `sh4_area0.cpp`), Holly soft reset,
    the in-game menu and the 1 Hz stats block. `snd_lock` first runs the
    queued entries itself, so the SH4 sees the AICA and ARM7 as before
    (caught up to its last MediumUpdate). It nests.
  - Not locked: ARAM. The SH4 and the ARM7 read and write it directly, as
    on hardware. The ARM7 can see an SH4 write up to the worker's lag early
    (Batch 4 already gives 4 MediumUpdates of this).
  - SH4 interrupt (Holly SPU IRQ): on the worker, `UpdateSh4Ints` only keeps
    the level (`snd_irq_set`); the SH4 thread raises or cancels it at its
    next MediumUpdate (`aica_sh4_irq_apply`, new in `aica.cpp`). On the SH4
    thread it acts at once, as before.
  - CD-DA: `gdrom_get_cdda` reads the image file and changes GD-ROM state,
    so it stays on the SH4 thread. The SH4 reads each sector at the
    MediumUpdate where the old code read it (every 588 samples) into an
    8-slot ring before it queues the entry; `cdda_refill` takes the next
    slot, then the old LE -> BE swap.
  - Queue 128 entries. At 16 queued the SH4 posts the worker's semaphore if
    it sleeps. At 32 (~1.5 ms of guest time at 150 MHz) the SH4 runs the
    queue itself if the worker is not on it ("help"); at 120 it waits. The
    worker sleeps 50 µs between checks, 2 ms after 200 empty checks.
  - Priorities while the worker runs: SH4 thread 1001 -> 998, worker 999,
    render thread 1000 (unchanged). The worker never waits behind a render
    job and never takes the SH4's hardware thread. Restored at stop. If lv2
    refuses the SH4 change, the worker gets 1002 (below both); the `up` line
    says which.
  - Start: at the first `snd_push`. Stop: after `sh4_cpu.Run()` returns,
    at `ResetDC` and in `TermAica`; the SH4 then runs what the worker left.
  - Speed trade: the order of SH4 and sound events now depends on the
    worker's lag, so runs are not deterministic. An AICA interrupt reaches
    the SH4 up to the lag + 1 MediumUpdate late.
  - Log: `[sndthr] up/down` and every ~5 s: entries/s, share run by the
    worker and by the SH4, worker busy % of wall, wakes/s (empty), SH4 help
    and wait % of wall, posts/s, `crit` (waits for room), `maxfill`, locks/s
    by reason, irq/s, cdda/s, `miss` (must stay 0). With the option ON,
    `arm:` and `aica:` on the SPEED line show the sound work on whichever
    thread ran it; the SH4's share is `sh4 help` + `wait` on `[sndthr]`.
  - Files: `ps3/ps3_sndthr.{h,cpp}` (new), `plugs/vbaARM/arm_aica.cpp`,
    `plugs/nullAICA/aica.cpp`, `sgc_if.cpp`, `nullAICA.cpp`,
    `dc/mem/sh4_area0.cpp`, `dc/dc.cpp`, `plugs/drkPvr/SPG.cpp`,
    `ps3/ps3_menu.cpp`, `ps3/ps3_presets.{cpp,h}`.
  - PC test, `misc/sndthr_check/` (WSL): the real `ps3_sndthr.cpp` with
    pthreads for the lv2 calls and a fake sound core. The core checks: one
    thread at a time in sound code, every entry once and in order, CD-DA
    sectors in order with the right content (disc data or zeros), GD-ROM
    reads and Holly line changes only on the SH4 thread, `g_snd_on_worker`
    right for the thread. The driver checks after each `snd_lock`: caught
    up, no sound code running, Holly line = the level of the last entry,
    token still held after a nested unlock. Random pacing waits in the core,
    random SH4 blocks, stops and restarts.
    - `run.sh`: ThreadSanitizer 3 x 60000 entries, -O2 8 x 400000: ALL PASS,
      no TSan report. The worker ran ~96% of the entries.
    - `mutate.sh`: 10 of 10 protocol bugs caught (no token, no catch-up in
      snd_lock, no line apply, stop leaves entries, wrong CD-DA slot,
      missing sector, entry queued before its sector, no wait when full,
      nested unlock frees the token, worker line never applied).
  - Build: no warnings. Disassembly: the token CAS ends in `isync`, the
    queue head load is `lwz; cmpw; bne-; isync`, tail and token releases are
    `lwsync; stw`. ELF `.got` 0x9FC8, 0 r2off. Package of 15:18.
  - HW test: Dynarec ON, JIT Fastmem ON, SH4 Clock 150, SPE AICA Sound ON
    (as the A2/A3 runs), AudioBuffers as before.
    1. Crazy Taxi, Sound Thread OFF: 2-3 min of dense traffic (baseline in
       the same build).
    2. Next boot, Sound Thread ON: the same. Pass: `[sndthr] up`, sound and
       music right (no new crackle, no stuck or late notes), `miss=0`,
       `crit` near 0. Read dense VPS/SPEED against step 1, `run by worker`
       %, `sh4 help`/`wait` %, worker busy %, wakes/s, posts/s.
    3. ChuChu Rocket stage, ON: echo and effects as before (F1 is old),
       VPS. 4. Soul Calibur fight, ON: no slowdown against OFF (render-bound;
       the worker shares the render thread's hardware thread).
    5. In-game menu once (SELECT twice): the game and sound continue.
  - HW 2026-10-04, build of 15:18 (log `ps3log_sndthr_run1.txt`, scratchpad
    of session cfef67ac):
    - Boot 1, Sound Thread OFF (baseline): Crazy Taxi, 126 dense seconds:
      VPS 52.9 of 59.95, SPEED 88.3%, 29% at full speed; vt 6-9K / 9-12K /
      12K+ 50.9 / 52.0 / 59.0; `ta` 5.1, `arm` 6.0, `aica` 2.5%; render busy
      40.6%. Matches the A3 run.
    - Boot 2, Sound Thread ON: hung in the BIOS (vbl 27). `[sndthr] up`, the
      SH4 priority change worked, ARM7 self-test PASS, then right after the
      ARMRST write: `Verify Failed: in_s16(d) in e_bc_to` (arm7_jit.cpp).
    - Cause: the ARM7 JIT borrowed the SH4 driver's global `emit_ptr` (its
      `ppc_emit` and helpers are the driver's). On the worker it compiled
      while the SH4 JIT compiled the BIOS, through the same pointer.
    - Fix: arm7_jit.cpp has its own emit pointer: `ppc_emit` and
      `emit_GetCCPtr` map to `arm7_emit`/`arm7_emit_cc` in that file, and
      `ppc_li`, `ppc_call`, `ppc_jump`, `ppc_call_c`, `ppc_addr_high` are
      local copies of the driver's. The emitted words are the same; the boot
      self-test (45 tests) checks it. The ".text section attributes" assembler
      warning of arm7_jit.cpp is old (HEAD gives it too). ELF `.got` 0x9FD0,
      0 r2off, two `emit_ptr` symbols (driver global, arm7_jit local).
      Package of 15:30. HW pending: repeat boot 2.
    - Boot 3 (build of 15:30), Sound Thread ON, Crazy Taxi: runs, ARM7
      self-test PASS, `miss=0`. FAILURE on speed:

      | | dense VPS | SPEED | full-speed s | TA ns/call | render ms/render | render busy |
      |---|---|---|---|---|---|---|
      | OFF (boot 1) | 52.9 | 88.3% | 29% | 149 | 9.50 | 39.7% |
      | ON (boot 3) | 51.6 | 86.0% | 11% | 164 | 12.72 | 58.8% |

      - The worker ran 99.8% of the entries, busy 9.0% of wall; SH4 help
        0.02%, wait 0.01%; 62 posts/s; reg locks 14/s. So the sound work left
        the SH4 thread, but the SH4 thread itself got ~10% slower: the same TA
        store code takes 164 ns per call, not 149.
      - The worker woke 18.6K times/s (5.1K with work, 13.5K empty: the 50 µs
        poll), each time above the render thread's priority: render jobs took
        34% longer (970 -> 1344 ns per vertex), the second hardware thread got
        much busier.
      - Older runs for scale: render-thread load alone barely slows the SH4
        (Crazy Taxi render busy 23% vs 40%: TA 154 vs 149 ns; Soul Calibur
        render 90%: 160-170 ns). The wake pattern and the worker's own work
        cost the SH4 far more per second of activity.
- 5.15 (Matt 2026-10-04, after 5.14 boot 3: "It sounded the same. Let's
  apply 2."): the ARM7, nullAICA's registers and the caique control twin run
  on an SPU (`spe_arm7`). AUDIO -> "SPE ARM7" (`nullDC.cfg [ps3] SpeArm7`),
  default OFF, set before boot, on the `[sndcfg]` line. It needs SPE AICA
  Sound ON (else the log says so and the ARM7 stays on the PPU). Sound Thread
  (5.14) is off while it runs.
  - Why: 5.14 put the same work on the second PPE hardware thread, and SMT
    made the SH4 thread ~10% slower. An SPU shares no core with the SH4.
  - Model:
    - The SH4 thread queues commands in a 4096-entry ring
      (`ps3/ps3_arm_spe.cpp`): each MediumUpdate {SH4 cycles, AICA samples,
      Arm7Batch, ARM7 Speed stage, Idle Skip SAFE}, each SH4 AICA register
      write and read, CD-DA sector slots, ARM7 reset and code flush. It
      stores the new head in a bell line the SPU holds a reservation on.
    - The SPU (`ps3/spu/spe_arm7.cpp`) runs the commands in order, as
      `arm_aica_run` did: the ARM7 slice (Batch rules unchanged), then per
      sample nullAICA's timers, interrupts and CD-DA count, and the control
      twin with its log records for the sound SPU (spe_aica, unchanged).
      After each batch it puts an echo line: commands done, the Holly SPU
      interrupt level, the last read value, stats.
    - Code: the ARM7 is `arm7_spu.h` (B0), nullAICA's register semantics are
      `aica_ctl.h` (B1, byte-exact with `aica_reg`), the twin and the log are
      the PPU part of `ps3_aica_spe.cpp`, moved.
    - An SH4 register read waits for the SPU. Writes do not, except MCIEB,
      MCIPD and MCIRE: these wait, so the Holly line is right before the SH4
      goes on (an interrupt ack). The PPU applies the line at each
      MediumUpdate and after each read.
    - ARAM: the SPU reads through a 32 KB LS cache (128-byte lines) and
      writes through at once (exact-size fenced puts; a cached line gets the
      write too). Each SH4 store to ARAM (`libAICA_WriteMem_aica_ram`, every
      SH4 and DMA path) marks its line; the next command carries the range
      and the SPU drops those lines first. ARM7 reset and ARMRST release
      drop the whole cache.
    - CD-DA and the audio pacing stay on the SH4 thread, at the samples
      nullAICA used. A sector goes to a free AicaShared slot (free once the
      sound SPU applied the CD-DA record that last used it), then a CDDA
      command. Pacing every 64 samples, as `ps3aica_step`.
    - Speed trades, as 5.14: the ARM7 runs behind the SH4 by the SPU's lag.
      It can see an SH4 ARAM store early, and the SH4 sees ARM7 stores
      later; the Holly line reaches the SH4 a few MediumUpdates late. The
      SH4 waits when 256 commands are in flight.
    - Other differences: the SPU ARM7 executes ARAM as it is now
      (arm7_cached keeps decoded code until a reset, so only self-modifying
      code or code changed without ARMRST differ); its block map has 8192
      entries (an eviction can re-decode a block from another start, so
      ticks can differ, see B0). The `[arm7s]`, `[aicarw]`, `[key]` and
      `[montrace]` lines stop (nullAICA's PPU counters do not move);
      `[arm7spe]` has the SPU's counts.
    - SPU LS: 208 KB used (text 75 KB; register file, block map, ARAM cache
      32 KB each; twin 31 KB).
  - Log: `[arm7spe] SPU thread up ... SPU LS used`, and every ~5 s per
    second: commands, ARM7 runs, ticks, FIQs, samples, lag in commands
    (max), log records, checks, monitor reads, ARAM misses and puts, block
    map decodes and evicts, SPU busy %, wakes, SH4 reads, writes and wait %,
    lag waits, log waits and drops, CD-DA underruns. Drops and underruns
    must stay 0. `[aica-spe]` goes on: bad must stay 0.
  - Files: new `ps3/spu/aica_ctl.h`, `spe_arm7.h`, `spe_arm7.cpp`,
    `ps3/ps3_arm_spe.{h,cpp}`; hooks in `arm_aica.cpp`, `arm7.cpp` (reset,
    code flush), `nullAICA_plug.cpp`, `nullAICA.cpp`, `aica.cpp` (`[arm7s]`
    off), `ps3_aica_spe.{cpp,h}` (log addresses, pacing, CD-DA slot),
    `ps3_sndthr.cpp`, `SPG.cpp`, `ps3_menu.cpp`, `ps3_presets.{cpp,h}`,
    `Makefile`.
  - PC tests (WSL `run.sh`; `build_spu.sh` and `mutate.sh` in Git Bash):
    - B0 `misc/arm7spu_check/`: arm7_spu.h against the real `arm7.cpp` +
      `arm7_cached.cpp`, 6 seeds x 400 random programs and the 45
      conformance tests: ALL PASS (8192-entry map: 399 of 400, evictions).
    - B1 `misc/aicactl_check/`: aica_ctl.h against nullAICA's own
      `aica.cpp`, `aica_mem.cpp`, `sgc_if.cpp`, `dsp.cpp` and vbaARM's
      `arm_mem.cpp`, built for the SPU (big-endian) and run in
      `misc/spu_sim`. Random SH4 and ARM7 register traffic and samples;
      after every op: read values, the 32 KB register file, timers, e68k,
      the Holly line, twin calls in order. 8 seeds x 4000 ops ALL PASS;
      `mutate.sh` 14 of 14 caught. The copies get two test entry points
      appended (AICA_Init without sgc_Init's float tables, which the
      simulator cannot run; they only feed nullAICA's own mixer). Odd 16-bit
      register accesses are left out: the SPU build of the reference aligns
      them down, the PPU does not (aica_ctl.h does what the PPU does).
    - B2 `misc/arm7spe_check/`: the spe_arm7 image in `misc/spu_sim`
      against an x86 reference (arm7_spu.h, aica_ctl.h and the caique model
      on flat memory, arm_mem.cpp's ARAM rules, ps3_aica_spe's twin and
      log), fed the same commands: random ARM7 programs that use AICA
      registers and take FIQs, SH4 register traffic, SH4 ARAM stores into
      the running code, ARMRST, resets, flushes, CD-DA. After each batch:
      ARM7 registers and block map, the register file, all 2 MB of ARAM,
      every log record, the echo. 8 seeds x 4000 batches ALL PASS (2 with
      an SH4 write storm and a stalled sound SPU: full log, wraps);
      `mutate.sh` 12 of 12 caught (no ARAM invalidation, no write-update,
      swizzles, echo line, CD-DA, log wrap, Batch, Idle SAFE, ARMRST, read
      size, twin catch-up).
    - The 5.14 check `misc/sndthr_check` still passes.
  - Build: no warnings. ELF `.got` 0xA188, 0 r2off; `cmd_push` is
    `lwsync; stw; stw`. Package of 17:34.
  - HW test: Dynarec ON, JIT Fastmem ON, SH4 Clock 150, SPE AICA Sound ON,
    SPE AICA DSP and AudioBuffers as in the last runs, Sound Thread OFF.
    1. Crazy Taxi, SPE ARM7 OFF: 2-3 min of dense traffic (baseline in this
       build).
    2. Next boot, SPE ARM7 ON: the same. Pass: `[arm7spe] SPU thread up`,
       sound and music right (no new crackle, no stuck or late notes),
       `[aica-spe]` bad=0, log drops and CD-DA underruns 0. Read dense
       VPS/SPEED against step 1, `arm:`/`aica:` on the SPEED line, SPU busy
       %, lag.
    3. ChuChu Rocket stage, ON: sound as before (the echo F1 is old).
    4. Soul Calibur fight, ON: sound right, no slowdown against OFF.
    5. In-game menu once (SELECT twice): the game and sound continue.
  - HW 2026-10-04, package of 17:34 (log `ps3log_sndthr_run1.txt`, scratchpad
    of session cfef67ac). Matt skipped the OFF boot; the baseline is 5.14
    boot 1 (same settings, sound on the SH4 thread).
    - Crazy Taxi, ON, 75 dense seconds: VPS 54.7 (OFF 52.9), SPEED 91.3%
      (88.3%), 20% of dense seconds at full speed (29%; less light-dense
      traffic in this run); `arm` 0.0% (6.0), `aica` 1.1% (2.5); TA store
      157 ns/call (149); render busy 46.6% (39.7), 9.77 ms/render (9.50).
      ARM7 SPU busy 14-16% at 21K commands/s; FIQ 1744/s (as in s3_run1);
      ARAM misses 5-15K/s, puts 55-100K/s. `[aica-spe]` bad=0; no log drops,
      no CD-DA underruns, no silent audio blocks at full speed.
    - Matt: popping in the audio, also at full speed. Likely 6.6's race,
      made larger: at full speed the SPU was 120-180 commands (~8 ms) behind
      the SH4 at the stats point (the cap was 256, ~12 ms; the SH4 waited at
      it up to 32 times/s at boot); in dense driving 0. 5.14 kept its worker
      within ~32 entries and sounded the same as OFF.
    - ChuChu Rocket stage, ON: SPEED 100.0%, VPS 59.9; sound work on the SH4
      thread 1.6% of wall (before 5.15 the control pass alone took 8.9%, A2
      high voices run); ARM7 SPU busy 14-24%, sound SPU up to 33%; bad=0, no
      silent blocks in the stage; the driver's 4-7K monitor reads/s now run
      on the SPU. Matt: the sound is the same as before.
  - Option 1 (Matt 2026-10-04: "You can build option 1 now"): `LAG_MAX` 256
    -> 32 commands (~1.5 ms of guest time, 5.14's limit). At 32 in flight the
    SH4 spins until 16 are left. `max` on the `[arm7spe]` line is now the
    largest real lag found at a check. Build: no warnings, `.got` 0xA188, 0
    r2off; package of 18:18. Plan 6.6 stays the fix for the race itself;
    Matt wants it later. In 5.15 it needs the ARM7 SPU to publish the slots'
    play positions for the SH4's ARAM stores; the ARM7's own stores can be
    checked on that SPU.
  - HW test (option 1): Crazy Taxi, SPE ARM7 ON, settings as above: is the
    popping at full speed (menus with music, light driving) gone or back to
    the old 6.6 crackle? Read `[arm7spe]` lag max (<= 32) and lag-waits, and
    dense VPS against 54.7.
  - HW 2026-10-04, package of 18:18: the popping in the Crazy Taxi main menu
    is still there (Matt). The log: lag max 32, lag 0 at the stats points;
    the SH4 waited 170-530 times/s in the menu (up to 8% of wall); bad=0.
  - A/B boot for 6.6: the log shows `SpeAicaSound=0 SpeArm7=0` (nullAICA
    made the sound, the known clean case), so it did not test 5.15. Matt:
    5.15 causes the popping; look into it.
  - Lead: with the SPU behind the SH4, the two share ARAM out of the PPU
    path's order. The ARM7 can read an SH4 store made later in guest time
    (and the SH4 reads ARM7 state from earlier). Crazy Taxi's ARM7 writes
    ~58K words/s in the menu (`puts`), near the two PCM16 music rings'
    176 KB/s, so the driver likely copies the music from a buffer the SH4
    fills. A lag cap only narrows that window.
  - Diagnostic build, package of 18:41: AUDIO -> "SPE ARM7 Sync"
    (`SpeArm7Sync`, live, default LAG). LAG: as 18:18 (32 commands). ARAM:
    the SH4 also waits for the SPU to finish every queued command before its
    next ARAM load or store after a push. FULL: the SH4 waits after every
    MediumUpdate, the PPU path's order exactly (diagnostic, costs speed).
    The SPU now waits for its ARAM puts before it moves `tail`, so a command
    reported done has its ARM7 stores in memory. `[arm7spe]` adds SH4 ARAM
    loads and stores per second, syncs per second and the mode. B2 still
    passes. No warnings, `.got` 0xA1E8, 0 r2off.
  - HW test (diagnostic): Crazy Taxi, SPE AICA Sound ON, SPE ARM7 ON (both
    before boot). In the main menu, 30 s per mode, switched live in the
    in-game menu: LAG, ARAM, FULL. Which modes pop?
    - FULL clean, LAG pops: the order is the cause. ARAM clean: keep ARAM if
      its wait % is small. ARAM pops: look at the other shared state.
    - FULL pops too: not the order. Next: the PPU path with ARM7 JIT OFF
      (the cached interpreter, which arm7_spu.h copies) against JIT 5.
  - HW 2026-10-04, package of 18:41, Crazy Taxi main menu at SH4 Clock 200:
    ARAM and FULL both pop (Matt). FULL ran the PPU path's order exactly
    (the SH4 waited after each of ~28K MediumUpdates/s, ~22% of wall), so
    the order is not the cause. The SH4 itself writes ~44K words/s to ARAM
    in the menu: it streams the music.
  - A/B boot, SH4 200, SPE AICA Sound ON, SPE ARM7 OFF, ARM7 JIT 5: the
    popping is there too (Matt). So it is SPE AICA Sound's own crackle (6.6),
    not 5.15; the ARM7 JIT 0 boot was not needed.
  - Clean-up (Matt: "let's make these changes first"): `LAG_MAX` back to
    256 (at 32 the SH4 waited up to 8% of wall in light scenes for nothing);
    the "SPE ARM7 Sync" option is gone (a saved `SpeArm7Sync` key is now
    ignored). Kept: the SPU waits for its ARAM puts before it moves `tail`,
    and the lag `max` fix. No warnings, `.got` 0xA188, 0 r2off; package of
    19:03.
- 5.16-5.19 (Matt 2026-10-04: "Anything else we can do to optimize Crazy Taxi
  to reach full speed?"). Dense Crazy Taxi at SH4 150 with SPE ARM7 ON (19:03
  run): VPS 54.8, so ~9% more SH4-thread speed gives 60 VPS. The SH4 never
  waits for the render thread (busy 46.7%). SH4 thread: ~92% recompiled code
  and its C calls, `ta` 6.0%, `aica` 1.1% (5.15's PPU part). Matt: 5.17 and
  5.18 first, in a new session.
- 5.16 SPE Draw check. No build: VIDEO / DEBUG -> "SPE Draw" ON, SPE ARM7
  ON, Crazy Taxi dense traffic, against the 19:03 run. Before 5.15, S3 freed
  the render thread (busy 40 -> 23%) with no VPS change, so expect little.
  Read dense VPS and TA ns/call (155 now, 149 when the render thread was 40%
  busy).
- 5.17 TA store off the SH4 thread.
  - Facts: dense `ta` 6.0% of wall at 384K calls/s, ~155 ns (~500 PPU
    cycles) per 32-byte store-queue block. The path: the guest stores 8 words
    into `sq_both` (JIT stores), PREF -> `do_sqw` (`sh4_cpu_prelude.h`, C
    call, `CCALL(CC_SQW)`, QACR address) -> `TAWriteSQ` (`pvr_if.cpp`, range
    check) -> `libPvr_TaSQ` (`ta.cpp`: two `gettime()` for `TaTicks`,
    `TaCalls++`) -> `ps3ta_store_sq` (`ps3_rend.cpp`: 32-byte memcpy into the
    TA raw buffer, `raw_note_peak`, `spe_note`, one `ta_store_fsm_run` table
    step; a list end takes `ta_store_slow`, which raises the list-end
    interrupt on time). The DMA path (`ps3ta_store_dma`) is the same per
    batch.
  - Suspects for the 500 cycles: (a) load-hit-store: the memcpy reads the 8
    words the guest stored a moment before (the PPE's large LHS penalty, see
    5.0's Cell note); (b) the two mftb of the measurement itself, on every
    call; (c) the call chain; (d) the FSM step.
  - Step 1, measure: sample 1 call in 64 with mftb at entry, after the copy
    and after the FSM; print the split once per stats window (`[ta-split]`).
    Take `TaTicks` from the same samples, so the measurement stops costing 2
    mftb per call.
  - Step 1 built 2026-10-04 (Matt: "Let's start with 5.17 TA"), package of
    19:33. No warnings, `.got` 0xA238, 0 r2off.
    - `do_sqw` counts every SQ flush; about 1 in 64 (random gap 48-79, so
      the sample cannot lock onto a record pattern) is a sample with 5 mftb:
      `do_sqw` entry, `ps3ta_store_sq` entry, after the copy, after the FSM,
      `do_sqw` exit. A 4th and 5th point split the call chain from the copy
      (suspect c against a). Code: `ps3/ps3_tasplit.h`, `ta_split_leave` /
      `ta_split_dump` in `plugs/drkPvr/ta.cpp`, `store_sq<probe>` in
      `ps3_rend.cpp` (the unsampled body is the old one).
    - `libPvr_TaSQ` lost its bracket (2 mftb per block). The stats line's
      `ta%` is now (copy + fsm) of each sample x the calls it stands for,
      plus the DMA brackets (unchanged, one per batch). Disassembly: the
      unsampled path gained ~18 simple instructions (counter step, flag
      test, one more saved register) and lost the 2 mftb.
    - Line, once a second after the stats line: `[ta-split] sq=Nk/s n=A/B
      long=C ns: in I copy P fsm F out O = S mftb M | sq path X% | dma
      calls=D blocks=K Y%`.
      - sq: SQ flushes per second, all targets. n: samples used / taken
        (taken - used - long = flushes to RAM, VRAM or YUV). long: samples
        over 20 us (preempted, or the Cell timebase carry bug), dropped.
      - in: `do_sqw` entry to `ps3ta_store_sq` entry (QACR address,
        `TAWriteSQ`, `libPvr_TaSQ`, 3 calls). copy: memcpy, `raw_note_peak`,
        `spe_note`. fsm: `ta_store_fsm_run`, list ends included. out: the 3
        returns back to `do_sqw`'s exit (`TaCalls++` included).
      - Every segment holds one mftb. mftb: cost of one back-to-back read
        (4096 reads per window). sq path: (S - 4 mftb) x TA flushes/s, % of
        wall, the C side of the SQ path. dma: TA DMA calls, 32-byte blocks
        and bracket time in the window (Crazy Taxi's vt/s is above its TA
        calls/s, so part of its geometry may come by DMA).
      - Not measured: the JIT side (the 8 guest stores into `sq_both` through
        the vmem table, the PREF's call into `do_sqw` and its return).
    - HW test: install the package; set Dynarec ON, JIT Fastmem ON, SPE
      AICA Sound ON, SPE ARM7 ON (an install resets them); leave SH4 Clock
      at 200, the default (Matt's rule 2026-10-04); Crazy Taxi dense
      traffic, ~3 min. Listener: session scratchpad `ps3log_517_run1.txt`.
      Read: the segment split in dense windows and the DMA share. Dense VPS
      has no 200 MHz baseline with these settings (54.8 was at 150).
  - Step 1 HW 2026-10-04, package of 19:33, Crazy Taxi, SH4 200, Dynarec +
    JIT Fastmem, SPE AICA Sound + SPE ARM7 ON, IFB Probe ON (as 19:03).
    153 s, no fault (the XMB exit took the old 3 s fallback). 74 dense
    windows (vt > 3K): SPEED 80.7%, VPS 48.4, RPS 48.1, vt 9.5K, render
    thread busy 44-59%. Script: session scratchpad `split517.py`.
    - SQ flushes 507K/s: 75% go to the TA (~380K/s), 25% to other targets
      (RAM, direct VRAM or YUV; not timed).
    - Per TA flush, raw ns (each segment holds one mftb): in 66.7, copy
      72.9, fsm 90.0, out 81.7 = 311.3. One mftb costs 14.9 ns (~48 PPU
      cycles). Net: in 51.8, copy 58.0, fsm 75.1, out 66.8 = ~252 ns (~800
      cycles). The call chain (in + out, ~119 ns) is 47% of it.
    - sq path: 9.5% of wall in dense windows, 11.4% at vt 12K+. The old
      `ta%` held only copy + fsm; the chain was in the "JIT and its C calls"
      share.
    - DMA: ~146 calls/s, 129K blocks/s (~25% of the TA blocks), 0.67% of
      wall = ~52 ns per block for the same copy + FSM work in one batch. So
      about 200 of the ~252 ns of an SQ flush is per-call overhead.
    - The old bracket's 2 mftb cost ~30 ns per flush (~1.1% of wall); gone.
    - ~150 instructions per flush through 4 calls, ~5 cycles each. Likely
      causes (not measured one by one): SMT with the render thread,
      dependent TOC and global loads, load-hit-store on the stack restores
      (`ta_store_fsm_run` saves r24-r31 and restores them ~30 instructions
      later) and on the PCW read just after the copy stored it.
    - Step 2 proposal = option 1 (2026-10-04: first in the order after the
      Minicast review; build when Matt says): the call-chain fix, as one
      flat TA store function that the JIT's PREF calls directly: the copy
      into the raw buffer, one FSM step on the PCW already in a register,
      the counters; no nested call and no stack frame on the common path.
      Other SQ targets, the MMU path and list ends keep today's code.
  - Step 2 built 2026-10-04 (Matt: "let's do option 1"), package of 20:20.
    No warnings, `.got` 0xA278, 0 r2off.
    - Option VIDEO / DEBUG -> "TA SQ Direct" (`TaSqDirect`), default OFF
      until HW validated, live (`k_live_opts`).
    - `ps3_driver.cpp` is not touched (Matt's JIT work; also its 167 long
      comments): the JIT's PREF already calls `do_sqw_nommu`, which now
      lives in `ps3_rend.cpp`. The `do_sqw` template moved to
      `dc/sh4/sh4_sqw.h`, split into `do_sqw_target` and the probed
      `do_sqw` (interpreters and `do_sqw_mmu`, unchanged).
    - ON, TA polygon flush (area 4 below 8 MB, TA Fast Store ON): one body,
      no stack frame, no call (~85 instructions, one red-zone spill): the
      probe counter, the target test, the census count, the 32-byte copy,
      the peak, one FSM table step on the PCW read from `sq_both` before
      the copy, `TaCalls++`. Rare cases leave by tail calls: a sample
      (`sqw_sampled`), no raw buffer or full (`sqw_flat_general`), an SPE
      batch due or a list open/close (`sqw_finish`), another target or the
      option OFF (`sqw_chain`: census + `do_sqw_target`, the old path).
    - OFF: the Step 1 path, plus the counter step moved to the top and one
      tail call (~4 instructions).
    - Probe: same line plus `| direct=N` (the option at print time). In a
      flat sample, "in" = target test + census, "out" = `TaCalls++`.
    - PC (WSL): `misc/tr4_check/run.sh` now builds in WSL (the docker image
      it named is gone) and walks every stream also with the flat path's
      per-record step (`ta_fsm_entry`, `ta_fsm_step` on TAS_SLOW): PASS,
      200000 streams, 8.39M records, 0 failures. Three mutants (index mask,
      slow step skipped, the next record's PCW) all FAIL. The flat target
      test equals the old address logic on 65536 cases (all QACR pairs x
      dest bits 5, 23-25).
    - For Step 3: the flat body reads the 32 bytes from `sq_both`; when the
      JIT writes the guest's stores straight into the raw buffer, the copy
      goes and the PCW read stays.
    - HW test: settings as Step 1 (SH4 200) plus TA SQ Direct ON; Crazy
      Taxi dense traffic. For a same-session A/B, switch the option in the
      in-game menu (SELECT) every ~60 s of dense driving; each `[ta-split]`
      line carries `direct=`. Read: sum and sq path per flush, `ta%`, dense
      VPS by vt bin, ON against OFF; no missing geometry; list ends still
      on time (no hangs). Log `ps3log_517_run2.txt`, script `split517.py`
      (bins by `direct`).
  - Step 2 HW 2026-10-04, package of 20:20, Crazy Taxi, settings as Step 1
    (SH4 200), TA SQ Direct ON for the whole run (no OFF segments). Matt:
    "No wrong geometry nor hangs. The performance was greater". 183 s; the
    XMB exit took the old 3 s fallback.
    - 70 dense windows: SPEED 84.2% (Step 1 run: 80.7%), VPS 50.5 (48.4),
      `ta%` 5.3 (6.9).
    - Same load, 1K vt bins against the Step 1 run (script `vtbins.py`):
      every bin up, weighted +3.2% VPS; the heaviest bins gain most (10-11K
      +4.0%, 12-13K +7.1%, 14-15K +12.4%), as they have the most flushes.
    - Probe, sampled flat flush (raw ns): in 94.6, copy 68.4, fsm 48.2, out
      33.4 = 244.6 (Step 1: 311.3); sq path 7.2% (9.5%). A sample runs
      `sqw_sampled`, a cold copy of the code (about 1 call in 64: I-cache
      and branch history), so its "in" is inflated. The VPS gain is the
      better measure: about 3-4 points of wall freed.
    - Default still OFF; Matt decides default ON and other games.
  - Step 2, fix by the split:
    - Copy / LHS: when QACR points at the TA (area 4), the JIT writes the
      guest's SQ stores straight into the next 32-byte slot of the TA raw
      buffer, not into `sq_both`. PREF then only advances `raw_used` and runs
      the FSM step on the first word. Other SQ targets keep today's path.
    - Call chain: a direct JIT call to one lean TA store function for TA
      targets (no `do_sqw` / `TAWriteSQ` / `libPvr_TaSQ` hops).
    - FSM: run the store FSM on the SPE that already pulls the raw stream
      (5.12 S1-S3). The SH4 thread raises the list-end interrupts when the
      SPE reports them, a few us late; check games that wait on them.
  - Expected: up to ~5% of wall.
  - Checks: PC: the stored raw stream and the list-end events stay identical
    (FSM equivalence on recorded streams). HW: Crazy Taxi dense VPS and `ta`
    %, no missing geometry; Soul Calibur; one game that feeds the TA by DMA.
  - Step 3 (Minicast M2, after option 1): the JIT side of the SQ path.
    - Facts (code read 2026-10-04): a guest store to the SQ area
      (0xE0000000-0xE3FFFFFF) fails the JitFastmem RAM guard (region 7) and
      runs its cold fragment: the `_vmem_MemInfo_ptr` table load, `slw` +
      `srw` (variable shifts are microcoded on the PPE, 12+ cycles each),
      the `stwx` into `sq_both`, and 2 taken branches (to the cold code and
      back). 507K flushes/s x 4-8 stores = 2-4M stores/s: est. 2-4% of
      wall. Not measured.
    - Minicast (`sq_pref`, option `opt_sqw_on_pref`, default OFF; it comes
      from Reicast's `unstable_opt`): a store whose address register a
      later `pref` in the block uses is taken as an SQ store and goes to
      `sq_both` with no address check. Unsafe: `pref` also prefetches RAM.
    - PS3 form: the guess only picks the shape. An inline check
      (`addr >> 26 == 0x38`), then `stwx` (`stfs` for a pinned FR) into
      `sq_both + (addr & 0x3C)` (pairs: `addr & 0x38`). Any other address,
      and byte or word stores, take today's path, so it is always correct.
    - Option 1 sets the final form: with a flat PREF function, the stores
      can go straight into the TA raw buffer (Step 2's copy / LHS idea),
      which also removes the copy.
    - Checks: PC: the same `sq_both` bytes as the canonical path for every
      address class. HW: Crazy Taxi dense VPS, no missing geometry.
  - Step 3 built 2026-10-04 (Matt: "Do the Step 3 then"), package of 20:49.
    `.got` 0xA2A0, 0 r2off; `ps3_driver.cpp` has its 7 old warnings (the HEAD
    file gives the same 7), no new one.
    - Option JIT SPEED -> "JIT SQ STORES" (`JitSqStore`), default OFF until
      HW validated, takes effect on boot; `[jitcfg]` prints `sqst=`.
    - JitFastmem path only (fastmem OFF stays the baseline). A 32-bit or
      pair store (`flags` 4 or 8) whose base register (`rs1`) a later
      `shop_pref` in the block also uses (`sq_store_guess`) gets
      `emit_sq_store` (`ps3/ps3_jit_sq.h`): `rlwinm r0 = addr >> 26`,
      `cmplwi 0x38`, `bne` cold, then `rlwinm` (`& 0x3C`, pairs `& 0x38`) +
      `addis` + `stw`/`stfs` with the low half of `&sq_both` as the
      displacement: 6-7 instructions, no taken branch, no table, no
      microcoded shift. Any other address takes the cold path, which runs
      the `_vmem` table tier (RAM, VRAM and the SQ too) and the C call, so
      every address stays correct.
    - Not the "final form" above: the SQ keeps its contents after a PREF,
      and a game may rewrite only some words before the next one, so the
      stores still go to `sq_both`; the flat PREF path copies them.
    - `[ta-split]` adds `sqst=N`: store sites given the SQ shape since boot.
    - PC (WSL): `misc/sqstore_check/run.sh` runs the emitted code on a small
      PPC64 model (rlwinm, cmpli, bc, addis, stw, stfs; 64-bit registers
      with a garbage upper half) against the canonical SQ write, for all 4
      shapes x 6 register choices as `ps3_driver.cpp` passes them: PASS,
      4.8M cases (2.23M SQ addresses), 0 failures; also checks that no
      other register changes. Six mutants (compare 0x39, shift 5, second
      word +8, SQ1 bit dropped, no carry in `ha`, branch on EQ) all FAIL.
    - Comment rule: `ps3_driver.cpp` had 167 comment blocks over 2 lines;
      all trimmed to 2 lines (1279 -> ~300 lines). Code identity checked:
      the object file's instructions differ only in 44 `li` line numbers
      (verify/die), and every data section is the same.
    - HW test: settings as Step 2 plus JIT SQ STORES ON (set before boot),
      TA SQ Direct ON; Crazy Taxi dense traffic. Read `sqst=` (> 0, else
      the guess matched nothing), dense VPS by vt bin against the Step 2
      run (`vtbins.py`), no missing or broken geometry. Log
      `ps3log_517_run3.txt`.
  - Step 3 HW 2026-10-04, package of 20:49, Crazy Taxi, settings as Step 2
    plus JIT SQ STORES ON, TA SQ Direct ON for the whole run. 147 s, log
    clean (the XMB exit took the old 3 s fallback). Matt's visual check and
    verdict: pending.
    - `sqst` 55 in the BIOS (its SQ copies to RAM), 229-232 in the game.
    - 68 dense windows: SPEED 86.0% (Step 2: 84.2%, Step 1: 80.7%), VPS
      51.5 (50.5, 48.4), `ta%` 5.9.
    - Same load (1K vt bins): +4.2% VPS against Step 2 (9 of 11 bins up),
      +6.7% against Step 1.
    - Probe: the sampled flush's copy segment rose 68.4 -> 81.6 ns raw in
      every bin. Likely a load-hit-store: the guest's SQ stores now come a
      few instructions before the PREF, so the flat path's loads of
      `sq_both` hit stores still in flight. A lead for a next step.
    - Sonic Adventure, same package and settings, ~12 min: Matt "Sonic
      Adventure looked right". Log clean: in-game SPEED 99.6%, VPS 59.7
      (RPS ~29, the game's 30 fps); `sqst` 7674 (it also copies RAM through
      the SQ: 61% of its SQ flushes go to the TA); the code cache clears on
      the game's code loads, no fault.
- 5.18 JIT hot-block profile, then fixes.
  - Facts: ~92% of the SH4 thread is recompiled code and its C calls. 5.0's
    lesson: removing C calls or interpreter fallbacks did not move SPEED; the
    time is inside the emitted code. On the PPE, features that keep values
    in registers gained +3.5% each (FPU PIN, T FORWARD, CR0 BRANCH).
  - Step 1, profiler: `get_hotblocks_preset()` is hard-wired to 0
    (`ps3/ps3_presets.cpp`). Make it an option (VIDEO / DEBUG "JIT
    HOTBLOCKS", set before boot). The probe exists (`ps3/ps3_driver.cpp`
    `hotblocks_*`): 4 instructions per block entry; once a second the top 8
    blocks by entries with PPC bytes per SH4 op; it dumps the SH4 source and
    the generated PPC of the top block and of each new worst density. It
    counts entries, not time: rank by entries x PPC bytes. Diagnostic only
    (it costs speed).
  - Step 2: Crazy Taxi dense traffic with the profiler; read the dumps of
    the hottest blocks for: SH4 registers stored to the context and loaded
    back (load-hit-store), repeated SR / T / FPSCR syncs, memory accesses
    outside main RAM (out-of-line handlers), block exits and lookups (block
    linking), and the remaining fallbacks (addc / subc / div1: 5.2 JIT Carry
    is broken on the PS3; the fix is in 5.2: zero-extend the operands, take
    the carry from bit 32).
  - Step 3: fix the top pattern first, each behind a JIT SPEED toggle
    (default OFF until HW validated); measure dense VPS after each.
  - Expected: unknown before the profile; most of the SH4 time is here.
  - Notes for the session that does it (2026-10-04 evening):
    - Matt: "5.18 for measurements, and we'll think from there". Build Step
      1, run Step 2, report the ranking; Matt picks what to build next.
    - The JIT now has 5.17 Step 3 (SQ stores), 5.20 (ftrv/fipr), 5.21
      (SHIL passes) and 5.22 (cycle check). Profile with the settings of
      the last runs (section 0), so the dumps show the code that runs.
    - Patterns to count in the dumps, weighted by entries: `mfcr` (set* and
      `tst` results kept in a register, `fcmp`, `cmp/str`; 5.24),
      load-hit-stores (`float` has two, `ftrc`, `flds`/`fsts` one each,
      pinned-FR bounces in memory ops; 5.24), `fmov.s @Rn+` pairs (5.23),
      fastmem shapes per block (7 instructions per access), `ror` by a
      constant (4 instructions for one `rotlwi`), cold paths and C calls.
    - The probe counts block entries, not time: rank by entries x PPC
      bytes, and remember that a microcoded op or a load-hit-store costs
      far more than its bytes.
  - Step 1 built 2026-10-04 (Matt: "We start from 5.18"), package of
    23:13. Build clean (the 7 old warnings), `.got` 0xA438, 0 r2off.
    - Option VIDEO / DEBUG -> "JIT HOTBLOCKS" (`JitHotblocks`), default
      OFF, set before boot; `[jitcfg]` prints `hotblocks=1`. Diagnostic
      only: every block entry pays a 4-instruction counter.
    - The old probe printed the top 8 blocks each second and dumped about
      16 blocks, most of them in the boot menus. It now measures the dense
      gameplay:
      - Entries per block, and cold-path entries per block: a 5-instruction
        counter at the entry of each out-of-line fragment
        (`emit_hot_cold_count`: r0 and rarg1, or rarg2 for a scalar store,
        which are free at every cold entry; nor + neg, so XER[CA] stays).
        Only blocks with <= 128 fragments whose first fragment branch is
        < 16 KB from the cold start count, so the 32 KB branch reach holds.
      - Windows with vt > 3K (the stats line's vertices per render, the
        dense test of all runs) add to per-block totals. Every 10 dense
        windows a `[HOTC]` census: totals, the per-entry average of each
        class, then the top 64 blocks by entries x hot instructions
        (entries/s, share and running share of the weight, sizes, cold
        entries/s, the block's classes).
      - Classes (`ps3/ps3_jit_hot.h`, counted once per block at compile
        time over the hot body, the probe's counter left out):
        instructions, loads, stores, context (r30) accesses,
        load-hit-stores (a load from a context slot that a store of the
        last 24 instructions wrote; `lhs_if` when GPR and FPR data meet),
        `mfcr`, variable shifts and rotates, other microcode (algebraic
        loads, multi-field `mtcrf`, `lmw`/`stmw`), XER moves, C calls,
        link-stub calls, `bctr`, conditional branches and those into the
        cold area, FP arithmetic, FP moves, FPSCR ops, multiply / divide.
        Weighted by entries, they cover every block, not only the dumped
        ones. Not seen: load-hit-stores across blocks or through guest
        memory, and which path of a conditional exit runs.
      - Dumps: from the 5th dense window, the 4 heaviest blocks not dumped
        yet per dense window, up to 128 (by SH4 pc): the SH4 source and
        every PPC word (hot body with the counter, then the cold area).
        The census gives the share of the weight the dumped blocks hold.
      - Lines go out in batches of up to 1400 bytes per printf (one UDP
        datagram each), so a dump is a few datagrams.
      - Table: 32768 blocks (was 16384; ~15K compile after Crazy Taxi's
        last cache clear), ~3.3 MB, allocated only with the option ON. A
        cache clear prints a census first, then resets the totals.
    - PC (WSL): `misc/hot_check/run.sh` tests `hot_classify` on 138
      instructions assembled by the build image's ppu-as (`gen.py` writes
      `hot_words.inc`), on 22 load-hit-store sequences, the probe skip and
      12 branch and `mtcrf` cases; it checks `emit_hot_cold_count` against
      the ppu-as words and runs it on a PPC64 model (7 addresses x 6 start
      values x 4 kinds: the counter gets +1, only r0 and the base register
      change). PASS, 3226 checks. `mutate.py`: 21 of 21 planted bugs fail.
    - HW test steps (Step 2, package of 23:13):
      1. Install the package. Set (an install resets them): EMULATION ->
         Dynarec ON, JIT Fastmem ON, SH4 Clock 200 (default); AUDIO ->
         ARM7 Idle Skip SAFE, SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG
         -> TA SQ Direct ON, IFB Probe ON, JIT C-Calls ON, JIT HOTBLOCKS
         ON; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD
         VALUES ON, JIT CYCLE CHECK LOOPS ONLY (default). These are the
         settings of the reference run `run522b2.txt` plus the two probes.
      2. UDP listener on port 18194.
      3. Crazy Taxi, dense traffic, ~3 min (the profiler costs speed),
         then exit to the XMB.
      4. Check: `[jitcfg]` ... `hotblocks=1`, a `[HOT] table of 32768
         blocks` line, `[HOTC]` censuses, `[HOT] --- dump` blocks.
      5. Analysis: the last census (class totals, top 64), the dumps
         disassembled with ppu-objdump (block address from `host=`), the
         `[CC]` line. Rank patterns by entries x instructions and the PPE
         costs; Matt picks what to build.
  - HW 2026-10-04, package of 23:13, Crazy Taxi, SH4 200, settings as the
    steps. Logs and scripts: scratchpad of session 29bdd1c1.
    - Run 1: JIT HOTBLOCKS was OFF at boot (it was set in the in-game menu,
      which acts at the next boot), so no profile. Its dense VPS 54.9 at vt
      9.7K (66 windows) is the no-probe reference.
    - Run 2: profiler ON, but no listener ran in its first part (the VMU
      session's listener had stopped); only the last census was logged.
    - Run 3 (`run518b.txt`): complete, 147 s, 68 dense windows, 128 dumps
      (70.5% of the weight), log clean (the old XMB 3 s fallback only).
      `hot518.py` disassembles the dumps with the build image's ppu-objdump
      and classifies them again in Python: 0 mismatches against the
      census on 64 blocks. `hot518_deep.py` splits the load-hit-stores by
      a C call between store and load.
    - Probe cost: dense VPS 51.5 at vt 9.5K against 54.9 at vt 9.7K without
      it (~6%). With the probe the code cache reaches 81% (warning lines,
      no clear).
    - JIT code: 10.4M block entries/s x 40.4 hot PPC instructions = 421M/s.
      Per entry: 5.8 loads, 3.2 stores (4.7 of them on the context), 2.4
      guarded RAM accesses (fastmem shape, ~7 instructions each), 2.1 FP
      ops, 0.34 `mfcr`, 0.25 dynamic ends, 0.13 C calls.
    - C calls (`[CC]`, dense): sqw 544K/s, sync_fpscr 158K/s, getcode
      115K/s, writemem 97K/s (a0 51K, a7 23K; a3 25K not from the JIT),
      timeslice 96K/s.
    - Ranking (the probe counts instructions, not time; costs are
      estimates):
      1. Wait loop, 5.25: 412K turns/s, 12.4% of the JIT instructions, and
         0.82M of the 2.6M dynamic ends/s.
      2. Real load-hit-stores ~6M/s (4.46M/s in the dumped 70%), ~40
         cycles each. Another 2.9M/s are the register save and reload
         around C calls (IFB, sync_fpscr), not stalls.
         - T for `bt.s`/`bf.s` 1.9M/s: the compare stores T, and `jcond`
           reloads it into r31 a few instructions later. New item.
         - FPUL 0.6M/s and `jit_scratch` 0.57M/s: `lds`, `float`, `flds`,
           `fsts` through memory (5.24).
         - Pinned FR values moved through the context ~1M/s; FPSCR 0.22M/s.
      3. Dynamic ends (`rts`, `jsr @Rn`, `braf`) 2.6M/s, each ~15
         instructions and an indirect branch: DYN_IC (5.3, forced OFF on
         the PS3).
      4. `mfcr` 3.5M/s, always for a set*/tst result: 5.24 (`mfocrf`).
      5. Store-queue stores that JIT SQ STORES misses: 0.88M slow-path
         entries/s, mostly `fmov.s FRm,@-Rn` in 0C081D58, 0C081D5A and
         0C0820C2, whose PREF is in another block. The table tier runs 2
         microcoded shifts. Fix: test for the store queue first in the
         slow path, or a wider guess.
      6. sync_fpscr full path 158K/s: a C call with 16 FPR stores and 16
         FPR loads around it.
      7. Small: variable shifts in hot code 0.45M/s, IFB addc/subc/div1
         ~50K/s.
    - Against `OPTIMIZATION_POTENTIAL.md`: its C-call rates match (timeslice,
      getcode, area-0 writes). Its section 4 says idle skip has nothing to
      take; item 1 shows otherwise. It does not list sync_fpscr.
- 5.19 SH4 Clock below 150. The option allows 150-200 MHz (`ps3_menu.cpp`).
  A lower floor (e.g. 120-140) buys VPS, but the guest gets fewer cycles per
  frame, so heavy scenes drop more of the game's own frames (already at 150:
  dense 46.6 fps at 54.8 VPS). Last resort, Matt's call.
- 5.20-5.24 Minicast JIT review (2026-10-04, Matt: "Can you investigate the
  JIT optimizations that Minicast has? ... I'd like to port them"). Code
  read only, nothing built. Minicast
  (`E:\Users\Matteo\Desktop\Dreamcast\minicast`) is a Reicast fork for the
  MiSTer (ARM Cortex-A9). skmp's July 2026 JIT work is in
  `libswirl/hw/sh4/dyna/shil.cpp`, `regalloc.h` and
  `jit/backend/arm32/rec_arm.cpp`; `libswirl.cpp` `InitSettings` has the
  defaults. Our rec_v2 has the same nullDC base, so the SHIL passes map to
  `dc/sh4/rec_v2/shil.cpp` (`AnalyseBlock`). Our backend has none of them.
  - Item map: M1 = 5.20, M2 = 5.17 Step 3, M3 + M4 = 5.21, M5 = 5.22,
    M6 = 5.23. Not taken (section 4): M7 fallthrough, the register
    allocator work, XMTRX kept in registers, the Linux perf map.
  - PPE facts used below: `fmadds` has a 10-cycle latency and issues one
    per cycle (CellPerformance); variable shifts (`slw`/`srw`/`sraw`) are
    microcoded, 12+ cycles (cbloom, Per Vognsen); `mfcr` over all fields
    is microcoded, `mfocrf` for one field is fast; a load-hit-store costs
    ~40+ cycles.
- 5.20 (M1) ftrv with known w + interleaved MAC chains.
  - Minicast (`ftrv_known_w`, option `opt_fipr_w`, default ON; commits
    ca2109a, 19eafde): the `fldi0`/`fldi1` that sets w just before an
    in-place `ftrv` is dropped, and the ftrv gets `FTRV_W_ZERO` /
    `FTRV_W_ONE`. The emitter skips the fourth column for w=0 and uses a
    plain add for w=1. It runs the 4 output sums side by side, column by
    column.
  - Ours (`ps3/ps3_driver.cpp` `shop_ftrv`): out[0] is finished before
    out[1] starts, so the 16 FP ops form one dependent chain, ~160 cycles;
    the input vector is copied with 4 `fmr`. Side by side: ~40-50 cycles.
    Each `fldi` is `li` + `stw` + `lfs` on the same slot (`ppc_sh_store`
    with FPU PIN) = one load-hit-store.
  - PS3 form: 4 interleaved chains that write the pinned outputs (copy v1-v3
    aside first, write v0 last); the matrix columns load under the previous
    column's FMAs (xf is not pinned). `fldi` loads its constant with `lfs`
    from a 2-entry table {0.0f, 1.0f}. `fipr`: read the pinned operands in
    place, no `fmr` copies.
  - Results: the interleave is bit-exact (each sum keeps its order). w=1:
    `fadds` equals `fmadds` with 1.0 (m*1.0 is exact). w=0: skipping the
    column differs only for -0 and NaN/Inf matrix entries.
  - Expected: est. 1-3% of wall in dense Crazy Taxi, if most vertices pass
    one ftrv; more if the game also transforms normals. Not measured.
  - Files: `ps3/ps3_driver.cpp` (`shop_ftrv`, `shop_fipr`, float immediate
    in `shop_mov32`), `dc/sh4/rec_v2/shil.cpp` (pass), `shil.h` (flags),
    toggle in `ps3/ps3_presets.*` + `ps3/ps3_menu.cpp` (JIT SPEED, set
    before boot).
  - Checks: PC: random vectors and matrices through the old and new emit
    order (bit-exact for the interleave). HW: Crazy Taxi dense VPS, no
    geometry fault; Soul Calibur.
  - Built 2026-10-04 (Matt: "While you continue the plan I'll try Sonic
    Adventure"), package of 21:05. `.got` 0xA2B0, 0 r2off, no new warning.
    - Option JIT SPEED -> "JIT FTRV" (`JitFtrv`), default OFF until HW
      validated, takes effect on boot; `[jitcfg]` prints `ftrv=`.
    - The PS3 form only, all bit-exact (`ps3/ps3_jit_fv.h`):
      - ftrv: v1-v3 copied aside (3 `fmr`), then 4 columns of 4 `lfs` + 4
        independent `fmuls`/`fmadds` straight into the pinned outputs, v0's
        sum last. Was 4 `fmr` in, 4 dependent chains of 4, 4 `fmr` stash and
        4 `fmr` out. Not pinned (FPU PIN OFF): the same order in f4-f7,
        then `stfs`. Used only when the vector is updated in place and
        XMTRX is the unpinned, contiguous xf bank (`ftrv_v2_ok`).
      - fipr: pinned operands read in place (was 8 `fmr`); the sum stays in
        f0, as rd is the first vector's fourth element.
      - fldi0 / fldi1 into a pinned FR: one `lfs` from a 2-entry table (was
        `li` + `stw` + `lfs` on the same slot: a load-hit-store).
    - Not built: Minicast's known-w pass (drop the fldi, skip or add the 4th
      column). With the table fldi it would save one instruction for w=1,
      and for w=0 it is not exact (-0, NaN/Inf matrix entries).
    - PC (WSL): `misc/fv_check/run.sh` runs the emitted code on a PPC float
      model (lfs, stfs, fmr, fmuls, fmadds with PPC NaN precedence A, B, C)
      against the old order: PASS, 800000 cases (pinned, memory and mixed
      vectors; fipr with FVm == FVn), 0 failures; it also checks that no
      other FPR or context word changes. Six mutants (fmuls roles, v0
      written first, no v1-v3 copies, matrix transposed, fipr roles, fipr
      writing an input) all FAIL.
    - HW test: settings as the 5.17 Step 3 run plus JIT FTRV ON (set before
      boot); Crazy Taxi dense traffic: dense VPS by vt bin against the Step
      3 run (`vtbins.py`), no geometry fault; then Soul Calibur.
  - HW 2026-10-04, package of 21:05, Crazy Taxi, settings as the 5.17 Step
    3 run plus JIT FTRV ON (`ps3log_517_run3.txt`, boot 3). Log clean (the
    XMB exit took the old 3 s fallback). Matt's visual check: pending.
    - 65 dense windows: SPEED 89.2% (Step 3 run: 86.0%), VPS 53.5 (51.5).
    - Same load (1K vt bins): +3.8% VPS against the Step 3 run (10 of 12
      bins up), +8.3% against Step 2, +11.1% against Step 1.
    - Soul Calibur, same package, all three options ON (`run3`, boot 4),
      78 s: log clean (the old XMB fallback only), `sqst` 703. The fight
      stays render-bound, as before: VPS ~28 of 50, RPS ~9-10, render
      thread busy 81-97% with texture decode 69-85% of wall, the SH4 waiting
      ~1.6 s per 5 s window. So no speed reading here; Matt's visual check
      pending (its old faults, flicker and one black texture, are parked).
- 5.21 (M3 + M4) constant propagation + dead value elimination.
  - Minicast (`constprop`, option `opt_constprop` 1 = safe, the default;
    commits 6e0fa3c, 96ebdba, 67e5949): a forward walk tracks constant
    r0-r15 and folds ALU ops. It moves constants into immediate slots,
    turns `shld`/`shad` by a constant into `shl`/`shr`/`sar`, merges shift
    chains (`shll16` + `shll8`), gives a `readm` with a constant base the
    immediate-address form, turns a `jdyn` with a constant target into a
    static end, and drops a `pref` to a non-SQ constant. Constants are
    written back before an op that needs the register and at block end.
    Level 2 also bakes memory reads; that needs SMC page locks (Reicast
    mprotect), so not on the PS3. This is a new pass, not Reicast's old
    `constprop` ("crashes on ip", `Optimization.md` 2.4).
    `dead_value_elim` (commit 8b558b7, always on): backward liveness; it
    removes pure ops whose outputs are overwritten before any read, and
    takes everything as live at block end.
  - PS3 value: medium, unknown until 5.18 profiles the hot code. Main gain:
    `shld`/`shad` by a constant (e.g. `mov #-5,r1; shad r1,r0`) becomes one
    `rlwinm`/`srawi`; today it is ~14 instructions with 2 microcoded
    shifts. A constant address skips the 7-instruction fastmem shape.
    Section 4 lists Flycast's SSA pass as "do not take" (JIT work measured
    at 0-3%); this pass is smaller and aims at the PPE microcode cost.
  - PS3 rules: `rs3` immediates must fit s16 (`shop_readm` / `shop_writem`
    verify it). Promote constant addresses only for integer destinations of
    1, 2 or 4 bytes (a float destination would bounce through memory).
    Keep the block's last compare as the last op (block-end write-backs go
    before it), or JIT CR0 and T FORWARD stop working. `ifb` writes back
    r0-r15, `sync_sr` writes back r0-r7. Dead value elimination must see
    our hidden operands: `div1` reads and writes SR.Q/M (`sr_status`),
    `mac_l`/`mac_w` read MACL/MACH; `ifb`, `sync_sr` and `sync_fpscr` are
    barriers.
  - PC test first: port Minicast's `tests/constprop_test.cpp` (backend-form
    validator, directed tests, differential fuzzer: random blocks run with
    `shil_canonical.h` before and after the passes; registers, the memory
    write trace and the exit target must match) to WSL as
    `misc/constprop_check/`, with a PPC-form validator.
  - Files: `dc/sh4/rec_v2/shil.cpp`, `driver.cpp`, `decoder.h` (static end
    from `jdyn`), `ps3/ps3_driver.cpp` (any immediate form it lacks),
    toggle.
  - Built 2026-10-04 (Matt: "It's time for 5.21"), package of 22:06. Build
    clean (the 7 old warnings only), `.got` 0xA328, 0 r2off.
    - Two options, JIT SPEED page, both default OFF, set before boot:
      "JIT CONSTPROP" (`JitConstProp`) and "JIT DEAD VALUES" (`JitDve`).
      `[jitcfg]` prints `cprop=` and `dve=`. While one is ON, a `[cprop]`
      line per stats window (only after new compiles) gives the totals since
      boot: blocks, SHIL ops in -> out, and the count of each change (fold,
      imm, shift, chain, readm, jdyn, pref, late, dve).
    - The passes are in the new `dc/sh4/rec_v2/shil_opt.h`; `AnalyseBlock`
      (`shil.cpp`) runs them. No `driver.cpp`, `decoder.h` or backend change
      was necessary: every form the pass makes already exists in
      `ps3_driver.cpp`.
    - Constant propagation (Minicast's safe level, no memory read at compile
      time): it folds ALU ops, compares, `ext`, `not`, `neg`, `swap.b`,
      `cmp/str` and the 16/32-bit multiplies on known r0-r15. A known
      operand goes to the immediate slot the backend has (rs2 of
      add/sub/and/or/xor/shifts/mul/test/set*; commutative ops and `seteq`/
      `test` swap a known rs1 there). `shld`/`shad` by a known count become
      `shl`/`shr`/`sar` (or 0). Shift chains on one register merge. A
      `readm` with a known address takes the immediate-address form
      (addis + load) for integer destinations of 1, 2 and 4 bytes; a known
      r0 index becomes an s16 displacement. A `jdyn` to a known target
      becomes a static end (not with JitSafe / JitDynEnds). A `pref` to a
      known non-SQ address goes away. Pending constants are written back
      before `ifb` (r0-r15), `sync_sr` (r0-r7), any op that reads them as a
      register, and at block end; on a BET_Cond block the end write-backs go
      before the last op when it only writes sr_T and reads none of them
      (JIT CR0 / T FORWARD), else after it (`late`).
    - Dead value elimination: backward liveness, everything live at block
      end; pure register ops whose results are all overwritten before a read
      go. `ifb`, `sync_sr`, `sync_fpscr` are barriers; `div1` reads
      `sr_status`, `mac.l`/`mac.w` read MACH:MACL.
    - One fix to Minicast's pass found on the way: a full rotation (`swap.w`
      twice) re-read the first `ror`'s source through the constant state,
      which is wrong when that register got a new constant in between.
      Ours moves the value the emitted code still holds.
    - The old disabled write-after-write pass (`SHIL_DCE 0`) stays as it was.
  - PC test 2026-10-04 (WSL): `misc/constprop_check/run.sh [seed] [blocks]`
    compiles the real `shil_opt.h`, `shil.h` and `decoder.h` (no mocks). It
    has a SHIL interpreter of our ops (incl. adc/sbc/negc/addv/subv/div1/
    cmpstr/mac, float and vector ops as data moves), a validator of
    `ps3_driver.cpp`'s operand forms, 16 directed tests and a fuzzer of the
    decoder's shapes (all block ends, delay slots, ifb, sync_sr/fpscr,
    division, mac, tas.b, stc.l SR, GBR, PC-relative, float pairs). Each
    block runs before and after the passes from 3 start states; registers,
    banks, the memory read and write trace, the ifb / sync checkpoints and
    the exit must match. PASS: 8 seeds x 200000 blocks, 0 failures (88% of
    blocks get smaller; every transform fires).
    `misc/constprop_check/mutate.py` plants 31 bugs one at a time: all 31
    FAIL, 30 also with the fuzzer alone (the one left, `mov32` of any float
    constant, has no decoder shape; a directed test catches it).
  - HW test steps (package of 22:06):
    1. Settings as the 5.20 run (an install resets them): Dynarec ON, JIT
       Fastmem ON, SH4 Clock 200, VIDEO / DEBUG -> TA SQ Direct ON, JIT
       SPEED -> JIT SQ STORES ON, JIT FTRV ON. Then JIT SPEED -> JIT
       CONSTPROP ON and JIT DEAD VALUES ON. Reboot the emulator.
    2. UDP listener on port 18194. The listener of the 5.17 session (PID
       5656) still runs and appends to its `ps3log_517_run3.txt`; this run
       starts after byte 2548073.
    3. Crazy Taxi, dense traffic, as long as the 5.20 run. Check: no hang,
       no wrong geometry, sound as before; the log has `cprop=1 dve=1` and
       `[cprop]` lines.
    4. Analysis: same-load VPS against the 5.20 run (`vtbins.py`), the
       `[cprop]` totals.
    5. On a fault: JIT DEAD VALUES OFF first, then JIT CONSTPROP OFF, one
       boot each.
  - HW 2026-10-04, package of 22:06, Crazy Taxi, 149 s, settings as the
    5.20 run plus both options ON (`[jitcfg]` ... `cprop=1 dve=1`). Log
    clean (the old XMB 3 s fallback only). Log: this session's scratchpad,
    `run521.txt`. Matt: "geometry and sound looked right".
    - `[cprop]` at the end: 20934 blocks, SHIL ops 155978 -> 151379
      (-2.9%); fold 741, imm 5231, shift 169, chain 421, readm 62, jdyn 4,
      pref 1, late 16, dve 714.
    - 66 dense windows (vt > 3K): SPEED 90.4% (5.20 run: 89.2%), VPS 54.2
      (53.5), but at a lower mean load (vt 9.0K against 9.5K).
    - Same load (1K vt bins, `vtbins.py` against the 5.20 run): +0.2% VPS,
      within noise (bins 8-11K +0.4 to +1.5%, 7-8K -1.4%). No measurable
      gain in Crazy Taxi: the ops it removes are mostly single `li`/`mr`.
- 5.22 (M5) cycle check.
  - Minicast (`opt_cyclecheck_backwards_only`, default OFF; commit
    c524f7a): only blocks with a backward static branch poll; the others
    only subtract. It also skips the poll on dynamic ends, so a loop closed
    only by `jmp`/`rts` would never poll. Do not copy that part.
  - Ours (`ngen_Begin`): every block runs `addic.` and a `bc` that is taken
    in the common case (it jumps over the exit `li` + `b`).
  - PS3 form, two steps: (a) put the exit out of line, so the common path
    falls through (every block); (b) forward static ends (static jump,
    static call, cond with a forward branch target) use `addi` only;
    dynamic, interrupt and backward ends keep the poll.
  - Expected: low, unknown (one taken branch per block entry).
  - Checks: HW: Crazy Taxi dense VPS; a game with tight wait loops boots;
    the in-game menu (SELECT) and the XMB exit still work.
  - Note from 5.21: JIT CONSTPROP can turn a `jdyn` into a static end
    (Minicast marks these `static_from_constprop`, so its backward-only
    check still polls them). Decide forward / backward from the target
    address, not from the end type.
  - Built 2026-10-04 (Matt: "Yes, do it"), package of 22:27, both steps
    behind one option: JIT SPEED -> "JIT CYCLE CHECK" (`JitCycleCheck`),
    set before boot: OLD (default, the old shape), COLD EXIT (step a),
    LOOPS ONLY (steps a + b). `[jitcfg]` prints `cyc=`; with COLD EXIT or
    LOOPS ONLY a `[cyc]` line (per stats window, after new compiles) gives
    the blocks compiled with and without the poll since boot.
    - COLD EXIT: `addic.` + a not-taken `blt` to the exit (`li next_pc` +
      `b loop_do_update_write`), which ngen_Compile places first in the
      block's cold area (verify: within the 16-bit reach).
    - LOOPS ONLY: a block polls only if its end can close a loop: a static
      jump, static call or conditional end whose BranchBlock is at or below
      its start, or any dynamic or interrupt end; the others only `addi`.
      NextBlock is always above the start, and a 5.21 static end is judged
      by its address like any other. Along blocks without the poll the start
      address only rises, so every loop polls once per turn; an exit can
      come up to one forward chain later (the cycle count stays exact, and
      the update loop runs back to back to catch up).
    - No PC test: the plan lists HW checks only; the shapes reuse emitter
      forms the JIT already runs (`blt`, `addi` with a negative immediate,
      MarkLabel as in FlushCold).
  - HW test steps (package of 22:27):
    1. Settings as the 5.21 run (5.20 settings plus JIT CONSTPROP and JIT
       DEAD VALUES ON), so only the new option changes.
    2. Boot A: JIT CYCLE CHECK -> COLD EXIT. Crazy Taxi, dense traffic,
       ~150 s. Open and close the in-game menu (SELECT) once. Exit to the
       XMB.
    3. Boot B: JIT CYCLE CHECK -> LOOPS ONLY. The same.
    4. Check: boot to the game (BIOS and loading screens are wait loops),
       no hang, menu and XMB exit work, `[jitcfg]` `cyc=1` / `cyc=2`.
    5. Analysis: same-load VPS of each boot against the 5.21 run
       (`vtbins.py`), the `[cyc]` counts.
    The 5.17 listener (PID 5656) still appends to `ps3log_517_run3.txt`;
    this run starts after byte 2848377.
  - HW 2026-10-04, package of 22:27, Crazy Taxi, settings as the 5.21 run.
    Logs in this session's scratchpad: `run522a.txt` (boot A),
    `run522b2.txt` (boot B; a first boot B, `run522b.txt`, ended before the
    gameplay by mistake). Both logs clean (the old XMB 3 s fallback only),
    the same boot sequence as the 5.21 run (intro, ~33 s without renders,
    then gameplay).
    - Boot A, COLD EXIT (`cyc=1`), 144 s: 64 dense windows, VPS 54.6 at
      SPEED 91.1% (mean vt 9.3K). Same load: +0.8% against the 5.21 run.
    - Boot B, LOOPS ONLY (`cyc=2`), 143 s: `[cyc]` poll=9962 nopoll=11085
      (53% of the blocks skip the poll). 64 dense windows, VPS 54.2 at SPEED
      90.5% (mean vt 9.6K). Same load: +1.9% against the 5.21 run (bins
      7-10K +1.8 to +4.1%), +0.9% against boot A.
    - Since the 5.17 Step 1 run (VPS 48.4 at vt 9.5K): +13.5% at the same
      load.
    - Matt: "boot B played normally". Then (Matt: "Set LOOPS only as
      default"): `JitCycleCheck` defaults to 2 (LOOPS ONLY), package of
      22:44.
- 5.23 (M6) `fmov.s @Rn+` pair fusion. Minicast `fuse_readm_pairs`
  (option `opt_readm_pairs`, default OFF): `readm f; add rn,4; readm f`
  becomes one paired load. On our fastmem path it saves one guard + address
  (~6 instructions) per pair. The second word can cross the 16 MB RAM end
  (our window has no mirror past it). Many games use `fschg` + 64-bit moves
  instead. Not scheduled.
- 5.24 PPE codegen seen during the review (not Minicast). Built and PC
  tested 2026-10-05 (Matt: "Go with 5.24 in the Optimization plan"), HW
  tested the same day (Crazy Taxi, Sonic Adventure), default ON since the
  package of 13:49 (Matt).
  - Compares read CR0 with `mfcr` (`emit_cr0_bit_to_rarg0`): `set*` and
    `tst` when the block-end CR0 path does not apply, `fseteq`/`fsetgt`
    always. `mfcr` is microcoded over all fields; `mfocrf rD,0x80` reads
    CR0 fast.
  - `flds`/`fsts` (FPUL <-> FR), `float` and `ftrc` go through memory: one
    load-hit-store each. `float` has two, because it also builds its magic
    constant in the same slot; a constant table removes that one.
  - Facts (5.18 run 3, `run518b.txt` and `hot518b/report.txt` in the
    scratchpad of session 29bdd1c1):
    - Census (all blocks): `mfcr` 3.52M/s, each followed by the `rlwinm`
      of `emit_cr0_bit_to_rarg0`. Load-hit-stores by store -> load pair:
      stfs -> lwz 0.525M/s and stw -> lfs 0.523M/s (pinned FR values through
      their own slot), stw -> lfd 0.444M/s (`float`'s two loads of
      jit_scratch), stfd -> lwz 0.123M/s (`ftrc`).
    - The dumps (70.5% of the weight; entries/s at dump time, which run
      above the census): fsts 0.49M/s (13 sites), flds 0.44M/s (14), float
      0.17M/s (2, both in 0C0759C8), ftrc 0.11M/s (8), fcmp/gt 0.59M/s
      (17), lds 0.24M/s, sts 0.19M/s.
    - The old code in the dumps: flds FR15,FPUL in 0C081B9A is `stfs
      f29,124(r30); lwz r3,124(r30); stw r3,268(r30)`; fsts FPUL,FR2 in
      0C081BAA is `lwz r3,268(r30); stw r3,72(r30); lfs f16,72(r30)`; ftrc
      in 0C081D40 is `fctiwz f0,f25; stfd f0,128(r30); lwz r3,132(r30); stw
      r3,268(r30)`, and the `sts FPUL,R0` after it reloads 268(r30).
  - Built 2026-10-05, package of 13:01. Two options, JIT SPEED page, both
    default OFF until 13:49, set before boot; `[jitcfg]` prints `mfocrf=`
    and `fpul=`.
    A `[ppe] sites:` line (after new compiles) counts the compiled sites of
    each new form since boot. Code in `ps3/ps3_jit_ppe.h`.
    - "JIT MFOCRF" (`JitMfocrf`): `emit_cr0_bit_to_rarg0` emits `mfocrf
      rarg0,0x80` (CR0 only) in place of `mfcr rarg0`. The `rlwinm` after it
      keeps only the CR0 bit, so the fields mfocrf leaves undefined do not
      matter.
    - "JIT FPUL MOVES" (`JitFpulMoves`):
      - flds FRm,FPUL with FRm pinned: one `stfs fRm,fpul(r30)`.
      - fsts FPUL,FRn with FRn pinned: one `lfs fRn,fpul(r30)`.
      - ftrc: `fctiwz f0,fA; li r3,fpul; stfiwx f0,r30,r3` (no jit_scratch
        bounce).
      - float: the subtrahend 2^52 + 2^31 comes from `s_i2f_bias` (`lis` +
        `lfd`, before the value's stores), not built in jit_scratch.
      - The direct forms need FPU PIN for flds/fsts, and an integer side
        that is not pinned and, for flds and ftrc, not sr_T (its store also
        feeds JIT T COPY). Anything else keeps the old path.
    - Bit-exact: the same stfs, lfs, fctiwz and frsp as before, only fewer
      copies. What stays: the FPUL slot's own load-hit-stores (lds ->
      float, ftrc or flds -> sts: the PPE has no GPR <-> FPR move) and
      `float`'s first one.
  - PC (WSL): `misc/ppe_check/run.sh [cases]`.
    - Words against the build image's ppu-as (`gen.py` writes
      `ppe_words.inc`): 68 words, both forms of each op; the bias at three
      addresses (low half positive, negative and 0x7FF8).
    - A PPC64 model with bit-exact lfs / stfs (Power ISA rules, SNaN
      payloads and denormals) and fctiwz; the bits mfocrf and fctiwz leave
      undefined are random. Each new form runs against the old one from
      the same random state: equal results, and the new form changes
      nothing else (no other GPR, FPR, CR or context word). flds and fsts
      on random and special singles, ftrc on NaN, infinity, the s32 edges,
      float on random s32 with junk in the upper word. PASS, 1000069
      checks.
    - `mutate.py`: 22 of 22 planted bugs fail; 20 also on the model alone
      (the other two use another instruction for the same result).
  - Build: the 7 old warnings, `.got` 0xA570, 0 `r2off`; `s_i2f_bias` at
    0x1000DB80.
  - Expected: not measured. 3.5M/s microcoded `mfcr` and ~1.2M/s GPR <->
    FPR load-hit-stores go. 5.26 removed ~1.9M/s sr_T reloads with no
    measurable gain, so this may be within the noise too.
  - HW test steps (package of 13:01):
    1. Install. Settings as `run526a.txt` (an install resets them):
       EMULATION -> Dynarec ON, JIT Fastmem ON, SH4 Clock 200; AUDIO ->
       ARM7 Idle Skip SAFE, SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG
       -> TA SQ Direct ON, IFB Probe ON, JIT C-Calls ON, JIT HOTBLOCKS
       OFF; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD
       VALUES ON (JIT CYCLE CHECK LOOPS ONLY, JIT IDLE SKIP ON, JIT T COPY
       ON and JIT SQ MISS OFF are the defaults), then JIT MFOCRF ON and JIT
       FPUL MOVES ON. Set them on the main menu, not with SELECT (SELECT
       changes act at the next boot).
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: Crazy Taxi, dense traffic, ~150 s, then exit to the XMB.
       Log: `[jitcfg] ... mfocrf=1 fpul=1`; `[ppe] sites:` with all five
       counts above 0.
    4. Analysis: same-load VPS against `run526a.txt` (scratchpad of session
       3f596c0b) with `vtbins.py`.
    5. Boot 2: Sonic Adventure, a few minutes: no hang, game speed and
       music as before.
    6. On a fault: JIT FPUL MOVES OFF first, then JIT MFOCRF OFF, one boot
       each.
  - HW 2026-10-05, boot 1 (package of 13:01; Matt: "Gonna test Crazy Taxi
    first"), Crazy Taxi, 144 stats windows. The boot had JIT SQ MISS = +
    LOOK-AHEAD (`sqmiss=2`), not OFF as step 1 says, so the reference with
    the same settings is `run527b.txt` (5.27 boot 2), not `run526a.txt`. Log
    `run524a.txt` in the scratchpad of session fc4e881b, cut from the 5.27
    listener's `ps3log_527_run1.txt` after byte 2623117. Log clean: the same
    line tags as `run527b.txt` plus `[ppe]`; `[tcopy]` miss=0; the XMB exit
    took the old 3 s fallback.
    - `[jitcfg]` ... `sqmiss=2 mfocrf=1 fpul=1`. `[ppe]` at the end:
      mfocrf=2928 flds=61 fsts=102 ftrc=198 float=343 sites.

    | Run | dense n | VPS | SPEED | vt | full speed | 12K+ VPS (n, vt) |
    |---|---|---|---|---|---|---|
    | `run526a` (JIT SQ MISS OFF) | 62 | 57.1 | 95.2% | 9.3K | 32% | 48.2 (4, 14.7K) |
    | `run527b` (+ LOOK-AHEAD) | 61 | 57.6 | 96.1% | 9.5K | 41% | 54.8 (10, 13.4K) |
    | `run524a` (+ LOOK-AHEAD, 5.24 ON) | 61 | 58.4 | 97.4% | 9.2K | 51% | 54.7 (6, 14.0K) |

    - Same load (`vtbins.py`): +1.2% against `run527b.txt` (bins 9-10K
      +3.6%, 10-11K +4.0%, 6-9K -0.5% to 0, 12K+ +0.8% to +1.4%); +2.0%
      against `run526a.txt` (5.27 and 5.24 together). +1.2% is about the
      noise of one run.
    - Dense audio: 4.6 silent blocks/s (`run527b.txt`: 7.7), output ~47.8K
      samples/s.
  - HW 2026-10-05, boot 2 (Matt: "Doing the Sonic run"): Sonic Adventure
    (PAL disc), same settings (`sqmiss=2 mfocrf=1 fpul=1`), 233 s. Log
    `run524b.txt` (same scratchpad, the listener file after byte 3250802).
    Log clean: the same line tags as `run527c.txt` (5.27 boot 3, the same
    settings without 5.24) plus `[ppe]`; `[tcopy]` miss=0; the old XMB 3 s
    fallback. `[ppe]` at the end: mfocrf=15625 flds=12 fsts=349 ftrc=1103
    float=1799 sites.
    - NTSC windows with vt > 3K (`sonicbins.py`): 167, SPEED 99.7%, full
      speed 95%, 30 frames/s in 89%. `run527c.txt`: 206, 99.2%, 92%, 89%;
      `run526b.txt` (no 5.27, no 5.24): 163, 99.6%, 94%, 90%. Equal within
      noise; the windows below 30 frames/s are the heavy scenes as before.
    - Code size about as `run527c.txt` (the same early clears within
      -0.9% to +0.2%). Cache-full clears: 3 in 233 s (t=95, 153, 203);
      the windows after them ran at 96.6%, 86.8% and 97.6% SPEED (5.27
      boot 3: 91.7%, 98.0%, 72.8%, 64.4%). Dense audio: 0.5 silent
      blocks/s (`run527c.txt`: 1.5).
  - Result: correct in Crazy Taxi and Sonic Adventure. Crazy Taxi
    same-load VPS +1.2% (about the noise of one run).
  - Matt 2026-10-05: "I want them ON by default". `JitMfocrf` and
    `JitFpulMoves` default to 1 since the package of 13:49 (an install resets
    the settings to them). The listener of session 468f5b68 (PID 20640) was stopped (Matt:
    "Also close the listener"). Since 13:46 a Halo-PS3 session's listener
    (PID 16436) holds port 18194; not ours, left running.
- 5.25 Wait-loop idle skip (from the 5.18 ranking, item 1). Option a + safety built 2026-10-05.
  - Facts (5.18 run 3, dense Crazy Taxi, SH4 200):
    - 0C15A658: `mov.l @(disp,PC),r3; mov.l @r3,r2; jsr @r2; mov.l
      @(4,r3),r4` calls a hook through a pointer in RAM; the hook at
      0C158E90 is `rts; nop` (empty).
    - 0C15A660 reads three RAM words A, B, C and leaves the loop when
      C > A + B + 1 (`cmp/hi`, `bf`). 0C15A678 reads a flag D (`tst`) and
      goes back to 0C15A658 while D != 0.
    - The loop stores nothing and has no slow-path entry: it polls RAM that
      interrupt handlers change. What it waits for is not known yet.
    - 412,440 turns/s: 12.4% of the JIT instructions, and 2 dynamic ends
      per turn (0.82M/s).
    - The decoder's idle rule (`decoder.cpp`, "Small-n-simple idle loop
      detector": reads, no writes, no FPU, < 6 ops, conditional end ->
      cycles x30) fits 0C15A678 only (4 -> 120 cycles). A turn bills 4 + 2
      + 10 + 120 = 136 cycles, so the loop takes about a third of the
      emulated SH4 cycles. The `[hb]` line (a monitor thread reads the pc
      of the last timeslice end once a second, so it samples guest cycles)
      shows 0C15A678 in 11 of 22 samples.
    - The host still runs ~13 turns per 1792-cycle timeslice. A block bills
      at most `max_cycles` = 224 (`SH4_TIMESLICE / 2`), so the rule cannot
      go below ~7.5 turns per timeslice.
    - Reicast: the same rule with x3, plus a list of known idle blocks by
      code hash (BIOS, Soul Calibur, HOTD2, Ikaruga) billed `max_cycles`.
      Crazy Taxi is not in it.
  - Options:
    a. End the timeslice when a block that fits the idle rule takes a
       backward branch (BranchBlock <= start): about 1 turn per timeslice.
       Same assumption as the x30 rule, only stronger: a real work loop of
       that shape would lose more guest time (see the `mac.l` note in
       `decoder.cpp`).
    b. A list of known idle blocks by code hash, as Reicast (Crazy Taxi
       first). Safe, but per game.
    c. Jump to the next scheduled event, not only to the timeslice end.
       Needs a "next event" time (`OPTIMIZATION_POTENTIAL.md` P5).
    d. Make each turn cheaper: DYN_IC for its `jsr`/`rts` (5.3).
  - Expected: up to ~12% of the JIT instructions, about what full speed
    needs (~10.6%). The share of time is not measured.
  - Checks: HW: Crazy Taxi dense VPS at the same load against the 5.18
    no-probe run (54.9 at vt 9.7K); no change in game speed (frame pacing,
    music tempo); BIOS boot, Sonic Adventure, Soul Calibur; the probe again
    to see the loop's share fall.
  - Matt 2026-10-05: "Let's go with the wait loop"; option "a + safety"
    (a, with two checks against work loops). Built 2026-10-05, package of
    00:45. Build clean (the 7 old warnings), `.got` 0xA460, 0 r2off.
    - Option JIT SPEED -> "JIT IDLE SKIP" (`JitIdleSkip`), default OFF, set
      before boot; `[jitcfg]` prints `idle=`. While idle loops run, an
      `[idle]` line per stats window: idle blocks compiled, checks/s and
      skips/s (timeslices ended).
    - Decoder (`dc/sh4/rec_v2/idle_loop.h`, run by `dec_DecodeBlock`):
      `has_store` = the block has a writem, pref or ifb. `idle_loop` = the
      x30 idle rule (reads, no writes, no FPU, < 6 ops, conditional end),
      plus a backward branch (BranchBlock <= start), plus a pure turn: only
      plain integer ops (no div1, mac.*, sync, ifb or float param), and no
      register read before the block writes it and then written (a counter,
      a moving pointer). The x30 rule itself is unchanged.
    - Backend (`ps3/ps3_jit_idle.h`): a block with `has_store` starts with
      `stw r30,jit_idle_work(r30)` (the context base is never 0). An idle
      block's backward path, before its link: load and clear the mark,
      count the check, and with no mark set r29 (cycles left) to 0, so the
      next poll ends the timeslice; count the skip. 12 instructions, on that
      path only. r29 >= 0 there (the block's own poll passed), so it only
      drops the rest of the timeslice.
    - Crazy Taxi: 0C15A678 is the idle block (r3 from a literal, r2 = @r3,
      tst: pure). No block of the turn stores, so from the second turn on
      each turn ends the timeslice: ~1 turn per timeslice, not ~13. A store
      in the hook or in an interrupt handler (work) costs one more turn.
    - New fields at the tail of `Sh4Context` (`jit_idle_work`,
      `jit_idle_checks`, `jit_idle_skips`): no older offset moves.
    - Comment rule: decoder.cpp (17 blocks) and sh4_registers.h (5)
      trimmed to 2 lines; two commented-out code blocks in decoder.cpp
      removed.
  - PC (WSL): `misc/idle_check/run.sh [seed] [blocks]`.
    - idle_loop.h against constprop_check's SHIL interpreter: 22 directed
      cases (the Crazy Taxi blocks, wait shapes, work loops, refused ops,
      stores) and a fuzzer: a pure block run twice from one state must
      leave the same registers and read the same addresses, and a memory
      write needs `has_store`. PASS, 8 seeds x 200000 blocks (~21% pure).
    - ps3_jit_idle.h: words against the build image's ppu-as (`gen.py`
      writes `idle_words.inc`), and the check on a PPC64 model (work, count,
      cycle, base and offset values): PASS, 7230 checks.
    - `mutate.py`: 20 of 20 planted bugs fail; 13 of the 14 in idle_loop.h
      fail on the fuzzer alone (the last, float params allowed, is a policy
      the directed cases hold).
  - HW test steps (package of 00:45):
    1. Install. Set (an install resets them): EMULATION -> Dynarec ON, JIT
       Fastmem ON, SH4 Clock 200 (default); AUDIO -> ARM7 Idle Skip SAFE,
       SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG -> TA SQ Direct ON, IFB
       Probe ON, JIT C-Calls ON; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT
       CONSTPROP, JIT DEAD VALUES ON, JIT CYCLE CHECK LOOPS ONLY (default),
       JIT IDLE SKIP ON. These are the 5.18 run 1 settings (the no-probe
       reference) plus JIT IDLE SKIP. Set it on the main menu, not with
       SELECT (SELECT changes act at the next boot).
    2. UDP listener on port 18194, only when Matt says so.
    3. Crazy Taxi, dense traffic, ~150 s. Open and close the in-game menu
       (SELECT) once. Exit to the XMB.
    4. Check: `[jitcfg]` ... `idle=1`, `[idle]` lines, no hang, game speed
       and music tempo as before.
    5. Analysis: same-load VPS against the 5.18 run 1 (`vtbins.py`); the
       `[idle]` skips/s (the 5.18 profile had 412K loop turns/s).
    6. Sonic Adventure and Soul Calibur, ~2 min each, option ON: no hang,
       speed and music as before.
    7. On a fault: the same boot with JIT IDLE SKIP OFF.
  - HW 2026-10-05, package of 00:45, Crazy Taxi, 150 s. JIT HOTBLOCKS was
    ON too, so the reference is the 5.18 run 3 (`run518b.txt`, the same
    settings with the profiler; dense VPS 51.5 at vt 9.5K), not run 1.
    Log: this session's scratchpad, `ps3log_525_run1.txt` (Crazy Taxi part
    `run525_ct.txt`). Log clean; the in-game menu opened and closed; the
    XMB exit took the old 3 s fallback.
    - `[jitcfg]` ... `idle=1`. Menus and light scenes: ~103K skips/s (about
      one per timeslice). Dense windows: 48K checks/s, 36K skips/s.
    - Wait loop: 412,440 -> 35,978 turns/s (-91%); its share of the JIT
      instructions 12.4% -> ~1.1%. JIT instructions per emulated frame
      -12% (421M/s at VPS 51.5 -> 384M/s at VPS 53.6).
    - 66 dense windows: VPS 53.6 at SPEED 89.3% (mean vt 9.1K), against
      51.5 at 85.8% (vt 9.5K). Same load (`vtbins.py`): +3.0% VPS (bins
      8-10K +5.1% and +3.5%).
    - Dense audio: 19.2 silent blocks/s (run 3: 27.1), output 44.3K
      samples/s (42.3K).
    - So the loop cost far less time than its share of instructions (high
      IPC: L1 loads, predicted branches); the rest of the code stalls.
    - Matt: sound and game speed normal in Crazy Taxi.
    - Sonic Adventure (profiler OFF, `run525_sonic.txt`): Matt "It feels
      much slower". The log agrees: VPS 60 and SPEED 100% all along, but
      the game draws 20 frames/s in long stretches (the boss: RPS 20.0 in
      every window) where the 5.17 run drew 30 (634 of 730 windows). The
      game loses CPU time per frame (3 vblanks, not 2), so a skipped loop
      does work the two checks miss: a counter or pointer that changes in
      another block of the turn, with no store.
    - Soul Calibur (`run525_soul.txt`): as in the 5.20 run, render-bound
      in the fight (VPS 27.8 of 50, render busy 83.5%); log clean.
  - Fix (Matt 2026-10-05: "Fix build"), package of 01:13. Build clean (the
    7 old warnings), `.got` 0xA498, 0 r2off.
    - Third check: a hash of the SH4 GPRs (the 15 pinned in r14..r28 and
      r11 from the context; rotate by 5 and xor, low words only, so the
      junk high words of pinned registers do not count) must equal the
      hash this block stored at its last check. A counter or pointer that
      moves anywhere in the turn changes it, so that turn runs on. 47
      instructions on the idle path (was 12); the work mark is unchanged.
    - Per idle-loop pc an entry (checks, skips, last hash; 1024 entries,
      the last shared on overflow, kept across cache clears). The context
      keeps only `jit_idle_work`.
    - `[idle]` line: checks/s, skips/s and the 6 busiest idle blocks
      (pc checks/s / skips/s); then, once per pc (up to 64), its SH4 code
      from the branch target to the block end (`[idle] code`).
    - PC: idle_check PASS (8 seeds as before; the analysis is unchanged);
      emitter: words against ppu-as for three entry addresses (low half
      positive, negative and 0x7FF0) and the check on the PPC64 model with
      junk high words, stale hashes, work marks, one changed bit in every
      GPR and r11, and i++/j-- together: PASS, 91346 checks.
  - HW test steps (package of 01:13):
    1. Install. Settings as the runs above, JIT IDLE SKIP ON, JIT
       HOTBLOCKS OFF.
    2. Sonic Adventure: the stretches that drew 20 frames/s (the boss),
       ~2 min. They must draw 30 again. The `[idle]` lines name the loops
       that still skip.
    3. Crazy Taxi, dense traffic, ~150 s: same-load VPS against the 5.18
       run 1 (no probe; copied to this session's scratchpad as
       `run518_run1_noprobe.txt`, dense VPS 54.9 at vt 9.7K).
    mutate.py with the new emitter mutants: 26 of 26 planted bugs fail.
  - HW 2026-10-05, package of 01:13, Crazy Taxi only (Matt: "Sonic
    Adventure already runs well"; the Sonic check of the fix was not run).
    Log `run525b.txt` (the listener file after byte 2116220). Log clean,
    the old XMB 3 s fallback.
    - 62 dense windows: VPS 57.1 at SPEED 95.3% (mean vt 9.3K), 35% of
      them at full speed; the 5.18 run 1: 54.9 at 91.5% (vt 9.7K), 26%.
      Same load: +3.0% VPS (bins 7-9K +8.4% and +6.2%, 10-11K +5.8%).
    - Dense `[idle]`: 50.8K checks/s, 36.9K skips/s, all from the Crazy
      Taxi wait loop 0C15A678. Six other idle blocks check up to 3.9K
      times/s and never skip; their code shows work loops (0C0679F8 scans
      an array with float compares, 0C172BC2's loop adds to r12 and
      stores). The first version skipped such loops on turns without a
      store: the Sonic Adventure slowdown.
  - Matt 2026-10-05: "Turn it on by default". `JitIdleSkip` defaults to 1
    since the package of 01:27 (an install resets the settings to it).
- 5.26 T for `bt.s`/`bf.s` kept in a register (from the 5.18 ranking,
  item 2). Option a built 2026-10-05 (Matt's pick), HW pending.
  - Facts (5.18 run 3, `hot518b/report.txt` in the scratchpad of session
    29bdd1c1): load-hit-stores by context slot: sr_T 1.985M/s, the largest
    (fpul 0.655, jit_scratch 0.567, each FR slot ~0.2, fpscr 0.218).
  - Today (`ps3/ps3_driver.cpp`, `shop_jcond`): a delayed branch copies T
    into r31 (djump) before its delay slot. With T FORWARD it takes T from
    rarg0 only when the compare is the op just before it; else it loads
    sr_T, a few instructions after the compare stored it.
  - The 36 dumped blocks with that load (`jcond518.py` on `run518b.txt`,
    session 1a0d7654's scratchpad; ~1.3M entries/s, 70% of the weight):
    - The compiler puts 1-4 ops between the compare and `bt.s`/`bf.s`
      (`tst; add #4,Rn; bf.s` is the top shape, 0.24M/s). Some have 5-24
      ops between (`dt` near the top of a long block).
    - `fcmp/gt` just before `bf.s` (0C042D96, 94K/s) reloads too:
      `shop_fseteq`/`shop_fsetgt` store T without setting `t_in_rarg0`.
    - One block has no T writer (T comes from the block before).
  - Options:
    a. The block's last T writer before a jcond also copies T into r31
       (`mr r31,rarg0`), when no op between them writes T; that jcond then
       emits nothing. fseteq/fsetgt get the same copy. r31 is free in a
       conditional block (only jcond and the block end use it), and C
       calls keep it (non-volatile).
    b. Keep sr_T in a pinned register for the whole block (also helps
       `movt`, `rotcl`, `addc` and other T readers), written back before C
       calls and at block end. Larger change: every T reader and writer.
  - Expected: if all ~1.9M/s are such reloads at ~40 cycles each, ~80M
    PPE cycles/s, up to ~2.5% of the SH4 thread. Not measured.
  - Checks: PC: the emitted T copy on the hot_check-style PPC64 model
    (both CR0 forms, delay slots that write T, IFB in between). HW: Crazy
    Taxi same-load VPS against `run525b.txt`; the profiler again (sr_T
    load-hit-stores must fall); a Sonic Adventure and a Soul Calibur boot.
  - Matt 2026-10-05: "Let's start with 5.26", option a. Built and PC
    tested 2026-10-05, package of 09:28: JIT SPEED -> "JIT T COPY"
    (`JitTCopy`), default OFF, set before boot; `[jitcfg]` prints
    `tcopy=1`.
    - `ps3/ps3_jit_tcopy.h`: `tcopy_writer()` finds the last op before the
      jcond that may write T (rd or rd2 is sr_T, an IFB op, a jdyn). If
      that op stores sr_T inline (set*, tst, cmp/str, fcmp/eq, fcmp/gt,
      and/or/xor/shl/shr/sar, mov32, addc/subc/negc/addv/subv, div1), its
      store in `ppc_sh_store` also emits `mr r31,rX`, and the jcond emits
      nothing. Else the jcond loads sr_T as before. An IFB or jdyn after
      the copy drops it.
    - The 5.18 dumps: every jcond block with a T writer has one of these
      writers (dt, tst, cmp/*, fcmp/gt); only the 5.4K/s block without a
      writer keeps its load.
    - `[tcopy] jcond sites: copy= fwd= load= miss=` (compiled sites since
      boot, after new compiles): copy = no reload, fwd = T FORWARD's
      `mr`, load = the old `lwz`. miss counts armed writers that did not
      copy; it must stay 0.
    - PC (WSL): `misc/tcopy_check/run.sh` runs the header as ngen_Compile
      drives it, on constprop_check's SHIL interpreter: 40 directed blocks
      (the 5.18 shapes, every writer op, IFB / jdyn / readm / delay-slot
      cases, the drop rule) and 200000 random blocks (decoder shapes, 0-24
      ops between writer and jcond, CONSTPROP and DEAD VALUES on and off),
      3 start states each. At every block end r31 equals the sr_T that
      the old code loads at the jcond: PASS (seeds 1 and 7). `mutate.py`:
      18 of 18 planted bugs fail it, 14 in the fuzzer alone (the other 4
      need shapes the decoder does not emit: jdyn or readm before the
      jcond, a jcond on another register, a forced writer).
    - Not built: the PPC64-model check of the plan's "Checks". The new PPC
      code is one `mr r31,rX` after the writer's existing `stw rX` to
      sr_T, so the check runs at the SHIL level.
    - Build: the 7 old warnings, `.got` 0xA4D8, 0 `r2off`.
  - HW test steps (package of 09:28):
    1. Install. Settings as `run525b.txt` (an install resets them):
       EMULATION -> Dynarec ON, JIT Fastmem ON, SH4 Clock 200; AUDIO ->
       ARM7 Idle Skip SAFE, SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG
       -> TA SQ Direct ON, IFB Probe ON, JIT C-Calls ON, JIT HOTBLOCKS
       OFF; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD
       VALUES ON, JIT CYCLE CHECK LOOPS ONLY, JIT IDLE SKIP ON, and the
       new JIT T COPY ON.
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: Crazy Taxi, dense traffic, ~150 s, then exit to the XMB.
       Log: `[jitcfg] ... tcopy=1`, `[tcopy] ... miss=0`. Same-load VPS
       against `run525b.txt`.
    4. Boot 2: the same with JIT HOTBLOCKS ON (set on the main menu before
       boot), ~3 min: sr_T load-hit-stores must fall (5.18 run 3:
       1.985M/s).
    5. Boot 3: Sonic Adventure, a few minutes. Boot 4: Soul Calibur, one
       fight. Both must play as before.
  - HW 2026-10-05, boot 1 (package of 09:28, Crazy Taxi, 142 s). Log
    `run526a.txt` in the scratchpad of session 3f596c0b. `[sndcfg]` and
    `[jitcfg]` as `run525b.txt`, plus `tcopy=1`. Log clean (the old XMB
    3 s fallback only).
    - `[tcopy]` at the end: copy=2107 fwd=0 load=17 miss=0. 99% of the
      compiled jcond sites no longer reload sr_T.
    - 62 dense windows: VPS 57.1 at SPEED 95.2% (vt 9.3K), 32% at full
      speed; `run525b.txt`: 57.1 at 95.3% (vt 9.3K), 35%. Same load: -0.1%
      (bins from -1.1% to +0.4%, the few outer windows aside): no
      measurable gain. So the ~1.9M/s reloads cost far less than the
      ~40-cycle estimate (or under ~1%, below the noise of one run).
  - HW 2026-10-05, boot 3 (Sonic Adventure, PAL disc, 216 s; log
    `run526b.txt`, same scratchpad). Log clean (XMB fallback only);
    `[tcopy]` copy=10360 fwd=0 load=30 miss=0. 149 NTSC windows at RPS 30
    (SPEED 99.4%); 20 at RPS 20, all in heavy scenes (vt 12.4K, SPEED
    91.5%), so the game misses its frame there, not a wrong branch. The
    faulty 5.25 run had 54 windows at RPS 20 at SPEED 98%.
  - HW 2026-10-05, boot 4 (Soul Calibur (U), 86 s; log `run526c.txt`).
    Log clean (XMB fallback only); `[tcopy]` copy=4126 fwd=0 load=48
    miss=0. Fight windows as the 5.25 run (`run525_soul.txt`): RPS ~10-15,
    VPS 27.8 (was 28.5) at vt 15.4K; still render-bound (texture decode
    82% of the render thread, busy ~91%).
  - Result: correct in all three games, no measurable speed gain. Boot 2
    (profiler) not run.
  - Matt 2026-10-05: "And make it default ON". `JitTCopy` defaults to 1
    since the package of 10:13 (an install resets the settings to it).
- 5.27 Store-queue stores JIT SQ STORES misses (from the 5.18 ranking,
  item 5). Both options built 2026-10-05 (Matt's pick). HW 2026-10-05:
  same-load VPS +1.7% (SLOW PATH) and +0.9% (+ LOOK-AHEAD), logs clean.
  - Facts (5.18 run 3): 0.88M slow-path entries/s, mostly `fmov.s
    FRm,@-Rn` in 0C081D58, 0C081D5A and 0C0820C2, whose PREF is in another
    block, so 5.17 Step 3's guess (a later PREF on the same base register)
    does not see them. The slow path's table tier runs 2 microcoded
    shifts.
  - Options: test for the store queue first in the slow path, or a wider
    guess (a store through a register that held a store-queue address in
    an earlier block).
  - The dumps (`run518b.txt`, read 2026-10-05): the three blocks are Crazy
    Taxi's vertex loops (SZ=1). Each writes 32 bytes through R6 with four
    `fmov DRm,@-R6` (0C081D58/5A also `mov.l R7,@R6` in the `bt.s` slot)
    and ends at `bt.s`. The `pref @R6` is in the next block: 0C081D72 (the
    target) starts with it, 0C081D6E (the fall-through) has it in its
    `bra` slot, 0C0820DA (0C0820C2's fall-through) has it as its third op.
    Cold entries/s: 0C081D5A 231K, 0C081D58 144K, 0C0820C2 136K (0.51M of
    the 0.88M), 0C081966 47K (its PREF is on another register), 0C0804C4
    44K (a copy loop). Each such store runs the guard, the table load, 2
    microcoded shifts and 2 taken branches.
  - Estimate: 5.17 Step 3 gained +4.2% for ~1.7-3.5M stores/s moved to the
    inline shape, so the 0.51M/s give ~+1%; the slow-path test saves part
    of that for every missed store. About the noise of one run.
  - Matt 2026-10-05: "Work on 5.27 then"; option "both, 3 levels". Built
    and PC tested 2026-10-05: JIT SPEED -> "JIT SQ MISS" (`JitSqMiss`):
    OFF / SLOW PATH / + LOOK-AHEAD, default OFF, set before boot;
    `[jitcfg]` prints `sqmiss=`. JitFastmem path only.
    - SLOW PATH (1): a 32-bit or pair store that keeps the RAM guard gets
      `ColdFrag::vmem_tier` 2. Its cold fragment starts with
      `emit_sq_first` (`ps3/ps3_jit_sq.h`): 5.17 Step 3's test and store
      (`emit_sq_store`, the fast path's registers), then `b done`. Other
      addresses fall into the table tier as before. 7 (scalar) or 8 (pair)
      instructions more per such fragment, none on the fast path.
    - + LOOK-AHEAD (2, needs JIT SQ STORES ON): `sqla_guess`
      (`ps3/ps3_jit_sqla.h`) also takes a store as a store-queue store when
      its base register keeps its value to the block end (steps by an
      immediate aside; no IFB or sync_sr after it) and a static successor
      (bt/bf: target and fall-through; static jump: target) runs `pref
      @Rn` within its first 6 SH4 ops. The scan reads guest code in main
      RAM only (`ReadMem16`); it stops at any op that may write Rn, switch
      banks or branch, and a delayed branch's slot is its last op. Such a
      store gets 5.17 Step 3's inline shape: no taken branch. A wrong guess
      only costs speed (RAM stores then take the cold path).
    - `[ta-split]` adds `sqla=` (look-ahead sites, part of `sqst`) and
      `sqcold=` (cold fragments with the test), both since boot.
  - PC (WSL):
    - `misc/sqstore_check/run.sh`: `emit_sq_first` with FlushCold's
      registers on the PPC64 model (now with `b`): a store-queue address
      stores and jumps to done, any other reaches the table label with only
      r0 changed. PASS, 4.8M cases, plus the 4.8M Step 3 cases. `mutate.py`
      (new): 12 of 12 planted bugs fail (Step 3's six, six new).
    - `misc/sqla_check/run.sh [seed] [n]`: `gen.py` builds a reference from
      `dc/sh4/sh4_opcode_list.cpp` as BuildOpcodeTables fills OpDesc (PC
      writers, delay slots, bank switches, GPRs written from the
      disassembly text). The classifier against it on all 65536 opcodes x
      16 registers: 0 wrong, 12976 conservative stops. Directed: the Crazy
      Taxi blocks and successors; R6 overwritten, IFB, sync_sr, a step by a
      register, other block ends, an immediate base; BIOS, VRAM, P4 and odd
      pcs are never read. 2M random successor sequences against the
      reference scan: 0 wrong. PASS (seeds 1 and 7). `mutate.py`: 26 of 26
      planted bugs fail; 13 fail on the reference checks alone (all 12
      classifier bugs), the scan and block-rule bugs on directed cases.
  - Build: the 7 old warnings, `.got` 0xA518, 0 `r2off`; package of 11:19.
  - HW test steps (package of 11:19):
    1. Install. Settings as `run526a.txt` (an install resets them):
       EMULATION -> Dynarec ON, JIT Fastmem ON, SH4 Clock 200; AUDIO ->
       ARM7 Idle Skip SAFE, SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG
       -> TA SQ Direct ON, IFB Probe ON, JIT C-Calls ON, JIT HOTBLOCKS
       OFF; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD
       VALUES ON (JIT CYCLE CHECK LOOPS ONLY, JIT IDLE SKIP ON and JIT T
       COPY ON are the defaults). Set JIT SQ MISS on the main menu, not
       with SELECT (SELECT changes act at the next boot).
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: JIT SQ MISS = SLOW PATH. Crazy Taxi, dense traffic, ~150 s,
       then exit to the XMB. Log: `[jitcfg] ... sqmiss=1`, `[ta-split]
       ... sqcold=` above 0, `sqla=0`.
    4. Boot 2: JIT SQ MISS = + LOOK-AHEAD, the same run. Log: `sqmiss=2`,
       `sqla=` above 0 (the three loop blocks alone give 14 sites).
    5. Analysis: same-load VPS of each boot against `run526a.txt` (JIT SQ
       MISS OFF, same settings) with `vtbins.py`. The expected gain is
       about the noise of one run, so a second boot per level helps.
    6. Optional: boot 2 again with JIT HOTBLOCKS ON. The cold entries of
       0C081D58, 0C081D5A and 0C0820C2 must be 0.
    7. Sonic Adventure and Soul Calibur, one boot each with + LOOK-AHEAD:
       no hang, game speed and music as before.
    8. On a fault: the same boot with JIT SQ MISS OFF.
  - HW 2026-10-05, package of 11:19, Crazy Taxi, settings as `run526a.txt`
    (same `[sndcfg]` and `[jitcfg]` lines plus `sqmiss=`). Log
    `ps3log_527_run1.txt` in the scratchpad of session 468f5b68, split into
    `run527a.txt` (boot 1) and `run527b.txt` (boot 2). Both logs clean: the
    old XMB 3 s fallback and the `[DIV]` probe lines, as in `run526a.txt`.

    | Run | dense n | VPS | SPEED | vt | full speed | 12K+ VPS (n, vt) | same load vs 526a |
    |---|---|---|---|---|---|---|---|
    | `run526a` OFF | 62 | 57.1 | 95.2% | 9.3K | 32% | 48.2 (4, 14.7K) | - |
    | boot 1 SLOW PATH | 61 | 58.2 | 97.1% | 9.0K | 46% | 52.5 (4, 13.9K) | +1.7% |
    | boot 2 + LOOK-AHEAD | 61 | 57.6 | 96.1% | 9.5K | 41% | 54.8 (10, 13.4K) | +0.9% |

    - Boot 1: `sqcold=17851` (cold fragments with the test), `sqla=0`.
      Boot 2: `sqla=80` (look-ahead sites; the three loop blocks give 14),
      `sqst=309` (was 232), `sqcold=17830`.
    - Boot 2 against boot 1 at the same load: -0.6% (bins -3.1% to +4.9%):
      the two levels are not apart in one run each. Boot 2 had the most
      heavy windows and the best VPS in them (12-13K: 57.5 in 6 windows).
      Matt: "Boot 2 run much better".
    - Dense audio: 5.3 (boot 1) and 7.7 (boot 2) silent blocks/s, against
      9.0 in `run526a.txt`; output ~47K samples/s in all three.
    - Matt 2026-10-05: Soul Calibur is no use for these tests while its
      renderer dominates. Step 7 is Sonic Adventure only.
  - HW 2026-10-05, boot 3: Sonic Adventure (PAL disc), + LOOK-AHEAD, 271
    s; log `run527c.txt` (same scratchpad). Matt: "It ran worse?". Against
    `run526b.txt` (5.26 boot 3, JIT SQ MISS OFF), NTSC windows with vt >
    3K (`sonicbins.py`): 206 windows, SPEED 99.2%, full speed 92%, 30
    frames/s in 89%; before: 163, 99.6%, 94%, 90%. Equal within noise; the
    20 frames/s windows are the heavy scenes as before (15K+ vertices).
    - The code is ~8% larger (the SLOW PATH test in every 32-bit and pair
      store's cold fragment): 1277332 -> 1381328 bytes at the same early
      clear. Cache-full clears (92%): 4 in 271 s (windows 89, 153, 202,
      254), against 3 in 216 s; the window after two of them ran at 72.8%
      and 64.4% SPEED (before: 97.5%, 91.5%, 86.7%). One intro window ran at
      37.6% (before, the same stretch: 61% or more). These short dips are
      the likely "worse"; one run each cannot tie them to 5.27.
  - Matt 2026-10-05: "I think It's fine for now" (no LOOK-AHEAD ONLY
    level). JIT SQ MISS stays default OFF.
- 5.3 DYN_IC: a per-site inline cache on dynamic block ends (Matt
  2026-10-05: "Go with DYN_IC (5.3) this session"). Built, PC tested and
  HW tested 2026-10-05; done (Matt), default JSR/JMP since the package of
  16:42 (Matt).
  - Facts: 5.18 run 3 counted 2.6M dynamic ends/s (`rts`, `jsr @Rn`,
    `braf`) in dense Crazy Taxi, 0.82M/s of them in the wait loop that
    5.25 now mostly skips. Each runs the inline lookup: 16 instructions, 3
    dependent loads (`cache[]`, `addr`, `code`), the `lookups` store and an
    indirect `bctr`.
  - The code was in `ps3/ps3_driver.cpp` since the port, with
    `get_dyn_ic_preset()` fixed at 0. PS3 review (Dynarec.MD 3.2,
    `jit-64bit-poison`):
    - The fill stub passed djump to the C fill (`u32 pc`) without
      zero-extending it, and djump can hold a junk high word (`shop_jdyn`
      adds to pinned registers). It now gets `clrldi`, as the static link
      stub has. The C side only used the low word, so no fault was seen.
    - The guard was right: `cmplwi` compares the low word only.
    - The cache sync works on 128-byte lines from any start address.
    - Every invalidation is a full clear (`recSh4_ClearCache`; nothing
      calls `bm_RemoveCode` or `bm_InvalidateRange`), as static links
      already need: guards and targets go together.
  - Built: option JIT SPEED -> "JIT DYN IC" (`JitDynIc`): OFF / JSR/JMP
    (jsr, bsrf, jmp, braf and the other dynamic jumps) / + RTS, default
    OFF, set before boot; `[jitcfg]` prints `dyn_ic=`. Code in
    `ps3/ps3_jit_dynic.h`:
    - Site, after `mr r3,r31`: `bl` to the fill stub, then 3 branches to
      site+16. Site+16 counts a miss (4 instructions; only filled sites
      reach it), then the old lookup.
    - Fill (`ngen_LinkBlock_Dynamic_IC`, once per site): finds or compiles
      the target; if slot 0 still holds its `bl` (a cache clear in the
      compile can reuse the address), it writes the guard `xoris
      r4,r31,hi; cmplwi r4,lo; bne site+16; b target`.
    - Hit: 4 instructions and a direct branch, no load. Miss: 3 more
      instructions and the counter, then the old lookup.
    - A `[dynic]` line per stats window: sites compiled by kind (call,
      jump, ret) and filled since boot, misses of filled sites per second
      by kind.
  - PC (WSL): `misc/dynic_check/run.sh`.
    - Words against the build image's ppu-as (`gen.py` writes
      `dynic_words.inc`): 50 words; sites with the stub below and above,
      guards with the low half 0, bit 15 set and all ones, the reach of `b`
      both ways, counters with the low half positive, negative and 0x7FFC.
    - A PPC64 model runs the whole path: the unfilled site calls the stub,
      the stub calls a model of the C fill (which checks the zero-extended
      pc, the site and the return address), the fill writes the guard,
      then hits and misses with junk high words in r31, near misses (each
      single bit, the halves swapped) and random pcs, targets on both
      sides. A hit changes only r3, r4 and CR0 (not LR); a miss also r5,
      r6 and the counter (+1). A cache clear in the fill: an overwritten
      site is left alone, a site rebuilt at the address is filled. PASS,
      637371 checks.
    - `mutate.py`: 25 of 25 planted bugs fail; 23 also on the model (the
      other two are in slots 1-3, which never run).
  - Build: the 7 old warnings, `.got` 0xA5B8, 0 `r2off`; `s_ic_miss` at
    0x114B2F10 (below 2 GB). Package of 14:19.
  - Expected: a hit saves the lookup's loads and the `bctr`, est. 20-30
    cycles. With ~1.8M ends/s left after 5.25, ~1% if most of them hit;
    a miss costs a few cycles more. Rts sites are often polymorphic (one
    function, many callers), hence the two levels. Not measured.
  - HW test steps (package of 14:19):
    1. Install. Settings as `run524a.txt` (an install resets them):
       EMULATION -> Dynarec ON, JIT Fastmem ON, SH4 Clock 200; AUDIO ->
       ARM7 Idle Skip SAFE, SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG
       -> TA SQ Direct ON, IFB Probe ON, JIT C-Calls ON, JIT HOTBLOCKS
       OFF; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD
       VALUES ON, JIT SQ MISS + LOOK-AHEAD (JIT CYCLE CHECK LOOPS ONLY, JIT
       IDLE SKIP, JIT T COPY, JIT MFOCRF and JIT FPUL MOVES ON are the
       defaults). Set JIT DYN IC on the main menu, not with SELECT (SELECT
       changes act at the next boot).
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: JIT DYN IC = JSR/JMP. Crazy Taxi, dense traffic, ~150 s,
       then exit to the XMB. Log: `[jitcfg] ... dyn_ic=1`; `[dynic]` with
       call and jump sites above 0, ret=0, filled above 0.
    4. Boot 2: JIT DYN IC = + RTS, the same run. Log: `dyn_ic=2`, ret
       sites above 0.
    5. Analysis: same-load VPS of each boot against `run524a.txt`
       (scratchpad of session fc4e881b) with `vtbins.py`; misses/s by
       kind.
    6. Boot 3: Sonic Adventure with the better level, a few minutes: no
       hang, game speed and music as before (against `run524b.txt` with
       `sonicbins.py`).
    7. On a fault: the same boot with JIT DYN IC OFF.
  - HW 2026-10-05, package of 14:19, settings as `run524a.txt` (same
    `[sndcfg]`; `[jitcfg]` differs only in `dyn_ic=`). Log
    `ps3log_53_run1.txt` in the scratchpad of session 48f8af9b, split into
    `run53a.txt` (Crazy Taxi, JSR/JMP), `run53b.txt` (Crazy Taxi, + RTS)
    and `run53c.txt` (Sonic Adventure, + RTS); scripts `dynic.py` (misses
    by kind in dense windows), `bins.py`, `vtbins.py`, `sonicbins.py`,
    `cleardip.py`. All three logs clean: no fault line, the old XMB 3 s
    fallback, the line tags of the references plus `[dynic]`.

    | Run | dense n | VPS | SPEED | full speed | 12K+ VPS (n, vt) | same load vs 524a |
    |---|---|---|---|---|---|---|
    | `run524a` (OFF) | 61 | 58.4 | 97.4% | 51% | 54.7 (6, 14.0K) | - |
    | `run53a` JSR/JMP | 60 | 59.0 | 98.4% | 62% | 55.7 (8, 13.7K) | +1.0% |
    | `run53b` + RTS | 60 | 59.3 | 98.9% | 63% | 56.2 (6, 13.9K) | +1.0% |

    - `run53b` against `run53a` at the same load: +0.2%; the levels are
      not apart. +1.0% is about the noise of one run.
    - Crazy Taxi sites at the end: call 5079, jump 459 (ret 2318 at +
      RTS); 15 cache clears in each boot, as in `run524a.txt`. Misses of
      filled sites in dense windows: call 11K/s, jump 92K/s, ret 860K/s
      (+ RTS). Rts sites return to many callers, so many miss, and the
      misses cost about what the hits save.
    - Sonic Adventure (+ RTS, 209 windows): 150 NTSC windows with vt > 3K,
      SPEED 99.6%, full speed 95%, 25+ frames/s in 92%; `run524b.txt`:
      167, 99.7%, 95%, 89%. Equal within noise. Misses: call 9K/s, jump
      3K/s, ret 437K/s.
    - Code size: +3-4% at the same early clears (1383960 -> 1423708 bytes,
      3685312 -> 3823076), from the 8 words per dynamic end. Sonic's
      cache-full clears came at t=79, 134, 178 s (`run524b.txt`: 95, 153,
      203); the window after the second ran at 81.2% SPEED (before: 86.8%
      after the second).
  - Matt 2026-10-05: "It kind of felt the same across both runs? We could
    mark it as done anyway." Then "Place it on": `JitDynIc` defaults to 1
    (JSR/JMP, Matt's pick of the two levels) since the package of 16:42
    (`.got` 0xA5B8, 0 `r2off`). Its sites also ran in the Sonic boot at +
    RTS, which holds the JSR/JMP sites too.
- 5.28 Larger SH4 code cache (Matt 2026-10-05: "We gotta move to the
  next goal", then picked "Larger SH4 code cache" from section 0's
  list). Built 2026-10-05, HW pending.
  - Facts (`run53c.txt`, Sonic Adventure, 6 MB cache): after the last
    code load (t=58 s) the cache reached the 92% flush mark at t=79, 134
    and 178 s, one refill every 44-55 s; a code load cleared it at t=211 s
    with 5.7 MB used. The windows after the clears at 134 and 211 s ran at
    81.2% and 79.2% SPEED, the others at 96% or more. The run has 8155
    "warning zone" lines: one per block compiled between 80% and 92%.
    Crazy Taxi stays below 80% (`run53a/b.txt`: 15 clears, all the game's
    I-cache invalidations, at most 1.4 MB used, no warning line).
  - Limits. Memory is not one: the boot log shows 164683 KB free with the
    6 MB array. The limit is the reach of `bl`, 32 MB: generated code
    calls C in `.text` with `bl` (`ppc_bx` verifies it), so every cache
    byte must reach all of `.text`. With a 24 MB array: 27549 KB (cache
    start to `__etext`), 5 MB of headroom. The ARM7 JIT cache (2 MB,
    after it, its own `verify`) reaches 27.4 MB back to the start of
    `.text`.
  - Built (package of 17:08):
    - `CODE_SIZE` 6 MB -> 24 MB (`dc/sh4/rec_v2/ngen.h`). ELF 13.3 ->
      32.2 MB; package 961 -> 981 KB (the zeros compress).
    - EMULATION -> "JIT Code Cache" (`JitCodeCache`): 6 MB / 12 MB / 24
      MB, default 6 MB, set before boot. `recSh4_Init` latches it
      (`s_code_size` in `dc/sh4/rec_v2/driver.cpp`); free space, the 80%
      and 92% marks and the init clear use it. `[jitcfg]` prints
      `ccache=`.
    - Boot line `recSh4: code cache at ... (N of 24576 KB in use, PS3
      .text), bl reach N KB of 32768`, from the linker's
      `__executable_start` and `__etext`; an ERROR line at 32 MB or more.
    - A cache clear now syncs only the code written since the last clear
      (blocks and patches are synced when written), so its cost follows
      the code made, not the array size. Before, it synced all of
      CODE_SIZE.
    - The warning-zone line prints once per fill.
    - New `[jcache]` line per stats window with new code: used KB of the
      size, compiled KB/s, clears since boot (`full` = out of room, `all`).
    - The main-loop dump (`dynarec_*.bin`) now writes the loop's size; it
      wrote `CODE_SIZE - free`, which would be 18 MB+ at 6 MB of 24.
    - At 6 MB the JIT behaves as before, except the two log changes and
      the clear's sync range.
  - Build: the 7 old warnings, `.got` 0xA618, 0 `r2off`; CodeCache at
    0x42600, `__etext` 0x1B29A98, ARM7 cache at 0x1882000.
  - Expected: Sonic Adventure has no full clear until ~22 MB of code, so
    the dips after full clears go; Crazy Taxi does not change. Risk: hot
    code spread over more memory (more I-ERAT and TLB misses) may cost a
    little; the Crazy Taxi boot shows it.
  - HW test steps (package of 17:08):
    1. Install. Settings as `run53b.txt` and `run53c.txt` (an install
       resets them): EMULATION -> Dynarec ON, JIT Fastmem ON, SH4 Clock
       200, JIT Code Cache 24 MB; AUDIO -> ARM7 Idle Skip SAFE, SPE AICA
       Sound ON, SPE ARM7 ON; VIDEO / DEBUG -> TA SQ Direct ON, IFB Probe
       ON, JIT C-Calls ON; JIT SPEED -> JIT SQ STORES, JIT FTRV, JIT
       CONSTPROP, JIT DEAD VALUES ON, JIT SQ MISS + LOOK-AHEAD, JIT DYN
       IC + RTS. Set them on the main menu, not with SELECT.
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: Sonic Adventure, about 4 minutes of play as in
       `run53c.txt`. Log: `[jitcfg] ... ccache=2`; `[jitprobe] test B`
       passes (it runs code in the last KB of the 24 MB array); the reach
       line; `[jcache]` lines; no "cache above flush threshold" line;
       `[ps3] largest free block at boot` about 18 MB below 164683 KB.
    4. Boot 2: Crazy Taxi, dense traffic, about 150 s, then exit to the
       XMB. Same-load VPS against `run53b.txt` (scratchpad of session
       48f8af9b) with `vtbins.py`.
    5. On a fault: the same boot with JIT Code Cache 6 MB.
  - HW boot 1 2026-10-05 (Sonic Adventure, 24 MB; log `run528a.txt` in the
    scratchpad of session 849b7627, cut from `ps3log_528_run1.txt`, lines
    1-12319). Settings as `run53c.txt` (`[sndcfg]` equal, `[jitcfg]` adds
    `ccache=2`). Log clean, the old XMB 3 s fallback. `[jitprobe] test B`
    PASS at 0x1842200; reach line 27549 KB; free at boot 146251 KB (18432
    KB less, as expected).
    - The same code loads and clears as `run53c.txt`, up to the last load
      (`run53c` t=58 s, here t=142 s). After it, 157 s of play: no full
      clear (`run53c`: full clears 21, 76 and 120 s after its last load,
      then a load clear with 5.6 MB used). The cache grew to 4.9 MB in 9 s,
      5.8 MB in 29 s (where 6 MB clears), then slowly: 6.6 MB at +75 s,
      7.8 MB at +157 s (10-20 KB/s).
    - 156 dense NTSC windows: SPEED 99.7%, 97% at full speed, 94% at 25+
      frames/s (`run53c`: 150, 99.6%, 95%, 92%). In play after the last
      load, one window under 95%: t=220 s, 89.3% SPEED, 16.6 frames/s,
      with a 129 KB/s compile burst (new code, no clear). `run53c` in the
      same span: 81.2% after the full clear at t=134 s, 94.9% at t=205 s,
      79.2% twice after the load clear at t=211 s.
    - Tool: `lowwin.py` (windows under a SPEED limit, with the last clear
      before each).
  - HW boot 2 2026-10-05 (Crazy Taxi, 24 MB; `run528b.txt`, lines
    12320-19771 of `ps3log_528_run1.txt`). Settings as `run53b.txt`
    (`[sndcfg]` equal, `[jitcfg]` adds `ccache=2`). Log clean, the old XMB
    3 s fallback. 15 clears as in `run53b` (the game's I-cache
    invalidations), no full clear, no warning line; 4884 KB used at the
    end.
    - 60 dense windows: VPS 59.2, SPEED 98.7%, 68% at full speed
      (`run53b`: 60, 59.3, 98.9%, 63%). Same-load VPS -0.2% against
      `run53b` (`vtbins.py`): noise, no cost from the larger cache.
  - Matt then asked about 12 MB. Answer: 12 MB is a menu setting, but it
    frees no memory (the array stays 24 MB; a 12 MB array needs a
    rebuild and gives ~12 MB back, with 146 MB free now); at ~15 KB/s
    Sonic would reach the 11 MB flush mark after ~6-7 min in one area
    without a level load. Matt: "Let's keep it 24 as default then":
    `JitCodeCache` defaults to 2 (24 MB) since the package of 19:08.
- 5.16 SPE Draw check (Matt 2026-10-05: "I'd like to get Crazy Taxi in full
  speed", then "Trying 1 and 2"). HW 2026-10-05, Crazy Taxi, settings as
  `run528b.txt` plus VIDEO / DEBUG -> SPE Draw ON. Log `run516.txt` in the
  scratchpad of session 8112eaf0 (the listener started mid-boot, so no
  `[jitcfg]` line; `[ta-split]`, `[dynic]` and `[jcache]` match `run528b`).
  - 59 dense windows: VPS 59.5, SPEED 99.2%, 80% at full speed (`run528b`:
    59.2, 98.7%, 68%). Same load +0.5% (bins 10K+ +2.4% to +5.0%, few
    windows). Render thread busy 56 -> 32% (decode ~30 -> 3-5%). Silent
    audio blocks/s 2.4 -> 1.4. Fences all ok, log clean.
  - Matt: "The geometry looked right. I want it ON by default": `SpeDraw`
    defaults to 1 since the package of 20:13.
  - Found: `ps3spe_wait_fence` spins with no pause on the render thread,
    0.3-0.7 ms per fence (~3% of wall), on the SH4 thread's PPE core.
    VIDEO / DEBUG -> "SPE Wait Yield" (`SpeWaitYield`, default OFF, set
    before boot): 4 x `or 31,31,31` (db16cyc) per turn, the timebase read
    every 16th turn. HW 2026-10-05 (`run516y.txt`, same settings plus the
    option): clean, same load -0.0% against `run516` (12K+ 100.4% against
    96.9%, 5 windows each; 6-12K bins -0.1% to -0.9%); fence wait 453 us
    (495). No measurable gain; default OFF.
- 5.29 JIT FRCHG (Matt 2026-10-05: "Do them if you want", items 3 and 4 of
  the full-speed list). Built and PC tested, package of 20:40, HW pending.
  - Facts: dense Crazy Taxi runs ~140K full `sync_fpscr` calls/s (`[CC]`),
    all from `frchg` in the hot code (the 5.18 dumps). Each one stores the
    16 pinned FRs, calls `UpdateFPSCR()` (bank swap in memory, `mtfsfi`)
    and loads the 16 FRs back.
  - VIDEO / DEBUG -> "JIT FRCHG" (`JitFrchg`), default OFF, set before
    boot (the JIT SPEED page is full at 720p); `[jitcfg]` prints `frchg=`,
    the `[ppe]` line counts its sites.
  - `ps3/ps3_jit_frchg.h`: when `fpscr ^ old_fpscr == FR` (only FR
    changed), `old_fpscr = fpscr` and the pinned f14-f29 trade places with
    xf[0..15], 8 at a time through f0-f7: no C call, no `mtfsfi` (RM did
    not change). Any other change takes the old C path. Needs FPU PIN.
    Each value takes the same lfs or stfs as the old path, so the result
    is bit-exact; the fr[] slots stay stale, as always while pinned.
  - Expected: est. ~1% (not measured). The `[CC]` sync_fpscr count must
    fall to about 0.
- 5.30 JIT PREFETCH (same request). Built and PC tested, package of 20:40,
  HW pending.
  - Facts: Crazy Taxi's hottest vertex loop (0C081BC4) runs `pref @R4` on
    its source data; the JIT drops every `pref` outside the store queue.
  - VIDEO / DEBUG -> "JIT PREFETCH" (`JitPrefetch`), default OFF, set
    before boot; `[jitcfg]` prints `pref=`, the `[ppe]` line counts its
    sites. `ps3/ps3_jit_pref.h`: the non-SQ path of `shop_pref` emits
    `rlwinm` (16 MB mask), `addis` (RAM base) and `dcbt` on the host line
    of that RAM address. A hint: an address outside RAM costs one useless
    prefetch. Not with the MMU.
  - Expected: 0-1% (not measured).
- PC (WSL) for 5.29 and 5.30: `misc/frpref_check/run.sh` checks both
  headers against the build image's ppu-as (66 words, `gen.py`) and on a
  PPC64 model: the frchg fast path against the old flush, swap and reload
  (f14-f29, xf, old_fpscr; nothing else but r3, r4, f0-f7 and CR0
  changes), every other FPSCR change straight to the C path, and the dcbt
  address for any address with a junk high word. PASS, 600067 checks.
  `mutate.py`: 23 of 23 planted bugs fail, all 23 also on the model alone.
- Build (package of 20:40): the 7 old warnings, `.got` 0xA658, 0 `r2off`.
- HW test steps (package of 20:40):
  1. Install. Settings as `run516.txt` (an install resets them): EMULATION
     -> Dynarec ON, JIT Fastmem ON, SH4 Clock 200; AUDIO -> ARM7 Idle Skip
     SAFE, SPE AICA Sound ON, SPE ARM7 ON; VIDEO / DEBUG -> TA SQ Direct ON,
     IFB Probe ON, JIT C-Calls ON (SPE Draw ON is the default); JIT SPEED ->
     JIT SQ STORES, JIT FTRV, JIT CONSTPROP, JIT DEAD VALUES ON, JIT SQ MISS
     + LOOK-AHEAD, JIT DYN IC + RTS. Set them on the main menu.
  2. Boot 1: VIDEO / DEBUG -> JIT FRCHG ON. Crazy Taxi, dense traffic,
     ~150 s, exit to the XMB. Log: `[jitcfg] ... frchg=1 pref=0`, `[ppe]
     ... frchg=` above 0, `[CC]` sync_fpscr near 0. Same-load VPS against
     `run516.txt`.
  3. Boot 2: JIT FRCHG ON and JIT PREFETCH ON, the same run. Log: `pref=1`,
     `[ppe] ... pref=` above 0. Same-load VPS against boot 1.
  4. On a fault: the same boot with the new option OFF.
- HW 2026-10-05, one boot with both ON (Matt: "Doing one boot"; a first
  try ran at 50 Hz and was redone at 60 Hz). Log `run529.txt` in the
  scratchpad of session 8112eaf0. `[jitcfg]` as `run516` plus `frchg=1
  pref=1`, SPE Wait Yield OFF; `[ppe]` frchg=193 pref=174 sites. Log clean
  (the old XMB 3 s fallback).
  - The fast path works: dense `[CC]` sync_fpscr 187.1K/s -> 2.4K/s.
  - 60 dense windows: VPS 59.3, SPEED 99.0%, 70% at full speed (`run516`:
    59.5, 99.2%, 80%). Same load -0.6% against `run516` (bins -3.2% to
    +0.8%, the heavy bins with few windows), -0.3% against `run516y`,
    +0.2% against `run528b`. Silent audio blocks/s 1.9 (1.4).
  - No measurable gain: removing ~185K full syncs/s did not show, so that
    path was cheap. Whether JIT PREFETCH costs a little is open (a boot
    with JIT FRCHG only would tell). Both stay default OFF.
- 5.31 JIT GUARD REUSE (Matt 2026-10-05: "Ok then, do them", the guard
  reuse proposal). Built and PC tested, package of 21:50, HW pending.
  - Facts: each fastmem access runs the RAM guard (`rlwinm`, `xoris`,
    `cmplwi`, `bge`; a dependent chain), each JIT SQ STORES store the SQ
    test (`rlwinm`, `cmplwi`, `bne`). Crazy Taxi's hottest vertex loop
    (0C081BC4) has 9 such accesses through 3 registers: 5 SQ stores through
    R6, 2 pair loads through R1, 2 loads through R4.
  - VIDEO / DEBUG -> "JIT GUARD REUSE" (`JitGuardReuse`), default OFF, set
    before boot; `[jitcfg]` prints `greuse=`, the `[ppe]` line `greuse=V/R`
    (checks that set a flag / accesses that reuse one, compiled since boot).
  - `ps3/ps3_jit_greuse.h`: a plan per block. An access through Rn (offset
    an immediate or none) whose check (RAM or SQ, the same kind) an earlier
    access through Rn ran, with Rn changed since only by add or sub of an
    immediate (@Rm+, @-Rn) and the address within 64 bytes of the checked
    one, reuses it. ifb and sync_sr end every group, any other write of Rn
    ends Rn's. The checking access sets a CR flag on its fast path (`crset`,
    cr2-cr4: C calls keep them, the JIT writes only cr0); its cold path
    clears it at the cold entry. A reusing access emits `bf flag,cold`, the
    same cold fragment as before, in place of the check: so a failed first
    check sends the later ones cold too. Up to 12 groups per block.
  - Exact unless the checked address lies within 64 bytes of the end of
    RAM's area or of the store queue's and a later access crosses it: no
    game walks a pointer across those inside one block.
  - `ps3_jit_sq.h`: `emit_sq_store` is now `emit_sq_check` +
    `emit_sq_body`, same words (`misc/sqstore_check` PASS, 12/12 mutants).
  - Expected: 2-3 instructions less per reusing access (in the hottest loop
    6 of 9 accesses, ~14 of ~164 instructions per vertex). Est. 0.5-1.5%,
    not measured.
- PC (WSL) for 5.31: `misc/greuse_check/run.sh` runs the header on
  constprop_check's SHIL interpreter. Directed: the 0C081BC4 shape (every
  second access of a base reuses), each cut (mov, add/sub of a register or
  from another register, shift, a load into the base, ifb, sync_sr, rd2,
  the reach, another kind), offsets and sizes at the reach's edge, twelve
  flags and a thirteenth group. Fuzzer: memory-heavy blocks plus
  constprop_check's decoder shapes, kinds as the driver or shuffled, 4
  states each with base registers in RAM, the store queue and near area
  ends. Structure (each REUSE names an earlier VALIDATE of its base and
  kind through only immediate steps, in reach; one flag per VALIDATE, in
  cr2-cr4) and run time (flags set and cleared as the driver does, cr0-cr1
  clobbered per op; a REUSE that runs unchecked must pass its check unless
  the checked address was near an area's end). PASS, seeds 1 and 7, 200000
  blocks each (~448K REUSE ops, ~1K edge cases each). Emitters against
  ppu-as (6 words). `mutate.py`: 25 of 25 planted bugs fail, 22 on the
  fuzzer alone (the other 3 are encodings, caught by the words).
- Build (package of 21:50): the 7 old warnings, `.got` 0xA690, 0 `r2off`.
- HW test steps (package of 21:50):
  1. Install. Settings as `run516.txt` (an install resets them; SPE Draw
     ON is the default), then VIDEO / DEBUG -> JIT GUARD REUSE ON. JIT
     FRCHG and JIT PREFETCH OFF. Set them on the main menu.
  2. Crazy Taxi at 60 Hz, dense traffic, ~150 s, exit to the XMB. Log:
     `[jitcfg] ... greuse=1`, `[ppe] ... greuse=` above 0/0, no fault.
  3. Analysis: same-load VPS against `run516.txt`.
  4. On a fault: the same boot with JIT GUARD REUSE OFF.
- HW 2026-10-05 (package of 21:50, our listener after Matt stopped the
  Halo-PS3 one; log `run531.txt` in the scratchpad of session 8112eaf0).
  `[jitcfg]` as `run516` plus `greuse=1` (frchg=0 pref=0, fence wait
  spins). 256 s, two rounds at 60 Hz. Log clean (the old XMB 3 s
  fallback). `[ppe]` greuse=6615/20994 at the end.
  - 119 dense windows: VPS 59.6, SPEED 99.4%, 77% at full speed (`run516`:
    59.5, 99.2%, 80%); 12K+ 99.6% in 10 windows (96.9% in 5). Same load
    +0.1% against `run516` (bins -1.9% to +5.2%), +0.7% against `run528b`.
    Silent audio blocks/s 1.1 (1.4).
  - No measurable gain; default OFF.
- The deepest dip of every Crazy Taxi run (VPS 40-44 at ~8.3K vertices,
  the same moment each time, also in `run528b`, `run516`, `run529`) is not
  SH4 speed: in that second the pacer slept at all 42 vblanks (`[pace]
  slept=42`), so one stall of ~300 ms ate the rest; the heartbeat pc was
  8C0028C6 (BIOS area) while CD audio streamed (`cdda/s=71.6`), and the
  audio starved (31 silent blocks). Likely a disc read on the SH4 thread:
  `OPTIMIZATION_POTENTIAL.md` P3 (async GD-ROM reads). One boot with
  Debug GD-ROM ON would confirm it.
- Debug GD-ROM boots 2026-10-05 (`rungd_ct.txt`, `rungd_sonic.txt` in the
  scratchpad of session 8112eaf0): the disc read is NOT the Crazy Taxi
  dip's cause (WRONG: these logs count reads, not their time; see the
  5.32 HW below). The dip second (VPS 44.7) had 2 reads, 57 sectors (29K
  words copied into RAM by the GD-ROM DMA in C), as many normal seconds of
  play (Crazy Taxi streams 3-10 reads/s all along); loading screens read
  up to 1774 sectors/s at full speed. Debug GD-ROM is not saved in
  nullDC.cfg, so the Sonic boot ran without it; its dips are at loads and
  scene changes (disc data into RAM up to ~1.4M words/s and JIT compile
  bursts of 1-3 MB/s), the intro at 40-44 VPS (CPU-bound, the SH4 never
  sleeps), and two short dips in play.
- 5.32 stall probe (Matt 2026-10-05: "It is worth a build to me"). Built,
  package of 22:22, HW pending. No speed change: it finds the dips' cause.
  - Rides on VIDEO / DEBUG -> JIT C-Calls (no new row: that page is full
    at 720p): with it ON, a `[stall]` line each second gives the SH4
    thread's slowest frame (vblank to vblank) split into exclusive times:
    jit (block compile, cache clear), gd read (host disc read), gd dma
    (copy into RAM), maple, dma (ch2), rend (VBlank, StartRender), spe
    (SPE TA ring waits), sys (UpdateSystem), log (the stats block), sleep
    (the pacer), and other (guest code and anything not listed); then the
    second's totals, counts and longest single section of each.
  - `ps3/ps3_stall.h` / `.cpp`: a section stack; a nested section pauses
    the outer one; the enable is latched at each vblank. Sections in
    `rdv_CompileBlock`, `recSh4_ClearCache`, `libGDR_ReadSector`,
    `UpdateGDRom`, `maple_DoDma`, `DMAC_Ch2St`, `VBlank`, `StartRender`,
    the SPE ring waits, `UpdateSystem_no_event` / `_handle_event`, the SPG
    stats block and the pacer's sleep; `stall_frame` first in
    `spg_PaceFrame`. Cost with JIT C-Calls ON: est. ~0.4% (two sections
    per timeslice).
  - Comment rule: maple_if.cpp (2 blocks) and dmac.cpp (1) trimmed;
    ImgReader.cpp's 92-line commented-out file reader removed.
  - Build: no new warning, `.got` 0xA790, 0 `r2off`.
  - HW steps: install; the usual settings with JIT C-Calls ON; Crazy Taxi
    at 60 Hz, one round through the dip; then Sonic Adventure through a
    level load and some play. Read the `[stall]` line of each dip second.
  - HW 2026-10-05 (package of 22:22, our listener; log `runstall_ct.txt`
    in the scratchpad of session 8112eaf0): Crazy Taxi at 60 Hz, 133 s.
    The dip second: worst frame 344.9 ms, of it gd read 330.0 ms (one
    read; 2 reads in that second, 335 ms). In the 56 windows at 6K+
    vertices: gd read 60.9 ms/s at 3.7 reads/s, and 11 windows had one
    read of 70-96 ms (or the 330 ms one); sys 64 ms/s (with the probe's
    cost), maple 7.7 ms/s, jit 2.4 ms/s. Cause: the CDI reader did one
    `fseek` + `fread` per sector (~2.5 ms each in play). Matt: the same
    reader slows Sonic Adventure. The Sonic boot was not done (Matt: "Just
    build the fix right away"). Fix: 5.33.
- 5.33 CDI batched reads (Matt 2026-10-05: "Just build the fix right
  away"). Built, package of 22:38, HW pending.
  - `cdi_DriveReadSector` (`plugs/ImgReader/cdi.cpp`) reads each run of
    sectors of one track with one `fseek` and one `fread` (up to 32
    sectors, a GD-ROM request, into a 78 KB buffer). Then it does the same
    per-sector work as before: the FAD 150 IP.BIN patch and
    `ConvertSector`. Sectors past the file end read as zeros (before: the
    buffer's old bytes). The old 2352-byte buffer overran on 2448-byte
    tracks; the new one holds 2448 bytes per sector.
  - PC: `misc/cdi_check/run.sh` (WSL) compares it byte for byte with the
    old one-sector reader on random track tables and requests (runs across
    tracks and gaps, past the file end, the FAD 150 patch): PASS, 20000
    requests. `mutate.py`: 14 of 14 planted bugs fail.
  - Build: no warning, `.got` 0xA790, 0 `r2off`.
  - Expected: much less disc read time in play (not measured).
  - HW steps (package of 22:38):
    1. Install. Settings as `runstall_ct.txt` (an install resets them),
       with VIDEO / DEBUG -> JIT C-Calls ON.
    2. Crazy Taxi at 60 Hz, one round through the dip, exit to the XMB.
       Compare the `[stall]` gd read times with `runstall_ct.txt`.
    3. Sonic Adventure through a level load and some play. Check that the
       loads and the graphics are right.
  - HW 2026-10-05 (package of 22:38, our listener at Matt's request; log
    `ps3log_533_run1.txt`, cut into `run533_ct.txt` and `run533_sonic.txt`
    in the scratchpad of session 8112eaf0). Settings as `runstall_ct`.
    - Crazy Taxi, 60 Hz, 132 s: clean. Read time per request: loading 6.29
      -> 5.87 ms, play 15.8 -> 14.6 ms (-7%); in windows without a long
      read 10.9 -> 10.8 ms. Dense (6K+ vertices, 56 windows): gd read 60.9
      -> 49.9 ms/s at 3.7 reads/s; windows with a read over 50 ms 11 -> 7.
      The long read stays: 333 ms (330). In both runs it is the first read
      after seconds with no reads (3 s in `runstall_ct`, 8 s now). Same
      load +0.7% against `runstall_ct` (noise); dense SPEED 99.5% (98.7%).
    - Sonic Adventure, 60 Hz, 195 s: clean. Loading 5.74 ms per request,
      play 4.49 ms (1.5 reads/s in dense play); longest read 169 ms; loads
      read up to 571 ms/s (VPS 37-47 there). Dense SPEED 99.8%, 94% of
      dense windows at full speed. No Sonic run with the probe before 5.33.
    - So the per-sector `fseek` + `fread` was not the main cost; each
      request costs ~6-15 ms, and a request after seconds of no reads can
      cost ~330 ms (likely the disk or lv2 waking). Open: newlib's stdio
      may still split each `fread` into small system calls (buffer size
      not checked); an unbuffered read would test that. The rest needs
      reads off the SH4 thread (`OPTIMIZATION_POTENTIAL.md` P3).
    - Matt 2026-10-05: "We can park them for now. It's okay." 5.33 stays
      in the code; the unbuffered read test and P3 are parked.
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
| Minicast fallthrough (M7) | our link sites are 3 words and cold code sits between blocks; almost no case to fall through |
| Minicast register allocator work, XMTRX kept in registers | we pin r0-r15 (not r11) and fr0-fr15; no free FPRs for the matrix |
| Minicast constprop level 2 (memory baking) | needs SMC page locks (mprotect) |
| Minicast perf map | needs Linux `perf` |
