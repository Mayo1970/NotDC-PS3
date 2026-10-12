# Optimization potential - what else to take from Reicast, Minicast and Flycast

Review of Reicast (`E:\Users\Matteo\Desktop\Dreamcast\reicast-emulator`) and
Minicast (`E:\Users\Matteo\Desktop\Dreamcast\minicast`) against our tree,
beyond the JIT items that are already in `OPTIMIZATION_PLAN.md` (5.17 Step 3,
5.20-5.24). Written 2026-10-04 (Matt: "check Minicast and Reicast to see if
there's other helpful stuff"). Review only: nothing is built.

Second review 2026-10-06 (Matt: "Anything worthy that we can take for our
emulator?"): Flycast (`E:\Users\Matteo\Desktop\Dreamcast\flycast`), `master`
(869038f40) and `origin/dev`. The dev branch has 124 commits that are not in
`master`. The first Flycast review (2026-09-23, `OPTIMIZATION_PLAN.md`
sections 1-4) never saw them. Five read-only reviews covered the unreviewed
commits, the SH4 JIT, the PVR and renderer, AICA/ARM7/audio, and the system
modules. Items P16-P40. Review only: nothing is built. Read dev files with
`git show origin/dev:<path>`. Do not check out the branch.

Third review 2026-10-06 (Matt: "Checking Flycast, Minicast and Reicast,
anything we can do to improve performance for FMVs?"): FMV speed. Items
P41-P43, plus FMV facts in P3 and P16. The facts come from old HW logs in
the session scratchpads. Review only: nothing is built.

Fourth review 2026-10-06 (Matt: "Check Flycast, Minicast and Reicast again.
Look up if there's things worth taking over. Things still not in
OPTIMIZATION_PLAN.md and OPTIMIZATION_POTENTIAL.md"): the sources that no
earlier review read.
- Minicast: `origin/r6b/tacore-opts` (it holds `r6b/anchored-loops` and
  `r6b/assorted-fixes`), `4e4ed42` (`r6b/anchored-loops-experiments`),
  `602d893` (`freerunning-ta`) and `f8235c6` (`static-fpr-alloc`): 34
  commits that are not in `main`, most from August 2026. Also the `main`
  commits that no earlier item cites.
- Reicast: every branch ahead of `alpha`, and the `alpha` files that this
  file never cited.
- Flycast: the options (`core/cfg/option.cpp`), `tests/src`, and the core
  and renderer files that this file never cited.
Items P44-P57. New facts in P2, P8, P10, P26, P34, P37, P40 and in section
4 (also for plan 5.17, 5.23 and 5.27). Five read-only agents did the
reading. The main claims of P44, P45, P47-P50, P54, P55 and P57 were checked
again in the code. Review only: nothing is built.

Fifth review 2026-10-08 (Matt: "Given the status of the repo, anything
that could we could take from Flycast or Minicast that we missed last
time?"):
- Minicast: no new commits. On 2026-10-08 all 9 branch heads on GitHub
  are the ones in our clone.
- Flycast: the commits after our clone (`master` 869038f40..0d9853df9, 14
  commits; `dev` 0a304f139..4dca1988d, 19 with merges). Our clone was not
  fetched; they were read from a scratch fetch. Run `git fetch` in the
  clone before reading them there.
- Flycast: the 526 core commits since 2018 that this file and the plan
  never cite, picked by subject (fixes, register and timing changes), in
  `core/hw/{sh4,pvr,aica,arm7,gdrom,holly,maple,mem,flashrom}`,
  `core/imgread`, `core/rend/gles`, `TexCache.cpp`, `transform_matrix.cpp`
  and `sorter.cpp`, without Naomi. Six read-only agents read them against
  our tree.
Items P58-P70. New facts in section 4 (also for P10, P22, P25 and P54).
The main claims of P58-P68 were checked again in the code, P58 also with a
big-endian PowerPC compile. Review only: nothing is built.

Sixth review 2026-10-11 (Matt: "Now that the project is more mature again,
please check if there's some stuff we can take from Reicast, Minicast and
Flycast"): a review by open fault, not by commit, because the upstream
trees hardly moved.
- Minicast: no new commits (all 9 branch heads are the ones in our clone).
  Reicast: `alpha` is still 2e32999c. caique-rtl: still 5aa2376.
- Flycast: `master` 0d9853df9..ed953fcbf is one translation commit; `dev`
  4dca1988d..ec56b5f26 is 2 commits (`bc3b8c71d`, a modem hang-up, in P83;
  `ec56b5f26`, the cycle model, in section 4). They were read from a
  scratch fetch again; our clone is still not fetched.
- Six read-only agents, one scope each, read our current code against the
  references: frame pacing in emulated time (the Looney Tunes race,
  Phantasy Star Online, Sonic Adventure, Capcom vs. SNK 2), render to
  texture and the output stage (the Crazy Taxi pause freeze), the draw
  path (the cost of TR Triangle Sort), the SH4 core with the MMU, the JIT
  and the disc swap, the sound output levels (the Looney Tunes balance),
  and the goals G10-G13.
Items P71-P85. New facts for P6, P10, P11, P14, P20, P25, P34, P37, P54,
P58, P60, P61 and P66, and corrections in section 4. The main claims of
P71, P72, P75, P77, P81, P84 and P85 were checked again in the code, P75
also in the PSL1GHT source. Several findings are faults in our own code or
in PSL1GHT that the comparison exposed, not takes from a reference. Review
only: nothing is built.

Merge 2026-10-06 (Matt: "Can you do the merge, then?"): the items with a
plan goal now sit in `OPTIMIZATION_PLAN.md` at that goal, with a short
entry; its section 0 lists where. Their headings here say "merged: plan
...". The text here stays the full reference (evidence, file and line
numbers). Items and parts with no plan goal stay only here.

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
One change per HW run, each behind its own toggle, default OFF until HW
validated, SH4 Clock 200.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

- 5.18 (JIT hot-block profile) is not part of this file. It stays the next
  step in `OPTIMIZATION_PLAN.md`.
- Every item was read in both trees. A claim that was not checked in the code
  is marked "unverified".
- Line numbers: P1-P15 from 2026-10-04, P16-P57 from 2026-10-06, P58-P70
  from 2026-10-08, P71-P85 from 2026-10-11. If a line moved, search for
  the function.
  `ps3/ps3_driver.cpp` changes often, so the new items cite it by function
  name.
- Reicast, Minicast and Flycast are little-endian only. Check every ported
  line for byte order.

---

## 0. Summary and comparison with the plan

| # | Item | Kind | Helps | Size | Relation to the plan |
|---|---|---|---|---|---|
| P1 | Texture path: palette snapshot, block decoders, 16-bit upload | speed, race | Soul Calibur (render-bound) | step 0 small, steps 1-3 medium | merged: plan section 0, parked Soul Calibur faults; also 5.12 SPE list #4, TArender.MD TR5 item 3 |
| P2 | Frame Drop = DROP A/B, then one pending slot | speed | Soul Calibur SH4 wait (22-28%) | A/B: no code; slot: medium | merged: plan section 0, parked Soul Calibur faults; TArender.MD TR5 item 1: "the A/B was not run" |
| P3 | Async GD-ROM reads + CD-DA read-ahead | hitches | loading, CD-DA games | measure: small; build: ~150 lines | = plan 5.34, HW SUCCESS 2026-10-06, default + CD-DA; CD-DA miss stalls known, parked |
| P4 | FPSCR.DN -> PPC NI bit | speed (unverified) | unknown | 1 line + option | CLOSED 2026-10-11 (Matt), no effect: NI does nothing on the PPE (`[ni]` probe, HW); "FPU DN" row removed 2026-10-12 (Matt); merged: plan 5.40; extends RESEARCH.MD 3e |
| P5 | Small speed items | speed | <= 1% each | small to medium | merged: plan 5.36; bind scan is 5.10 (not chosen) |
| P6 | JIT loop never stops | bug | every XMB exit, flash writeback | ~25 lines | = plan 5.35, HW SUCCESS 2026-10-06, default ON; EMU_TERM still never runs |
| P7 | SB_LMMODE0/1 ignored | bug | SQ and DMA texture uploads | ~40 lines | merged: plan 5.17 (with P55) |
| P8 | Intensity colour clamp order | bug | brightness (Reicast: Red Dog) | small | new |
| P9 | Hangs on rare input (5 fixes) | bug | robustness | 1-4 lines each | GD-ROM DMA part merged: plan 5.34; TArender.MD TR4 covers the store stage only |
| P10 | PVR register and list rules (7) | bug | unknown games | small to medium | new |
| P11 | TMU TCNT inside the timeslice | accuracy | Soul Calibur randomness (Flycast) | medium | merged: plan 6.4 |
| P12 | RTC DST adds 24 h | bug | clock under DST | 1 line | new |
| P13 | Puru Puru (rumble) pack | feature | rumble | ~120 lines | new |
| P14 | Fog, offset colour and bump map shader reference | reference | 3a, 3b; 3j: Rayman 2 bump maps | - | merged: plan 3a (built 2026-10-12, "Fog" row, HW SUCCESS 2026-10-12, default TABLE in source), 3b (open), 3j (done) |
| P15 | Freerunning (Minicast) | pacing option | - | medium | merged: plan 5.19; RESEARCH.MD says "not applicable" |
| P16 | Texture cache slots collide | speed | Soul Calibur texture decode (82-85% of the render thread) | 3 lines | merged: plan section 0, parked Soul Calibur faults (first); do before P1 and P2; HW SUCCESS 2026-10-06, default ON |
| P17 | Small JIT items: fschg, BCACHE | speed | noise level (estimate) | 5-10 lines each | fschg HW SUCCESS 2026-10-10; BCACHE HW SUCCESS 2026-10-11 (bundle 8, "JIT BCACHE" default ON; Looney Tunes race +2.3 points of SPEED); merged: BCACHE = plan 5.3 `bcache`, fschg = plan 5.37 |
| P18 | Depth precision | bug | Soul Calibur background flicker (probable cause) | ~15 lines | merged: plan section 0, parked Soul Calibur faults; "Depth Float" HW SUCCESS 2026-10-07, default ON |
| P19 | div32 match gives wrong results | bug | signed and 64/32 divides, every game | ~60 lines + PC check | = plan 5.38, HW SUCCESS 2026-10-06, default FIXED |
| P20 | The DSP: A3 by default, and its gaps against caique-rtl | default setting, bugs | no reverb or DSP-only voices by default; ring not shared with wave RAM; 64K-word rings | G1 no code; G2-G3 medium | merged: plan 5.13 A3 (the verdict); G2-G4 built 2026-10-11, bundle 5 (plan section 0); G2 ran on HW with no fault, default ON; G3, G4 HW not run |
| P21 | CD-DA during data reads, GD-ROM status timing | bug | Hydro Thunder, NFL Blitz 2001, Cool Boarders Burrrn, Ms. Pac-Man, Bust-a-Move 4 | 1 line, then ~60 | merged: plan 3h |
| P22 | GD-ROM command hangs and DMA rules | bug | Pro Pinball Trilogy, WSB 2K2, Sakura Taisen 3 | ~30-40 lines | DMA part merged: plan 5.34; commands: extends P9; corrects section 4 |
| P23 | TOC fields and the GDI parser | bug | GDI sets, V-Rally 2, Eldorado Gate | ~25 lines | TOC fields merged: plan 3h; GDI parser: new |
| P24 | Small G2 DMAs complete at once | bug | Bangai-O, Nomad Soul | 3 lines | built 2026-10-11 as "G2 DMA Small" (plan section 7, bundle 6), default OFF; HW run 1 (ChuChu Rocket): works, no fix for F1; the named games not run |
| P25 | Render rules: depth write, sprite cull, flat shading, trilinear, VQ formats | bug | Worms World Party, Bust-a-Move 4, Re-Volt, South Park Rally, NFL 2K2 | 1-5 lines each | new |
| P26 | Framebuffer output: write watch, interlaced and 24-bit reads | bug | Ikaruga loading screens, 2D screens | ~40 lines | GOALS.md G6.4 magic word |
| P27 | Modifier volumes in the TR list | bug | Xtreme Sports shadows | ~30 lines | merged: plan 3c |
| P28 | SPG timing: field bit, HBlank modes | bug | interlaced games (unknown) | small | new |
| P29 | Maple reply timing and commands | bug | Geist Force, Silent Scope | ~40 lines | corrects section 4 |
| P30 | P4 mirrors and operand-cache RAM banks | bug | NHL 2K2 | ~15 lines | new |
| P31 | AICA register details | bug | VC 2K sports games; nullAICA-only fixes | 1-10 lines each | CA clamp merged: plan 6.5; Mono and LFORE built 2026-10-11, bundle 5 (plan section 0), HW not run; rest: plan 2.2, 6.8 |
| P32 | SH4 interpreter: fneg with PR=1, mac.w with S=1 | bug | rare | 1-10 lines | mac.w merged: plan 5.3 `JitMac`; fneg: new |
| P33 | ELF loader: segments first | bug | homebrew .elf | ~30 lines | new |
| P34 | Disc swap | feature | Shenmue, Skies of Arcadia, Grandia II, D2, RE Code Veronica | ~150 lines | corrects section 4 |
| P35 | CUE/BIN and CHD readers | feature | more image formats | ~250 lines / 6-8K lines | new |
| P36 | Cheat files and 16:9 output | feature | widescreen codes | ~250 lines | new |
| P37 | Front-end extras: per-game VMU, VMU LCD, BIOS language, keyboard/mouse | feature | - | small each | P13 |
| P38 | Windows CE (SH4 MMU) | feature | WinCE games | ~1.5K lines (interpreter) | merged: plan 3d (those games need it) |
| P39 | reios (HLE BIOS) | feature | boot without `dc_boot.bin` | ~2K lines | low value |
| P40 | Flycast ARM7 tests as self-tests | test | rules out ARM7 engine bugs for F1 | medium | merged: plan 6.8 |
| P41 | FMV: interpreter fallbacks (addc, swap.b, xtrct) | speed | Sofdec FMVs: Sonic Adventure intro (SPEED 57-71%), Space Channel 5 (~94%) | no new code | = plan 5.2, HW 2026-10-06: Sonic FMV +23% VPS |
| P42 | FMV: YUV texture decode | speed | Soul Calibur opening (after P16), every YUV FMV | medium to large | merged: plan 5.12 (SPE list #4); P1 step 2, the Cg work of P1 and P14; (a) built 2026-10-07, "YUV Block Decode", HW pending |
| P43 | FMV: YUV converter on the SH4 thread | speed | Sofdec FMVs (~1-3% of the SH4 thread, estimate) | ~40 lines | merged: plan 5.12 (the converter stays on the PPU) |
| P44 | ARM7 resets in System mode, not SVC | bug | sound drivers that select SVC after reset; F1 and F3 unverified | 2 lines + a log probe | merged: plan 6.8 |
| P45 | Mipmaps: upload the whole mip chain | bug (image) | distant textures alias; about half the texture fills in Crazy Taxi and Sonic Adventure | ~70 lines | gives P25's trilinear weights a meaning |
| P46 | Flycast SH4 op tests as a PC check and a boot self-test | test | the interpreter runs every PR=1 FPU op | ~400 lines | merged: plan 5.3 `JitMac` (its check); P32 |
| P47 | GD-ROM REQ_STAT position bytes | bug | software that reads the CD-DA position (unknown) | ~8 lines | merged: plan 3h |
| P48 | GD-ROM SET_MODE writes at twice the offset | bug | SET_MODE with an offset (unknown) | 1 line | extends P22 |
| P49 | ARM7 PC reads: Rn = PC with a register shift, STR PC | bug | rare encodings (none in the hot-code dumps) | log census, then 2-3 lines per engine | merged: plan 6.8 |
| P50 | On-screen FPS and SPEED | feature | speed on the TV without the UDP listener | ~30-50 lines | new |
| P51 | Screenshot to PNG | feature | pixel evidence for visual faults | ~60-80 lines | merged: plan section 0, parked Soul Calibur faults (evidence) |
| P52 | Anisotropic filtering, texture filter override | quality | textures at steep angles | ~10 lines | after P45 |
| P53 | Widescreen: draw geometry outside 4:3 | feature | 16:9 output | ~20 lines | not P36's stretch |
| P54 | Render-to-texture pixel packing | bug | RTT games (none of our test games) | ~15 lines + modes 4 and 5 | merged: plan section 3 (RTT note); GOALS.md G6.4 |
| P55 | VRAM writes from SQ and DMA: one address map per block | speed | 0.5% of the SH4 thread or less (estimate) | ~10 lines | merged: plan 5.17 (with P7) |
| P56 | fmov, flds, fsts, fneg in the JIT when PR=1 | speed | PR=1 games (old logs: Space Channel 5 35-62 fmov/s, Quake3.cdi up to 3.5K/s, Rayman 2 1-3/s) | ~5 lines | built 2026-10-11 without fneg (bundle 8, "JIT PR MOVES" OFF), no HW run; merged: plan 5.39; fneg after P32 |
| P57 | Small: GDI `\` paths, `verify(RM<2)`, Flycast TMU test | bug, test | GDI sets in subfolders; WinCE; P11 | 1 line each; test ~100 lines | RM verify built 2026-10-11 (bundle 8, "JIT RM 2/3" VERIFY), no HW run; merged: plan 3d; TMU test: plan 6.4; GDI paths: P23 |
| P58 | MMUCR fields read the wrong bits on big endian | bug | Border Down (no picture); every game that uses the TLB for store queues | 2 lines; + JIT cache clear (~3) and V = 0 skip (1) | bundle 13 (2026-10-11); Border Down is an open item in plan section 0 |
| P59 | Flash broadcast byte: NTSC, as Flycast writes it | bug | Capcom vs. SNK 2, Border Down and Rayman 2 play at 50 Hz, not 60 | ~10 lines | bundle 15 (2026-10-11); corrects plan 3i ("Broadcast is not touched, as in Flycast") |
| P60 | Present only the renders the game shows | bug (image) | flashing screens and mirrors (South Park, NFL QB Club 2000, Vanishing Point, Metropolis Street Racer), FMV glitches; Quake III split screen | ~40-80 lines + ~30 | fits bundle 11; P2 has the swap rule as pacing only |
| P61 | CD-DA play limits and resume rules | bug | CD-DA that never stops, plays a data track, or stops looping after a resume | ~20 lines | fits bundle 2 (3h, P21 step 2) |
| P62 | GET_SCD format 1: Q data layout and length | bug | software that reads the CD-DA position (unknown) | 3-4 lines | extends P47 (plan 3h) |
| P63 | Face colours reset once per frame | bug | NHL 2K, NHL 2K2 (black players, black ice) | 1-2 lines per decoder | built 2026-10-11 in bundle 4 ("Face Reset", HW run, default ON) |
| P64 | FPSCR cause bits cleared on a write | bug | code that reads FPSCR.cause after an FPU op (Kita He, WinCE) | 1 line | fits bundle 8 |
| P65 | Ch2 DMA: length, 0x12 mirror, DMAOR mask | bug (hang) | games that wait for a ch2 end IRQ (unknown) | ~6 lines | fits bundle 1 (with P7) |
| P66 | Small TA and renderer fixes (8) | bug | Jet Grind Radio, DOA2 LE, Ooga Booga, Vigilante 8, Virtua Tennis, Skies of Arcadia (PAL) | 1-8 lines each | 3 built 2026-10-11 in bundle 4 (volume cap, data gate, invalid types; HW run, default ON); RTT parts: bundle 11 (P54) |
| P67 | Small SH4 and memory fixes (6) | bug | Resident Evil Code Veronica X; robustness | 1-30 lines each | fits bundle 8 |
| P68 | Small system fixes (8) | bug | Cheats 'N Codes; robustness | 1-15 lines each | maple: bundle 10; GD-ROM: bundle 2; G2: bundle 6 |
| P69 | Stretch to fill, VMU beep | feature | 16:9 with no geometry change; VMU alarms | ~10 / ~70 lines | stretch: bundle 11 |
| P70 | G11 facts from the new Flycast commits | reference | modem answer mode, Capcom Direct, DCNet | - | GOALS.md G11 |
| P71 | The x30 idle rule bills work blocks | pacing in emulated time | Looney Tunes race (20% of the billed SH4 cycles); Phantasy Star Online and Sonic Adventure unverified | 3 lines + an option | verdict 2026-10-12 (Matt): default X30, X3 BACKWARD as a level (it gives Phantasy Star Online and Sonic Adventure their 30 frames); built 2026-10-11 (bundle 8, "JIT IDLE RULE" X30); HW at X3: 29.95 frames per emulated second in the race, SPEED 60.9% (Matt: "It runs much worse"), X30 stays; corrects section 4 ("ours (x30) is stronger") and plan section 0 ("1 cycle per non-FPU op"); new fact for plan 5.25 and 5.45 |
| P72 | ps3gl state path: texture bind scan, no compare before set | speed | the render thread in every game; TR Triangle Sort in Crazy Taxi (busy 45%) | ~10 lines, then ~40 | bundle 14; = plan 5.10 (not chosen at 2.6-2.8%), now 11 points of busy; the parked cost of plan 3k |
| P73 | Small render-thread items (4) | speed | fewer draws and copies (not measured) | 4-30 lines each | bundle 14; plan 3k's parked "cheaper first pass"; counters first |
| P74 | Store-queue flushes with AT = 1 take the old chain | speed | Border Down, Rez, Ikaruga, Sonic Shuffle (after P58) | ~20 lines | bundle 13, after P58; plan 5.17 Step 2 and 5.44 do not act there |
| P75 | RTT read-back never returns (PSL1GHT `rsxSetTransferData`) | bug (hang) | the Crazy Taxi pause freeze; every render to texture | 3 + 2 lines | bundle 11, first; the open item of plan section 0; then P66's RTT scaler |
| P76 | Small RTT and present fixes (5) | bug | RTT games; static 2D screens | 1-8 lines each | bundle 11; new facts for P54 and P10; corrects section 4 (`9dbca9632`) |
| P77 | Disc Swap: the command in flight, the lid-open phase | bug (hang) | every disc swap (Disc Swap is not HW tested yet) | 8 lines less, then ~12 | bundle 2; corrects P34 (the Reicast `c050ee8f` clears) |
| P78 | Small MMU and BSC register fixes (3) | bug | WinCE (P38); robustness | 1-2 lines each | bundle 13 (with P58); RFCR: bundle 8 |
| P79 | Punch-through alpha is 1 after the alpha test | bug (image), speed | Langrisser Millennium (Flycast); Jet Grind Radio draws with no blend | ~6 lines | bundle 4; corrects P25 ("PT Blend") |
| P80 | Sprite fourth corner from the plane | bug (image) | trapezoid sprites (no game named) | ~15 lines PPU, ~30 SPU | bundle 4 |
| P81 | Sound output levels: the budget, and a probe | investigation | the Looney Tunes balance; the SPU clipping | 2 tests with no code; probe ~45 lines | bundle 5; extends P20 ("Output stage"); corrects plan section 0 and 6.6 |
| P82 | Small sound fixes (4) | bug | CD level meters (unverified); nullAICA only | 1-10 lines each | EXTS read-back: bundle 5 (P20 G4); lead-out: bundle 2 (P61); the rest not bundled |
| P83 | G11 facts: a first stage with no V.42, the flash account, the traps | reference | G11 (modem) | stage 1 ~1,600 lines, not 4,176 | GOALS.md G11 (not bundled); extends P70 |
| P84 | G12 facts: the device name and the edits; a radial stick dead zone | reference, small fix | G12; the stick feel in every game | dead zone ~15 lines | dead zone: bundle 10; the rest GOALS.md G12 |
| P85 | Flash defaults, and the flash save at exit | bug, release | a new flash boots in Japanese; console ID; settings lost on a forced exit | ~40 + 2 lines | bundle 15; GOALS.md G13 and G10; new facts for P37 and P6 |

Main negative finding: the SH4-side modules (scheduler, TMU, INTC, memory
handlers) take only ~1-2% of the SH4 thread (estimate). The ~10.6% that dense
Crazy Taxi needs is not there. 5.18 stays the way to find it.

Flycast review (2026-10-06): the strongest items are bugs in our own code that
the comparison exposed: P16, P18 and P19. P20 is a gap of the default sound
setup (no DSP). Nothing in Flycast explains F1 (ChuChu): on every register
the sound driver uses, Flycast reads the same as ours.

FMV review (2026-10-06): Sofdec FMVs are SH4-bound. Most of the lost time is
probably interpreter fallbacks for addc, swap.b and xtrct (P41 = plan 5.2,
already built). The Soul Calibur opening is render-bound by P16.

Fourth review (2026-10-06): again the strongest items are bugs in our own
code: P44 (ARM7 reset mode; Reicast and Flycast both fixed it in 2020), P47
and P48 (GD-ROM). P45 is the largest image gap: mipmapped textures draw
only level 0. Minicast's new JIT work (anchored loops, multi-block regions,
self-loops, fmov.d fusion, inline TA ring push) gives 0.3% or less each on
our backend, because every SH4 register is pinned (section 4).

Fifth review (2026-10-08): the strongest items are again bugs in our own
code. P58 is a big-endian bitfield layout: no game can turn the MMU on, and
it explains Border Down. P59: Flycast writes NTSC into the flash at every
boot, ours keeps the PAL byte, so three of Matt's games play at 50 Hz. P60
is the largest image gap: we flip every DC render, also the ones that the
game never shows. Minicast has no new commits. Rayman 2 is plan 3j.

Sixth review (2026-10-11): upstream has almost nothing new, so the review
went by open fault. The strongest items are again faults in our own code,
or in PSL1GHT. P71: the decoder bills small work blocks 30 times (Reicast:
3 times, Flycast: no such rule), which is 20% of the emulated SH4 time in
the Looney Tunes race. P75: the Crazy Taxi pause freeze is PSL1GHT's
`rsxSetTransferData`, which never returns. P77: Disc Swap clears the
command in flight, which Flycast stopped doing in 2024. P72: the cost of TR
Triangle Sort is the ps3gl state path (the bind scan of plan 5.10), not the
sort. P81: the Looney Tunes boot of the complaint played no CD audio, and
the level laws differ from Flycast by 1.5 dB at most. No new fact for F1
(ChuChu) or for the Capcom vs. SNK 2 register.

---

## 1. Speed

### P1 Soul Calibur texture path  `[ ]` merged: plan section 0 (Soul Calibur faults); steps 2-3 dropped by Matt 2026-10-10 (bundle 3)

2026-10-10: step 2's goal is met by SPE Tex Decode (5.12 #4, default ON):
the SPU writes the RSX order straight into RSX memory. Step 3 gives no
image and no measured speed. Matt dropped both (plan section 0, bundle 3).

2026-10-06: do P16 first. In the fight windows of `run526c.txt` (scratchpad
of session 3f596c0b), `[texfmt]` shows yuv=840 565=210 p4=0-7 per 5 s. That
is all of `tex decodes` (~1054), with `live` at 88. `[texfmt]` counts only
cache misses, so these decodes are misses, not palette or texel changes.
"Mostly palette textures" below is the TR strip mix, not what gets decoded.
Measure again after P16, before steps 0-3.

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
- Flycast form of the palette lookup (2026-10-06), for the "mostly palette"
  case:
  - Which textures, and the key: `core/rend/TexCache.h:163-201` (pal4/pal8,
    no mipmaps, no VQ, filter at most bilinear). The key leaves out
    PalSelect, so one index texture serves every palette.
  - A palette change never re-decodes (`TexCache.cpp:229-241`). The palette
    is converted at STARTRENDER, only when it was written
    (`texconv.cpp:81-154`). Converters that output 8-bit indices:
    `texconv.cpp:437-438`.
  - Shader: `core/rend/dx9/d3d_shaders.cpp:126-177` (1 tap, or 4 taps plus a
    lerp for bilinear). Palette texture: `d3d_renderer.cpp:1250-1272` (32x32
    A8R8G8B8).
  - RSX form: ps3gl needs a B8 texture format, a second texture unit for the
    palette, and a Cg fragment program with `palIndex` and `texSize`
    uniforms. Bilinear = 4 nearest taps at texel centres; the sampler applies
    wrap or clamp to each tap. Byte order: index byte k of a twiddled 8-byte
    block is at host byte k^3. Medium-large; it shares the Cg work with P14.
- Checks: PC test first (every format, mip and VQ case, random VRAM, old
  decode against the new one, bit-exact on the final RSX bytes). HW: Soul
  Calibur fight (`tex split`, busy, RPS, VPS), Crazy Taxi (no stale textures:
  FMV, menus, map).

### P2 Frame Drop A/B, then one pending slot  `[ ]` merged: plan section 0 (Soul Calibur faults)

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

Flycast (2026-10-06):
- `core/hw/pvr/ta_ctx.cpp:49-82`: by default it waits for the previous frame
  with no time limit. Option level 1 waits only while the SH4 ran at 85% or
  more over the last 4 vblanks (`spg.cpp:167-178`); else it drops.
- The render-done IRQ waits until the texture stage is done
  (`Renderer_if.cpp:591-609`).
- It presents when the game swaps: FB_R_SOF1/2 equals the last FB_W_SOF1
  (`pvr_regs.cpp:192-201`; commit `ce58ba347` notes that Soul Calibur swaps
  through SOF2). Its swap-interval detector (`Renderer_if.cpp:299-407`,
  `b03335bca`) does not apply: we flip once per DC render.
- PS3 form: the 85% rule as a fourth Frame Drop option, ~20 lines.

Steps:
- Step 1 (no code): Soul Calibur fight, Frame Drop = DROP against the default.
  Read `wait=`, `drop=`, RPS, VPS. This is the TR5 A/B that never ran. With
  DROP, the SH4 no longer leaves the core to the render thread (SMT), so RPS
  can fall.
- Step 2 (medium, if step 1 shows the trade): one pending slot that the newest
  STARTRENDER overwrites. It needs Reicast's context detach, so a ListInit on
  the pending address takes a free slot (there are 4). Keep the "second
  STARTRENDER redraws" rule (nullDC-v1).

Fourth review (2026-10-06), a trap for any "render-done waits for the
render" form:
- Minicast made that wait its default on `r6b/tacore-opts` (`58e4015`:
  `settings.pvr.FPSTarget = 0`, `libswirl/libswirl.cpp:272`;
  `rend_end_sch()` in `libswirl/hw/pvr/spg.cpp:349-358` polls until the
  render is done). Its `e856da0` fixes a wait that ended too early: until
  the queued render started, the done flag still came from the previous
  render.
- Ours: the wait must test this STARTRENDER's own job. A dropped frame
  leaves `rth_busy()` false (`ps3/ps3_rend.cpp:845-853`), which would end
  the wait at once.

### P3 Async GD-ROM reads + CD-DA read-ahead  `[x]` = plan 5.34, HW SUCCESS 2026-10-06

- HW 2026-10-06 (plan 5.34): dense Crazy Taxi disc reads on the SH4
  thread 50 -> 0 ms/s, 95% of dense windows at full speed (70-77%). Known,
  parked (Matt): with + CD-DA, a ring miss at a song start or loop still
  stalls the SH4 thread 24-85 ms (plan section 0).

- Built 2026-10-06 (plan 5.34, Matt: "Go with P3"): EMULATION -> "GD-ROM
  Async" OFF / DATA / + CD-DA. The 5.32 probe already passed the "measure
  first" gate below (gd read ~5% of the SH4 thread in dense Crazy Taxi, max
  330 ms); the `[gdr]` line measures per caller at every level. The disc
  lock is an lv2 `sys_mutex` (as `ps3_main.cpp`), not an lwmutex. PIO reads
  stay on the SH4 thread (measured; async only if they show).

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

FMV (2026-10-06): in the Sonic Adventure intro FMV (`ps3log_533_run1.txt`,
scratchpad of session 8112eaf0, after 5.33), `[stall] gd read` is 77.4,
15.3, 19.9 and 134.2 ms in four seconds (18-52 reads per second, max
28.6 ms), and the SH4 thread never sleeps. That is 1.5-13% of the SH4
thread. P3 stays parked (Matt 2026-10-05).

### P4 FPSCR.DN -> PPC NI bit  `[x]` CLOSED 2026-10-11 (Matt), no effect: the `[ni]` boot probe on HW shows NI = 1 changes no value and no time on the PPE (plan section 0); row "FPU DN" removed from source 2026-10-12 (Matt); merged: plan 5.40

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

### P5 Small speed items  `[~]` merged: plan 5.36; JIT SYS GATE HW SUCCESS 2026-10-06, default + RESUME; TA Peak Frame built, not run on HW

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

### P16 Texture cache slots collide  `[x]` HW SUCCESS 2026-10-06, default ON; merged: plan section 0 (Soul Calibur faults)

Built 2026-10-06 (package of 23:16): VIDEO / DEBUG -> "Tex Slot Hash", live, with the fix formula below
(`ps3/ps3_tex_slot.h`) and `evict=` / `hot=` on the `[texfmt]` line. No 16 probes: one change per HW run.
PC: `misc/texslot_check` PASS, 8 of 8 planted bugs fail. HW (Matt: "It's a success"): the Soul Calibur fight runs
at full speed, 0-8 decodes per 5 s. Default ON since the package of 23:46. Results: plan section 0.

Our bug, found by the Flycast comparison. Flycast keeps its textures in an
`unordered_map` with a 64-bit key (TSP size bits + TCW,
`core/rend/TexCache.h:185-218`), so two textures never fight for a slot.

Ours (`ps3/ps3_tex.cpp`):
- :442 `slot = ((key0 * 2654435761u) ^ key1) % CACHE_SLOTS`, with 1024 slots
  and 8 probes (:20-21, :445-455). key0 is the TCW; key1 is below 1024.
- `% 1024` keeps the low 10 bits of the product. With an odd multiplier,
  those bits depend only on the low 10 bits of key0, which are TexAddr bits
  0-9 (8-byte units). So textures whose addresses differ by a multiple of
  8 KB, with the same size and flags, start at the same slot and share one
  8-slot window. With more than 8 of them in use, each lookup evicts
  another one.

Evidence (`run526c.txt`, Soul Calibur fight, scratchpad of session 3f596c0b):
per 5 s window, `[texfmt]` yuv=840 565=210 (12 + 3 per render),
`tex decodes=1054`, `[ps3gl] texture uploads: 1054 alloc`, `live=88`.
`[texfmt]` counts only misses, so every decode is a miss while only 88
textures are live. That fits eviction. A key that changes every frame would
also fit, so measure.

Fix: take the slot from the high bits of a hash over all key bits, for
example `h = (key0 ^ (key1 * 0x9E3779B1u)) * 0x85EBCA6Bu; slot = h >> 22`
(1024 slots). Optional: 16 probes, and a counter of evicted entries that
were used in the last few frames. About 3 lines, no byte-order issue, low
risk.

HW check: Soul Calibur fight (`tex decodes`, `live`, render `busy`, RPS,
VPS). Crazy Taxi and Sonic Adventure: no stale textures.

FMV (2026-10-06): the Soul Calibur opening shows the same misses.
- `ps3log_517_run3.txt` (session daecf347, SH4 200) and
  `ps3log_a2hv_run1.txt` (session f83a8fb2): per 5 s, `[texfmt] yuv=11172`,
  11231 and 14101 against `tex decodes=11178`, 11237 and 14108. So every
  decode is a miss. `live` 29-32. `[ps3gl] texture uploads` 11178-14108
  alloc: each miss also allocates new RSX memory.
- Render thread 69-84% busy, decode 62-76% of its wall. The SH4 waited
  304-538 ms per 5 s, with one second at SPEED 65%.
- The movie is 80 opaque YUV quads over 640x480 (`[rendz] op n=80 ...
  fmt=0/0/0/80`), so most likely a 10x8 grid of 64x64 textures (inferred).
  A 64x64 YUV texture is 8 KB. If the tiles sit back to back, all 80 start
  at the same slot and share one 8-slot window. The addresses are not
  logged.
- After the fix, only the tiles that change should decode. In two windows
  a third of the decodes gave an all-black tile (`blacktex` 3596, 3615).
- HW check: also the Soul Calibur opening (`tex decodes`, `live`, busy).

### P17 Small JIT items from Flycast  `[~]` fschg HW SUCCESS 2026-10-10 (plan 5.37); BCACHE HW SUCCESS 2026-10-11, bundle 8 (plan section 0): row "JIT BCACHE", default ON in source (Matt); the Looney Tunes race SPEED 97.62% -> 99.95%, about 35 ms/s of SH4 thread time, more than the "noise level" below; Crazy Taxi no gain to see; merged: plan 5.3 (BCACHE), 5.37 (fschg)

Expect gains at noise level: 5.26 removed 1.9M load-hit-stores/s with no
measurable gain.
- fschg without a sync or a block end. Flycast
  (`core/hw/sh4/dyna/decoder.cpp:297-312`): fschg is an xor on FPSCR plus a
  decoder SZ flip; frchg is a bank swap; neither ends the block.
  `UpdateFPSCR` (`sh4_core_regs.cpp:132-140`) compares only FR, so a stale SZ
  in old_fpscr is harmless.
  - Ours (`dc/sh4/rec_v2/decoder.cpp:393-420`) emits a sync and ends the
    block outside delay slots. The SZ-only sync in `ps3/ps3_driver.cpp`
    reloads FPSCR right after the xor stored it: a load-hit-store on every
    fschg (hot block 0C0820C2 in `run518b.txt`). This is the "FPSCR 0.22M/s"
    item of 5.18.
  - Change: drop the `dec_End` and the SZ-only sync. `emit_frchg_test`
    (`ps3/ps3_jit_frchg.h:7-14`) must then mask bit 20 before its compare.
    About 10 lines. Blocks already assume one SZ at entry.
- Dynamic-end lookup (BCACHE). Flycast's dynamic ends and main loop do one
  masked load and a branch (`blockmanager.cpp:44`, `rec_arm64.cpp:1282-1309`,
  :1471-1491).
  - Ours: the lookup stub in `ps3/ps3_driver.cpp` (BET_Dynamic*) is about 16
    instructions: `ldx cache[idx]`, dependent loads of addr and code, and a
    `lookups++` load and store. Every rts takes it at the default DYN IC
    level. `loop_no_update` calls `bm_GetCode` in C after every timeslice
    (getcode 115K/s in 5.18).
  - The BCACHE path exists but cannot run on the PS3:
    `BlockCacheFlatEntry` (`dc/sh4/rec_v2/blockmanager.h:49-53`) is
    `{u32 addr; DynarecCodeEntry* code}`, 16 bytes on PPC64, and the emitted
    code expects 8 (it reads code with `lwz` at +4). Declare `code` as a u32
    (the code cache is in `.text` below 32 MB). Result: about 9
    instructions, two independent loads, no store. About 5 lines. Never run
    on the PS3.
  - An exact table (Flycast's way) needs 8M slots (32 MB) and aliases the RAM
    mirrors and the BIOS. Not proposed.

### P41 FMV: interpreter fallbacks (addc, swap.b, xtrct)  `[x]` = plan 5.2, HW 2026-10-06

- HW 2026-10-06 (plan 5.2): the Sonic Adventure FMV after boot ran +23%
  VPS (SPEED 69-71% -> 86-87%); play equal.

This is plan 5.2 (JIT Carry), built and waiting for its HW test. It changes
the 5.2 line "Expected: correctness only" for FMVs.

Facts (old HW logs, JIT Carry OFF, IFB Probe ON):
- Sonic Adventure intro FMV (`ps3log_533_run1.txt`, scratchpad of session
  8112eaf0, SH4 200): SPEED 66.3, 69.3, 71.2 and 57.5% in four seconds,
  `[pace] slept=0`, render thread 12% busy. `[IFB]` 3.3, 3.5, 3.0 and
  1.4 M/s: addc 1.3-3.4 M/s, swap.b 71-117 K/s, xtrct 15-36 K/s.
- The same FMV in `ps3log_a2_run2.txt` (session 43d8f55c, build of
  2026-10-03): SPEED 73-87%, addc 0.4-2.1 M/s.
- Space Channel 5 stage FMV (`ps3log_a2hv_run1.txt`, session f83a8fb2):
  SPEED 93.8-94.8%, `slept=0`, addc 1.0-1.2 M/s, swap.b ~157 K/s, xtrct
  36-46 K/s.
- Outside FMVs: 3-6 K/s in light scenes, ~41 K/s in dense Crazy Taxi (plan
  5.1c). The FMVs have 25-85x more.

Cost of one fallback:
- `shop_ifb` (`ps3/ps3_driver.cpp:3553`) spills and reloads every pinned
  GPR and the float file around the C call. The narrowed bracket
  (`ifb_bracket()`, :1401) is off: `get_ifb_flush_preset()` returns 0
  (`ps3/ps3_presets.cpp:298`).
- The probe line shows it: "full 62" memory ops per fallback, 189-218 M/s
  in the Sonic FMV seconds. The reloads come right after the stores, so
  load-hit-store stalls are probable (not measured).
- Estimate: these fallbacks are most of the missing SH4 time in the FMVs.
  Not measured.

Flycast, Reicast and Minicast compile these ops:
- Flycast emits adc, swaplb and xtrct (x64 `core/rec-x64/xbyak_base.h:197`,
  :143, :445; arm64 `core/rec-ARM64/rec_arm64.cpp:456`, :399, :816).
- Reicast x64 emits adc and swaplb
  (`libswirl/jit/backend/x64/rec_x64.cpp:391`, :338).
- Minicast decodes addc and swap.b to SHIL ops
  (`libswirl/hw/sh4/sh4_opcode_list.cpp:183`, :324; ARM32 adc
  `libswirl/jit/backend/arm32/rec_arm.cpp:1234`). Its xtrct stays in the
  interpreter (:168).

Ours: JIT Carry (`dc/sh4/rec_v2/decoder.cpp:147-166`, gate :916-918) puts
addc, swap.b and xtrct in the JIT, with subc, negc, addv, subv, div1,
cmp/str and clrmac.

Steps:
- Add one boot to the plan 5.2 HW steps: Sonic Adventure, wait on the title
  screen for the intro FMV (or play a Space Channel 5 stage), ~60 s, JIT
  Carry ON, IFB Probe ON, SH4 200. Compare `[IFB]` and SPEED with
  `ps3log_533_run1.txt`.
- Expected: `[IFB]` near 0 in the FMV and a higher SPEED. The size of the
  gain is not known.
- If 5.2 fails on HW, the narrowed bracket is the fallback: the probe line
  says it cuts the memory ops by ~94% ("sel"). It needs its own toggle and
  a HW test.

### P42 FMV: YUV texture decode  `[~]` merged: plan 5.12

2026-10-07 (Matt: "Let's work on the YUV Decode.", option (a)): built as
"YUV Block Decode" (`ps3/ps3_tex_yuv.h`, `ps3gl_tex_direct_begin()`), PC
tested (`misc/yuv_check`), default OFF, HW pending. Details and HW steps:
`OPTIMIZATION_PLAN.md` section 0. Options (b)-(d) not built.

Facts:
- Soul Calibur opening (`ps3log_a2hv_run1.txt`): ~270-280 us per tile
  decode, ~66 ns per texel if the tiles are 64x64 (inferred, see P16). RSX
  allocation is included: every decode was a miss (P16).
- One-texture FMVs from the YUV converter: Sonic Adventure intro ~10 ms per
  decode (render busy 10-12%), Looney Tunes ~22 ms (17-19%), Space Channel
  5 ~5.5 ms (11%) (`ps3log_a2_run2.txt`, `ps3log_a2hv_run1.txt`). Texture
  sizes are not logged.
- In these FMVs the SH4 thread is the limit, and the two threads share the
  PPE core (SMT). So the decode time also slows the SH4 thread (estimate).

Ours:
- `decode()` (`ps3/ps3_tex.cpp:296`): per texel, a twiddle-table lookup and
  a `vram16` load and select (:338-344 twiddled, :352-369 planar).
  `yuv_pair()` (:110-120): per pair, 4 multiplies by a constant, 3 signed
  divides and 6 clamps.
- Then a second pass: `glTexImage2D` -> `convert_pixels()`
  (`ps3/ps3gl/ps3gl_textures.c:649-651`), 4 byte loads per texel, written
  to RSX memory.
- Converter textures: `decode()` converts the whole texture.
  `yuv_conv_lookup()` (`dc/pvr/pvr_if.cpp:83`) knows the size that the
  converter wrote, but nothing calls it.

Reicast and Flycast: `convYUV_PL` and `convYUV_TW` (Reicast
`libswirl/rend/TexCache.h:285`, :413) read one 32-bit word per texel pair
(planar) or one 8-byte 2x2 block (twiddled), with no table per texel.
`YUV422()` (:129) does the same math as ours, per texel. The gain is in the
memory pattern and the single pass, not in the math.

PS3 options:
- (a) A block decoder that writes straight to the RSX buffer (with P1 step
  2: a ps3gl entry point that takes ready A8R8G8B8 data). Planar: one
  32-bit load per pair (the host word is the DC word
  U | Y0<<8 | V<<16 | Y1<<24). Twiddled: one 64-bit load per 2x2 block,
  as Reicast. Then 4 chroma tables of 256 entries built with the same
  formula (bit-exact), one clamp table, no multiply and no branch.
  Estimate: ~10-12 instructions per texel against ~60 now (both passes;
  not from the disassembly).
- (b) The RSX 4:2:2 formats: PSL1GHT
  `GCM_TEXTURE_FORMAT_COMPRESSED_B8R8_G8R8` (45) and
  `COMPRESSED_R8B8_R8G8` (46) (`ppu/include/rsx/gcm_sys.h:302-304`). The
  sampler shares the chroma of each texel pair, and a Cg fragment program
  applies the DC matrix. CPU work: a copy with a byte reverse (`lwbrx` /
  `stwbrx`) for planar textures, a detwiddle with a 16-bit pair shuffle for
  twiddled ones. The formats are linear (PSL1GHT gives them with the LIN
  bit, 0x20, set). Needs: a ps3gl texture format other than A8R8G8B8
  (`ps3gl_textures.c:494`), Cg programs (shared with P1 and P14), and a HW
  byte-order test with a non-uniform image. Filtering of these formats is
  unverified.
- (c) SPE: plan 5.12 SPE list #4 (texture decode on a miss).
- (d) Converter textures: decode only the rows and columns that
  `yuv_conv_lookup()` gives. The gain depends on the texture size against
  the video size (not logged). Unverified.

Checks: PC test first, bit-exact against the current `decode()` (random
VRAM; planar, twiddled and VQ YUV). HW: Soul Calibur opening (after P16)
and the Sonic Adventure intro FMV: `tex split` decode %, busy, SPEED.

### P43 FMV: YUV converter on the SH4 thread  `[ ]` merged: plan 5.12

Facts (Sonic Adventure intro FMV, `ps3log_533_run1.txt`): `[stall] dma`
(ch2 DMA, `dc/sh4/dmac.cpp:53`) is 15.6-41.6 ms per second, ~1 ms per
call, and `[CC] sqw` (store-queue C calls) is 125-135 K/s. Both can feed
the converter (TA area 0x10800000-0x10FFFFFF), but both also have other
targets. The split is not logged.

Ours (`dc/pvr/pvr_if.cpp`):
- `YUV_ConvertQuadrant` (:117-140): per output word, 4 byte loads, each
  with an address XOR (`host_ptr_xor`), then shifts and ORs.
- `YUV_data` (:219-258) copies every block into `YUV_tempdata` (:247),
  also when the block arrives whole. It reads TA_YUV_TEX_CTRL through
  `libPvr_ReadReg` on every call (:227) and every macroblock (:176).
  `YUV_RememberRegion` scans 4 slots per macroblock (:186).

References:
- Minicast `0e11e22` + `9d18e6d` (`libswirl/hw/pvr/pvr_yuv.cpp`):
  `YUV_Block384` with NEON zips, 16 output bytes per store.
- Flycast (`core/hw/pvr/pvr_mem.cpp:148-151`) and Reicast: no copy when a
  block starts at offset 0.

PS3 form (the PPU build uses `-mno-altivec`, `Makefile:58`):
- Load the planes as 32-bit words. The host word is the guest
  little-endian word, so plane byte k is bits 8k..8k+7 of one load. Build
  each output word (U | Y0<<8 | V<<16 | Y1<<24) with `rlwimi`.
- Convert straight from the source when a block starts at offset 0. Read
  the control register in `YUV_init` only (a CTRL write already calls
  `YUV_init`, :282-284).
- About 40 lines. Estimate: ~3x less converter work, ~1-3% of the SH4
  thread in a Sofdec FMV. Low priority.
- Plan 5.12 leaves the converter off the SPE list (Matt: "latency-bound,
  less important"). This form stays on the PPU.

Checks: PC test first, bit-exact against the current converter (random
planes, all four quadrants, blocks split across calls). HW: Sonic
Adventure intro FMV, `[stall] dma`.

### P55 VRAM writes from SQ and DMA: one address map per block  `[ ]` merged: plan 5.17

- Minicast `2d84a2e` (`r6b/tacore-opts`, `libswirl/gpl/lxdream/tacore.inl`
  `ta_write_polygon_buffer`): when a run stays inside one 4 MB VRAM bank,
  it maps the 32-bit address to the 64-bit layout once, then stores every
  other word. An inline fast path and a noinline cold path.
- Ours: `TAWriteSQ()` (`dc/pvr/pvr_if.cpp:357-361`) and the DMA path
  `TAWrite()` (:336-341) call `pvr_write_area1_32` once per word. Each call
  goes to the out-of-line `vramlock_ConvOffset32toOffset64()`
  (`dc/pvr/pvrLock.cpp:93-104`); the build has no LTO. A 32-byte SQ block
  never crosses the bank bit (bit 22).
- PS3 form: `u32* d = (u32*)&vram[conv(off)]; d[2 * i] = data[i];`. The
  DMA path needs a fallback when a run crosses a bank. About 10 lines, own
  toggle. It is the same branch as P7, so do the two together.
- Byte order: none. The stores are the same native u32 stores at the same
  addresses.
- Value: unverified. Dense Crazy Taxi sends ~127K SQ flushes/s to targets
  other than the TA (RAM, VRAM or YUV, not split; plan 5.17 Step 1 HW). If
  all were VRAM: about 0.5% of the SH4 thread (estimate). Count the
  VRAM-direct blocks in `[ta-split]` first.

### P56 fmov, flds, fsts and fneg in the JIT when PR=1  `[~]` built 2026-10-11, bundle 8 (plan section 0): row "JIT PR MOVES", default OFF, without fneg (P32 not done); HW 2026-10-12 (Quake3.cdi): 20 sites compiled, no fault in the log, no word on the image, the row stays OFF (a Looney Tunes run compiled no PR=1 move: that game has none); PR=1 FPU ops in the old logs: Space Channel 5, Quake3.cdi, Rayman 2; merged: plan 5.39

- Flycast `62085539a` (2022, `core/hw/sh4/dyna/decoder.cpp:660-667`): with
  PR=1 every FPU op falls back to the interpreter, except fmov, flds, fsts
  and fneg, which do not depend on PR.
- Ours: every FPU op with PR=1 is an interpreter fallback
  (`dc/sh4/rec_v2/decoder.cpp:942-943`), with a full register spill (the
  cost is in P41). Our fmov pair paths depend only on SZ. About 5 lines.
  Add fneg only after the P32 fix.
- Value: none seen. The Crazy Taxi census (plan 5.0) has no FPU op among
  the fallbacks. Build it only if a PR=1 game shows up in `[IFB]`.

### P71 The x30 idle rule bills work blocks  `[x]` HW 2026-10-11 and 12, verdict Matt 2026-10-12: default X30, X3 BACKWARD stays as a level for the games that gain (Phantasy Star Online, Sonic Adventure); what is left is the plan's list "more friendly for other users" (section 7, Bundle 8). Built 2026-10-11, bundle 8 (plan section 0): row "JIT IDLE RULE" X30 / X3 BACKWARD / OFF, default X30; HW 2026-10-11 at X3 BACKWARD (Looney Tunes): the race draws 29.95 frames per emulated second (26.4), but SPEED is 60.9% and the wall rate 18.2 (Matt: "It runs much worse"); the host cost is x1.64, not the +14% below; OFF: the race at SPEED 56.4%, the boot after the BIOS logo at 12.8% (Matt: "OFF is much worse."); X3 BACKWARD in Sonic Adventure: 29.69 frames/s (28.65), 14K+ scenes 29.6 (26.3), SPEED 100% (Matt: "VERY smooth"): Sonic has the spare host time that Looney Tunes lacks; X3 BACKWARD in Phantasy Star Online: its 20 fps scene at 30.0 frames/s, SPEED 100%; X30 stays the global default, a per-game level is open

- Ours: the "Small-n-simple idle loop detector" of the decoder
  (`dc/sh4/rec_v2/decoder.cpp:1316-1351`). A block with fewer than 6
  opcodes, a memory read, no memory write and no FPU op, that ends in bt or
  bf in either direction, bills its cycles x30 (:1319-1326). A block that
  branches to itself bills x10 more (:1328-1331). The cap is `max_cycles`
  (224). A literal load (`mov.l @(disp,PC),Rn`) counts as a read
  (`dc/sh4/sh4_opcode_list.cpp:220`).
- Reicast and Minicast (`libswirl/hw/sh4/dyna/decoder.cpp:1158-1168`): x3,
  and a bt block only when its branch goes back. Flycast removed the rule
  with its cycle model (`095e5ede8`, 2023-03-07). The x30 is the
  nullDC4Wii value (its `decoder.cpp:1409`); it is in our tree since the
  first commit. It is a Wii-era speed trade.
- Evidence, the Looney Tunes race (`run_prof.txt`, the profile of
  2026-10-08, scratchpad of session 898d8ac1):
  - 7 of the dumped hot blocks fall under the rule, and all are work: the
    loop tail 8C19CC68 (`add #1; cmp/hs; bf`), the command test of the
    vertex loop at 8C087382, the null checks 8C072F2C, 8C072EE6 and
    8C072FFA. They run 1.26M ops/s and bill 37.8M cycles/s: 20% of all
    billed cycles, about 7 ms of emulated time per frame.
  - A bound from the last census, not a count: the 64 hottest blocks are
    50% of the host instructions and bill 34.6M of 184M cycles/s. The
    other half (7.4M entries/s, about 38 host instructions each, so 8 ops
    at most) bills 150M, 20 cycles per entry. So 90M cycles/s or more
    (about 49%) cannot be 1 cycle per op.
- Crazy Taxi (`run525_ct.txt`): 4 work blocks under the rule, 13.6M
  cycles/s. No hot-block profile exists for Phantasy Star Online, Sonic
  Adventure or Capcom vs. SNK 2, so the rule's share there is not known.
- What it changes in the plan:
  - Section 0, Looney Tunes Step 2: "the JIT bills 1 cycle per non-FPU
    op" is not what the decoder does.
  - Step 3 (5.45): +25% cycles only moved the mean frame work from 2.27
    to 1.82 vblanks. So that test does not rule out the cycle budget for
    the 3-vblank frames that stayed.
  - 5.25 knew the rule for Crazy Taxi's wait loop (4 -> 120 cycles), but
    not that it also bills work.
- The same code bills blocks at 8C000000-8C00FFFF x5 (syscalls) or x15
  (IP.BIN) (:1335-1346; Reicast :1172-1184 the same, Flycast has none).
  No race block is there. Put it under the same option.
- PS3 form: a JIT option "Idle Rule": X30 (today) / X3 BACKWARD (Reicast) /
  OFF (Flycast), set before boot (the decoder bills at compile time). About
  3 lines and the option. Keep the `block.idle_loop` line (:1325): JIT IDLE
  SKIP (5.25) needs it at every level.
- Cost: it is the reverse of a speed trade. Without the rule the host runs
  more real SH4 ops per emulated second. Estimate: a game does a fixed work
  per frame, so 26.4 -> 30 frames/s is about +14% SH4 work in the race,
  where the SH4 thread sleeps 99.5 ms/s (51 ms/s at 30K+ vertices). SPEED
  may not hold in the heaviest scenes. A wait loop that JIT IDLE SKIP does
  not catch spins up to 30 times more turns per slice on the host (Capcom
  vs. SNK 2's 8C068808 has 0 skips).
- Risk: medium. Timing-sensitive code sees a faster SH4, as with a higher
  SH4 Clock, but the time bases do not change. With the rule off, the
  render-done delay is what keeps a variable-rate game from a flip at
  every vblank (section 4, sixth review).
- HW: one boot per level, the same Looney Tunes race: RPS against 26.4,
  SPEED, `[stall]` sleep. Then Phantasy Star Online (its 20 fps scene) and
  a dense Crazy Taxi drive (SPEED must hold). A proof with no HW run: the
  sum of entries x (billed - plain cycles) on the `[HOTC]` line.
- Byte order: none.

### P72 ps3gl state path: texture bind scan, no compare before set  `[ ]`

- Flycast keeps a GL state cache (`core/rend/gles/glcache.h:11-28`,
  :57-70, :85-112, :126 on): every setter compares first. A run that
  changes only its texture sends one bind and one draw.
- Ours (ps3gl):
  - `glBindTexture` finds the texture with `ps3gl_texture_find()`, a
    linear scan of up to 4096 slots (`ps3/ps3gl/ps3gl_textures.c:111-119`);
  - every `glTexParameteri` sets the texture dirty (:902-918), so the next
    bind sends 4 RSX texture calls again (:1037-1058);
  - the state setters mark their group dirty with no compare
    (`ps3/ps3gl/ps3gl_states.c:57-246`, :405-537);
  - each draw binds 4 vertex attributes
    (`ps3/ps3gl/ps3gl_vertices.c:206-240`).
  The callers are `ps3/ps3_tex.cpp:942-944` and `apply_param()`
  (`ps3/ps3_rend.cpp:1681-1782`).
- Evidence: Crazy Taxi at TR Triangle Sort + OPAQUE DEPTH (HW run 5 of
  plan 3k, `ct_last.txt`, scratchpad of session c485c4b0; 32 windows at
  9K+ vertices). Render busy 45.2% = param 21.0 (texture 15.4, of it the
  bind `gl` 11.1, other 3.5) + glEnd 12.5 + sort 3.6 + vertices and other
  4.6. A draw costs 4.17 us, 1.37 us of it the bind. So the cost of 3k is
  the state per draw, not the sort and not the vertex copies.
- Plan 5.10 has the bind scan as "not chosen", at `gl` 2.6-2.8% of the
  render thread. With 1064 TR draws per render it is 11 points of busy.
- PS3 form:
  1. A direct table from the GL name to the slot (the names come from our
     own `glGenTextures`). About 10 lines.
  2. Compare before set in the texture parameters and the state setters,
     and no attribute bind when the pointers did not change. About 40
     lines.
  One ps3gl flag keeps the old path for an A/B.
- Trap: every place that changes RSX state behind ps3gl (the UI, the
  framebuffer present, the RTT, `reset_param_cache()`) must reset the new
  cache too.
- Value: Crazy Taxi SPEED 97.9% in 11-13K vertex scenes at + OPAQUE DEPTH,
  Sonic Adventure busy 20%; every opaque draw also binds. Confidence: high
  for the bind (measured). The gain of the compares is not measured (the
  glEnd share is not split).
- HW: a dense Crazy Taxi drive at + OPAQUE DEPTH: busy, the `gl` share,
  SPEED at 11-13K vertices. Byte order: none.

### P73 Small render-thread items  `[ ]`

- The first pass of + OPAQUE DEPTH (plan 3k, the parked "cheaper first
  pass"). The sort leaves about 2.4 triangles per run (Crazy Taxi: 1240
  triangles, 1064 TR draws in the 2 passes), so about 530 draws are the
  first pass.
  - Ours (`ps3/ps3_rend.cpp:2186-2203`) calls `apply_param()`, bind
    included, for every pass-1 run, also for the runs it never draws
    (`tri_covers` 0: 71 of Rayman 2's 253 strips), and it draws ZWriteDis
    runs with every mask off (:1769).
  - Flycast's depth pass (`core/rend/gles/gldraw.cpp:327-358`; Reicast
    `rend/gles/glesdraw.cpp:837-867`) skips ZWriteDis polygons and sets
    only the cull mode.
  - Skip both cases; `alpha_rule()` (:1921) gives the vertex alpha. About
    4 lines. Crazy Taxi has neither case: its gain is a first pass in game
    order (the depth pass needs no sort, so it can draw the merged
    strips).
  - Flycast's trick cannot replace ours: it writes the depth of every
    texel, after the colour. That is our + DEPTH WRITE level.
- Merge key bits that the draw never reads. Our strip-merge key is
  Flycast's (`core/hw/pvr/ta_ctx.h:86-92`; ours `ps3/ps3_rend.cpp:1867-1872`,
  the cache at :1672; the SPU builder was not read). Flycast's shaders use
  every bit. Our draw ignores PCW Gouraud (with Flat Shading OFF), Offset
  and Volume; TSP MipMapD, SuperSample, ColorClamp, FogCtrl, SrcSelect,
  DstSelect and FilterMode beyond zero (`ps3/ps3_tex.cpp:815`); and ISP
  DepthMode in an autosorted list (:1711). One Crazy Taxi frame has 331 of
  972 opaque strips without Gouraud. A mask on these bits merges more
  strips: fewer draws, the same picture. Gain not measured: a counter
  first. About 10 lines, in both builders. The bits must come back with
  plan 3a/3b (FogCtrl, Offset, ColorClamp) and with Flat Shading ON.
- Texture lifetime. Flycast and Reicast delete only a texture whose VRAM
  was overwritten (`core/rend/TexCache.h:244-264`; Reicast
  `rend/gles/glestex.cpp:823-845`). Ours frees every texture not used for
  180 frames (`ps3/ps3_tex.cpp:43`, :797-803). Log: 4731 cache-miss
  decodes in 383 s of Crazy Taxi, with 323 live at most. How many are
  returns of a freed texture is not counted: a counter first. Keep a cap
  (RSX local memory).
- An index list for the sorted triangles. Flycast and Reicast draw the
  sorted list through indices (`core/hw/pvr/ta_util.cpp:156-171`; Reicast
  `glesdraw.cpp:735-789`). Ours copies 3 vertices per triangle (108 bytes,
  27 word stores; `ps3/ps3_rend.cpp:1883-1900`, :2206-2210): Crazy Taxi
  134 KB per render, against 7 KB of indices plus 69 KB of vertices once.
  ps3gl has `rsxDrawIndexArray` (`ps3/ps3gl/ps3gl_draw.c:295`). The
  ceiling is small: vertices and other are 4.6 points of busy in all.
  Low.
- Order: after P72, which takes the larger share.

### P74 Store-queue flushes with AT = 1 take the old chain  `[ ]`

- After P58, a game with MMUCR.AT = 1 flushes through `do_sqw_mmu`
  (`dc/sh4/sh4_sqw.h:15-47`; from the JIT PREF, `ps3/ps3_driver.cpp:4343`
  -> `dc/sh4/sh4_cpu.cpp:40`): `TAWriteSQ` -> `libPvr_TaSQ`, or
  `WriteMemBlock_nommu_ptr`. The flat path (`ps3/ps3_rend.cpp:1151-1171`,
  :1224-1252) reads QACR only, so TA SQ Direct (plan 5.17 Step 2) and RAM
  SQ Direct (5.44) do not act for these games.
- Flycast has no fast path there either (`core/hw/sh4/storeq.cpp:77-80`).
  This is our own work, not a take.
- PS3 form: an MMU form of the flat flush, with target =
  `sq_remap[(dest >> 20) & 0x3F] | (dest & 0xFFFE0)` (the rule of Flycast
  `core/hw/sh4/modules/mmu.cpp:567`). The RAM copy must use the translated
  address. About 20 lines, own toggle.
- Helps: Border Down (it builds its lists in RAM through the store
  queues), Rez, Ikaruga, Sonic Shuffle. Gain not measured. Build it only
  if one of them is SH4-bound after P58.

---

## 2. Bugs

### P6 The JIT loop never stops (XMB exit)  `[x]` = plan 5.35, HW SUCCESS 2026-10-06

- Correction (2026-10-06, plan 5.35): nothing calls `Term_DC()`, so
  `EMU_TERM` does not run even when the loop returns. The loop fix alone
  only removes the 3 s forced exit. The build saves the flash itself after
  `sh4_cpu.Run()` returns (Matt: "Save the flash only"); the rest of
  `EMU_TERM` stays unrun. Also needed: the PS3 no longer frees the DC
  memory at exit (the SPUs write AICA RAM until the process ends), and the
  loop frame keeps f14-f31 and cr2-cr4 (callee-saved; FPU PIN and JIT GUARD
  REUSE write them).

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
- Flycast (2026-10-06): it tests CpuRunning once per timeslice, as this fix
  does. Dev `bcbc43fad` ("end the timeslice on Stop") adds nothing for us
  (section 4).

### P7 SB_LMMODE0/1 ignored  `[ ]` merged: plan 5.17

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

### P8 Intensity colour clamp order  `[~]` "Intensity Clamp" built 2026-10-11, bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt)

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
- Fourth review (2026-10-06):
  - lxdream's TA core (Minicast `3da5e8a`) uses the same order as Reicast:
    the face colour becomes bytes once at the header, the intensity is
    clamped to 0..1, then each channel is (face byte x i) >> 8 with
    i = 256 x I. Alpha stays the face alpha.
  - Cost fact: our SPU decoder does 3 double-precision multiplies per
    intensity vertex (`mul_byte()`, `ps3/spu/spe_dec.c:84-99`, called at
    :141-144). The integer form of P8 removes them. On the Cell BE SPU,
    double precision is not fully pipelined (Cell documentation; not
    measured here).

### P9 Hangs on rare input  `[~]` GD-ROM DMA clamp ("GD DMA Fix") and 2340-byte sectors built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source; the TA replay built 2026-10-11 as "TA Bad Records", bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt); the AICA item built 2026-10-11 as "G2 DMA Read", bundle 6 (plan section 0), default OFF, never used on HW (ChuChu Rocket sends no SB_ADDIR = 1 transfer); the maple item open

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
- More GD-ROM hangs, found in the Flycast review: P22.

### P10 PVR register and list rules  `[~]` "Region Array", "List Passes", "Tile Clip", "TA List Regs", "Core Reset" built 2026-10-11, bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt); Empty Frame left out (Matt); the screen clip and scaler are bundle 11

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
- Flycast (2026-10-06), for the LIST_CONT item:
  - Opaque blending is forced off only in pass 0
    (`core/hw/pvr/ta_vtx.cpp:1248-1255`), so opaque polygons in later passes
    do blend.
  - Each pass draws OP, PT, MV, then TR (`gldraw.cpp:594-641`). A TR depth
    pass ends each non-final pass (`gldraw.cpp:327-358`, Cosmic Smash).
  - Contexts are keyed by TA_OL_BASE stamped into the tile blocks
    (`ta.cpp:463-520`) and found per pass through the region array
    (`ta_util.cpp:281-319`). z_clear is parsed, but no Flycast renderer uses
    it.
  - No log line of ours counts LIST_CONT, so the Soul Calibur case is
    unverified.
- Fourth review (2026-10-06), for the screen clip item: FB_X_CLIP and
  FB_Y_CLIP are post-scaler values. Flycast scales the clip before it uses
  it as a scissor: x by 2 when SCALER_CTL.hscale is 1, y by
  vscalefactor / 1024 (`core/rend/transform_matrix.cpp:130-144`,
  `getScissorScaling()` :191-203). Our X Scaler option doubles the canvas
  (`plugs/drkPvr/SPG.cpp:84`), so our scissor must scale the clip the same
  way.

### P11 TMU TCNT inside the timeslice  `[x]` built 2026-10-11 as "TMU Fine TCNT", bundle 7 (plan section 0); HW run 2026-10-11 (ChuChu Rocket: boot, load and menu, no fault), default ON since the package of 22:16 (Matt); **default OFF again in source since 2026-10-12 (Matt)**: about 95 ns per TCNT read, Quake3.cdi 10-17 SPEED points slower with it (plan section 0, bundle 8 runs 15 and 17); merged: plan 6.4

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

### P18 Depth precision (Soul Calibur background)  `[x]` merged: plan section 0 (Soul Calibur faults); option (a) as "Depth Float", HW SUCCESS 2026-10-07, default ON

- Ours: `ps3/ps3_rend.cpp` `draw_scene()` (:1827-1858) maps depth to
  z = 1 - n/W (:1854 `m[14] = -2n`) in a fixed-point Z24S8 buffer
  (`ps3/ps3_video.c:94`). n = 1 / (largest 1/W of the frame x 1.001).
  Modifier-volume corners also feed n (:572-582).
- That is linear in 1/W, with a step of 2^-24 / n. In the Soul Calibur fight
  logs n = 0.00019-0.00048 (`near=` on the `[rend]` line; the HUD sets it:
  PT w=[0.0005..0.01], TR down to 0.0002), and the stage runs to W
  960-2550. So the 1/W step is 1.2-3e-4: 6-15% of the value at W=500 and
  30-75% at W=1000-2500. The background then z-fights, with SPE Draw ON and
  OFF. That fits the parked fault.
- Flycast: `core/rend/gles/gles.cpp:353-359` and
  `core/rend/dx9/d3d_shaders.cpp:279-286` write a per-pixel depth
  `log2(1 + 1e5 / W) / 34` (commit `4b5e863aa`, "Fixes many depth issues").
  Its near value fZ_max only feeds the GLES2 fallback. Modifier-volume z is
  left out of it (`332ab95b6`, `ta_vtx.cpp:1146-1162`).
- PS3 options:
  - (a) RSX float depth: `rsxSetDepthFormat(context, GCM_DEPTH_FORMAT_FLOAT)`
    (PSL1GHT `ppu/librsx/commands_impl.h:302`, `gcm_sys.h:430`) with a
    reversed mapping z = n/W (m[10] = -1, m[14] = 2n). Clear depth to 0, use
    the DC depth-function table without the flip, and GL_GREATER for
    modifier volumes. About 15 lines, no shader. The RSX float-depth
    behaviour is unverified (HW test).
  - (b) Flycast's way: a Cg fragment program that writes DEPTH, with W passed
    as a TEXCOORD. It loses early Z.
  - Small extra step for both: leave modifier-volume z out of n.
- HW check: Soul Calibur fight (background stable); Crazy Taxi, Sonic
  Adventure and the BIOS menu without new depth faults.
- Built 2026-10-07 (plan section 0, "Depth Float"), with one correction to
  (a): m[10] = -1, m[14] = 2n under GL's depth range 0..1 computes
  (z/w + 1) / 2, which rounds n/W back to the 2^-24 step. The build uses
  m[10] = 0, m[14] = n and `glDepthRange(-1, 1)` (ps3gl viewport z scale 1,
  offset 0), and also GEQUAL for punch-through and autosort.
  `misc/depth_check` checks it on a model of the RSX depth path. HW SUCCESS
  2026-10-07: the Soul Calibur flicker is gone, and Sonic Adventure text and
  a Power Stone 2 fault are fixed too; default ON since the package of 01:01.

### P19 div32 match gives wrong results  `[x]` = plan 5.38, HW SUCCESS 2026-10-06

- Ours: the matched `div0u`/`div0s` + 32 x (`rotcl`; `div1`) becomes one
  divide (`dc/sh4/rec_v2/decoder.cpp:1158-1186`). But rs3 is empty (:1163,
  :1173), so the high dividend word is dropped. The canonical ops
  (`shil_canonical.h:439-491`) and the native cases in `ps3/ps3_driver.cpp`
  do a 32/32 `divwu`/`divw` with an unsigned-only fixup. This is the
  devcast/Reicast form from before 2019. It is ON by default; only JitSafe
  turns it off.
- Flycast (`core/hw/sh4/dyna/decoder.cpp:830-910`,
  `shil_canonical.h:547-651`, test `tests/src/div32_test.cpp`): a 64/32
  divide with the high word from reg3. The signed form takes and returns
  one's-complement values. It carries the quotient sign in T bit 31 into the
  remainder fixup, then cleans T back to 0/1. Commits `e0949fb9a` (2019),
  `8740f22c0` (2020), `5cfed1e48`, `eb2c76221` (divide by zero, Sakura
  Taisen 2).
- Simulation (200K random cases per row, our interpreter's div1 chain as
  the reference; `div32sim.py` in the scratchpad of session 813810f6):
  - unsigned 64/32: ours wrong in 100%;
  - signed with the SH manual idiom (dividend sign-extended into reg3): ours
    wrong in 49% after the chain and in 32% of final quotients (15119757 / -5
    gives -3023950; hardware gives -3023951);
  - unsigned 32/32 and non-negative signed: ours right;
  - Flycast's form: 0 mismatches in every row.
- The signed idiom matches in Crazy Taxi, Sonic Adventure and Soul Calibur
  (`run525_ct`, `run525_sonic`, `run525_soul`, scratchpad of session
  1a0d7654: 8C00CF7E, 8C09108C, 8C0911EC, 8C00D0DE). Flycast's per-game safe
  mode list for this bug (removed in `8740f22c0`): Demolition Racer, Ducati
  World, Aqua GT, Rayman 2, Elysion, Silent Scope, Power Stone, Metropolis
  Street Racer, Donald Duck. `e0949fb9a` fixed Pro Pinball Trilogy, Star
  Wars Racer, Record of Lodoss War and Surf Rocket Racers.
- Correction (5.38 HW, 2026-10-06): 8C09108C and 8C0911EC are the BIOS
  boot animation's SDK division routine, run before every game. Game code:
  Sonic Adventure and Rayman 2 match the same routine at their own
  addresses; Crazy Taxi decoded no div0u/div0s after the game started.
- PS3 notes:
  - `divdu`/`divd` read 64 bits. Build the dividend with `clrldi` on reg1
    and `rldimi` for reg3, and `extsw` the signed divisor (pinned registers
    have poisoned upper halves).
  - Keep T at 0/1 for JIT T COPY, T forwarding and CR0: hold the sign in a
    temp, or end with `and T,T,1` as Flycast does.
  - The PPC does not trap on divide by zero. Flycast returns quo = 0,
    rem = dividend.
  - The `[DIV]` probe's 16 slots fill with boot sites, so game sites never
    print. Skip pc < 0x8C010000.
- About 60 lines. PC check first: port `div32_test.cpp` plus random cases
  into a `misc/*_check`, against the interpreter.

### P20 The DSP: A3 by default, and its gaps against caique-rtl  `[~]` merged: plan 5.13 A3; G1 done; G2-G4 built 2026-10-11 as "DSP Ring RAM", "DSP Ring 64K" and "DSP Reg Reads", bundle 5 (plan section 0); DSP Ring RAM ran on HW 2026-10-11 with no fault (PSO), default ON; the other two OFF, HW not run

Missing DSP in the default setup:
- Default setup: SPE AICA Sound ON, SPE AICA DSP OFF
  (`ps3/ps3_presets.cpp:80-81`). That is A2: the SPU model runs with
  `no_dsp` (`ps3/spu/spe_aica_model.cpp:117`). It skips EFREG in
  `final_mix()` (`plugs/caiqueAICA/aica_model.cpp:1094`), and its slot path
  mixes only the direct level (:773-776, `slot_fast<true>`). nullAICA's DSP
  is a stub (`plugs/nullAICA/dsp.cpp`, DSPEnabled = 0).
- So the default setup has no DSP effects (reverb, echo), and a voice with
  DISDL = 0 that only feeds the DSP is silent. On hardware such a voice is
  heard only through the DSP program's output (EFREG at EFSDL/EFPAN).
- The fix is the DSP itself: A3 (AUDIO -> "SPE AICA DSP"). It is bit-exact
  against caique on the PC and was HW tested on 2026-10-04 (`bad=0`). Matt's
  verdict is pending. Cost: SPU time (plan 5.13 "A3" table).
- Do not port Flycast's dry mix (`core/hw/aica/sgc_if.cpp:709-710`:
  `oLeft = oRight = oDsp >> 4` when a voice has no direct output). Matt
  2026-10-06: "the DSP fix is wrong".
  - It is Flycast's fallback for its own default: `aica.DSPEnabled` is false
    (`core/cfg/option.cpp:48`).
  - It is not hardware behaviour. It plays the send dry, centred, at the IMXL
    level, and ignores the DSP program, EFSDL and EFPAN. A pure reverb send
    becomes a dry copy, and a send whose effect output is off becomes
    audible.
- Ear tests against Flycast: its default has no DSP either. Turn its DSP
  option ON to compare reverb.
- May relate to F3 (Looney Tunes voices silent): not shown. Test: boot
  Looney Tunes with SPE AICA DSP ON.

caique-rtl check (2026-10-06; Matt asked to check caique-rtl for the DSP "now
that the project is more mature"):
- caique-rtl (`E:\Users\Matteo\Desktop\Dreamcast\caique-rtl`) has not
  changed since we copied it: the local clone and GitHub `main` are both at
  `5aa2376` (2026-09-24), and `plugs/caiqueAICA` comes from that commit. So
  our DSP math is caique's console-tested DSP (`model/NOTES.md` sections
  "DSP", "Channel / DSP collisions" and "MIXS retention"; cases dsp_basic,
  dsp_mem, dsp_mem2, dsp_mem3, dsp_pack, dsp_unpack, dsp_temp, dsp_wslot,
  dsp_coll, mixs_write, mixs_rd). The SPU recompiler gives the same results
  bit for bit (dspjit_check: 28,377,429 checks, 0 bad; HW `bad=0`).
- caique's cycle model and `rtl/v1` only add timing inside a sample:
  `cycle-model/replay_all.sh` gives the same output samples as the sample
  model in all 65 cases. Nothing to take for the DSP.

Gaps in how we run it:
- G1. The default has no DSP (above). Fix: A3 ON, with SPE DSP JIT = ON
  (CHECK costs ~2.5x the SPU time).
  - A3 HW 2026-10-04 at SH4 150 (5.13 "A3"): SPU busy 12-16% in Crazy Taxi
    (ON; CHECK 31-40%), 22.6% average and 35.4% max in Sonic Adventure. Dense
    SPEED equal to A2 within noise.
  - No logged run has A3 and SPE ARM7 both ON: every `[sndcfg] boot:` line
    with SpeArm7=1 has SpeAicaDsp=0. SPE ARM7 is default ON since
    2026-10-05.
- G2. The DSP ring is not in wave RAM. On the console and in caique the ring
  is plain wave RAM (`model/sample-model/aica_model.cpp:767`, :771: `ram[ba]`).
  Ours keeps it in SPU local store, up to RBL 32K words
  (`ps3/spu/spe_aica_ram.h:10`, :53-82), cleared only at a model reset
  (`ps3/spu/spe_aica_model.cpp:151`):
  - DSP writes never reach wave RAM, so the SH4, the ARM and the slots never
    see them.
  - Guest writes into the ring area never reach the DSP: guest wave RAM
    writes go only to main memory (`plugs/nullAICA/nullAICA_plug.cpp:71`). So
    a driver that clears the delay line at a program change, or a table that
    the guest puts inside the ring for TABLE reads, is missed.
  - A ring moved by a 0x2804 write keeps the old data.
  - Risk: old echo after a DSP program change. Not seen on HW yet.
  - Fix idea: the PPU sees every guest wave RAM write. A write inside the ring
    range sends a "ring dirty" record, and the SPU reloads the ring by DMA at
    that sample. The SPU writes its ring output back by DMA after each block:
    each MWT step with TABLE = 0 writes one word per sample (MDEC_CT moves the
    address), so a 64-sample block is a few runs of 128 bytes.
- G3. A 64K-word ring (RBL 3) and TABLE accesses outside the LS ring go
  through the 16 KB line cache. Their writes are dropped at every CHECK
  (`am_check()` -> `aram_invalidate()`, `spe_aica_model.cpp:168`), i.e. every
  64 samples. A game with a 128 KB delay line gets broken reverb.
  - Crazy Taxi, Sonic Adventure and ChuChu Rocket show `slow=0`. Other games
    are unknown: the `slow=` field of the `[aica-spe] dsp jit` line shows it.
  - Fix idea: write dirty lines back by DMA before the invalidate. LS has no
    room for a 128 KB ring (the A3 image's bss ends at 0x3C6B8 of 0x40000).
- G4. Guest reads of TEMP, MEMS, MIXS and EFREG come from nullAICA's register
  copy (`plugs/nullAICA/aica_mem.cpp:79-91` stores the CPU writes, and the
  reads return them), not from the DSP. On the console they return live DSP
  values (caique NOTES "Access / register map": TEMP 24 bits R/W, MEMS bits
  7:0 read back what the DSP wrote, MIXS by its two-bank rule, EFREG R/W).
  Games rarely read them (unverified).

Not needed: the memory read latch (dsp_mem3) and ARM reads that replace a DSP
read (wren7 collide; caique does not model this one). caique says programs
must not rely on them. Timing inside a sample and 0x2804 bit 15 (TESTB0) do
not change the DSP results per sample.

Output stage: caique marks the analog side as unverified (EFSDL/EFPAN,
DISDL/DIPAN, MVOL, the DAC: `model/README.md` "Not observable from the SH4",
`model/LIMITATIONS.md` section 5). So there is no console reference for the
0.9-1.4 dB level difference in plan 6.6.

HW test for G1: today's defaults (SPE ARM7 ON) plus SPE AICA DSP ON and SPE
DSP JIT ON. Crazy Taxi, Sonic Adventure, ChuChu Rocket, Looney Tunes. Read the
`dsp jit` line (`slow=`, `bad=`), SPU busy and SPEED/VPS. Listen for reverb,
for voices that were missing, and for old echo after a scene change (G2).

### P21 CD-DA during data reads, GD-ROM status timing  `[x]` "GD Status" (steps 1 and 2) built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source; merged: plan 3h

- Step 1 (1 line): SPI_CD_READ stops CD-DA.
  - Flycast master `core/hw/gdrom/gdromv3.cpp:752` (`cdda.status = NoInfo`;
    from `dcf5444ac`, 2020; reverted by `9869ea885` and restored by
    `04b74c3a1` on 2026-09-13: Hydro Thunder, also needed for NFL Blitz
    2001).
  - Ours (`dc/gdrom/gdromv3.cpp:552-598`) leaves `cdda.playing` on. Our
    GET_SCD takes the audio status from `SecNumber.Status` (:835-857), so
    also change PLAY to PAUSE there. CD Audio is ON (Cdda=1).
- Step 2 (~60 lines, medium risk): Flycast dev `e6707469d` + `fff68b3f5`
  (`origin/dev core/hw/gdrom/gdromv3.cpp`: cddaTransition :173-182,
  statusSched :1291-1307).
  - Status reads PLAY while a data read runs and for 67 ms after it, then
    PAUSE. CD_PLAY starts CD-DA 5 ms later. Seek reports SEEK with DSC=0 for
    20 ms, then PAUSE. Stop (seek type 3) reports BUSY for 5 ms, then
    STANDBY. Resume reports BUSY for 20 ms.
  - The subcode audio status comes from the CD-DA state (:595-612).
  - Games in the commits: NFL Blitz 2001 (matchup screen freeze), Cool
    Boarders Burrrn (load freeze with the real BIOS), Ms. Pac-Man (spurious
    CD-DA pause), Bust-a-Move 4 music (regression fix).
  - Ours: CD_PLAY (:722-762) sets PLAY at once. CD_SEEK (:764-810) sets PAUSE
    or STANDBY at once, with DSC=1.
  - PS3 notes: the SCHED preset is OFF by default (`ps3/ps3_presets.cpp:12`),
    so use a cycle countdown in SlowUpdate next to `gdrom_spinup_tick`
    (`dc/sh4/sh4_interpreter.cpp:290`): delay = ms x SH4_CLOCK_EFF / 1000.
    Resume-only-when-paused needs a 3-state CD-DA flag (overlaps plan 3h).
    Cancel a pending change in `gd_setdisc` and on ATA soft reset. No
    byte-order issue.
  - The change is 2 days old in dev and already needed one regression fix.
- HW check: ChuChu Rocket and Looney Tunes CD-DA, Crazy Taxi loading.

### P22 GD-ROM command hangs and DMA rules  `[x]` "GD Command Fix", "GD DMA Fix", "GD-ROM Speed" (default REAL) built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source

More hangs on rare input (P9 has the first five). Flycast
`core/hw/gdrom/gdromv3.cpp` against ours (`dc/gdrom/gdromv3.cpp`):
- Unknown ATA command: Flycast aborts with an IRQ (:510-519). Ours calls
  `die()` (:520), an endless loop.
- ATA_EXEC_DIAG: Flycast completes it (:450-459). Ours does nothing
  (:462-465), so BSY stays set and no IRQ comes.
- Slave drive: Flycast ignores commands to the slave (:1048, :1168,
  :1194-1202; `c3d954263`, DreamShell boot). Ours stores GD_DRVSEL (:1055)
  and runs every command.
- CD_READ2: Flycast takes the length from bytes 6-7 (:763-766). Ours reads
  bytes 8-10 for both forms (:580): 0 sectors, then the verify at :1128
  hangs.
- SET_MODE and GET_TOC lengths: Flycast clamps them (:830, :790). Ours: the
  verify at :640 hangs; no clamp at :609. Our ATA_IDENTIFY_DEV (:474-477)
  uses stale packet bytes.
- SB_GDLEN = 0 means 32 MB (Flycast :1217, :1247; `d0d971254`, Pro Pinball
  Trilogy intro hang). Ours (:1099-1106) raises the end IRQ with no data and
  leaves the drive mid-read.
- DMA pacing: Flycast moves 10 KB chunks at 1.8 MB/s and small transfers at
  25 MB/s (:1211-1228; `914fa29d3` World Series Baseball 2K2 freezes,
  `8f3124d14` Sakura Taisen 3 music). Its FastGDRomLoad mode stays
  asynchronous (512 cycles per chunk). Ours copies 32,000 bytes inside the
  SB_GDST write, then more every SlowUpdate (~446 MB/s). This corrects the
  section 4 line "no game is known to need pacing". Form: a "GD-ROM Speed"
  FAST / REAL option, counted down in SlowUpdate. No CPU cost; REAL makes
  loads as slow as on a Dreamcast.
- About 30-40 lines. No byte-order issue. Low risk.

### P23 TOC fields and the GDI parser  `[~]` "TOC Fields" built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source; "GDI Parser" default ON, no HW test yet

- GDI parser (`plugs/ImgReader/iso9660.cpp`):
  - :174 reads each line with `fscanf("%d %d %d %d %s %d")`. A quoted file
    name with spaces (common in GDIs converted from Redump sets) reads as
    `"Crazy`. The next `%d` fails, every later line is misread, and the
    tracks do not open. Flycast `core/imgread/gdi.cpp:59-79` reads quoted
    names (`180e67060` rejects an unterminated quote).
  - :203 `gdi_ses.SessionStart[0]=3;` must be `[1]`. Today REQ_SES reports
    session 1 at track 3 and session 2 at 0xFF.
- TOC fields: Flycast `core/imgread/common.cpp:192-200` sets ADR=1 for every
  entry. :247-250 sets the single-density lead-out to the end of track 2 + 1.
  `common.h:146-147` gives the lead-out ADR=1, CTRL=4. Commits `1cadd4e92`
  (V-Rally 2 CD-DA tracks silent), `59480ea5a` (Eldorado Gate 4-7 disc
  check), `e592650af`.
- Ours: `plugs/ImgReader/common.cpp:207-236` passes each reader's Addr
  through. GDI uses Addr=0 (`iso9660.cpp:188`). MDS uses Addr=0 and lead-out
  CTRL=0 (`mds.cpp:65-66`, :108). CDI lead-out CTRL=0 (`cdi.cpp:425`). The
  single-density lead-out is fixed at 13085 (`iso9660.cpp:99`).
- About 25 lines. TOC bytes are built one by one, so byte order is neutral.
  Low risk.

### P24 Small G2 DMAs complete at once  `[~]` built 2026-10-11 as "G2 DMA Small", bundle 6 (plan section 0), default OFF; HW run 1 2026-10-11 (ChuChu Rocket): 12 small ends, no fault, no fix for its sound fault F1; Bangai-O and Nomad Soul not run

- Flycast `core/hw/aica/aica_if.cpp:404-409`: a transfer of 512 SH4 cycles
  or less (128 bytes or less) raises its end IRQ inside the start call.
  Longer ones are scheduled at len x 4 cycles. History: `1e661a4d4` (2018,
  Bangai-O startup freeze); `5c5c872af` (longer ones must stay scheduled,
  Sports Jam announcer); `439105856` (limit 224 -> 512, Nomad Soul
  freeze/crash).
- Ours (`dc/aica/aica_if.cpp:272-291`): with DmaFix=1 (the default) every
  transfer is deferred by at least 64 cycles (:277), so it ends on a later
  timeslice.
- Port: complete inline when cycles <= 512, behind DmaFix for an A/B. Keep
  the Read_SB_ADST double-DMA guard (Flycast has the same at :414-427).
  3 lines. Separate from plan 6.5.

### P25 Render rules (fixed function)  `[~]` palette VQ and planar VQ built 2026-10-10 as "VQ Formats", bundle 3 (plan section 0), default ON in source (Matt), no game with such a texture run on HW yet; "PT Depth Write", "TR Depth Write", "PT Blend", "Sprite Cull", "Flat Shading" built 2026-10-11, bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt) but TR Depth Write (GAME: OFF IF SORTED fixes Rayman 2 water and breaks Sonic Adventure models; plan 3k "TR Triangle Sort" at + OPAQUE DEPTH fixes both, HW SUCCESS 2026-10-11, default in source) and Flat Shading (OFF: no difference seen on HW); the trilinear weights left out

Each one is small and needs no shader.
- Depth write. Flycast (`core/rend/gles/gldraw.cpp:261-280`, the same in
  `dx9/d3d_renderer.cpp:486-506`): autosorted TR writes no depth. PT always
  writes depth ("Fixes Worms World Party, Bust-a-Move 4 and Re-Volt"). PT
  blending is on (`1a019b79b`). Ours (`ps3/ps3_rend.cpp:1289`) uses
  `!ZWriteDis` for every list; Soul Calibur sends ~616 TR strips with Z
  write on under autosort. 2 lines in `apply_param_body`.
- Sprite cull direction. Flycast `core/hw/pvr/ta_vtx.cpp:990` flips the cull
  mode for sprites (`6ffe4c028`: South Park Rally and Wacky Races menus were
  invisible). Its sprite strip order D,C,A,B is the same as ours. Ours
  (`ps3/ps3_rend.cpp:503-547`, `ps3/spu/spe_dec.c:384-430`) keeps the mode as
  sent. Use `CullMode ^ (pcw.ParaType == 5)` in `apply_param_body` and in the
  merge parity check (:1442). The SPU decoder stays the same, so the S2
  compare is unchanged. About 5 lines.
- Flat shading. On the DC a flat polygon takes the colour of its last
  vertex (Flycast `gles.cpp:75-81`, `d3d_renderer.cpp:426`). Ours
  (`ps3/ps3_rend.cpp:1862`) is always GL_SMOOTH. One call:
  `glShadeModel(Gouraud ? GL_SMOOTH : GL_FLAT)` (`ps3gl_states.c:207`
  supports it). The RSX provoking vertex is unverified.
- Two-pass trilinear. Flycast `gldraw.cpp:124-133` scales the colour by
  1 - 0.25 x (MipMapD & 3) in pass A (FilterMode 2) and by
  0.25 x (MipMapD & 3) in pass B (FilterMode 3), for mipmapped textures only
  (`a1997637b` NFL 2K2 overbright field, `ff69efff4` Shenmue snowflakes).
  Ours draws both passes at full weight. Scale the vertex RGBA. Unverified
  in a game.
- Palette VQ and planar VQ. Flycast `texconv.cpp:441-447` (`60ec054b5`) and
  `TexCache.cpp:328-362` (`a7f200876`). Ours (`ps3/ps3_tex.cpp:171-172`)
  ignores VQ on palette textures and always treats VQ as twiddled, so these
  decode as garbage. Small.
  - Built 2026-10-10 (bundle 3, the "VQ Formats" row). Flycast's forms:
    palette VQ has one index byte per 4x4 (4-bit) or 2x4 (8-bit) block,
    and a codebook entry is that block's 8 index bytes; planar VQ has one
    index byte per 4 texels of a row (`texture_PLVQ`), not per 2x2 block.
  - The trilinear weights above were left out of bundle 3 (Matt): the
    vertex colour does not reach a decal texture, and SPE Build makes the
    vertices on the SPU.

### P26 Framebuffer output: write watch, interlaced and 24-bit reads  `[ ]`

- Write watch:
  - Flycast master `core/hw/pvr/pvr_mem.cpp:220-234`: any 32-bit-area VRAM
    write inside the displayed buffer sets `fb_dirty`.
    `Renderer_if.cpp:611-638` presents the VRAM image at VBlank only if it is
    set and no STARTRENDER came since the last VBlank. `pvr_regs.cpp:173-180`
    resets it on an FB_R_SIZE write.
  - Flycast dev `7db29cc2f` (FramebufferWatcher, `origin/dev pvr_mem.cpp:321-403`)
    watches the current and the previous FB_R_SOF1 buffer, and marks the
    frame dirty only when the data changes. Flycast names Ikaruga and Armada
    loading screens (Armada is WinCE), Sega Rally JP network menus and VF3tb
    history videos.
  - Ours: the magic word 0xDC4F0FB0 at the start of the framebuffer
    (`ps3/ps3_rend.cpp:2054-2070`, :2759, :2811). VBlank (:2734) presents only
    if word 0 changed. A 2D screen that later redraws only part of the screen
    (a loading icon or bar) is never presented again. Full redraws work.
  - Port: a range check in `pvr_write_area1_16/32` (`dc/pvr/pvr_if.cpp:311-321`;
    the 0x05 window is a handler page, `dc/mem/sh4_mem.cpp:43`) and in the
    direct-VRAM path of TAWrite/TAWriteSQ. FB_R_SOF1 and FB_R_SIZE cases in
    the `plugs/drkPvr/regs.cpp` switch (:33). Present if the mark is gone OR
    the frame is dirty; clear dirty at StartRender. About 25-40 lines,
    endian-neutral (compare before `vramlock_ConvOffset32toOffset64`).
    Unverified: whether PVR-DMA block writes pass through the handler. Risk:
    a spurious present when a game reuses an old framebuffer area for
    textures.
- Reads: Flycast `core/rend/TexCache.cpp:715-840` reads an interlaced buffer
  as one 480-line image and supports 24-bit 888 and the fb_concat bits. Ours
  (`ps3/ps3_rend.cpp:2079-2130`) reads one field (half the lines) and does not
  present 24-bit. Small; the word-shift unpacking is endian-neutral.
- Fourth review (2026-10-06): watch one framebuffer only. Reicast
  `8850e031` (branch `dd/msvc-heap`: "Use FB_R_SOF2 only to detect direct
  FB writes. Fixes flashing video in Virtua Fighter 3tb") stopped watching
  SOF1 and SOF2 together. Flycast master watches
  `SPG_CONTROL.interlace ? FB_R_SOF2 : FB_R_SOF1`
  (`core/hw/pvr/Renderer_if.cpp:633-637`).

### P27 Modifier volumes in the TR list (Xtreme Sports)  `[~]` merged: plan 3c; "TR Volumes" built 2026-10-11, bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt)

- Flycast dev `f2013c652` (`origin/dev core/hw/pvr/ta_vtx.cpp:1303-1322`):
  when a pass shares the opaque and translucent modifier-volume pointers
  (region-array word +8 equals word +16, `ta_vtx.cpp:1739`) and the TR list
  has more volumes, it draws the TR list's volumes as the opaque ones. Fixes
  the Xtreme Sports shadows. Its other half (drop pcw.Texture when the
  texture is invalid) we already do (`ps3/ps3_rend.cpp:1295`).
- Ours: list 3 is dropped at decode. `ps3/ps3_rend.cpp:420` EndList and
  :550-581 StartModVol / AppendModVolVertexA/B accept only
  ListType_Opaque_Modifier_Volume. The SPU decoder does the same
  (`ps3/spu/spe_dec.c:432-466`, `dec_list != 1`).
- Port: accept list 3 too, tagged in the free pad byte (ModVolParam /
  SpeModVol.pad; the layout is unchanged), with its own terminator.
  `draw_modvols` (:1621) draws list 3 when words +8 and +16 match with bit 31
  clear and list 3 has more volumes (Flycast does not check bit 31, and two
  empty pointers also compare equal). Read the words with `vri` at
  StartRender, as `using_autosort` does (:1089). About 30 lines in both
  decoders; both lists share the MAX_MODVOL / MAX_MODTRI caps.

### P28 SPG timing: field bit, HBlank modes  `[x]` built 2026-10-11 as "SPG Field Bit", "HBlank IRQ" and "VBLANK INT Reset", bundle 7 (plan section 0); HW runs 2026-10-11 (Crazy Taxi, ChuChu Rocket, the three rows together, no fault), default ON since the package of 22:16 (Matt)

- Field bit: ours flips `SPG_STATUS.fieldnum` on every scanline when
  interlaced (`plugs/drkPvr/SPG.cpp:247`, inside the per-scanline block of
  `libPvr_UpdatePvr`). Flycast flips it once per frame, at scanline 0
  (`core/hw/pvr/spg.cpp:158-163`). A game that reads the field number gets a
  value that depends on the scanline. 1 line.
- HBlank interrupt: ours raises `holly_HBLank` once, at vblank (:259).
  Flycast follows the SPG_HBLANK_INT mode (`spg.cpp:140-155`): mode 0 at the
  line in line_comp_val, mode 2 on every line. Small.
- Reset value: ours sets SPG_VBLANK_INT to 0x01500104
  (`plugs/drkPvr/regs.cpp:134`); Flycast uses 0x00150104 (`pvr_regs.cpp:237`),
  so SCANINT2 comes at line 21, not 336. The BIOS writes the register, so
  the value is low.
- Games affected: unknown.

### P29 Maple reply timing and commands  `[ ]`

- Flycast:
  - `core/hw/maple/maple_if.cpp:255`, :315-341: replies are buffered and
    copied to RAM when the end IRQ fires (`8f80bcb15`, Geist Force freeze).
  - :301-312: 2 Mb/s in, 740 Kb/s out (`d84108de3`, Silent Scope VMU load
    at boot).
  - `maple_devs.cpp:142-212` answers DeviceReset, Kill and AllStatusReq.
- Ours: `dc/maple/maple_if.cpp:118-145` writes replies into RAM at once.
  :163 uses one 2 Mb/s rate both ways, so replies are ~2.8x too fast. The
  controller and the VMU reject commands 2, 3 and 4
  (`dc/maple/maple_devs.cpp:220-222`, :643-645). This corrects the section 4
  line "Maple DMA timing: same formula".
- Port: about 40 lines. Byte order: keep a u32-aligned scratch buffer, so
  the XOR-3 w8/w16 writes (`maple_devs.cpp:100-101`) stay valid, then copy
  words. Pair it with the P9 SB_MDST fix. Low risk.

### P30 P4 mirrors and operand-cache RAM banks  `[ ]`

- Flycast `core/hw/sh4/sh4_mem.cpp:160-174` mirrors areas 0-6 into
  0xE0-0xFF; the P4 handlers cover only E0-E3, F0-F7 and FF.
  `sh4_mmr.cpp:560-579` selects the operand-cache RAM bank by address bit 13
  (bit 25 when OIX=1). Commit `156aafe95`: NHL 2K2 missing helmet and jersey
  textures.
- Ours: one catch-all handler for 0xE0-0xFF that reads 0 and drops writes
  (`dc/mem/sh4_internal_reg.cpp:776`, :240, :352). `dc/mem/sh4_mem.cpp:121`
  builds 7 mirrors. On-chip RAM uses `addr & OnChipRAM_MASK` (:664, :679),
  so the two banks alias: 0x7C001000 and 0x7C003000 both give offset 0x1000.
- About 15 lines. Only the RAM offset changes; storage stays the same. Low
  risk. NHL 2K2 itself not checked.

### P31 AICA register details  `[~]` CA clamp merged: plan 6.5 (done); "Mono Mix Fix" and "LFORE Keep" built 2026-10-11, bundle 5 (plan section 0), rows OFF, HW not run

Default path (SPE AICA Sound ON):
- CA clamp. The LEA-3 clamp of plan 2.2 (Flycast `788cbfb64`: VC 2K sports
  games detect a sound's end) runs only in nullAICA mode
  (`plugs/nullAICA/sgc_if.cpp:1347-1353`). The twin and SPE ARM7 paths return
  the raw CA (:1342-1345, `ps3/spu/aica_ctl.h:132-134`, caique
  `aica_model.cpp:1702`). Clamp after `ps3aica_read_mon` and `ac_twin_mon`
  with the slot's LSA/LEA (read the halfword at A+2 on BE). caique says the
  hardware reads LEA at a one-shot end, so the clamp is Flycast's workaround,
  not hardware behaviour. New fact for F3: near the end of Looney Tunes' ring
  (LEA 8192) the two modes read a different CA (8189 with the clamp, 8191
  without).
- Mono = (L+R)/2 (`136ce267d`, Sega Bass Fishing Challenge saturation). Ours
  adds L+R with no halving (`sgc_if.cpp:1564-1568`, caique
  `aica_model.cpp:1098`). Only when a game sets MONO. 2 lines per path.
- LFORE stays set after a write (`27fd0d426`; caique keeps it too). Ours
  clears it (`sgc_if.cpp:814`, `ps3/spu/aica_ctl.h:153-154`). Low value.

nullAICA only (SPE AICA Sound OFF):
- A pitch write recomputes the AEG rates. Flycast `sgc_if.cpp:978-983`
  (`92c06a724`, `1ac497ba3`: Bomberman Online FMV intro freeze, Jet Grind
  Radio and Psyvariar 2 intro music fade-out). KRS scaling depends on OCT and
  FNS. Ours (`plugs/nullAICA/sgc_if.cpp:871-872`) only calls `UpdatePitch()`;
  caique already does both. One line: `UpdatePitch(); UpdateAEG();`.
- VOFF (reg 0x28 bit 6) turns off the TL, AEG and ALFO attenuation (Flycast
  `sgc_if.cpp:328`, :664-672, :893; caique measured it, `aica_model.cpp:761`).
  Ours ignores it: `rez_28_0:3` (`sgc_if.cpp:196` BE branch, :289 LE branch).
  Byte-order trap: in the AICA_PS3 branch, split `rez_28_0` MSB-first into
  `rez:1, VOFF:1, LPOFF:1`. About 10 lines.

### P32 SH4 interpreter fixes  `[ ]` mac.w merged: plan 5.3 (JitMac)

- fneg with PR=1 on an odd register. Flycast `11fcadf37` (issue #2139) flips
  FR(n) whatever PR is. Ours (`dc/sh4/sh4_fpu_ops.h:474-483`) flips
  `fr_hex[n & 0xE]`, the wrong register when n is odd. The JIT sends every
  FPU op to the interpreter when PR=1 (`dc/sh4/rec_v2/decoder.cpp:942-943`),
  so JIT runs have it too. 1 line.
- mac.w with S=1: our interpreter prints a message and skips the op, with no
  pointer increments (`dc/sh4/sh4_cpu_misc.h:225-233`). JIT_MAC handles it;
  Flycast's interpreter dies there. It matters when the op runs in the
  interpreter (JitMac OFF).

### P33 ELF loader: segments first  `[ ]`

- Flycast dev `643374c91` (`origin/dev core/reios/reios_elf.cpp:60-120`): it
  loads the PT_LOAD segments first (with the zero-filled memsz tail), then
  the ALLOC sections that no segment covers.
- Ours (`plugs/ImgReader/elf_loader.cpp:136`) loads only SHT_PROGBITS + ALLOC
  sections. PT_LOAD (:170) runs only when no section loaded. So
  INIT_ARRAY / FINI_ARRAY / PREINIT_ARRAY (types 14-16, C++ constructors)
  are never loaded. A section that starts at an address that is not a
  multiple of 4 gets its bytes in the wrong order (`elf_copy_to_ram` :19
  swaps whole words from that start).
- About 30 lines; keep the per-word swap from word-aligned starts. Homebrew
  only. Whether current KOS ELFs use .init_array: unverified.

### P44 ARM7 resets in System mode, not SVC  `[ ]` merged: plan 6.8

- Ours: `arm_Reset()` sets `armMode = 0x1F` (System;
  `plugs/vbaARM/arm7.cpp:350`, used by the cached engine and the JIT). The
  SPE ARM7 does the same (`ps3/spu/arm7_spu.h:188`, default ON). nullDC-v1
  and nullDC4Wii have the same VBA value.
- Hardware: a reset enters SVC (0x13). The AICA's ARM7DI is an ARMv3 core,
  and ARMv3 has no System mode.
- Reicast `4caa2b47` (2020-01: "SoundCPU_impl::Reset should enter SVC
  mode, not USR", `libswirl/hw/arm7/SoundCPU.cpp:169`) and Flycast
  `882e14f13` (2020-02, `core/hw/arm7/arm7.cpp:301`) both changed 0x1F to
  0x13.
- Effect: the reset leaves R13_SVC at the VBA value 0x03007FE0. Our ARM
  map masks addresses to 24 bits (`plugs/vbaARM/arm_mem.cpp:85`), so that
  is ARAM 0x7FE0. A driver that sets SP right after reset (for SVC), then
  selects SVC again with a constant after its FIQ setup, runs with SP at
  ARAM 0x7FE0: its stack can overwrite its own code or data. A driver that
  stays in the reset mode, or sets SP after the switch, is not affected.
- Link to F1 (ChuChu: key-offs come seconds late) and F3 (Looney Tunes
  voices silent): unverified. No log shows the ARM modes.
- Steps:
  - Probe first (log only): the first 8 CPSR mode changes after an ARM
    reset (from, to, pc, r13), and every 5 s armMode, R13_USR, R13_SVC and
    R13_FIQ. An SP of 0x03007Fxx in use confirms it.
  - Fix: 0x13 in both engines (2 lines), own toggle. Run the 45 ARM7
    self-tests again (they start from reset), and `misc/arm7spe_check` and
    `misc/arm7spu_check`.
- No byte-order issue. Risk: low.
- HW check: ChuChu Rocket, Crazy Taxi, Sonic Adventure, Looney Tunes.

### P45 Mipmaps: upload the whole mip chain  `[x]` "Mipmaps" built 2026-10-10, bundle 3 (plan section 0), HW SUCCESS 2026-10-10, default ON in source

Built 2026-10-10 with two changes to the form below (Matt's picks): the
hash covers the top level only, as Flycast's lock does, and no LoD bias.
With SPE Tex Decode ON the SPU decodes the levels of 8x8 and up.

- Hardware: the PVR picks a mip level per pixel for every mipmapped
  texture. Ours draws level 0 only, so distant floors and roads alias.
- Ours:
  - `describe()` decodes only the top level (`ps3/ps3_tex.cpp:176-201`) and
    uploads it as level 0 (:473, :516). The filter is GL_LINEAR or
    GL_NEAREST (:420-422). The hash covers only the top level (:282).
  - ps3gl already has a mip chain for both layouts
    (`ps3/ps3gl/ps3gl_textures.c:357-418`; levels accepted from :600). But
    `g_mipmap_enabled` is 0 (:40), and nothing calls
    `ps3gl_set_texture_options()` (:43).
- Flycast:
  - `rend.UseMipmaps` is ON by default (`core/cfg/option.cpp:69`). Only
    twiddled textures get mips (`core/rend/TexCache.h:125-128`), the same
    rule as our `ps3_tex.cpp:176`.
  - It decodes every level from VRAM (`TexCache.cpp:561-592`). The VQ 1x1
    level is texel (1,1) of the 2x2 block; the YUV 1x1 level is decoded as
    565.
  - It uploads all levels (`core/rend/gles/gltex.cpp:57-69`) and uses
    `GL_LINEAR_MIPMAP_NEAREST` for bilinear, never for punch-through
    (`core/rend/gles/gldraw.cpp:214-229`). TSP MipMapD becomes an LoD bias
    (`gldraw.cpp:231-234`, `TexCache.cpp:31-34`, commit `54261b69c`).
- Evidence: mipmapped texture fills (`[texfmt] mip=` against all fills in
  `run525_ct.txt`, `run525_sonic.txt` and `run525_soul.txt`, scratchpad of
  session 1a0d7654): Crazy Taxi 879 of 1856, Sonic Adventure 810 of 1252,
  Soul Calibur 21 of 40688. `[ps3gl] mip uploads` reads 0 in every window.
  The RSX waits on nothing (`vring fence wait 0.00ms` in `run516.txt` and
  `run526c.txt`).
- PS3 form (~70 lines, own toggle):
  - Decode each level, largest first, with the existing offset tables; one
    `glTexImage2D` per level. Turn the ps3gl mips on only around the first
    allocation of a mipmapped texture (the layout is fixed at allocation).
  - Hash the whole chain from TexAddr.
  - Mip filter for opaque and translucent bilinear only.
  - LoD bias: optional. `rsxTextureFilter` has a 13-bit bias field, and
    ps3gl passes 0 (`ps3gl_textures.c:1058`). Its fixed-point format is
    unverified.
- Cost: on cache misses only. About +33% decode work for those textures
  (decode is ~0.8% of the render thread in dense Crazy Taxi, `run526a.txt`)
  and about +1% of the render thread for the longer hash (estimate). No SH4
  cost.
- Risk: medium-low. The ps3gl mip path has never run on HW (dormant code).
  Mirrored textures take the linear layout (`ps3gl_textures.c:577-585`),
  where every level uses the base pitch. RSX linear textures froze the
  console twice in G7 until the layout and pitch rules were found. First
  build: no mips on mirrored textures.
- It also gives the P25 trilinear weights a meaning.
- Byte order: the same `vram8`/`vram16` accessors as today.
- HW check: Crazy Taxi roads and Sonic Adventure floors at a distance;
  `[ps3gl] mip uploads` above 0; render `busy` against the last run.

### P47 GD-ROM REQ_STAT position bytes  `[x]` "GD Position" built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source; merged: plan 3h

- Ours (`dc/gdrom/gdromv3.cpp:653-678`): `stat[5..7]` =
  `cdda.CurrAddr.B0`, `B1`, `B2`. The union (:127-136) puts B0-B2 on the
  first three bytes of the u32. On our big-endian host those are bits
  31-8, so the guest reads FAD >> 8 (FAD 45150 reads as 176). On a
  little-endian host the same code gives the three low bytes in reverse
  order, so the upstream code was wrong too. It also sends a fixed
  control/ADR 0x4, track 1 and index 0.
- Flycast `e5344d37d` (2023, `core/hw/gdrom/gdromv3.cpp:837-871`): the
  bytes are `curFad >> 16`, `curFad >> 8`, `curFad`. curFad is the CD-DA
  position while CD-DA plays or pauses, else the last sector read. Track,
  control/ADR and index 1 come from the TOC. The same commit fixes CD-DA
  restarts (Mr. Driller, Bust-a-Move 4, Wetrix+, Reel Fishing) with
  `CurrAddr = StartAddr` at CD_PLAY, which we already do (:734).
- GET_SCD, same commit: Flycast takes control/ADR and the track from the
  TOC, as `(ctrl << 4) | adr`; Reicast sends 0x41
  (`libswirl/hw/gdrom/gdromv3.cpp:790`). Ours sends 0x14 and track 1
  (:875-876, :898). Section 4 says our GET_SCD is tuned for the BIOS CD
  player and the ChuChu pause fix, so test on HW before changing it.
- About 8 lines, own toggle. Use shifts, not the union.
- Value: which software reads these bytes is unverified.
- HW check: ChuChu Rocket and Looney Tunes CD-DA, the BIOS CD player.

### P48 GD-ROM SET_MODE writes at twice the offset  `[x]` "SET_MODE Fix" built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source

- Ours: SPI_SET_MODE keeps the byte offset (`dc/gdrom/gdromv3.cpp:638-641`),
  then `gds_process_set_mode` copies to `gd_data_0x11[set_mode_offset*2]`
  (:335). REQ_MODE reads at the byte offset (:548), and `gd_data_0x11` is
  a u8 array (`dc/gdrom/gdrom_response.cpp:20`). The `* 2` copies the
  indexing of Reicast's u16 table `reply_11`
  (`libswirl/hw/gdrom/gdromv3.cpp:222`), which has the same bug. So a
  SET_MODE with an offset of 1 or more writes the wrong field.
- Flycast `6059831ec` (2019): byte offsets in both directions
  (`core/hw/gdrom/gdromv3.cpp:304`).
- Fix: 1 line, no byte-order issue. Extends P22, which has the length
  clamp of the same command.
- Extra fact: our REQ_MODE strings are "Rev 6.42" / "990316"
  (`gdrom_response.cpp:25-26`); Flycast has "Rev 6.43" / "990408"
  (`core/hw/gdrom/gdromv3.cpp:1373-1374`).

### P49 ARM7 PC reads: Rn = PC with a register shift, and STR PC  `[ ]` merged: plan 6.8

- Rule (ARM7 pipeline): when the shift amount comes from a register, every
  PC operand reads insn+12, not insn+8. STR and STRB of PC store insn+12.
- References: Reicast `a18ed07a` (branch `skmp/fix-arm7-pc-pipeline-reads`;
  the change is also in `alpha`, `libswirl/gpl/vba-arm/arm-new.h:524`),
  Reicast `libswirl/hw/arm7/arm7_jit_virt.cpp:976`, Flycast
  `core/hw/arm7/arm7_rec.cpp:210` (Rn), :260 (Rm), :328 (STR of PC).
- Ours: three live engines (`arm-new.h` is compiled out,
  `plugs/vbaARM/arm7.cpp:23`, :467-479).
  - Rm = PC with a register shift: +12, right
    (`plugs/vbaARM/gen_arm7_cached.py:126-129`, from the HW test
    arm7di-tests-dreamcast MOV_1; `arm7_jit.cpp:1145-1146`;
    `ps3/spu/arm7_spu.h:424-425`).
  - Rn = PC with a register shift: +8, wrong (`gen_arm7_cached.py:143-144`
    with `arm7_cached.cpp:434`; `arm7_jit.cpp:1162-1163`;
    `arm7_spu.h:523-524`). The Rd = PC forms have the same fault.
  - STR and STRB with Rd = PC: reg[15] is set only when Rn or Rm is PC
    (then to +8), else the store writes a stale reg[15]
    (`gen_arm7_cached.py:401-403`, :441; `arm7_jit.cpp:1358-1360`, :1424;
    `arm7_spu.h:702-703`, :745).
  - STM with PC: +12, right.
- Evidence: the `[ARMHOT]` opcode dumps of five old logs (Crazy Taxi
  `s3_run1.log`, `a1_run1.log`, `a1_run2.log`; Soul Calibur `s2_sc.log`,
  `s3_sc.log`) hold neither wrong form. ChuChu is in no dump. A wrong PC
  breaks control flow or data; it does not delay key-offs, so a link to F1
  is unlikely.
- Steps: a log-only census first: at decode in the cached engine, the JIT
  and the SPU engine, print the first 8 PC/opcode pairs of the two forms.
  One ChuChu Rocket boot and one Crazy Taxi boot. Fix only if it fires:
  2-3 lines per engine, regenerate `arm7_cached_gen.inc`, add two
  self-tests (`orr r0,pc,r1,lsl r2`; `str pc,[r1]`). The B0/B2 checks
  compare the engines, so change all three together.
- Trap: do not paste Reicast's diff into `arm-new.h`. Its loop sets
  armNextPC = reg[15] (`arm-new.h:1553`), so a +4 that is never undone
  skips an instruction.
- No byte-order issue. Risk: low (rare encodings).

### P54 Render-to-texture pixel packing  `[ ]` merged: plan section 3 (RTT note)

RTT has never fired in Crazy Taxi, Sonic Adventure or Soul Calibur
(`rtt=0/0`). These fixes matter only for RTT games (plan section 3 lists
Flycast's RTT games).
- Ours (`ps3/ps3_rend.cpp`):
  - `pack16()` (:1967-1977): 1555 alpha is `a >> 7` (a fixed threshold of
    128), and every channel is truncated. A feedback effect darkens by one
    step per pass.
  - `rtt_render()` draws the clip rect (:1979-1983, :2011), but packs every
    row from the start of the target (:2025-2035): the clip origin x0/y0 is
    not added.
  - Pack mode 6 drops alpha under a wrong "KRGB0888" comment (:2043-2044).
    Mode 6 is ARGB8888; KRGB0888 is mode 5.
  - Modes 4 (RGB888, 24-bit) and 5 (KRGB0888) are not handled: a message,
    then no copy (:1992-1997).
- Flycast (commit `5722dc90f`, `core/rend/TexCache.cpp`):
  - 1555 alpha is `alpha >= FB_W_CTRL.fb_alpha_threshold` (:1027-1028;
    the field is bits 16-23, `core/hw/pvr/pvr_regs.h:400-409`).
  - `roundColor()` rounds to nearest (:844-851).
  - Pixels go to their clip position (:1122-1128).
  - Mode 6 keeps alpha, and modes 4 and 5 have writers (:1234-1241).
- About 15 lines, plus the two missing modes. Endian-neutral: the pack
  works on host words.

### P57 Small fixes: GDI paths with `\`, the RM verify, a TMU test  `[~]` GDI paths ("GDI Backslash") built 2026-10-10, bundle 2 (plan section 0), default ON in source (Matt), no HW test yet; RM verify built 2026-10-11, bundle 8 (plan section 0): row "JIT RM 2/3", default VERIFY, no HW run; TMU test: built 2026-10-11 in `misc/timebase_check`, bundle 7 (plan section 0)

- GDI track paths with a backslash. Flycast's fixture
  `tests/files/test_gdis/a/cs.gdi` names `tracks\cs01.bin`. Ours
  (`plugs/ImgReader/iso9660.cpp:158-180`) appends the name to the GDI's
  folder as it is, so `fopen` fails on the PS3. Every read of that track
  then prints "Read from missing sector" (:26) and returns nothing, so the
  boot fails without a clear message. Fix: map `\` to `/` (1 line).
  Fixture `d` is P23's quoted-name case. The four fixtures and
  `tests/src/imgread/GdiTest.cpp` can become a check in the
  `misc/cdi_check` style (~60 lines).
- FPSCR.RM = 2 or 3 hangs the JIT. `dc/sh4/rec_v2/decoder.cpp:86` has
  `verify(fpu_cfg.RM<2)`, and our `verify` ends in an endless loop
  (`types.h:186-188`). Flycast removed it (`decoder.cpp:936`: "Happens with
  many wince games (set to 3)"). Our rounding setup already treats every RM
  other than 1 as round to nearest (`dc/sh4/sh4_registers.cpp:92-95`). 1
  line. Only WinCE games are known to set it (P38).
- Flycast's TMU test as the P11 check:
  `tests/src/hw/sh4/modules/TimerTest.cpp` (4 tests, 27 asserts). Its
  "precision" and "underflow" cases encode P11's rule (TCNT counts the
  cycles inside the current timeslice), so they fail on ours today. About
  100 lines of harness. Only together with P11.

### P58 MMUCR fields read the wrong bits on big endian  `[ ]`

- Ours: `CCN_MMUCR_type` (`dc/sh4/ccn.h:81-108`) declares 28 bits: AT:1,
  res:1, TI:1, res_2:5, SV:1, SQMD:1, URC:6, URB:6, LRUI:6, and the same
  fields in reverse for big endian. GCC fills big-endian bitfields from bit
  31, so only LRUI is right. URB reads bits 25-20 (should be 23-18), URC
  19-14 (15-10), SQMD 13 (9), SV 12 (8), TI 6 (2) and AT 4 (0).
- Checked with devkitPPC `powerpc-eabi-gcc -O2 -S` (the same big-endian
  bitfield rule as ppu-gcc): `.AT` compiles to `rlwinm 3,3,28,31,31` (bit
  4), `.TI` to `rlwinm 3,3,26,31,31` (bit 6), `.URC` to
  `rlwinm 3,3,18,26,31` (bits 19-14).
- Flycast `9473c235f` (2023-01, "Some reserved bits were missing, which
  was breaking the (slow) MMU") adds `u32 : 2;` after URC and after URB
  (`core/hw/sh4/sh4_mmr.h`). On little endian only URB and LRUI moved; on
  our build every field below LRUI moves.
- Effects in ours:
  - `CCN_MMUCR_write()` (`dc/sh4/ccn.cpp:31-52`) sees AT = 0 when a game
    writes AT = 1. The store queues never use the TLB: `do_sqw_target()`
    (`dc/sh4/sh4_sqw.h`) and the JIT PREF (`shop_pref` in
    `ps3/ps3_driver.cpp`) take QACR.
  - A TLB flush (TI, bit 2) is not seen, so `sq_remap` is not cleared.
  - `ldtlb` writes `UTLB[CCN_MMUCR.URC]` (`dc/sh4/sh4_cpu_misc.h:10-13`)
    with URC from the wrong bits.
- Border Down (JP), HW 2026-10-08 (`borderdown.txt`, scratchpad of session
  d24c41ee): the game loads 25 UTLB entries through the address and data
  arrays (F6, F7). They map the SQ area: E0000000-E0FFFFFF to 0C000000
  (RAM) and E1000000-E18FFFFF to 10000000 (TA) (`SQ remap` lines). The
  whole log has no `CCN: MMU address translation ENABLED` line. So the
  flushes take QACR (area 0): 0xE01FF3E0 goes to 0x001FF3E0 (the `Write
  to Boot ROM` lines, a polygon list), not to 0x0C1FF3E0 in RAM. The game
  builds its lists where it cannot read them back: no picture.
- Take two more Flycast rules with the fix:
  - Clear the JIT code cache when AT changes. Flycast `CCN_MMUCR_write()`
    (`core/hw/sh4/modules/ccn.cpp:38-58`) calls `mmu_set_state()` and
    `ResetCache()`. Our JIT picks `do_sqw_mmu` or `do_sqw_nommu` at
    compile time (`shop_pref`), and JIT GUARD REUSE and JIT PREFETCH also
    test AT at compile time. Blocks compiled before the switch keep the
    QACR path.
  - `UTLB_Sync()` (`dc/mem/mmu.cpp:41-60`) fills `sq_remap` from every
    SQ-area entry, V = 0 ones too, and keeps PPN bits below the page size.
    Flycast's slow MMU skips V = 0 entries
    (`core/hw/sh4/modules/mmu.cpp:61-62`); its fast MMU masks VPN and PPN
    to the page size (`fastmmu.cpp:184-185`).
- Size: 2 lines for the layout (or masks on `reg_data`), ~3 for the cache
  clear, 1 for V = 0. A check before HW: the devkitPPC `-S` test, or a boot
  self-test line that writes 0x00000105 and prints AT, TI and URC.
- Risk: a game that turns AT on now sends its SQ flushes through the TLB.
  In the saved HW logs only Border Down loads SQ entries; the BIOS writes
  UTLB entry 0 at boot (`MEM remap [0]`). WinCE games still need P38. HW:
  Border Down; Crazy Taxi, Sonic Adventure and Looney Tunes unchanged.
- HW 2026-10-08 (bundle 1 run, plan section 0 "Open, separate"): Rez
  (PAL GDI), Ikaruga (JP GDI) and Sonic Shuffle load the same 25 SQ-area
  UTLB entries, with no MMU-enabled line, and stop right after the BIOS
  logo; Sonic Shuffle's flushes land in boot ROM. Add them to the HW check.

### P59 Flash broadcast byte: NTSC, as Flycast writes it  `[ ]`

- Flycast `fixUpDCFlash()` (`core/hw/flashrom/nvmem.cpp:151-170`) runs at
  every boot, after the console flash loads (:255-257). It writes Region,
  Language and Broadcast into both factory copies (0x1a002-0x1a004 and
  0x1a0a2-0x1a0a4). `Broadcast` defaults to 0, NTSC
  (`core/cfg/option.cpp:34`). A few PAL and NTSC titles have per-game
  overrides (`core/emulator.cpp`, `loadSpecialSettings`).
- Ours: `dc_flash_set_region()` (`dc/flash_fix.cpp:144-177`) writes only
  the region bytes, and `dc_flash_restore_region()` puts them back before
  the save. Plan 3i says "Broadcast is not touched, as in Flycast". That is
  wrong: Flycast writes it at every boot.
- Logs: the BIOS boots at PAL 50 Hz (plan 5.41 step 1 HW: "Each game boots
  in PAL 50 Hz (the BIOS)"), so Matt's flash holds a PAL broadcast byte
  (the byte itself was not read: unverified). Games that follow the flash
  play at PAL 50 Hz:
  - Capcom vs. SNK 2 (JP): `VPS:50.00(PAL480i50.00)` (`cvs2.txt`);
  - Border Down (JP): `PAL480i50.00` (`borderdown.txt`);
  - Rayman 2 (US GDI): 50 Hz (plan section 0).
  A JP or US console has an NTSC byte, so there these games run at 60 Hz.
  Crazy Taxi, Sonic Adventure and Looney Tunes switch to NTSC by
  themselves.
- PS3 form: an option CONSOLE / NTSC / PAL / DISC. DISC = NTSC when the
  disc area lists J or U, PAL when it lists only E (no per-game lists).
  Write both copies (0x1a004, 0x1a0a4) next to the region bytes and
  restore them in `dc_flash_restore_region()`. About 10 lines. Byte order:
  none (the flash is read bytewise).
- Cost: a 60 Hz game has 20% more frames per second to run and draw. HW:
  Capcom vs. SNK 2, Border Down and Rayman 2 (VPS ~60, SPEED, the menus);
  Crazy Taxi unchanged.

### P60 Present only the renders the game shows  `[ ]`

- Flycast `ac43a8c54` ("don't swap gl buffer until FB_R_SOF is set to the
  rendered framebuffer"), today the DelayFrameSwapping option, ON by
  default except on macOS (`core/cfg/option.cpp:89-93`;
  `core/hw/pvr/Renderer_if.cpp:245`, :586-587, :654-666;
  `core/hw/pvr/pvr_regs.cpp:192-200`). A render shows at once only if
  FB_R_SOF1 already holds the FB_W_SOF1 it rendered to. Else it waits
  until the game writes that address to FB_R_SOF1/2. A render into a
  buffer that is never shown is never presented. Named: flashing screens
  in South Park: Chef's Luv Shack and NFL Quarterback Club 2000, glitchy
  FMVs in The Next Tetris, Nightmare Creatures II and Rune Jade, the
  flashing rear-view mirror in Metropolis Street Racer.
- Flycast `3a7a08f3f` ("Do not clear the framebuffer before rendering"):
  today the colour is cleared only when FB_W_SOF1 is not one of the last
  two framebuffer addresses (`core/hw/pvr/Renderer_if.cpp:562-571`,
  `core/rend/gles/gles.cpp:1239`). Only the background plane clears,
  inside the FB clip. Named: Vanishing Point menus (flashing), the Test
  Drive Le Mans and Vigilante 8 loading screens, the Metropolis Street
  Racer mirror.
- Flycast `93523df80` ("pvr: implement tile array region clipping"): the
  PVR draws only the tiles in the region array. Flycast scissors to their
  bounding box (x and y times 32), intersected with TA_GLOB_TILE_CLIP and
  the FB clip (`core/hw/pvr/ta_vtx.cpp:1664-1691`, :1211-1219;
  `core/rend/transform_matrix.cpp:130-143`). Entries are 5 words, or 6
  when FPU_PARAM_CFG bit 21 is set; an empty first entry is skipped.
  Named: Quake III Arena split-screen multiplayer (Quake III is in Matt's
  set, a HW log of session 9d577958; split screen not tested).
- Ours (`ps3/ps3_rend.cpp`):
  - every STARTRENDER that is not RTT draws and flips at once
    (`rth_job_draw()` calls `ps3_video_end_frame()` or `_at()`), and
    `plugs/drkPvr/regs.cpp` has no FB_R_SOF1/2 write case;
  - `draw_frame()` clears colour, depth and stencil at every render
    (:2251-2254), and `draw_scene()` draws the background over the whole
    canvas;
  - the region array is read only for autosort (`using_autosort()`).
  So a second render into the same DC frame (a mirror, a split-screen
  half, a menu layer) replaces the first on the TV, and the two alternate.
- P2 notes Flycast's swap rule only as pacing; P10 has the FB clip as a
  scissor (bundle 11).
- PS3 form (outline): at STARTRENDER latch FB_W_SOF1. A held job ends with
  `ps3_video_end_frame_noflip()` (`ps3/ps3_video.c`) and keeps the PS3
  buffer, so a later render draws into it with no colour clear. A matching
  FB_R_SOF1/2 write posts a flip-only job. The vblank JOB_PRESENT and
  Frame Hold must respect a held frame. Scissor to the region bounding box
  and the FB clip. About 40-80 lines, plus ~30 for the region scissor.
- Byte order: none. Compare the registers with mask 0x00FFFFFC (as
  Flycast); read region entries with `vri()` and shifts, as
  `using_autosort()` does.
- Risk: medium (the present path, Frame Hold, SPE Draw). Of the named
  games only Quake III is in Matt's set.

### P61 CD-DA play limits and resume rules  `[x]` "CD-DA Rules" built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source

- Flycast `e44ed4a8e` (2020-03, "gdrom: don't resume cdda if not playing.
  stop if cur > end") and the CD-DA part of `959b6344b` (2020-06). No game
  named.
- Ours (`dc/gdrom/gdromv3.cpp`):
  - (a) the end test is `CurrAddr.FAD == EndAddr.FAD` (:196). Flycast uses
    `>=` (`core/hw/gdrom/gdromv3.cpp:84`). A start past the end, or a
    play that moved past it, never stops.
  - (b) SPI_CD_PLAY (:741-775) keeps an end FAD of 0. Flycast takes the
    session end from REQ_SES session 0 (:917-924).
  - (c) :747-748 set `playing` and PLAY for every param type, so param 7
    (resume) plays also when nothing is paused. After SEEK type 3 (stop,
    CurrAddr = 150) it plays the data track at FAD 150 as audio; after a
    track ends it runs on past EndAddr, because of (a). Flycast resumes
    only from Paused, and stops when CurrAddr > EndAddr.
  - (d) :772 takes the repeat count from byte 6 of every packet, resume
    too. Flycast sets it only for params 1 and 2 (:926). If a resume
    packet has 0 there (unverified), a looping track stops looping after a
    pause.
  - (e) at the end of a play Flycast sets PAUSE and audio status 0x13
    (terminated); ours sets STANDBY (:200-202). Do (e) only with P21's
    3-state flag, else GET_SCD reports 0x15, not 0x13.
  - (f) `gd_setdisc()` (:368-399, also reached by the ATA soft reset,
    :472-477) keeps CD-DA playing. Flycast stops it (:318).
- P21 step 2 has the 3-state flag (resume only when paused). In Flycast
  master this rule is a separate fix since 2020, not part of the dev
  timing work.
- About 20 lines. Byte order: none (u32 FADs; use shifts, not the B0-B2
  union of P47). HW: ChuChu Rocket and Looney Tunes CD-DA (pause, resume,
  looping tracks), the BIOS CD player. Bundle 2.

### P62 GET_SCD format 1: Q data layout and length  `[x]` "GET_SCD Q Data" built 2026-10-10, bundle 2 (plan section 0), HW SUCCESS 2026-10-10, default ON in source; extends P47

- Ours, the format 1 branch of SPI_GET_SCD
  (`dc/gdrom/gdromv3.cpp:911-939`): `subc = &subc_info[5]` with
  `subc[0] = 0x14` writes the control/ADR byte twice, so TNO, index, the
  elapsed FAD and the absolute FAD land one byte late. `sz = 12` then cuts
  the absolute FAD. It always reports track 1, index 1 and control 4
  (data), also during CD-DA.
- Flycast (`core/hw/gdrom/gdromv3.cpp:642-673`): the Q data starts at
  `subc_info[4]`, 14 bytes, with the real track, the elapsed FAD and
  ADR/control from the TOC (`libGDR_GetTrackNumber()`,
  `libGDR_GetTrackAdrAndControl()`).
- `959b6344b` also clamps the reply to the allocation length. Our format 0
  sends `alloc_len` bytes from a 100-byte stack buffer (:848, :905): an
  over-read when a game asks for more than 100.
- 3-4 lines, plus the track lookup. Byte order: bytes only. Build it with
  P47 (the same function). Impact unknown: our comment says the BIOS asks
  for format 0, 100 bytes.

### P63 Face colours reset once per frame  `[~]` "Face Reset" built 2026-10-11, bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt)

- Flycast `f599891e0` and `537c2a4ed` (NHL 2K2 black ice, NHL 2K black
  players): at every TA context parse it sets FaceBaseColor and
  FaceOffsColor, and the volume-1 copies, to 0xFF, opaque white
  (`core/hw/pvr/ta_vtx.cpp:126-139`, through `ta_parse_reset()`). Inside a
  frame they carry over, so intensity mode 2 reuses them.
- Ours: the PPU decoder sets `s_face_a/r/g/b` = 1 only at program start
  (`ps3/ps3_rend.cpp:180`); `ListInit()` (:612) resets only the lists. The
  SPU decoder is the same (`ps3/spu/spe_dec.c:53`; `dec_reset()` :714 does
  not touch them). So an intensity mode 2 polygon before any mode 1 header
  in a frame takes the last colour of the previous frame.
- 1-2 lines in each decoder. Change both, or the S2 compare fails (with
  SPE Draw ON the RSX draws the SPU lists).
- Same commit, 2 lines: TA_LIST_CONT resets the store list type and the
  FSM state (Flycast `core/hw/pvr/ta.cpp:506-517`). Ours keeps
  `s_fsm_state` and `s_store_list` (`ListCont()` in `ps3/ps3_rend.cpp`).
  It matters only if a list is open at LIST_CONT.
- When 3b or 3j add the per-vertex offset colour, reset the face offset
  colour the same way.
- Byte order: none. Neither NHL game is in Matt's set. Bundle 4.

### P64 FPSCR cause bits stay set after a guest write  `[ ]`

- Flycast `bbdef06a0` (master, 2026-10-07, issue #1058): `UpdateFPSCR()`
  clears the cause field (bits 12-17: `fpscr.full &= ~0x3f000`) on every
  guest write. Its FPU ops never set the cause field, so a cause bit that
  lds or lds.l writes stays set forever. Named: Kita He. White
  Illumination (T14301M, WinCE) resets to the BIOS when a karaoke song
  starts; its matrix code reads cause.V after fcmp/gt.
- Ours: `UpdateFPSCR()` (`dc/sh4/sh4_registers.cpp:100-106`) has no clear,
  and our FPU ops do not set the cause field either. Every FPSCR write
  reaches it: the interpreter (lds and lds.l in `dc/sh4/sh4_cpu_misc.h`)
  and the JIT `shop_sync_fpscr` (`ps3/ps3_driver.cpp`). The fschg SZ-only
  path writes no cause bits, and JIT FRCHG takes the full path when more
  than FR changed.
- 1 line. Byte order: none. The named game is WinCE (P38); no game in
  Matt's set is known to need it.

### P65 Channel 2 DMA: length, the 0x12 mirror and DMAOR  `[ ]`

- Flycast `0d81c9fb0` ("clean dmac module, correct address/len mask, add
  missing mirror"; "fix infinite loop", no game named): a ch2 DMA always
  completes and raises holly_CH2_DMA. The length drops its low 5 bits
  instead of refusing the DMA, and 0x12xxxxxx is a TA mirror. `sb.cpp`
  masks the registers on write: SB_C2DLEN & 0x00FFFFE0, SB_C2DSTAT =
  (v & 0x03FFFFE0) | 0x10000000.
- Ours: `DMAC_Ch2St()` (`dc/sh4/dmac.cpp:52-80`) returns when `len & 0x1F`
  or `len == 0`: no transfer, no TE, no IRQ, and SB_C2DST stays 1. A guest
  that waits for the end IRQ hangs. `isDstTA()` (:45) accepts only
  0x10xxxxxx; a 0x12 target takes the invalid branch (no data, IRQ
  raised). `dc/mem/sb.cpp:536-548` stores both registers unmasked.
- Flycast `8001af974` (register write masks) masks DMAOR with 0x8307.
  `WriteDMAOR()` (`dc/sh4/dmac.cpp:228-231`) stores the raw value, and
  `DMAOR_MASK` is 0xFFFF8201 (`dc/sh4/dmac.h:121`). The ch2, PVR-DMA and
  GD-ROM DMA checks use it, so junk in bits 31:16 aborts them with no IRQ.
- About 6 lines. Byte order: none. P7 (LMMODE) is the other ch2 item.
  Bundle 1. Not Capcom vs. SNK 2: `cvs2.txt` has no `Ch2 abort` line.

### P66 Small TA and renderer fixes  `[~]` "Volume Cap", "TA Data Gate" and the invalid TA types (in "TA Bad Records") built 2026-10-11, bundle 4 (plan section 0), HW run 2026-10-11, default ON in source (Matt); the TA store size, the object list overflow, the RTT items and TA_YUV_TEX_BASE open

None of the named games is in Matt's set.
- Modifier-volume triangle cap: Flycast `c42799726` and `9f13ded50` (Jet
  Grind Radio) raised it from 4096 to 8192; today it uses growing lists
  (`core/hw/pvr/ta_ctx.h:352-354`). Ours: `MAX_MODTRI` 4096 and
  `MAX_MODVOL` 256 (`ps3/ps3_rend.cpp:86-87`, `ps3/spu/spe_bell.h:17-18`).
  Triangles past the cap are dropped (`ps3/ps3_rend.cpp:587`, :598;
  `ps3/spu/spe_dec.c:448`, :459), which breaks the volume parity. 2 lines
  plus ~1.2 MB for 4 contexts. Our logs peak at ~452 triangles per render
  (Crazy Taxi).
- TA store size: Flycast `7da4f09c0` raised its TA store from 2 MB to 8 MB
  for Dead or Alive 2 LE. Ours is 2 MB per context (`TA_RAW_SIZE`,
  `ps3/ps3_rend.cpp:91`); past it the replay copy is dropped (geometry
  lost, no hang; `ta_raw_of=` counts it). 1 line, +8 MB at 4 MB per
  context. Our 64K vertex cap may bind first (unverified).
- Object list overflow: Flycast `13de6b9a1` (Ooga Booga crash or hang in
  multiplayer) raises holly_MATR_NOMEM and drops the record when its 8 MB
  store is full (`core/hw/pvr/ta.cpp:534-539`). Ours raises nothing (the
  enum only, `plugins/plugin_header.h:143-144`). About 3 lines. Raise it
  only past 8 MB of stream: our cap is a quarter of Flycast's. A
  heuristic: the hardware raises it from TA_OL_LIMIT and TA_ISP_LIMIT.
- TA data with no list init: Flycast `2bcc3c574` (Vigilante 8 freeze in
  an FMV) drops TA records while no TA context is current: before the
  first TA_LIST_INIT, and after STARTRENDER until the next list init
  (`core/hw/pvr/ta_ctx.cpp:175-188`). Ours: `s_ta` starts at context 0
  (`ps3/ps3_rend.cpp:142`) and STARTRENDER never clears it, so such
  records still raise list-end IRQs and are drawn by the next STARTRENDER.
  About 5 lines. Keep the nullDC-v1 rule "a second STARTRENDER redraws".
  Unverified that ours freezes in Vigilante 8.
- Invalid TA types: Flycast `c301bf082` skips a polygon header with
  Volume = 1 and Col_Type = 1 (obj_ctrl 0x50-0x5F and 0xD0-0xDF, where
  `ta_type_lut` is 0xFFFFFFFF). Our PPU splitter calls
  `ta_poly_param_lut[255]` (`plugs/drkPvr/ta.h:384-406`), out of a
  5-entry table: a crash or a hang. Only when the PPU replays (SPE Draw
  OFF, a failed SPU fence, TA Fast Store OFF); the SPU decoder skips the
  record (`ps3/spu/spe_dec.c:589-593`). About 3 lines. It fits P9;
  `TArender.MD` notes the index.
- RTT stride: Flycast `b77843b92` ("Happens for Virtua Tennis") clamps the
  RTT row width to FB_W_LINESTRIDE (`core/rend/TexCache.cpp:1126`);
  `3256e0281` names the symptom (RTT overwrites other textures: the tennis
  ball, the players' bags). Ours writes w pixels per row whatever the
  stride (`rtt_render()`). 2 lines, with P54.
- RTT scaler: Flycast `e96b38ff5` (Skies of Arcadia PAL pause menu and
  battle animation, issue #1127) applies SCALER_CTL to RTT
  (`getWriteFBToVramParams()`, `core/rend/transform_matrix.cpp:458-472`).
  Ours maps the TA area 1:1 onto the FB clip (`rtt_render()`), and
  SCALER_CTL is not in the render snapshot. About 8 lines. RTT has never
  fired in our games (`rtt=0/0`).
- TA_YUV_TEX_BASE: Flycast masks it with 0x00FFFFF8
  (`core/hw/pvr/pvr_regs.cpp:182-185`); ours keeps the low 3 bits
  (`YUV_init()`, `dc/pvr/pvr_if.cpp:165`). 1 line.

### P67 Small SH4 and memory fixes  `[ ]`

- Area 7 registers outside P4: Flycast `b3de6a166` (Resident Evil Code
  Veronica X, Chris, floor 1F crash; Dolphin Blue) maps the on-chip
  registers only at P4 (0xFF). In P0-P3, area 7 (0x1F, 0x3F, 0x5F, 0x9F,
  0xBF, 0xDF) reads 0 and drops writes; on-chip RAM stays at 0x7C-0x7F.
  Ours maps `area7_handler` at 0x1F | base for every P0-P3 base
  (`map_area7()`, `dc/mem/sh4_internal_reg.cpp:764-770`), so a stray
  pointer there writes real TMU, INTC, CCN or DMAC registers. 2 lines.
  Unverified that this part alone fixed RE CVX (the commit has three
  parts; ours already reads 0 from unmapped areas).
- Block writes of odd sizes: Flycast `82e0fc7f6` and `10d9761a6` let
  `WriteMemBlock_nommu_ptr` take any size (word, halfword and byte
  tails). Ours has `verify((size & 3) == 0)` (`dc/mem/sh4_mem.cpp:224`,
  :245, :265), and our `verify` ends in an endless loop (`types.h:186`).
  A PVR DMA with SB_PDLEN not a multiple of 4 reaches it: DMA Fix (default
  1) skips the length check (`dc/pvr/pvr_sb_regs.cpp:79`), then :148 calls
  the block write. Flycast also masks SB_PDLEN to 32-byte units
  (`core/hw/holly/sb.cpp:598`). 1 line (the mask), or ~10 (the tails, at
  addr ^ 3 and addr ^ 2 on our word layout).
- SCI: Flycast `4d815570d` defines the 7 SCI registers (SCBRR1 = 0xFF,
  SCTDR1 = 0xFF, SCSSR1 = 0x84 = TDRE | TEND at reset). Ours registers
  none (`dc/sh4/sci.cpp`): every access returns 0 and prints a line, so a
  TDRE poll never ends and floods the log. About 25 lines. No game named.
- SB_TFREM: Flycast `e5b18cd8a` reads 8 (256 bytes free in the TA FIFO).
  Ours reads 0 (`dc/mem/sb.cpp:55`, :630-634), so a guest that polls for
  FIFO space waits forever. 1 line. Named only for WinCE games.
- 8-bit VRAM reads: Flycast `c5a42a57b` returns `vram[pvr_map32(addr)]`.
  Ours returns 0 and prints a line (`dc/pvr/pvr_if.cpp:385-389`). 2 lines;
  on our word layout read `vram[off ^ 3]`. No game named.
- Register access sizes on big endian (Flycast `099a758c9`, `8001af974`):
  Flycast stores each register as a u32 and drops or logs wrong-size
  accesses. Ours points `data8`, `data16` and `data32` at typed variables
  (`RegisterStruct`, `types.h:212-222`; on-chip
  `dc/mem/sh4_internal_reg.cpp:64-66` and :122-124; SB
  `dc/mem/sb.cpp:355-361` and :415-423). On our host a byte or halfword
  access to a 32-bit register hits bits 31:24 or 31:16 (a byte write of 1
  to SB_ADEN sets bit 24), and a mov.l of a 16-bit register returns it
  and the next one (`tmu_regs_CR` is u16[3]: TCR0 << 16 | TCR1). About 30
  lines to make them size-safe. No game known (unverified).

### P68 Small system fixes  `[ ]`

- Maple reply outside RAM: Flycast `8fdcea2e2` (Cheats 'N Codes Volume 1)
  drops the reply and raises holly_MAPLE_OVERRUN (error bit 9) at the end
  of the DMA. Ours masks the address into RAM and writes there
  (`dc/maple/maple_if.cpp:112-117`); our HollyInterruptID has no maple
  error ids. About 8 lines.
- SB_MMSEL and the recipient address: in Flycast `8192eea38`, SB_MMSEL = 0
  swaps every frame word in and out, and a recipient address with no unit
  bit goes to the main unit. Ours ignores SB_MMSEL
  (`dc/maple/maple_if.cpp:85-164`) and sends that address to sub-unit 1
  (`dc/maple/maple_helper.cpp:15`). About 15 lines; swap the native u32
  values, never the XOR-3 byte view. No game named. Its DeviceReset, Kill
  and AllStatusReq part is P29.
- SB_MDSTAR outside RAM: `GetMemPtr()` returns NULL and `maple_DoDma()`
  dereferences it (`dc/maple/maple_if.cpp:121-124`): a PPU crash. Flycast
  `a1aea570c` stops the DMA. About 5 lines. Only a Naomi test menu named.
- BIOS and flash mirror: in Flycast (`6af509159`) the BIOS and the flash
  are not mirrored at 0x02000000 (reads 0, writes dropped). Ours mirrors
  pages 0x00-0x01 at 0x02 (`dc/mem/sh4_area0.cpp:296-297`), although the
  comment there says Flash and Boot ROM are excluded. About 5 lines. No
  game named.
- ATA soft reset: Flycast `959b6344b` sets GDStatus 0, Error 1, IntReason
  1, ByteCount 0xEB14 and PAUSE ("DC Checker expects these values", a
  test program). Ours only calls `gd_reset()`
  (`dc/gdrom/gdromv3.cpp:472-477`). About 6 lines.
- SB_ADSTAR and SB_ADSTAG: Flycast `79be46432` stores them as
  `data & 0x1FFFFFE0`, also the E1, E2 and DD pairs
  (`core/hw/aica/aica_if.cpp:431-455`, :474-493). Ours stores the raw
  value (`dc/mem/sb.cpp`, REG_WRITE_DATA) and uses it as it is
  (`dc/aica/aica_if.cpp:294-305`, E1 :365-366, E2 :415-416): a G2 DMA
  starts at the raw address, not at the 32-byte boundary below it. About
  10 lines. No game named (a crash report).
- ELF boot: Flycast `333b21fac` writes SCIEB = 0x48, SCILV0 = 0x18,
  SCILV1 = 0x50 and SCILV2 = 0x08 when it boots an ELF without the BIOS
  (`core/reios/reios.cpp:409-435`). Ours sets only PC, SP and SR
  (`elf_apply_entry()`, `plugs/ImgReader/elf_loader.cpp:239-245`). 4
  lines through `libAICA_WriteMem_aica_reg`, so that nullAICA and SPE
  ARM7 both get them. Homebrew .elf only; with P33.
- VMU LCD media info: Flycast `dc7f60846` (WinCE VMU issues) sends 2 in
  the 4th byte of the LCD GetMediaInfo reply; ours sends 0
  (`dc/maple/maple_devs.cpp:400`). 1 line, WinCE only (P38).

### P75 RTT read-back never returns: the Crazy Taxi pause freeze  `[ ]`

- The fault (plan section 0, open since 2026-10-10): Crazy Taxi, START in
  a drive: `[hb] ... phase=rtt ... rend=rtt`, the vblanks stop, no fault
  line. It is the first render to texture seen on hardware.
- Cause, read in the source. `rtt_render()` (`ps3/ps3_rend.cpp:3012`)
  calls `ps3_video_readback()` (:3051), which calls PSL1GHT's
  `rsxSetTransferData` (`ps3/ps3_video.c:386-388`, its only caller). In
  PSL1GHT (`ppu/librsx/commands_impl.h:1826-1885`) the pitches are `u32`
  and the limit is `const s32 MIN_PITCH = -32768`. The compare is
  unsigned, so `srcPitch < MIN_PITCH` is true for every pitch. That branch
  loops on `while (--rowCount >= 0)` with a `u32` rowCount: it has no end.
  The call never returns, for any input. It stops only if the FIFO
  callback fails.
- The binary agrees (disassembly of `build/notdc_ps3.elf`,
  `.rsxSetTransferData`): after the last chunk the code branches back,
  loads the count again and sends the same 9-word NV0039 command.
- The log agrees (`ct_b2.txt`, scratchpad of session 12aaf4cf): in the
  last second the render sequence rose by 267 = 53 renders x 5 stamps +
  TADEC + RTT. The PH_READBACK stamp (the line after the call) never came,
  and there is no timeout line in 39 s.
- Fix form (read in the source, not run): three straight-line PSL1GHT
  calls in this order. The transfer starts at the last one.
  `rsxSetTransferDataMode(ctx, GCM_TRANSFER_LOCAL_TO_MAIN)`,
  `rsxSetTransferDataFormat(ctx, s_pitch, w * 4, w * 4, h, 1, 1)`,
  `rsxSetTransferDataOffset(ctx, dst, src)`
  (`commands_impl.h`, from :1898). 3 lines.
- A second fault in the same function: `rsxFinish(s_ctx, 1)` (:391).
  PSL1GHT's `rsxFinish` (`ppu/librsx/buffer_impl.h:23-30`) sets a
  reference command and waits `while (ctrl->ref != ref_value)`. Every
  caller of ours passes 1 (`ps3/ps3_video.c:88`, :310, :391;
  `ps3/ps3gl/ps3gl_glapi.c:46`) and nothing resets the reference. So only
  the first call waits. From the second RTT on, the pack would read the
  staging buffer before the RSX copied it (inferred). Fix: a counter as
  the value. 2 lines.
- After the fix the pause background needs the RTT scaler. Flycast
  `8d4188159`: "Vertical scale factor support added - fixes Crazy Taxi
  pause menu background positioning". Today's rule is
  `core/rend/transform_matrix.cpp:295-302` (target height x 1024 /
  vscalefactor, width / 2 with hscale; the matrix from the unscaled size,
  :94-102). P66 lists the RTT scaler, but it cites
  `getWriteFBToVramParams`, which is Flycast's framebuffer emulation.
- Crazy Taxi is not on Flycast's VRAM-copy list (`core/emulator.cpp:64-161`):
  Flycast shows its pause with no copy to VRAM
  (`core/rend/gles/gltex.cpp:262-275`). That list is a per-game override
  of an option default; ours always copies.
- Other waits with no end on this path: `sysCondWait(s_done_cv, 0)` in
  `rth_run()` (`ps3/ps3_rend.cpp:3839`, :3881). The vring fence and the
  SPU waits time out and print.
- Not checked: the fix on hardware; that the RSX accepts the local-to-main
  transfer; Crazy Taxi's RTT registers (size, pack mode, SCALER_CTL): log
  them in the first run.
- HW: Crazy Taxi, START in a drive, twice in one boot (the second shows
  the `rsxFinish` fix). A printf before and after the transfer.
- Byte order: none here. The pack keeps its form (:3072).

### P76 Small RTT and present fixes  `[ ]`

- Dropped with no trace. `ps3_video_begin_frame()` returns 0 while a flip
  is queued (`ps3/ps3_video.c:343-352`; 15 of 10882 in the log). Then
  `rtt_render()` returns with no copy to VRAM (`ps3/ps3_rend.cpp:3033`),
  so the game's texture keeps old data. `present_fb_draw()` returns too
  (:3281), and the vblank still stamps its mark (:4002-4009): a static 2D
  screen is not tried again. An RTT higher than the display (720 lines)
  returns silently (:3021). Flycast's default waits for the previous
  render (`core/hw/pvr/ta_ctx.cpp:59-64`). PS3 form: an RTT waits for the
  flip (it blocks the SH4 thread already); the 2D present does not stamp
  the mark when it did not draw; `rtt=copies/count` in the stats line
  shows a drop. About 8 lines.
- RTT size from the tile clip. Flycast: with the VRAM copy the target size
  is the tile clip (the region box capped by TA_GLOB_TILE_CLIP;
  `core/rend/transform_matrix.cpp:63-74`, dev
  `core/hw/pvr/ta_vtx.cpp:1235-1243`). The write then clips to the FB clip
  and to linestride / 2 (`core/rend/TexCache.cpp:1122-1126`). `9dbca9632`
  only moves that call. Ours uses FB_X_CLIP and FB_Y_CLIP only
  (`ps3/ps3_rend.cpp:3014-3021`); `glob_clip_x/y` is in the snapshot
  already (:1474). An RTT whose FB clip is larger than its tiles writes
  past the texture in VRAM (game unverified). About 4 lines.
- Pack modes 4-7 (new fact for P54). No reference renders to texture in
  these modes: Flycast (`core/rend/gles/gltex.cpp:176-184`) and Reicast
  (`rend/gles/glesrend.cpp:1070-1078`) return. P54's writers for modes 4,
  5 and 6 come from `WriteFramebuffer`, Flycast's framebuffer emulation.
  Ours writes 32-bit words for 6 and 7 (`ps3/ps3_rend.cpp:3075-3084`).
  1 line: return from mode 4 up. Also: Flycast clears an RTT target to
  (0,0,0,0) (`core/rend/gles/gles.cpp:1237`); ours clears alpha to 1
  (:3040). It shows in the 1555 and 4444 packs.
- `FB_R_CTRL.fb_line_double` (new fact for P10): Flycast halves the canvas
  height (`core/rend/transform_matrix.cpp:58-59`); ours ignores the bit
  (`plugs/drkPvr/SPG.cpp:91-101`). 2 lines, game unknown.
- Blank video: a 2D present with `VO_CONTROL.blank_video` shows
  VO_BORDER_COL in Flycast (`core/rend/gles/gldraw.cpp:672-677`); ours
  tests fb_enable only (`ps3/ps3_rend.cpp:3988`). 4 lines. Low.
- Bundle 11, after P75. Byte order: VRAM words are native u32 with the
  left pixel in the low half (`ps3/ps3_rend.cpp:1331-1336`); keep that
  form in any ported `u16*` write.

### P77 Disc Swap: the command in flight, the lid-open phase  `[ ]`

- Ours: `gdrom_disc_swap()` (`dc/gdrom/gdromv3.cpp:1625-1648`) zeroes
  read_params, packet_cmd, read_buff, pio_buff and ata_cmd (P34's note
  from Reicast `c050ee8f`), but it leaves `gd_state`.
  - A swap in gds_waitpacket resets `packet_cmd.index`, so the packet
    never reaches its 6 words (:1303-1306).
  - A swap in gds_pio_send_data loses the reply (:1236-1237: the index
    goes below zero and the state never advances).
  - The next ATA command then hits `verify(gd_state==gds_waitcmd)`
    (:1358-1359), and our verify is an endless loop (`types.h:186-188`).
- Flycast removed exactly this. `cbd01fba3` (2024-01-24, "gdrom: fix disk
  swapping for some games": Skies of Arcadia, Shenmue II, Alone in the
  Dark, Dancing Blade, First Kiss Story II, Kaen Seibo, the BIOS menu)
  took `libCore_gdrom_disc_change()` out of the swap. Today
  `gd_disc_change()` runs only at init and reset
  (`core/hw/gdrom/gdromv3.cpp:366-376`, callers :1349, :1403); a swap
  calls `gd_setdisc()` only (`core/imgread/common.cpp:351-364`, :369-380).
- Fix: drop the clears; keep the CD-DA stop, the sense and the busy
  second. About 8 lines less.
- The lid-open phase. Flycast cannot insert a disc without it: its button
  reads "Eject Disk" until `gdr::isOpen()` (`core/ui/gui.cpp:620-630`);
  `openLid()` sets the type Open, and `gd_setdisc()` gives GD_OPEN with
  DRDY (`core/imgread/common.cpp:148-154`, `gdromv3.cpp:330-334`). So each
  game that Flycast names saw OPEN, then BUSY for 1 s, then sense 6/0x28.
  Ours goes from the old disc straight to BUSY
  (`dc/gdrom/gdromv3.cpp:1640-1645`). No reader returns Open
  (`plugs/ImgReader/ImgReader.cpp:49-55`), although `gd_setdisc` has the
  case (:449-451). About 12 lines: an OPEN phase before the BUSY second.
  Which games wait for OPEN: unverified. It is the first suspect if a game
  ignores the swap on HW. The plan's bundle 2 block (section 0, "Not taken
  from Flycast") lists the open-lid state as not taken.
- The same in both: the sense rules (2/04/01 while BUSY, then 6/0x28 or
  0x29), TEST_UNIT, NOP, the REQ_ERROR clear, PAUSE after the second.
  Flycast's `diskChange()` (`core/emulator.cpp:1124-1144`) only reloads
  per-game settings and cheats.
- Not known: where the SELECT menu stops the SH4 against a GD command, so
  how often the first fault hits (games poll the drive during the swap
  prompt). Byte order: none.
- HW (Disc Swap has no HW test yet, bundle 2): a 2-disc game at its swap
  prompt.

### P78 Small MMU and BSC register fixes  `[ ]`

- MMUCR write mask: Flycast masks the written value with 0xfcfcff05
  (`core/hw/sh4/modules/ccn.cpp:41`); ours stores it raw
  (`CCN_MMUCR_write()`, `dc/sh4/ccn.cpp:31-52`). 1 line, with P58.
- UTLB and ITLB data array 2: a write with address bit 23 set (F78xxxxx,
  F38xxxxx) goes to the Assistance word in Flycast
  (`core/hw/sh4/sh4_mmr.cpp:297-305`, :358-366) and in Reicast
  (`libswirl/hw/sh4/sh4_mmr.cpp:277`, :360). Ours writes every F7 and F3
  access into `Data.reg_data` (`dc/mem/sh4_internal_reg.cpp:274-279`,
  :339-344), so such a write replaces PPN and V. The four P58 games do not
  do it (two syncs per entry in the logs). 2 lines each; WinCE (P38).
- RFCR: Flycast returns a fixed 17 and drops writes
  (`core/hw/sh4/modules/bsc.cpp:90`, :141); ours is a plain register
  (`dc/sh4/bsc.cpp:167-168`). A loop that waits for a refresh count would
  spin in ours. No game known. 2 lines.
- Byte order: none (masks on whole words).

### P79 Punch-through alpha is 1 after the alpha test  `[ ]`

- Flycast GL and DX11: in the punch-through list a texel that passes the
  alpha test is drawn with alpha 1 (`core/rend/gles/gles.cpp:345-350`;
  `79bbd86c2`, `9f60c5e85`: Langrisser Millennium). Flycast DX9 blends
  with the real alpha (`core/rend/dx9/d3d_renderer.cpp:796-802`), as ours.
  Flycast's back ends disagree, and the hardware rule was not checked.
- Ours: `ps3/ps3_rend.cpp:1716-1720` blends the edge texels (ref <= alpha
  < 1) as translucent. That is the "PT Blend" row of bundle 4 (P25).
- Fixed-function form: in the PT list map SRC_ALPHA -> ONE and
  INV_SRC_ALPHA -> ZERO; ONE/ZERO is blend off. Jet Grind Radio: all 148
  PT strips use that pair, so they draw with no blend (cheaper). About 6
  lines, as a third level of the PT Blend row.
- Confidence: medium. HW: Jet Grind Radio (the edges of punch-through
  textures), Crazy Taxi, Sonic Adventure.

### P80 Sprite fourth corner from the plane  `[ ]`

- Flycast (`core/hw/pvr/ta_vtx.cpp:1030-1077`, :1099), Reicast and
  Minicast (`libswirl/hw/pvr/ta_vtx.cpp:1347-1392`): the Z, U and V of a
  sprite's fourth corner D come from the plane through A, B and C, at D's
  own x and y.
- Ours: A + C - B (`ps3/ps3_rend.cpp:682`, :690-691;
  `ps3/spu/spe_dec.c:441-448`). That is right only for a parallelogram.
- Fault: a skewed texture on a trapezoid sprite. No game named.
- PS3 form: about 15 lines on the PPU and about 30 on the SPU (2 divides;
  keep the two decoders bit-exact through doubles, `misc/ta4_check`). Keep
  the old path when D is the parallelogram point, so today's output does
  not change there.

### P81 Sound output levels: the budget, and a probe  `[ ]`

- Looney Tunes (open since 2026-10-11, Matt: "Voices and music is just
  unbalanced."). In the boot of the complaint (`ps3log_b5_run1.txt` lines
  11392-14684, scratchpad of session 01bbad3e; HW run 1, boot 4) no CD
  audio played: SPI_CD_PLAY 0 times, and all 65 `[gdr]` seconds read
  `cdda 0/0`. The last four 5 s windows render exactly 2.0 slots per
  sample, with G2 DMA writes in each: a 2-slot stream. Older boots with a
  race have 1-5 CD_PLAY. So the game has two music paths: a slot stream
  (menus, FMV) and CD-DA (races). Plan section 0's lead ("the music is CD
  audio (EXTS)") does not hold for that boot: there the balance is DSP
  voices (EFREG) against direct slots. `cdda/s=75` counts silent records
  too.
- The level budget, one voice to the DAC. Ours is
  `plugs/caiqueAICA/aica_model.cpp` (the SPU model is the same source);
  Flycast is `core/hw/aica/sgc_if.cpp`; Reicast agrees with Flycast
  unless said.
  - Voice: the 16-bit sample x16 (20 bits) in all.
  - TL and AEG: the references 2^(-TL/16), 0.376 dB per step (Flycast
    :1441, :670). Ours `(127 - a[5:0]) / 2^(7 + (a >> 6))` (:722-728),
    which caique measured (caique-rtl `model/NOTES.md:53-55`): -0.07 dB
    at TL 0, up to +0.42 dB inside an octave.
  - DISDL, DIPAN, IMXL, EFSDL, EFPAN, MVOL: the references 2^(-n/2), 3.01
    dB per step (:55-59, :133-147, :1437-1438). Ours x0.75 at odd n
    (`send_level()`, :729-735): +0.51 dB per odd stage. caique measured
    IMXL only (`NOTES.md:57-61`); the other five are its assumption.
    Level 0 is off, pan 0xF is far side off, centre is both sides full,
    in all.
  - DSP: MIXS 20 bits, MIXS << 4, EXTS << 8, EFREG = SHIFTED >> 8 in ours
    (:817-818, :893) and in Flycast (`dsp_interp.cpp:78-80`, :211).
    Reicast: 16-bit MIXS, EFREG += SHIFTED >> 4
    (`libswirl/hw/aica/dsp_interp.cpp:298`). Net unity in all three: a
    voice at IMXL 15, COEF 0x7FF8 and EFSDL 15 equals the same voice at
    DISDL 15.
  - CD-DA: the 16-bit sample goes in unscaled at EFSDL/EFPAN 16 (left)
    and 17 (right) in all (ours :1095-1098; Flycast :1569-1578), so it
    equals a full-level voice at DISDL 15. caique never measured EXTS
    (`NOTES.md:43`).
  - Mono: Flycast (L+R)/2 (:1606-1607), Reicast and ours L+R (P31).
    DAC18B >> 2, no final shift and a 16-bit clamp with no headroom in
    all. nullAICA x0.9.
  - Net, ours against Flycast with its DSP ON: an EFREG voice against
    CD-DA -0.6 to +1.4 dB; an EFREG voice against a direct voice +-1.5
    dB. So no level law moves one path against another by more than
    about 1.5 dB. The laws do not explain "unbalanced".
- The large difference is the baseline of a comparison. Flycast's default
  (DSP OFF, :709-710) and Reicast's (NoBatch false,
  `libswirl/hw/aica/sgc_if.cpp:1240-1243`) play a DSP-only voice dry, on
  both sides, at TL + IMXL: COEF, SHIFT, EFSDL and EFPAN do not act.
  Compare by ear only with Flycast's DSP ON. The dry mix itself stays
  rejected (P20).
- The SPU clipping (plan 6.6: "0.9-1.4 dB louder than nullAICA"). That
  figure is the dry path at one TL. A dry centre voice, ours against
  nullAICA: +0.85 to +2.36 dB; against Flycast -0.07 to +1.44 dB.
  nullAICA mixes no EFREG (its DSP is a stub), and Crazy Taxi runs a
  79-step DSP program (`live=79/128` in 135 windows of
  `ps3log_b5_run2.txt`), with SPE AICA DSP ON in every `peak=` window. So
  the SPU peaks hold effect returns that nullAICA never had, and "90% is
  nullAICA's level" (plan section 0) is too simple. Every reference
  clamps at 16 bits (Flycast :1630-1631), so peaks of 125-181% clip there
  too.
- Steps:
  1. No code: one Crazy Taxi drive with SPE AICA DSP OFF; read `peak=`.
  2. No code: Looney Tunes in Flycast with DSP ON against ours, in the
     same scene; note the scene (menu or race).
  3. A probe on the `[aica-spe] sound` line: MVOL (0x2800), the EXTS
     sends (0x2040, 0x2044), each EFREG channel with EFSDL != 0, the live
     DSP steps (IRA, YSEL, COEF, SHIFT, EWA), and window peaks of the
     direct sum, EXTS and EFREG. About 45 lines. The model's registers
     are host-order u16: no byte-order work.
- No fix is known. Not known: the values Looney Tunes writes; the
  console's law for the five stages caique did not measure, and for EXTS.

### P82 Small sound fixes  `[ ]`

- EXTS read-back: Flycast stores the CD-DA sample in EXTS at each sample
  (`core/hw/aica/sgc_if.cpp:1580-1581`), and 0x45C0/0x45C4 read it. Ours
  reads 0: the DSP read range ends at 0x45C0 (`ps3/spu/aica_ctl.h:188-190`,
  `plugs/nullAICA/aica_mem.cpp:120`; caique's `read()`,
  `aica_model.cpp:1831`, was measured with no disc playing). Use: CD level
  meters (unverified, no game known). About 10 lines, inside DSP Reg Reads
  (P20 G4). Big endian: write the halfword at [A+2].
- nullAICA only, the ALFO is twice too deep: `alfo = w >> (8 - ALFOS)` in
  TL units (`plugs/nullAICA/sgc_if.cpp:806`, :1057; Flycast :864, :1233)
  gives 47.8 dB at ALFOS 7. caique measured `(w & 0xFE) >> (7 - ALFOS)` in
  0.094 dB units: 23.8 dB (`NOTES.md:193`). 1 line (shift 9 - ALFOS). It
  acts only with SPE AICA Sound OFF.
- CD-DA past the lead-out (new fact for P61): Flycast stops the play when
  the read returns 0 (`core/hw/gdrom/gdromv3.cpp:73-80`,
  `core/imgread/common.cpp:301-302`): FAD - 1, Terminated, PAUSE, zeros.
  Ours never tests (`dc/gdrom/gdromv3.cpp:250`; `libGDR_ReadSector` is
  void). About 8 lines. P61's end FAD covers most cases, and the plan's
  bundle 2 block lists "the stop at the lead-out" as not taken. Low.
- ARMRST reads back raw; Flycast masks bit 0. No user known.

---

## 3. Features and references

### P13 Puru Puru (rumble) pack  `[ ]`

- Reicast `hw/maple/maple_devs.cpp:1023-1155` decodes SetCondition into power
  and duration. Minicast `a656d98` adds only the host side.
- Ours has no vibration device (slot 2 holds a second VMU). PS3: the DS3
  motors (`ioPadSetActDirect`, API not checked in this tree). About 120
  lines. No speed or fault value.
- 2026-10-06 (Flycast review): PSL1GHT `io/pad.h:204-218` and :422 provide
  `padActParam{small_motor, large_motor}` and `ioPadSetActDirect`. Flycast
  reads `AST = dma_buffer_in[10]` (`core/hw/maple/maple_devs.cpp:1091`); on
  our word-ordered buffer that needs XOR-3 byte access.

### P14 Fog, offset colour and bump map reference (plan 3a, 3b, 3j)  `[ ]` merged: plan 3a, 3b, 3j

Reicast GLES shader, the reference for a PVR Cg shader:
- shader text `rend/gles/glesrend.cpp:138-323`: `fog_mode2()` :208-216,
  offset add :289-292, fog modes :297-307, tile clip :229-240;
- uniforms :997-1028; fog table texture `UpdateFogTexture()` :702-728;
- vertex offset colour `hw/pvr/ta_vtx.cpp:1014-1019`.

Flycast (2026-10-06), closest to a Cg path:
- `core/rend/dx9/d3d_shaders.cpp`: fog lookup :101-115. The fog table is a
  128x2 A8 texture with linear filtering (`core/rend/TexCache.h:297-305`).
- ps3gl already draws everything with precompiled Cg fragment programs, one
  per texture mode (`ps3/ps3gl/ps3gl_shaders.c:37-56`). So fog, offset
  colour, colour clamp and bump maps mean more fragment programs plus a
  second colour attribute. Inside-mode tile clip also needs a fragment
  discard.
- Per-pixel 1/W: pass W as a TEXCOORD; perspective-correct interpolation
  gives the true W.
- Fixed function is enough for outside-mode tile clip (scissor,
  `core/rend/transform_matrix.cpp:206-254`), the FB clip and P25.

Bump maps (added 2026-10-08, plan 3j; evidence and parts are there):
- Reicast: `rend/gles/glesrend.cpp:253-257` (intensity into texel alpha,
  texel RGB = 1), :289 (no offset add for bump), `rend/TexCache.h:232-238`
  and :359-365 (`convBMP_PL`, `convBMP_TW`: raw ARGB4444),
  `rend/gles/glesdraw.cpp:209` (program picked by PixelFmt 4).
- Flycast: `core/rend/dx9/d3d_shaders.cpp:195-202` (offset colour read for
  bump), :224-228 (the same formula), :248 and :259 (no offset add, no
  vertex fog for bump); `core/rend/texconv.cpp:466` and :526 (`texBMP_TW`
  = `tex4444_TW`).
- Ours: `cvt16()` case 4 (`ps3/ps3_tex.cpp`) and `planes16()` f == 4
  (`ps3/spu/spe_pix_job.h`) return opaque white; `DcVertex` and `SpeVtx`
  hold no offset colour.

### P15 Freerunning (Minicast)  `[ ]` merged: plan 5.19

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

### P34 Disc swap  `[~]` "Disc Swap" built 2026-10-10, bundle 2 (plan section 0), default ON in source (Matt), no HW test yet

- Flycast: `core/imgread/common.cpp:148-154` (openLid) and :351-380
  (insertDisk: BUSY with sense 2/04/01, then after 1 s sense 6 / ASC 0x28);
  `core/emulator.cpp:1108-1143`; `gdromv3.cpp:407-412` (CHECK from the sense
  key). Related: ATA_NOP clears DRQ (:429, `cb33387ed`, Sentimental Graffiti
  2); TEST_UNIT reports CHECK while BUSY (:729, Alone in the Dark, RE Code
  Veronica).
- Ours: DiskChange fires only at init (`dc/gdrom/gdromv3.cpp:402-406`). There
  are no sense keys: REQ_ERROR (:682-696) returns key 2 only while BUSY.
  There is no menu entry. So Shenmue I/II, Skies of Arcadia, Grandia II, D2
  and RE Code Veronica cannot get past disc 1. This corrects the section 4
  line "no disc swap on PS3": the swap is the missing feature.
- Port: about 150 lines. A "Change disc" entry in the in-game menu
  (SELECT), TermDrive / InitDrive, and the gd_spinup countdown (:1234-1246)
  reused for the BUSY phase. No byte-order issue. Low-medium risk.
- Fourth review (2026-10-06): a disc change must also clear the drive
  state. Reicast `c050ee8f` (branch `dd/msvc-heap`) and Flycast
  `gd_disc_change()` (`core/hw/gdrom/gdromv3.cpp:366-376`) clear
  read_params, set_mode_offset, packet_cmd, the PIO and DMA buffers, the
  ATA command and CD-DA. Ours only sets the status (`gd_setdisc()`,
  `dc/gdrom/gdromv3.cpp:351`, called at :404-405).

### P35 CUE/BIN and CHD readers  `[ ]`

- CUE/BIN (Redump): Flycast `core/imgread/cue.cpp` (328 lines) handles INDEX
  00/01 pregaps (:224-256), the REM SESSION gap 6750+4500+150 (:117-121) and
  REM HIGH-DENSITY AREA at FAD 45150 (:133-136); pregap fix `b0284ba9b`
  (dev). For us about 250 lines. Swap its exceptions for return codes (we
  build with -fno-exceptions). BINARY audio is little-endian, as our CD-DA
  path expects; MOTOROLA audio needs a swap. Low risk.
- CHD: Flycast `core/imgread/chd.cpp` (304 lines): pregap PGTYPE 'V'
  (:199-210), tracks padded to 4 frames (:10, :218-220), the audio byte swap
  (:161-178; a byte-stream swap, so the same on BE), the MIL-CD session gap
  (:244-262).
  - Needs libchdr (its submodule is not checked out here; the old Reicast
    copy is 4,643 lines), LzmaDec.c (1,185 lines), a FLAC decoder and zlib
    (already linked, `Makefile:72-73`). zstd only for the 'cdzs' codec.
  - Memory ~1-1.5 MB (v5 hunk map ~0.8 MB for a GD-ROM, one 19.5 KB hunk,
    codec state).
  - One `fread` per 8-sector hunk = 4 HDD requests per 32-sector fill (each
    ~6-15 ms, plan 5.32-5.33), so add a read-ahead buffer.
  - 6-8K lines, mostly third-party. Medium risk. libchdr byte-order safety
    on the PPU is unverified.

### P36 Cheat files and 16:9 output  `[ ]`

- Flycast's built-in widescreen table (`core/cheats.cpp:34-318`) is per game,
  so it breaks Matt's no-per-game-list rule.
- The generic path: it loads a user `<disc>.cht` / `.txt` file (:444-485),
  parses the RetroArch format (:381-442) and applies the codes at vblank
  (:672-756). With a widescreen code it stretches 4:3 to 16:9
  (`core/emulator.cpp:669-673`).
- Ours: no cheat engine. The output is always pillarboxed
  (`ps3/ps3_rend.cpp:1938-1942`).
- Port: about 250 lines plus a 16:9 viewport option. Writes go through
  WriteMem*_nommu, so byte order is fine. Trap: codes that patch code are not
  seen by the JIT (our only SMC guards are CCR.ICI clears and a list of known
  addresses).

### P37 Front-end extras  `[ ]`

- One VMU file per game, named by the product ID from the disc header
  (Flycast `core/oslib/oslib.cpp:50-72`; `PerGameVmu` defaults to true,
  `core/cfg/option.cpp:234`). Ours uses one file per port, `vmu_save_A1.bin`
  (`dc/maple/maple_devs.cpp:270`). It reads the disc header, not a list.
- VMU LCD overlay (Flycast `maple_devs.cpp:716-741`, `maple_cfg.cpp:172-174`).
  Ours is a no-op (`dc/maple/maple_cfg.cpp:80-83`).
- BIOS language override (Flycast `core/hw/flashrom/nvmem.cpp:161-165`,
  :190-191).
- Keyboard and mouse: PSL1GHT has `io/kb.h` (:239, :255) and `io/mouse.h`
  (:71). Our keyboard device is a stub (`ps3/ps3_input.cpp:162-176`). There
  is no mouse device (Flycast `maple_devs.cpp:1230-1321`).
- Fourth review (2026-10-06): PSL1GHT raw key codes are USB HID usages
  (`ppu/include/io/kb.h:21-49`: ESC 0x29, F1 0x3A), which are the
  Dreamcast keyboard's own codes. So Minicast's 128-entry evdev-to-HID
  table (`38045d1`) is not needed. Its mouse bits: left = bit 2, right =
  bit 1, middle = bit 3, active low; the wheel is inverted.

### P38 Windows CE (SH4 MMU)  `[ ]` merged: plan 3d

- Flycast: `core/hw/sh4/modules/mmu.cpp` 596 + `fastmmu.cpp` 337 + `mmu.h`
  169 lines; `wince.h` (409 lines) is mostly tracing.
  - Detection: `mmu.cpp:440-460` looks for "SH-4 Kernel" as UTF-16 text in
    RAM; on-demand switch at `fastmmu.cpp:193-208`. The full MMU turns on
    when a valid UTLB entry is not identity-mapped, not the store queue and
    not VPN 0x30000/0x30040 (`fullMmuNeeded`; dev `cc4a59b1f` restricts the
    WinCE hacks to WinCE games).
  - Dynarec: a 4 MB `mmuAddressLUT` (`mmu.cpp:463`), inline lookups
    (`rec_x64.cpp:813-847`), an SR.FD check (:136-146), blocks keyed by
    virtual address (`driver.cpp:102-150`, :388-412).
- Ours: `dc/mem/mmu.cpp` only remaps the store queue. No FPU-disabled
  exception. We build with -fno-exceptions.
- Interpreter first: ~1.3-1.6K lines, with longjmp (trap: the jmp_buf
  AltiVec size; libc uses 512 bytes). The detection string needs ReadMem16,
  because our RAM is word-ordered. Expected speed ~25-50%: enough to boot.
- JIT form: another ~0.8-1.2K lines. It must flush the pinned registers on
  the slow path, exit with longjmp, and turn off fastmem, inline SQ stores,
  constprop and DYN_IC while the MMU is on. The LUT loads mostly miss the
  512 KB L2 (~1.3-2x slower). Very high risk.
- Games: the WinCE list in plan 3d (4x4 Evo, Armada, Caesar's Palace, ...).
  All estimates unverified.

### P39 reios (HLE BIOS)  `[ ]`

- It gives a boot without `dc_boot.bin` and skips the ~5-8 s intro. No
  in-game speed gain.
- Limits: Flycast keeps forced real-BIOS and forced-HLE game lists
  (`core/emulator.cpp:267-287`), so we would need a manual toggle. MIL-CDs
  need the real BIOS. No BIOS menu.
- Work: a trap opcode in our interpreter and JIT
  (`core/reios/reios.cpp:714-734`), a 536 KB font, and many raw little-endian
  pointer writes into ROM and RAM (:411-427, :740-765). About 2K lines. Low
  value.

### P40 Flycast ARM7 tests as self-tests  `[ ]` merged: plan 6.8

- Flycast `tests/src/AicaArmTest.cpp` (13 groups, ~312 checks): post-index
  loads with Rd == Rn, LSR/ASR #32 and RRX offsets, unaligned LDR with
  writeback, writeback when the condition is false (`00a74d81c`), LR on a
  conditional BL (`89fe36b88`), PC-relative ALU ops, register-shift operand
  2, LDM/STM.
- Ours: 45 tests. Plan 6.8 notes that there are no STR/STRB/unaligned-STR
  cases.
- Value for F1: one pass on all three engines (cached, JIT, SPU) would rule
  out an engine bug in the driver's note-off logic. The tests store opcodes
  as host u32 words, which is our ARAM layout. Medium effort, test only, no
  risk. Not run.
- Fourth review (2026-10-06): AicaArmTest would not catch P49. It tests
  Rm = PC with a register shift (`AicaArmTest.cpp:522`, :765) and STM {pc}
  (:1016), but never Rn = PC with a register shift or STR of PC.

### P46 Flycast SH4 op tests as a PC check and a boot self-test  `[ ]` merged: plan 5.3 (JitMac check)

- Flycast `tests/src/sh4_ops.h` (1473 lines, 300 asserts) with
  `tests/src/Sh4InterpreterTest.cpp` (13 test groups; commits `e70a0dcf3`
  and `6989a0aa2`). MovRmRn alone runs 256 cases.
  - Integer: loads and stores, `@-Rn` and `@Rm+` with Rn == Rm,
    PC-relative, GBR, sub-word lanes; addc, addv, subc, subv, negc, div0s,
    div0u, div1 with Rn == Rm, dmul, mac.l, mac.w.
  - Single precision with inf and NaN: fadd to fdiv, fmac, fsqrt, fcmp,
    ftrc saturation, fsrra, fsca 0x8000.
  - Double precision: fabs, fneg on an odd register, fadd, fsub, fmul,
    fdiv, fsqrt, fcmp, float, ftrc, fcnvsd, fcnvds.
- Why: the JIT sends every FPU op with PR=1 to the interpreter
  (`dc/sh4/rec_v2/decoder.cpp:942-943`), and the interpreter also runs every
  fallback op. Predicted from reading: only "fneg fr9, PR=1" fails (= P32).
- PS3 form, two parts:
  - (a) `misc/interp_check` (WSL): include `sh4_cpu_*.h` and
    `sh4_fpu_ops.h` with a flat-RAM stub. About 400 lines. It runs the
    little-endian branches only.
  - (b) A boot self-test like `plugs/vbaARM/arm7_selftest.cpp`, behind a
    toggle. It runs the real big-endian `GetDR`/`SetDR`
    (`dc/sh4/sh4_registers.h:87`), the 64-bit word order
    (`dc/mem/_vmem.cpp:66-77`) and the XOR byte lanes.
  - The PrepareOp/RunOp split also allows a JIT runner later.
- Trap: the helper `mach()` returns `mac.l` (`sh4_ops.h:76-77`). Only
  `mac()` is asserted, so compare `(mach << 32) | macl`.
- Risk: none (test only).

### P50 On-screen FPS and SPEED  `[ ]`

- Ours: SPG already builds "FPS: x / SPEED: y%" every second and calls
  `rend_set_fps_text()` (`plugs/drkPvr/SPG.cpp:334-343`), and the cfg key
  `OSD.ShowFPS` exists (`plugs/drkPvr/drkPvr.cpp:124`). But `SetFpsText()`
  is empty (`ps3/ps3_rend.cpp:1055`).
- Flycast: `rend.ShowFPS` (`core/cfg/option.cpp:72`, `core/ui/gui.cpp:1415`).
  Minicast `68a084c` adds stats to its OSD.
- PS3 form (~30-50 lines, own toggle): copy the string under a lock (SPG
  runs on the SH4 thread, the draw on the render thread). At the end of
  `draw_frame()`, draw it in the left pillarbox bar with the `ps3/ps3_ui.cpp`
  glyph atlas (`ps3ui_text()`, :558), in an overlay form of
  `ps3ui_begin()` (:432) with no frame start and no clear. Then call
  `reset_param_cache()` (`ps3/ps3_rend.cpp:1248`), so no GL state leaks
  into the next frame.
- Value: Matt reads SPEED on the TV without the UDP listener. Cost: a few
  quads per frame, nothing when OFF. Risk: low-medium (GL state).

### P51 Screenshot to PNG  `[ ]` merged: plan section 0 (Soul Calibur faults, evidence)

- Minicast: `3b5c9b7` (a mappable button), `e7bb698`, `450eb49`, `22c3a7c`
  (PNG with stb; `libswirl/rend/mister_rend/mister_support.cpp:277`
  handles the 4 framebuffer depths, :405 writes the file).
- Ours: no screenshot. The parts exist: `ps3_video_readback()`
  (`ps3/ps3_video.c:276`) DMAs back-buffer rows to main memory as
  A8R8G8B8, up to 1024x1024 (:39-40); libpng is linked (`Makefile:72-73`),
  with the jmp_buf fix (`ps3/ps3_ui.cpp:230-275`).
- PS3 form (~60-80 lines): a SELECT-menu entry sets a flag. The render
  thread reads back the 4:3 viewport before the flip and writes a PNG under
  `/dev_hdd0/data/NotDC/`. The default surface is 720p
  (`ps3/ps3_video.c:122`), so the viewport is 960x720 and fits the readback
  limit.
- Byte order: a big-endian A8R8G8B8 word is the bytes A, R, G, B: write
  bytes 1-3 (Minicast's stb path is little-endian).
- Value: pixel evidence for visual faults (the Soul Calibur flicker). Cost:
  one RSX wait per shot. Risk: low.

### P52 Anisotropic filtering and a texture filter override  `[x]` "Anisotropic" and "Texture Filter" built 2026-10-10, bundle 3 (plan section 0), HW SUCCESS 2026-10-10, default 16x and GAME in source

- Flycast: `rend.AnisotropicFiltering` (`core/cfg/option.cpp:110`, default
  1 = off), applied to every texture that is not nearest
  (`core/rend/gles/gldraw.cpp:236-248`). `rend.TextureFiltering` (:111)
  forces nearest or linear (`gldraw.cpp:204-212`).
- RSX: 1x to 16x through `rsxTextureControl(..., maxaniso)` (PSL1GHT
  `ppu/librsx/commands_impl.h:1143-1148`, `ppu/include/rsx/gcm_sys.h:346-360`).
- Ours: ps3gl passes `GCM_TEXTURE_MAX_ANISO_1`
  (`ps3/ps3gl/ps3gl_textures.c:1053`) and ignores
  `GL_TEXTURE_MAX_ANISOTROPY_EXT` (:914-916). The filter override is one
  line at `ps3/ps3_tex.cpp:420`.
- About 10 lines, a VIDEO option. No CPU cost. Best together with P45;
  whether it helps without mip levels is unverified. Risk: low.

### P53 Widescreen: draw geometry outside 4:3  `[ ]`

- Flycast `rend.WideScreen` (`core/cfg/option.cpp:70`):
  - It widens the canvas by 4/3 and centres it
    (`core/rend/transform_matrix.cpp:104-121`).
  - It uses a full-width scissor unless the FB clip or the tile clip
    narrows x (:146-155; `isClipped()` :77-84).
  - It extends the background plane by 256 pixels on each side
    (`core/hw/pvr/ta_vtx.cpp:1628-1649`). Commit `31d5ddb92`.
  - SuperWidescreen matters only on displays wider than 16:9: not on PS3.
- Ours: a pillarbox viewport and scissor (`ps3/ps3_rend.cpp:1938-1960`).
  `draw_scene()` already takes a canvas rect (:1816), evaluates the
  background plane at the canvas corners (:1874-1885) and passes the
  canvas to the modifier volumes (:1912). So a wider canvas needs no
  background work.
- PS3 form (~20 lines, own option): when FB_X_CLIP spans the full width,
  use the full viewport and call
  `draw_scene(-106.7 * sx, 0, 853.3 * sx, 480 * sy)`. RTT and 2D
  framebuffer presents stay 4:3. This is not P36's stretch.
- Value: a feature, no speed cost. Games cull at 4:3, so the new areas can
  show missing geometry (Flycast warns of it). Risk: low.

### P69 Stretch to fill and the VMU beep  `[ ]`

- Stretch to fill: Flycast `5ff3f974d` (dev, 2026-10-04) adds a
  `rend.StretchToFill` option. It stretches the 4:3 picture to the whole
  display (`core/rend/transform_matrix.cpp`,
  `getOutputFramebufferAspectRatio()` and `getVideoShift()`). Ours draws a
  4:3 pillarbox. PS3 form: one VIDEO option that sets a full-width
  viewport in `draw_frame()` and in the framebuffer present. About 10
  lines. Not P53 (no geometry outside 4:3) and not P36 (no cheat). Bundle
  11.
- VMU beep: Flycast `8b986dfab` and `4f87a3556`. The maple SetCondition
  clock command (on, period, alarm 2) drives a beep tone in the output mix
  (`core/hw/aica/sgc_if.cpp:150-236`, mixed at :1598-1603); a device reset
  or kill stops it. Ours only prints `BEEP` (`dc/maple/maple_devs.cpp:627`).
  About 70 lines. Flycast's VmuSound option is OFF by default
  (`core/cfg/option.cpp:64`); on hardware the beep comes from the VMU
  itself.

### P70 Facts for G11 (modem) from the new Flycast commits  `[ ]` reference for GOALS.md G11

- Our Flycast clone is from 2026-09-23 (`master` 869038f40, `dev`
  0a304f139). Fetch it before G11: the modem changed after that.
- `fe04dd650` (dev, 2026-09-29): V.42 answerer mode, and modem callbacks
  for dial out, answer mode and handshake start (`core/hw/modem/modem.cpp`
  +148 lines, `v42.cpp` +172). It also adds Capcom Direct
  (`core/network/capcom.cpp`): after the lobby matches two players, one
  modem answers and the other dials it. 15 JP Capcom titles, among them
  Marvel vs. Capcom 2 (T1215M), Power Stone 2 (T1218M) and Capcom vs. SNK
  Pro (T1247M); Capcom vs. SNK 2 is not on the list. `7d69c9cfd` and
  `4dca1988d` (dev): the peer link uses libjuice (ICE with STUN and TURN)
  and Flyinghead's battle server; battle code addresses fixed.
- DCNet (`core/network/dcnet.cpp`, master): before the TCP link, an
  access-point finder sends DISCOVER over UDP to dcnet.flyca.st:7655
  (magic 0xDC15C001), pings each access point 5 times and picks the lowest
  ping (since `4d6168dd0`, 2025-04; IPv4 only since `0d9853df9`). With no
  access point it uses dcnet.flyca.st itself. TCP ports: 7654 PPP (modem),
  7655 Ethernet (BBA), and 7656 for Power Smash, Pro Yakyuu Team de
  Asobou Net! and Virtual-On Oratorio Tangram (HDR-0113, HDR-0091,
  HDR-0040). `2ea7920fd` refuses Ethernet frames above 1514 bytes.
- GOALS.md G11 step 3a ("one TCP socket ... no handshake") is right for
  the TCP stream, but it does not list the finder or port 7656.

### P83 G11 facts: a first stage with no V.42, the flash account, the traps  `[ ]` reference for GOALS.md G11

- A first stage needs no V.42. Flycast `df2bdfc9b` (the parent of
  `98dfb4c32`): `core/hw/modem/modem.cpp` 764 lines + `modem_regs.h` 699
  (275 are the DSP RAM table), and no v42 files. It is the design of
  Reicast and Minicast (`libswirl/hw/modem/modem.cpp:196-230`, 749
  lines). In V.8 auto mode with ASYN = 0 it reports CONF 0xA1 (V.23), so
  the game's driver sends raw PPP bytes. Flycast ran online this way from
  2018 until `98dfb4c32` (2025-12-23), DCNet included (`a5a338d40`,
  2025-01).
  - Today's set is 4,176 lines (modem 1,343, regs 701, v42 1,166 + 227,
    v42bis 428 + 197, internal.h 114). Stage 1 is about 1,460 lines + ~150
    of socket code; V.42 later adds ~2,700. GOALS.md G11 step 1 asked for
    `v42.cpp` and `v42bis.cpp` from the start; it has the two stages
    since 2026-10-11.
  - The cost: `98dfb4c32` removed five per-game code patches that the old
    modem needed (The Next Tetris US, Bomberman Online, Outtrigger US,
    Gundam Federation vs. Zeon, Propeller Arena; `core/cheats.cpp`) and
    added V.14 for Power Smash and Pro Yakyuu Team. Those games need the
    V.42 stage.
  - Not checked: that today's DCNet servers still accept a stream without
    V.42.
  - The network end for this stage: `core/network/dcnet.cpp` PPPSocket
    (:60-184) and connect (:740-784): one thread, one TCP socket. The
    finder (:421-617, P70) is optional.
- The dial-up account must be in the flash. Flycast writes it
  (`core/hw/flashrom/nvmem.cpp:52-149`, :196; `aaf950d47`, `5b9dd9606`;
  the BBA block `eb662da38`, "Fixes outtrigger"; layouts
  `flashrom.h:153-206`): blocks 0x80-0xBF (INET, "AT&F"), 0xC0-0xC5 (ISP1:
  "SEGA", user, password, phone), 0xC6-0xEA (ISP2), 0xE0 (BBA netmask).
  Ours writes only block 0x05 (`dc/flash_fix.cpp:93-133`). About 80 lines
  on the existing `lookup_block` / `alloc_block`. Big endian: the block
  id, the CRC and the netmask as bytes (`wr16`), never through the
  structs. Which games need it: unverified.
- Big-endian traps beyond G11 step 2 (the u8 bitfields):
  - `v42.cpp:227`: the frame check is read as `*(u16*)`. On PPC every
    V.42 frame would fail its CRC.
  - `modem.cpp:945`, :963 and `modem_regs.h:262`: `u16 MEDA` over the
    registers 0x18/0x19 and `*(u16*)&dspram[]`; the odd-address path is
    byte-wise little endian.
  - `modem_regs.h:45-72` (reg02 v0/v1 declare 6 of 8 bits) and :286-294
    (reg1c_1d spans two bytes): the P58 class of fault.
  - `dcnet.cpp:444`, :513, :613: the finder magic is copied in host order;
    the wire bytes are 01 C0 15 DC.
  - picoTCP (option 3b): a big-endian host by `-DPICO_BIGENDIAN`
    (`core/deps/picotcp/include/pico_config.h:56`), but the
    `pico_dns_header` bitfields have no big-endian branch
    (`modules/pico_dns_common.h:70-80`; used by
    `core/network/dns.cpp:68-79`).
- Exceptions: our build has `-fno-exceptions` (`Makefile:64`).
  `v42bis.cpp:319`, :363 and :373 throw, and `bc3b8c71d` (dev, 2026-10-09)
  adds a `ConnectionClosed` try/catch (`modem.cpp:551-559`, :853-858; only
  `capcom.cpp` throws it). Take its `hangUp()` (:348-356: ABCODE 0x96,
  NEWS 1, CONF 0xC0, dspram[0x2E4] 0, DISCONNECTED) with a return code.
  DCNet itself only closes the socket (`dcnet.cpp:96-101`): the game is
  never told.
- Timer: Flycast uses one scheduler callback (`modem.cpp:358`, :613).
  Ours: the Scheduler option is OFF by default (`ps3/ps3_presets.cpp:12`),
  and `libExtDevice_Update()` has no caller
  (`plugs/nullExtDev/nullExtDev.cpp:33`). Call it from MediumUpdate
  (`dc/sh4/sh4_interpreter.cpp:352-362`): the tick is 7,168 cycles
  against 28,600-47,619 cycles per modem byte, and the SYS GATE plan
  stops at each medium tick already (:427-446). The interrupt
  (holly_EXP_8BIT, raise and cancel) is wired
  (`plugins/plugin_manager.cpp:289-291`). About 15 lines of glue.
- Flycast forces SH4 clock 200 while online (`core/emulator.cpp:950-965`,
  `dcnet.cpp:681-686`).
- Option 3b (local PPP) has a BSD-socket base. Reicast and Minicast
  `libswirl/hw/modem/picoppp.cpp` (834 lines) and Flycast `45f437818^`
  (1,236 lines) use socket, non-blocking connect + SO_ERROR, select,
  accept, recv/recvfrom, send/sendto and one thread with a 1 ms tick.
  Today's Flycast file is 1,510 lines of asio, std::future and miniupnp.
  picoTCP is 23 files, 20,357 lines (+5,407 of headers). PSL1GHT: select
  and poll exist (`net/select.h:22`, `net/poll.h:27`); gethostbyname
  only, no getaddrinfo (`net/netdb.h:70-83`); no fcntl, so use SO_NBIO or
  MSG_DONTWAIT (`net/socket.h:67`, :76); no `netinet/tcp.h`, so no
  TCP_NODELAY define. Keep "no netDeinitialize" (`ps3/ps3_main.cpp:327`).
- Not needed for dial-up over DCNet: Capcom Direct, answer mode, ICE,
  V.8bis, UPnP, the DirectPlay port lists.
- Not checked: TCP_NODELAY on the PS3; picoTCP PPP on a big-endian host
  (never compiled).

### P84 G12 facts: the device name and the edits; a radial stick dead zone  `[ ]` reference for GOALS.md G12

- The device name. Flycast's full controller reports "Dreamcast PantherDC
  Controller" (`core/hw/maple/maple_devs.cpp:1680-1724`, the name at
  :1718; `09b27ddd5`, 2025-08-13: "Fixes KISS: Psycho Circus"). GOALS.md
  G12 cited only `4d5ced1ce` (it has the name since 2026-10-11). So at
  least one game checks the name.
- The edits in ours: `dc/maple/maple_devs.cpp:161` `w32(0xfe060f00)` ->
  0xffff3f00 (bits 12 and 13 are X2 and Y2); :172 the name (exactly 30
  characters, it fits `wstr`); :213 and :215 `w8(0x80)` -> X2 and Y2 (the
  reply: function word, w16 buttons, RT, LT, X1, Y1, X2, Y2);
  `dc/maple/maple_cfg.cpp:74`. The same capability word at :763 (maracas)
  and :885 (fishing rod) belongs to other devices: leave it.
- Big endian: nothing new. `w32` is native. The six axis bytes go through
  `w8` (XOR-3), never as one u32.
- Flycast's libretro front end maps L1 = C, R1 = Z and Select = D for
  this device (`shell/libretro/libretro.cpp:1975-1996`). If Matt wants
  them, `ps3/ps3_input.cpp:147-153` must stop tying L1/R1 and C/D to the
  triggers (G12 step 3 keeps the 0xF901 mask for that reason).
- Games: no reference source file lists the games that read the second
  stick. Only that commit message names one.
- The stick dead zone (G10 too: it acts in every game). Flycast: a radial
  dead zone (default 10%, `core/input/mapping.cpp:452`) with a rescale
  (`core/input/gamepad_device.cpp:463-477`); then the magnitude is
  limited to 128 (`core/hw/maple/maple_devs.h:195-204`,
  `maple_devs.cpp:100-103`, :1705-1708). Ours (`ps3/ps3_input.cpp:13`,
  :52-59, :156-157): each axis is cut alone below 20, with no rescale. So
  the output jumps from 0 to 20, slow diagonals snap to one axis, and the
  corners go above 128. G12 step 1 would copy this dead zone to the right
  stick. About 15 lines. The feel is not verified on HW.

### P85 Flash defaults, and the flash save at exit  `[ ]` reference for GOALS.md G13 and G10

- Language: a new syscfg block gets language 0 = Japanese
  (`dc_flash_set_time()`, `dc/flash_fix.cpp:113-118`; its comment says
  "as in Flycast", which is only Flycast's struct reset). Flycast then
  writes its Language option, default 1 = English
  (`core/cfg/option.cpp:35`, `core/hw/flashrom/nvmem.cpp:183-191`). 1
  line, or the BIOS language option of P37.
- No `dc_flash.bin`: Flycast builds one
  (`core/hw/flashrom/flashrom.cpp:297-350`, `nvmem.cpp:252-281`); ours
  exits (`nullDC.cpp:132-141`).
- Console ID at 0x1A058: Flycast writes a random one when it is all FF,
  fixes the checksum and sets the machine version 0xFE
  (`nvmem.cpp:198-224`; `0b6b6339e`, `af2fe242f`: "used by some network
  games"). A fact for G11 too.
- About 40 lines in all, byte writes only.
- The flash save at exit: Flycast saves the flash at every stop and before
  a reset (`core/emulator.cpp:762`, :857, :868) and flushes the VMUs at
  unload (:793). Ours saves once, after Run returns (`dc/dc.cpp:86-91`);
  the 3 s forced path skips it (`ps3/ps3_main.cpp:179-183`). The VMUs are
  safe (fflush per write). 2 lines: `mem_SaveFlash` before
  `sysProcessExit`. Low value. Also: an RTC write calls `SaveSettings()`
  for a value that the next boot never loads (`dc/aica/aica_if.cpp:101`,
  `nullDC.cpp:235`).
- GOALS.md G10 step 4 ("Every exit to the XMB takes the 3 s fallback")
  was out of date: P6 is done (plan 5.35). Updated 2026-10-11, with G11,
  G12 and G13.
- A default question for G13 step 5: Flycast ships 1 controller plugged
  (`core/cfg/option.cpp:216-221`), ours Players 4
  (`ps3/ps3_presets.cpp:30`). No fault known.

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
  Corrected 2026-10-11: the x30 rule also bills work blocks (P71).
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
  delay saves nothing. New fact 2026-10-11: ours is the shortest of the
  four; the numbers are in the sixth review block.
- Sort DMA: ours follows the fixed rule (link value 2 ends the chain).
- ShadInstr, PT alpha, wrap and filter, modifier volumes, vertex buffers,
  strip merge: equivalent, or already taken from Flycast. Per-triangle sort
  stays rejected (Matt).
- Minicast YUV texel fix (`47c0ec2`): refsw only; our pairing is already right
  (`yuv_pair()` takes horizontal neighbours in twiddled, planar and VQ).
- Reicast YUV "avoid copy": FMV only, ~384 bytes per macroblock. Since
  2026-10-06 it is part of P43.

GD-ROM, maple, sound:
- GD-ROM DMA pacing (Reicast ~10 KB per 1 M cycles): ours is faster; no game
  is known to need pacing. Corrected 2026-10-06: Flycast names World Series
  Baseball 2K2 and Sakura Taisen 3 (P22).
- Sense data after a disc change: no disc swap on PS3. Corrected 2026-10-06:
  the disc swap itself is the missing feature (P34). 2026-10-11: Disc Swap
  is built (bundle 2); its two gaps against Flycast are P77.
- GET_SCD, REQ_STAT, REQ_SES: ours is tuned for the BIOS CD player and the
  ChuChu pause fix.
- Maple DMA timing: same formula. Corrected 2026-10-06: Flycast uses other
  rates and holds the replies until the IRQ (P29). VMU: same protocol, fflush
  on every write.
  Lightgun: moot (the PS3 controller type is fixed).
- G2 DMA bulk copy: under 0.1% of wall (estimate).
- ARM7 / AICA: no new lead for the ChuChu echo (F1) or the Looney Tunes
  silence. Reicast's differences are already in section 6 (6.2 interleave,
  6.3 256 ticks per sample, 6.8 reset state).
- Audio pacing: Reicast (4096-frame ring, 1 ms sleeps) and Minicast `b40f51b`
  (wait for the FIFO) add nothing over our vblank pacer, 8192-frame ring and
  `ring_wait_space`.

Flycast (2026-10-06):
- Unreviewed dev and master commits with nothing to take:
  - `bcbc43fad` (end the timeslice on Stop): it zeroes the interpreter's
    cycle counter; our JIT keeps the count in a pinned register. Exiting at
    the end of the timeslice is enough (Flycast x64 also tests CpuRunning once
    per timeslice, `rec_x64.cpp:689-694`). Nothing new for P6.
  - `675ce1b63` (ARM7 MSR): all four of our engines apply the c/x/s/f fields
    (`arm-new.h:2518-2650`, `arm7_cached.cpp:543-590`,
    `arm7_jit.cpp:598-647`, `ps3/spu/arm7_spu.h:657-692`).
  - `e64b28a75` (x86 DSP JIT: stale code pointer, no overflow check): our SPU
    DSP JIT runs from the buffer it writes and reports overflow
    (`ps3/spu/spe_dsp_jit.c:33-38`, :682).
  - `569dc1cd1` (AICA RAM alignment): only misaligned SH4 accesses, which
    raise an address error on hardware.
  - `29a54e36c` / `230d83654` (secondary accumulator): it needs an offscreen
    colour target that the shader reads at the pixel position; ps3gl has no
    FBO. New fact: `230d83654` names Space Channel 5 Part 2, Evil Dead and
    BANG! Gunship Elite as users. Corrected 2026-10-11: a second colour
    buffer is possible, because the surface code is ours (sixth review
    block).
  - `08ffbe6cf` / `e2722869f`: a per-game list of games without RGB output.
    IP.BIN has only the VGA flag, which we already use (`dc/dc.cpp:338`).
  - `55a8d6094` (render queue bound): our render thread has one job slot.
  - `b03335bca` (swap interval detector): we flip once per DC render.
    2026-10-11: out of date since Frame Hold (plan 5.41); still nothing
    to take.
  - `9dbca9632` (RTT size from the tile clip): our RTT never fires.
    Corrected 2026-10-11: it fires (the Crazy Taxi pause, P75); the size
    rule is in P76.
  - `c00a62c90` (TOC track bounds): our CDI reader already clamps
    (`cdi.cpp:131-138`); GDI sets the tracks itself.
  - `d93612734`, `804fcb4a9`, `bbe606c8f`: refactors. Logs, includes,
    verify() and savestates: no functional change. Per game or out of scope:
    `12bb43652`, `1607eb571`, `f3047b2ef`, `ead8ab222`, `215a5af16`,
    `b4d7e7597`, `565f5174f`, `b35b829a1`, `602da7d44`.
- SH4 JIT:
  - SSA passes: ConstProp (ours covers r0-r15; memory baking is rejected),
    DeadCode (our DVE), CombineShifts (our shift chain), SimplifyExpression /
    DeadRegister / IdentityMove (1-cycle movs), SingleBranchTarget (one taken
    branch). WriteAfterWrite is off in Flycast itself.
  - Do not port Flycast's shld/shad constant fold (`ssa.cpp:548-583`,
    `fff03f64a`, `15617c181`): for positive counts that are multiples of 32
    it gives 0 or the sign, where the SH4 keeps the value. Ours
    (`dc/sh4/rec_v2/shil_opt.h:200-232`) is right.
  - MatchDiv32 matches the same code in both (only the emitted ops differ,
    P19). Flycast has no other division matcher; Castlevania's loop form
    falls back to the interpreter in both.
  - Block linking: Flycast x64 does not link. arm64 links as we do, but loads
    and stores the cycle counter at every block start. Block cap: 500 SHIL
    ops (ours: cycles). No return-address prediction.
  - SMC: mprotect page locks and fault-driven DMA invalidation; none of it
    ports. Flycast ignores CCR.ICI (`22b18d97a`); we must keep the full
    flush. In our logs every ICI clear is at boot or during a load (Crazy
    Taxi 15, Sonic 19). Flycast names Shikigami no Shiro II as a frequent
    ICI writer.
  - Runtime: timeslice 448. On arm64, intc_sched returns into the same block
    after the timeslice update (saves ~96K C calls/s; 5.1a measured C calls
    as cheap).
  - Same semantics in both: ftrc, fsrra, fused fmac, the fsca table data,
    trapa, sleep, RTE/SR, movca/ocbi/pref, PC-relative addressing, div1 with
    Rn == Rm. Flycast keeps sin and cos in one cache line; ours does two
    loads 64 KB apart (unmeasured). Double-precision fipr/ftrv
    (`ec3ad9b32`): accuracy only, and it breaks the 5.20 bit-exact check.
  - `fh/rec-doublefp`: 64-bit FPR allocation (ours are pinned) and a TCNT0
    fast path. `wip/sh-block-graphs`: a 2015 profiling prototype.
- PVR and renderer:
  - Two-volume shading, SrcSelect/DstSelect and translucent modifier volumes
    exist only in Flycast's OIT renderers. 16-bit dithering exists only in
    its full framebuffer emulation.
  - RTT detection is the same as ours (FB_W_SOF1 bit 24 at STARTRENDER,
    `Renderer_if.cpp:546`). None of our three test games is on Flycast's RTT
    list. Test RTT with Jet Set Radio, Shenmue or Grandia II.
  - Mipmap and VQ offsets, the 32-bit VRAM bank mapping, PVR-DMA, sort-DMA:
    same as ours. Background plane (FillBGP), render-done delay, texture
    bleeding: no gain.
  - Soul Calibur black texture (~1 s): no Flycast fix matches (`affc9f262`
    is an Android context loss; `6b05c0ab0` is already done). The suspects
    stay the palette race of P1 and plan 6.7.
- Sound (no difference delays a key-off; nothing for F1):
  - 0x2810: Flycast, nullAICA and the twin take EG/SGC from the AEG and set
    LP at LEA. Word reads and byte reads of 0x2811 clear LP. Held ChuChu
    notes read 0x4000/0xC000 in all three. The EG range differs (0x3FF
    against 0x1FFF, already in plan 6.8).
  - KYONEX reads 0 in Flycast and nullAICA. KYONB is cleared on release in
    both (never in caique). Timers read the live count in Flycast and
    nullAICA (0 in caique). The L/M interrupt latch is the same code.
  - nullAICA restarts the timer prescaler only when md changes
    (`plugs/nullAICA/aica.cpp:168-176`), as Flycast does. Plan 6.0
    ("nullAICA restarts it") overstates this.
  - ARM memory map, 2 MB mirrors, 32-bit register access, the unaligned LDR
    rotate, VBA semantics, the FIQ check at block dispatch: same.
  - `core/audio/audiostream.cpp` only pushes 512-frame buffers: nothing for
    F2.
  - SB_ADDIR=1 (P9): the Looney Tunes logs have 0 message-box lines, so it is
    not F3.
  - EG_EffRate: caique (320 combinations measured on hardware) treats rate 0
    as no decay and adds no FNS bit when KRS+OCT is below 0. Flycast
    `265c5c6c3`, which nullAICA copied, does both. nullAICA only; it does not
    touch ChuChu's notes.
  - New facts for plan 6.2 and 6.6: Flycast `2e5309bfa` (2024-11) dropped
    32-sample SH4/ARM/AICA batching because it caused audio glitches in WinCE
    games (issues #1240, #1605). `59ff44132` had removed a no-batch list for
    Tony Hawk's Pro Skater 1/2. So a lag of tens of samples is audible; our
    SPU lag is 64-320 samples.
  - For F1: Flycast used 256 ARM cycles per sample until `48e3a3eb6` (2022)
    and 32-sample batches until 2024. Its git log has no ChuChu entry.
- System:
  - `loadSpecialSettings`: every rule is a per-game list (the region/VGA
    rule is already taken).
  - Flash command state machine (`core/hw/flashrom/flashrom.cpp:81-230`):
    it matters only for BIOS settings saves (stray bytes in an unused area;
    no erase after ~254 saves).
  - SB_SFRES 0x7611: Flycast fully resets the SH4; ours resets only the
    peripherals, on purpose. A game's "return to BIOS" may differ
    (unverified; test on HW first).
  - DMAC auto-request (`b8d47fe0a`, a homebrew demo), TMU with TCOR=0
    (DreamShell), SCIF fixes: homebrew, Naomi or battle cable only.
  - Holly INTC, BSC/PDTRA, PVR-DMA, SB_FFST: same as ours. CPU_VERSION,
    SB_MDAPRO, SB_MMSEL: Naomi only.

FMV (2026-10-06):
- Reicast `1f95eb24` / `e1a86b4e` ("video choppiness": store
  TA_YUV_TEX_BASE before `YUV_init`): ours writes the register first
  (`dc/pvr/pvr_if.cpp:274-285`).
- Minicast `e31b27e` (`sq_pref` accepts `add #size,Rn`, "fixes FMVs"): a
  fix to Minicast's own SQ store-pattern pass. We have no such pass. The
  5.17 SQ stores only guess the shape and still test the address at run
  time (`ps3/ps3_driver.cpp:2804-2818`).
- Minicast `58e4015`: compile time kept out of the freerun clock, ICI rules
  per SMC level, thread priorities. Freerun is P15. Our ICI clears happen
  only at boot and during loads.
- Flycast's YUV converter has no fast path (its own comment: "slow and
  ugly", `core/hw/pvr/pvr_mem.cpp:3`).
- Flycast `443d5a2ba` (background plane depth bias; YUV FMVs in WinCE
  games, Xtreme Sports, Blue Stinger JP): already in ours
  (`ps3/ps3_rend.cpp:1823-1824`).
- Flycast `565f5174f`, `e279abd0b`: custom textures and texture dumps.

Fourth review (2026-10-06):
- Minicast JIT branches (`r6b/*`, `static-fpr-alloc`), against our backend
  with every SH4 register pinned:
  - Anchored loops and multi-block regions (`84b031e`, `a5c248d`,
    `e746843`, `b0ffd66`, `79da894`): up to 32 linked blocks as one region,
    with direct internal branches, no poll inside, an overlay register
    allocation, and XMTRX loaded once per entry (`4e4ed42`). Minicast ships
    it OFF (`libswirl/libswirl.cpp:250` on `r6b/tacore-opts`). For us, only
    one taken `b` and the T store per internal edge are left: 0.3% at most
    (1.33M loop-block entries/s in the 5.18 dumps, scaled by the 5.22 COLD
    EXIT result). About 500+ lines, high risk (no poll inside a region).
  - Self-loops (`edb24c4`, `9b6b988`, default 0): ~0.27M self-loop
    entries/s in the 5.18 dumps, about 0.05%.
  - Inline TA ring push (`4e4ed42`, "crashy crashy with JGR"): our PREF
    already calls the flat, frameless `do_sqw_nommu`. Inlining saves about
    4 cycles per flush (~0.05%).
  - Register-allocator work (`71e3b33`, `ae7bed9`, `b1cd272`, `218870e`,
    `d7b588d`, `2d73887`, `f8235c6`): ARM32 spans and spills. Nothing for
    pinned registers (plan section 4).
  - `b4b35e9` (the backwards-only cycle check, only for static local
    branches): our LOOPS ONLY rule (`ps3/ps3_driver.cpp:332-343`) already
    polls every end that is not static, so every cycle holds a polling
    block.
  - `e842512` (stricter SQ store matching): it guards Minicast's unchecked
    stores. Ours still test the address at run time, and the 5.18 dumps
    have no PREF base that the block also loads through.
  - `b1096c1`: it flips Minicast's own MatchDiv32s option check. Not P19.
  - `08ade39`: a generator for a threaded block interpreter, never built or
    called.
  - `2b3f8e3` (perfheat): needs Linux `perf`.
  - No commit, comment or test records a speed number.
  - Minicast's defaults on `r6b/tacore-opts` (`libswirl/libswirl.cpp:243-258`):
    backwards-only cycle check OFF, SQ stores at PREF OFF, anchored loops
    OFF, read pairs 0, self-loops 0, constprop 1, fipr_w 1. Our HW-validated
    equivalents (5.17 Step 3, 5.22 LOOPS ONLY) are ON and keep run-time
    checks.
- New facts for the plan:
  - 5.27: Minicast's multi-block SQ guess (`a49d09f`) marks stores with no
    address check. Its author switched it off two days later (`00cac1a`:
    "it crashes things"). Keep our run-time check.
  - 5.17 ("FSM on the SPE"): Minicast `602d893` (`freerunning-ta`) built
    this design. The SH4 pushes blocks to a ring, the consumer runs the
    FSM and hands the list-end interrupts back. Its author marked it "WIP,
    it is slower", and it was never merged.
  - 5.23: Minicast `5be26fa` now also fuses `fmov.d` pairs;
    `opt_readm_pairs` is still 0 on every branch. With 5.31 in place,
    fusion saves here about 3 instructions per pair: 0.56M pairs/s in the
    5.18 dumps, about 0.1-0.2%. 5.23 stays unscheduled.
- Minicast outside the JIT:
  - `b7c2e05` ("Fix spg timing. Wow 20+ years old bug."): Minicast's SPG is
    a scheduler callback that dropped its own lateness. Ours is a
    per-timeslice counter that keeps its remainder
    (`plugs/drkPvr/SPG.cpp:213-219`), and the CPU side carries its
    overshoot. No such bug.
  - `86e6a06` (sprites with offset colour): the bug was in Minicast's FPGA
    vertex word count. Our parsers read sprite headers as the hardware does
    (`ps3/ps3_rend.cpp:503-507`, :728-735; `ps3/spu/spe_dec.c:609-615`).
  - `e22672d` removed a framebuffer watch that nothing read on MiSTer. P26
    stands.
  - `df958c9` (files above 2 GB): it fixes a 32-bit `long` on ARM. Our
    build is LP64, and no Dreamcast image is above 2 GB.
  - `58e4015`, ICI rule: Minicast clears its code cache on CCR.ICI only
    when SMC checks are off; it relies on per-block code checks and
    mprotect. Keep our full flush.
  - `58e4015`, PrintTaErrors: messages for ILLEGAL_PARAM, PRIM_NOMEM and
    MATR_NOMEM. We raise none of these interrupts (only the enum exists,
    `plugins/plugin_header.h:143-144`).
  - mtta/polly2 (`e856da0`, `91ee857`, `694fadc`, `35c7833`, `e09510d`),
    `6739c67`, `568f740`, `6fcc0e1`, `2aef457`: MiSTer, Linux or mmap only,
    or already in this section.
- Reicast:
  - `938eb8a8` (circular code invalidation, branch
    `skmp/circular-code-invalidation`): it discards the next 256 KB window
    and relinks the callers through per-block reference lists. We keep no
    incoming-link lists (static links, DYN_IC sites, hot-block slots).
    Since 5.28, Sonic Adventure fills the cache about once per 13-27
    minutes of play (estimate). Not worth the work.
  - `skmp/aica-asdr-experiments` (6 commits): continuous rate formulas with
    no cap. caique's hardware-measured rules
    (`plugs/caiqueAICA/aica_model.cpp:179-191`) win. Nothing for F1.
  - `10b0de80` (constprop "harmful parts"): the harmful part bakes 32-bit
    loads, which needs page locks. Ours folds only the address
    (`dc/sh4/rec_v2/shil_opt.h:357-387`).
  - `2e483079`, `tmu_old`: Reicast's scheduler-based TMU. Our per-slice TMU
    carries the overshoot (`dc/sh4/tmu.cpp:78-98`).
  - `fe634f6e` ("audiostream: only wait if full speed! easy win"): our
    audio waits only when the 8192-frame ring is full
    (`ps3/ps3_audio.cpp:223-235`), so never below full speed.
  - `skmp/dclp` and `skmp/softrend2` IRQ hacks at fixed test addresses,
    G2 Dev DMA (`2b28089b`), `efdc4971`, `4faff87e` (per-strip sort: ours
    does it), the idea branches (`42214480`, `skmp/rec_v3`, `19402a31`,
    `0a140e45`), the `arm7_jit_virt` ARM JIT, `rec_cpp.cpp`: nothing for
    us. `blockmanager-extras.cpp` is all `#if 0`.
  - The reference rasterizer (`libswirl/rend/soft/refsw*.cpp`): no rule
    that our fixed-function draw gets wrong without a shader. Offset
    colour, colour clamp and fog stay plan 3a/3b and P14. CullMode 1
    small-area cull is ignored by ours; it only affects sub-pixel
    triangles.
- Flycast:
  - SkipFrame and AutoSkipFrame: level 1 is P2's 85% rule, level 2 is our
    DROP. Skipping renders frees little SH4 time (5.16: render busy 56 ->
    32% gave +0.5%; 5.9 freed ~11% of the render thread with no VPS
    change).
  - Internal resolution (we draw 960x720), VSync and LimitFPS, Fast GD-ROM
    load (P22), PerStripSorting, ThreadedRendering, xBRZ, per-game
    settings, emulated framebuffer: nothing to take.
  - PR=1 interpreter ops: same results as Flycast `sh4_fpu.cpp`, except
    fneg (P32). The DRn word order on our host is consistent.
  - `storeq.cpp`, `sb_mem.cpp`, `ccn.cpp`, `rtc`, `cpg`, `ubc`, `intc`,
    `sh4_interrupts`, `maple_helper`: the same as ours, or less complete.
    The GD-ROM replies 0x71 and 0xA1 are byte-identical.
  - `87c184001` (`fh/rec-doublefp`, area 7 handlers): not in master, and
    area 7 is not hot here (Crazy Taxi ~23K writes/s).

Fifth review (2026-10-08):
- Minicast: no commits since the fourth review. `git ls-remote` on
  2026-10-08: all 9 branch heads are the ones in our clone (`main`
  934b374; `r6b/*` up to 2026-08-25).
- New Flycast commits with nothing to take: `df7aedf50` (CUE),
  `2a57c9ffd` (savestates), `9d4c33734` (GGPO), `89b72a8e3` (Windows
  HTTP), `b4c38d652` (mouse), `e36e9df2d` (Vulkan OIT), `df5feec55`,
  `04669ebe4`, `98ce9fb5d`, `552a86086` (build and config), and the
  translation commits.
- Rayman 2: plan 3j (bump maps) covers its untextured look. Flycast's
  Rayman commits do not apply: `09857008b` guards Flycast's log depth
  against NaN (we use Depth Float), and `688ba5c37` and `0f9218958` are
  autosort detection hacks (our Rayman logs read `sort=auto` in all 109
  windows).
- Border Down: P58, and P59 for its 50 Hz. Flycast uses the same
  store-queue rule as ours: AT = 1 takes `sq_remap`, AT = 0 takes QACR
  (`core/hw/sh4/storeq.cpp`, `setSqwHandler()`).
- Capcom vs. SNK 2 (its own goal, Matt 2026-10-08): no commit explains the
  slow menus. Leads for that goal:
  - It plays at PAL 50 Hz (P59).
  - The loop (`cvs2.txt` line 1440, 8C0687F4-8C06880E): three calls, then
    `cmp/hi r14,r4` + `bt` as a timeout exit, then `mov.l @r10,r2`,
    `tst r2,r2`, `bt` back. It is a timed wait for a 32-bit word to become
    non-zero. Log r10 first.
  - The `[CC]` a7 bucket is `(addr >> 26) & 7 == 7`
    (`dc/sh4/rec_v2/ccall_census.h:73`): P4 registers, on-chip RAM
    (0x7C-0x7F) and the area 7 mirrors. If r10 points into on-chip RAM,
    P30's bank aliasing fits (ours picks the bank with address bit 12,
    Flycast with bit 13).
  - P28's HBlank modes: ours raises holly_HBLank once per frame
    (`plugs/drkPvr/SPG.cpp:266-272`), whatever SPG_HBLANK_INT says.
  - Ruled out from `cvs2.txt`: GD-ROM and CD-DA completions (no SH4 reads
    during the spin, no audio tracks), maple (one DMA per frame), P65's
    ch2 abort and P67's SCI poll (no such lines).
- Looney Tunes race: nothing. `8564f497d` (opcode 0 in a delay slot), the
  only commit that names it, is already ours
  (`dc/sh4/sh4_interpreter.cpp:262`).
- Sound: nothing for F1; `c7d214e55` is part of the AICA Race Fix. F2,
  weak: `7287b3816` interpolates the last sample before LEA against LSA,
  as nullAICA does (`plugs/nullAICA/sgc_if.cpp:913-917`). caique and the
  SPU read the word past LEA, which caique measured on hardware (caique-rtl
  `model/NOTES.md:550-551`); caique wins. A stale word past LEA in a
  stream ring would click once per loop at a fractional pitch (SPE mode
  only). Check the FNS and OCT of the Crazy Taxi stream slots first: at
  pitch 1.0 the fraction stays 0.
- AICA byte order checked: the nullAICA lanes (^3, ^2), `ps3/spu/aica_ctl.h`
  and the AICA_PS3 bitfields are full width.
- Bitfield audit (2026-10-08): the tree has 62 unions with a big-endian
  bitfield branch. MMUCR (P58) is the only one whose fields do not fill
  their word. PCW's second big-endian struct (`plugs/drkPvr/ta_structs.h`)
  is intended.
- Per-game lists: Flycast's ExtraDepthScale list (NHL 2K2, Re-Volt, Test
  Drive V-Rally, South Park Rally, Capcom vs. SNK Pro) and its Broadcast
  overrides (`core/emulator.cpp`) are per-game lists (Matt's rule: none).
  P59 uses the disc area instead.
- New facts for older items:
  - P10 scaler: `c330cdd88`, `3cd9736fc` and `112e27207` name the games. A
    missing bottom half: Mr Driller (PAL 60 Hz), Kaen Seibo, Mahjong Taikai
    II Special, Pop'n Music 1 and 2 (composite or RGB). A stretched screen:
    Cho - Hatsumei Boy Kanipan. A missing half screen: Wacky Races, Buzz
    Lightyear of Star Command, Rainbow Six Rogue Spear and Sega Worldwide
    Soccer 2000 (all PAL); the factor must be a float. Flycast today
    (`core/rend/transform_matrix.cpp:41-60`, :191-203): in 240p (vclk_div
    0, not interlaced) with a vscalefactor that is not 1024 or 1025, the
    canvas height is 240 x vscalefactor / 1024; interlaced, only the FB
    clip scales. Ours sets `fb_scale_y` to 0.5 or 1 only
    (`plugs/drkPvr/SPG.cpp:94-100`).
  - P10 "empty frame": since `93523df80` Flycast always renders pass 0.
    Ours always draws, so that bullet is moot. P10 TA_LIST_INIT:
    `ac43a8c54` also sets TA_NEXT_OPB = TA_NEXT_OPB_INIT (1 line, next to
    TA_ISP_CURRENT).
  - P54: `2ff6d1b81` (Skies of Arcadia start menu) used alpha > threshold,
    but today's Flycast uses >= (`core/rend/TexCache.cpp:1028`). Keep
    P54's >=.
  - P25 flat shading: our strip-merge key already includes Gouraud
    (`strip_equivalent()`, mask 0x300CE), which `5ac99d3ad` needs.
  - P22: `6e017fd3e` (Maken X hang) made small DMAs faster. Ours is
    already fast, so Maken X is not a P22 game.
  - CD-DA miss stalls (plan 5.34, parked): nothing. Flycast reads CD-DA
    one sector at a time; its larger DMA buffers (`f76df2da6`,
    `4aee2eb5f`) were cut back to 16 sectors.

Sixth review (2026-10-11):
- Upstream on 2026-10-11 (`git ls-remote`): Minicast, all 9 branch heads
  unchanged. Reicast `alpha` 2e32999c unchanged. caique-rtl 5aa2376
  unchanged. Flycast `master` ed953fcbf: one translation commit. Flycast
  `dev` ec56b5f26: `bc3b8c71d` is in P83; `ec56b5f26` ("sh4: remove
  penalty cycles for MMU memory access", issue #1058) changes only
  Flycast's cycle model, and only with the MMU on.
- SH4 cycle billing, in billed cycles per 100 ops (the dumped Looney Tunes
  race blocks, MMU off; ours `dc/sh4/rec_v2/decoder.cpp:1252-1255`,
  Flycast `core/hw/sh4/sh4_cycles.cpp:22-87` and
  `dyna/decoder.cpp:949-952`). Ours / Reicast / Flycast:
  - FPU-heavy blocks 25 / 25 / 99; integer 100 / 100 / 127; memory-heavy
    integer 100 / 100 / 133; blocks under the idle rule 3000 / 300 / 135.
  - Crazy Taxi: 33 / 33 / 95, 100 / 100 / 99, 100 / 100 / 150.
  - Without the idle rule our emulated SH4 is 1.3 times (integer) to 4
    times (FPU) faster than Flycast's. So Flycast's cycle model (turned
    down, plan section 5) would slow the emulated SH4. Only the x30 rule
    (P71) bills more than the references do.
- Render-done delay, new numbers (the old line judged speed, not pacing):
  - Ours: `max(50000, 15 x vertices of the previous job)`
    (`ps3/ps3_rend.cpp:4056-4070`): 0.75 / 1.5 / 2.25 ms at 10K / 20K /
    30K vertices. With Render Delay ON: 4.0-4.5 ms.
  - Flycast: `min(450000 + 100 x TA bytes, 1.5M)` = 7.5 ms for any list
    above 10.5 KB (`core/hw/pvr/spg.cpp:293-310`, master and dev).
  - Reicast: 1.5M + 60 per vertex = 10.5 / 13.5 / 16.5 ms
    (`libswirl/hw/pvr/spg.cpp:51-67`); a vblank is 16.68 ms. Minicast:
    200M / FPSTarget 66 = 15.2 ms (the call site was not read).
  - No vblank is lost to it in ours. If P71's rule goes OFF, this delay is
    what keeps a variable-rate game from a flip at every vblank; Flycast's
    rule is 3 lines.
- Frame pacing, the same in all: TA list-end interrupts at once (ours
  `ps3/ps3_rend.cpp:969` -> `plugs/drkPvr/SPG.cpp:152-155`; Flycast
  `ta.cpp:231`); the SPG formula (NTSC 480i 6355 x 525 = 3,336,375 cycles,
  59.945 Hz; PAL 6400 x 625 = 4.0M); `sleep`; the ch2, PVR and sort DMA
  ends; the TMU rate, SCIF status, DMAC. Interrupt latency: a raise kicks
  the gate (`dc/sh4/intc.cpp:136-140`) and is taken at the next slice end,
  1792 cycles (9 us) at most against 448 in Flycast. JIT IDLE SKIP: 0
  skips/s in the race; it needs a pure block, a backward branch, no store
  in the turn and an unchanged register hash.
- New fact for P11 and plan 6.4: at Accuracy FAST, TCNT and SPG_STATUS
  step once per slice of 1792 cycles (8.96 us) (`dc/sh4/tmu.cpp:161-173`,
  `plugs/drkPvr/SPG.cpp:580-603`); P11 was written for 448. Flycast reads
  TCNT per block.
- Capcom vs. SNK 2 (its own goal), new facts from `cvs2.txt`:
  - The menu frame time is 180.00 ms (min 179.78, max 180.40, 28 frames):
    exactly 9 PAL vblanks.
  - 8C068808: 511,282 turns/s against 511,410 area-7 reads/s, so one read
    per turn (@r10 itself, or one register in a callee); 0 idle skips.
  - A second loop at 8C21A2DA-8C21A2F6 (77-94K turns/s, 511K in other
    windows): `jsr @r13` with r4 = 7, a RAM counter down, a RAM word
    against r14.
  - No "not implemented" or "out of range" area-7 line: the register is a
    mapped one. P30's bank bit is confirmed (ours bit 12, Flycast bit 13).
  - A test with no code: one boot at Accuracy ACCURATE (slice 448), the
    menu frame time against 180 ms. Then log once: r10, r13, r9, r11, r14
    and a count of area-7 reads per address. The cause is still not known.
- SH4 and JIT semantics, read in both trees, the same results: addc, subc,
  negc, addv, subv, div1 (the same Q/T algebra), cmp/str, swap.b, shld
  and shad (native forms), rotcl, rotcr, ftrc (`fctiwz` gives Flycast's
  clamps), float, fsca, fmac (fused), pref, the fschg and frchg decode,
  FPSCR PR and SZ at compile time (the same design), RTE.
  - Flycast `ec3ad9b32` (the dead register pass must assume that a
    fallback changes every register): ours does
    (`dc/sh4/rec_v2/shil_opt.h:413-417`, :547-551).
  - `fixNaN` (0x7fbfffff) is STRICT_MODE only (`core/hw/sh4/sh4_core.h`).
  - Not read line by line: `ps3_jit_div32.h`, `ps3_jit_fv.h`,
    `ps3_jit_tcopy.h`, `ps3_jit_dynic.h`, `ps3_jit_idle.h`, `idle_loop.h`,
    `div32_match.h` (their PC checks exist).
- New facts for P58 (the SQ path was traced with the fix assumed):
  - The JIT cache clear is mandatory, and on both AT edges. The PREF
    target, JIT PREFETCH and JIT GUARD REUSE read AT at compile time
    (`ps3/ps3_driver.cpp:4342-4348`, :2837), and `CCN_MMUCR_write()` has
    no ResetCache (the CCR write has one, `dc/sh4/ccn.cpp:67`). Flycast
    never fixes AT in its code: it calls `doSqWrite` through the context
    (`core/rec-x64/rec_x64.cpp:376-387`), swapped in `setSqwHandler()`.
    Reicast does not clear its cache on AT.
  - Right already: the interpreter and the cached interpreter test AT at
    run time (`dc/sh4/sh4_cpu_misc.h:43-48`); `do_sqw_mmu` uses `sq_remap`
    with Flycast's index and PPN (`dc/mem/mmu.cpp:57-58`); `TAWriteSQ`
    takes the translated address (`dc/pvr/pvr_if.cpp:566-581`); JIT SQ
    STORES, SQ MISS and the look-ahead only store into the queue
    (`ps3/ps3_jit_sq.h:6-35`).
  - `borderdown.txt`: each entry is synced twice, F6 first (PPN 0), then
    F7. So the V = 0 skip is safe with that order.
  - TI: Flycast (`core/hw/sh4/modules/mmu.cpp:505-511`) and Reicast clear
    only V; ours also clears `sq_remap` (`dc/mem/mmu.cpp:34`). The same
    for valid software. Flycast's QACR path uses QACR0 for both queues
    (`storeq.cpp:83-85`); ours picks per queue.
  - Not known: the MMUCR values and their order in the four games (AT and
    TI were read from the wrong bits). Add a log line per MMUCR write.
  - The speed and small-fix parts are P74 and P78.
- New fact for P60: a held frame and an RTT share the back buffer.
  `rtt_render()` draws into the back buffer with the scissor off, so an
  RTT that comes before the FB_R_SOF write would overwrite P60's held
  frame. P60 needs a flip before an RTT, or an own RTT buffer. Flycast:
  `becb0a5ef` presented the pending frame before an RTT (the Tony Hawk
  1/2 black screen); today an RTT address never becomes the latched one
  (`core/hw/pvr/Renderer_if.cpp:654-660`).
- Secondary accumulator (corrects the 2026-10-06 line). Our RTT gives no
  offscreen target: it draws into the back buffer and reads back. But the
  surface code is ours, not ps3gl's (`ps3/ps3_video.c:164-189`), so a
  second colour buffer is possible. Flycast dev
  (`core/rend/gles/gldraw.cpp:153-175`, `gles.cpp:470-474`): DstSelect
  polygons draw into a second colour buffer; SrcSelect polygons read it
  at the pixel position. Needed here: a 4th RSX colour buffer that is
  also a linear texture, one Cg program with WPOS (unverified on the
  RSX), and SrcSelect / DstSelect (`plugs/drkPvr/ta_structs.h:134-135`)
  in the merge key. About 120-150 lines. Low value (three games named).
- RTT, nothing to take: the Y flip (our matrix, viewport and read-back
  put DC line y0 in row 0, as Flycast GL; not run on HW); linestride x 8
  and the K bit (FB_W_CTRL bit 15); the cache after the copy (Reicast
  marks the overlapping entries dirty, our per-frame hash covers it); RTT
  upscale, `6a5db32d5` and `7a27a43ba` (the no-copy path or other back
  ends); RTT without the VRAM copy (it needs a texture as an RSX surface
  and Flycast's key rules); RTT in a list continuation (`rtt_render()`
  uses the passes of `draw_scene()`); `78b44919c` (fb_enable and
  blank_video do not act on TA renders: the same in ours); `pixel_double`
  (Flycast never reads it). Minicast has no GL renderer; its refsw takes
  pack modes 1 and 6 only.
- Renderer, nothing to take: our sort key, stable order, autosort-only
  rule and run rule equal `sortTriangles` and `GenSorted` (ours also
  merges across cull directions). Reicast's reorder of non-intersecting
  triangles (`glesdraw.cpp:701-732`) is `#if 0` ("uber slow and not very
  effective"). The background plane, the volume stencil modes,
  FPU_SHAD_SCALE, cull, depth compare, PT_ALPHA_REF, ShadInstr 0-3,
  UseAlpha, IgnoreTexA, clamp and flip, the vertex colour formats and
  16-bit UV are the same. Texture invalidation by page locks does not
  port. The half-pixel shift is Flycast DX9's own. Flycast `origin/dev`:
  its draw path differs only by the secondary accumulator.
- New facts for P14 (fog, plan 3a, and offset colour, plan 3b; both still
  open, no code beyond 3j):
  - 3j left the plumbing: an offset colour per vertex for bump polygons
    (`ps3/ps3_rend.cpp:502-503`), vertex word 6 as 4 bytes (:1896), a
    vertex program with a 4-component TEXCOORD1
    (`ps3/ps3gl/ps3gl_shaders.c:56-63`). Word 7 is free.
  - The reference form: `core/rend/dx9/d3d_shaders.cpp:101-115` (a 128x2
    A8 fog table; log2, floor, pow), :207-208 (mode 3), :248-250 (the
    offset add), :256-257 (mode 0, FOG_COL_RAM), :259-261 (mode 1,
    FOG_COL_VERT with the offset alpha).
  - A smaller form that is in no reference (an idea, not a take): fog per
    vertex, out = (texenv + offset.rgb) x k + f, with k = 1 - fog in the
    alpha of word 6 and f = fog colour x fog in word 7; the table is read
    at decode. One program per texenv, no constant, no table texture.
    Per-pixel table fog is lost. It still needs the SPU vertex change.
- Sound, nothing to take:
  - The PPU model and the SPU model are one source: `run()` (:1026-1051)
    and `final_mix()` (:1094-1116) hold the same steps, and
    `ps3/spu/spe_aica_model.cpp` `to()` (:219-255) sets EXTS, runs the
    DSP, then calls `final_mix`.
  - CD-DA: the bytes are swapped to host order
    (`ps3/ps3_arm_spe.cpp:145-151`, nullAICA `sgc_if.cpp:1418-1426`);
    word 2i = left = EXTS0, as Flycast. One sector per 588 samples in
    all, no resampler. A late read plays zeros and holds the FAD, up to
    150 sectors (`dc/gdrom/gdromv3.cpp:243-248`); `cdda-under` is 0 in
    the logs.
  - Flycast `origin/dev`, `core/hw/aica`: savestate lines only. Flycast
    commits that no doc cites: `93e47dd41` and `dac5b991e` (replaced by
    `265c5c6c3`, plan 2.2); `8d54382b1` (caique masks every fetch);
    `83bfa9158` (caique reads the FEG monitor).
  - Timers, SCIPD/MCIPD, SCIRE, the L/M latch, the ARM reset, VREG, the
    Holly SPU line: the same code as Flycast, or in plan 6.1 and 6.8.
  - Key-on and key-off, loops, the ADPCM state at the loop, the LFO
    shapes, the FEG and filter, noise, the MIXS wrap: caique measured
    them, and Flycast differs. caique wins. The SPU path runs the FEG;
    nullAICA's is a stub (plan 2.2).
  - Reicast and Minicast: `sgc_if.cpp`, `aica_mmio.cpp`,
    `dsp_interp.cpp` and `aica.cpp` are byte-identical.
  - F1 (ChuChu): no new fact.
- Goals, nothing to take:
  - The controller reply layout and the VMU create, format and media
    info: the same as Flycast.
  - Remap files: Reicast `input/mapping.cpp` is 212 lines (INI,
    `[dreamcast] btn_a = host code`), Flycast's is 1,090. Ours needs a
    table of `[ps3]` keys, not a port.
  - Rumble: Flycast's decode equals P13. New fact: VIBSET is a word read
    (`r32`), so only AST needs XOR-3 access.
  - Per-game VMU (new fact for P37): in Flycast only slot A1 is per game
    (`core/oslib/oslib.cpp`, `getVmuPath`).
  - The RTC takes the host clock at each boot in both (P12 has DST).
  - The game scanner, and Minicast's input and modem (the same as
    Reicast): nothing.

---

## 5. Suggested order

Merge 2026-10-06: pick the merged items from `OPTIMIZATION_PLAN.md` (its
section 0 lists where). The lists below stay as the reference order.

Updated 2026-10-06 with the Flycast items. The Soul Calibur items (P1, P2,
P16, P18) stay parked until Matt brings them back.

1. P16 (3 lines), then one Soul Calibur fight run: `tex decodes`, `live`,
   busy, RPS, VPS. It can change the facts of P1 and P2.
2. P18 option (a) (Soul Calibur background).
3. P19 (PC check first; it touches every game).
4. P20 G1: Matt's A3 verdict (SPE AICA DSP ON and SPE DSP JIT ON by
   default), after one HW run with SPE ARM7 ON. Then G2-G3 if a game shows
   them. No dry-mix port.
5. Small game fixes: P21 step 1, P23, P22, P24, P25.
6. The 2026-10-04 order: P2 step 1 and P1 step 0 (if still needed after
   P16), P1 steps 1-3, P6, P9.
7. The rest when Matt picks them.

FMV order (2026-10-06):
1. P41: done (plan 5.2, HW 2026-10-06: Sonic FMV +23% VPS).
2. P16 (parked with the Soul Calibur items). It also fixes the Soul Calibur
   opening misses; run the opening after it.
3. P42 (a) or (b), after P16 shows how many decodes are left.
4. P3 (parked, Matt 2026-10-05). The FMV numbers are in P3.
5. P43.

Fourth review order (2026-10-06):
1. P44: the log probe in one ChuChu Rocket boot, then the 2-line fix with
   its own toggle. It may touch F1 and F3.
2. P47 and P48 (GD-ROM, a few lines each).
3. P46 (PC check first; it also covers P32).
4. P45, then P52.
5. P49: the log census only.
6. The rest when Matt picks them. P50 and P51 also help the HW tests.

Fifth review order (2026-10-08):
1. P58 (Border Down). A check first: the devkitPPC `-S` test, or a boot
   self-test line. HW: Border Down; Crazy Taxi, Sonic Adventure and Looney
   Tunes unchanged.
2. P59. HW: Capcom vs. SNK 2, Border Down and Rayman 2 at 60 Hz (VPS,
   SPEED, the menus); Crazy Taxi unchanged.
3. P64 and P65 (1 line and ~6 lines).
4. P61 and P62, with P47 (bundle 2). HW: ChuChu Rocket and Looney Tunes
   CD-DA (pause, resume, looping), the BIOS CD player.
5. P63 (both decoders, then the S2 compare).
6. P60 (larger). HW: Quake III split screen, or a game from its list.
7. P66-P68 when Matt picks them; P69 when Matt wants the options; P70 at
   the start of G11.

Sixth review order (2026-10-11):
1. P75 (3 + 2 lines). HW: Crazy Taxi, START in a drive, twice in one
   boot. Then the RTT scaler of P66 for the pause background, with P76.
2. P71 (the option with three levels). HW: the Looney Tunes race at each
   level (RPS against 26.4, SPEED, sleep), then Phantasy Star Online and a
   dense Crazy Taxi drive.
3. P77, before the first HW test of Disc Swap.
4. P72 (the bind first, then the compares), then P73's first-pass skips.
   HW: Crazy Taxi at + OPAQUE DEPTH.
5. P81 steps 1 and 2 (no code), then the probe.
6. P58 with P78 and the cache clear on both AT edges. P74 only if one of
   the four games is SH4-bound after it.
7. P79, P80 and P82 when Matt picks them. P83 at the start of G11, P84 at
   G12, P85 at G13.

Placement 2026-10-11 (Matt: "Yes, I want these in the OPTIMIZATION
PLAN."): P71-P85 have a place in section 7 of `OPTIMIZATION_PLAN.md`.
Bundle 2: P77, P82's lead-out stop. Bundle 4: P79, P80. Bundle 5: P81,
P82's EXTS read-back. Bundle 8: P71, P78's RFCR. Bundle 10: P84's dead
zone. Bundle 11: P75, P76. New bundle 13 (MMU store queues): P58, P78,
P74. New bundle 14 (render-thread draw cost): P72, P73. New bundle 15
(flash defaults): P59, P85. P83 stays with GOALS.md G11.
