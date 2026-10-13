# Optimization plan - what to take from Flycast

Review of Flycast (`E:\Users\Matteo\Desktop\Dreamcast\flycast`) against our tree,
Reicast and Minicast. Written 2026-09-23. Flycast is an allowed reference from
this date (Matt).

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

Flycast is little-endian only. Check every ported line for byte order.

---

## 0. Resume here (updated 2026-10-13)

**Bundles re-cut 2026-10-12 (section 7; Matt: "Yes, do it."): the open
work is in bundles 10 to 21 now. Bundles 1-9 are closed, and the table of
section 7 says where each tail went. A bundle number in the blocks below
has its meaning of before the re-cut: old 10 (Maple) is now 10 and 16,
old 11 (Output stage) is 11 and 12, 13 and 15 grew, 16-21 are new.
Nothing is built by the re-cut. Recommended order: 11, 13, 14, 10, 15,
19, 17, 12, 16, 18, 20, then 21, then G11, then G10 with G13.**

**Bundle 15 (Flash and clock) built 2026-10-13 (Matt: "We will work on
Bundle 15 now. Prepare everything, when done, I'll tell you when to run
and when we start the test."). HW run 1 is below; no verdict yet. Package of 09:56 on
2026-10-13 (`build/notdc_ps3.pkg`, 1675824 bytes) = the G14 package of
09:35 + this bundle. Built again at 10:05 (Matt: "Build."): the same
sources and the same size, so the file's time is 10:05. The G14 package itself is kept as
`build/notdc_ps3_g14_0935.pkg` (1673008 bytes). A new page FLASH / CLOCK
(13 of 13) has six rows. Every row defaults to the old path (Matt named
no default).**
- **HW run 1, 2026-10-13 10:36 to 10:45 (Matt: "Rayman 2 is done. Seems
  like it worked!"): Broadcast DISC, BIOS Language ENGLISH, Flash
  Defaults FLYCAST, Console ID FLYCAST, RTC Local Time ON, Exit Flash
  Save OFF, all from the first boot (no boot with the rows OFF).** Log:
  `b15_hw.txt`, scratchpad of session e511e0f6 (tool `b15_read.py`
  there). Three boots (Rayman 2, Capcom vs. SNK 2 at X30, then at X3
  BACKWARD), no fault line. The first two exits are clean with the
  flash saved; the third boot still ran when this was written. No
  verdict and no default named yet.
  - Broadcast: Rayman 2 runs at `NTSC480i59.95`, VPS 59.95, 29.98
    renders per second (the old boots: PAL, 49.98 and 24.99), SPEED
    median 100.00%, lowest window 97.64%. Capcom vs. SNK 2: NTSC 59.95,
    SPEED median 100.00%. Border Down: not run yet.
  - The `[flash]` line: `region=1 language=1 broadcast=0 (flash file: 1
    1 1) syscfg language=1 (flash file: 1) console
    id=800417C324AD checksum=ok version=FE user blocks=241/254`. So the
    flash file holds USA, English and PAL: the PAL byte is verified.
  - BIOS Language ENGLISH is the flash's own language: the row had
    nothing to change. **BIOS Language ITALIAN, a boot with no disc
    (Matt: "Done. It works"): the BIOS menus are in Italian.** Log
    (`b15_hw.txt` from line 24294): `language=5 ... syscfg language=5
    (flash file: 1)`. The boot before it, with the row at CONSOLE and
    no disc: `language=1`, and `broadcast=1` at 50 Hz, as DISC keeps
    the flash's byte when there is no disc. The settings block moved
    from physical block 241 to 242 in that boot (a BIOS save): 12 are
    left.
  - RTC Local Time: `[rtc] PS3 time zone 60 min (rc 00000000), summer
    time 1 (rc 00000000)`. So the summer-time value is a flag (1), not
    minutes; the code counts 1 as one hour. `[nullDC] RTC=` is `time()`
    + 631152000 + 7200 in the three boots: right for Italy.
  - Console ID: the flash has an ID, and it is not the one the generator
    makes from this boot's seed, so FLYCAST kept it. The checksum is
    right. The machine version is FE now; its value in the file before
    this run was not logged.
  - Flash Defaults FLYCAST: no effect, as expected (both flash files are
    there). Exit Flash Save: not run.
  - Side result for bundle 21: JIT IDLE RULE X3 BACKWARD fixes the
    Capcom vs. SNK 2 menus (item 4 of bundle 21 has the numbers).
- The rows (`[flashcfg]` line at boot and at a menu change; cfg keys
  Broadcast, BiosLanguage, FlashDefaults, ConsoleId, ExitFlashSave,
  RtcLocal):

  | Row | Values (first = old path) | What it does |
  |---|---|---|
  | Broadcast | CONSOLE / NTSC / PAL / DISC | The TV system byte of the flash (0x1a004, 0x1a0a4). DISC: NTSC when the disc lists J or U, PAL when it lists only E; a disc with no region and a boot with no disc keep the flash's byte. |
  | BIOS Language | CONSOLE / JAPANESE / ENGLISH / GERMAN / FRENCH / SPANISH / ITALIAN | The language byte of the flash (0x1a003, 0x1a0a3) and of the BIOS settings block (syscfg byte 7). |
  | Flash Defaults | OLD / FLYCAST | FLYCAST: with no `dc_flash.bin` and no `dc_flash_wb.bin` a flash is built (USA, English, NTSC); a new BIOS settings block is English, not Japanese. OLD: the emulator exits when `dc_flash.bin` is missing. |
  | Console ID | OLD / FLYCAST | FLYCAST: a blank console ID (0x1a058) gets a random one in both copies; the checksum is set; the machine version is 0xFE. |
  | Exit Flash Save | OFF / ON | The forced exit (3 s after an XMB quit that the SH4 does not answer) saves the flash first. Live. |
  | RTC Local Time | OFF / ON | The Dreamcast clock is UTC + the XMB's time zone + its summer time. OFF: UTC, as it is today. |

- Facts from the old logs (the scratchpads of the older sessions):
  - The clock: in each of 293 old boots (2026-10-03 to 2026-10-13) the
    `[nullDC] RTC=` line is `time()` + 631152000 with no offset. So the
    PS3's `time()` is UTC, its C library has no time zone, and
    `tm_isdst` is 0. The Dreamcast clock is UTC: 2 hours behind in the
    Italian summer, 1 hour in the winter. P12's line (one day, not one
    hour, under summer time) is fixed, but that line never runs on the
    PS3. It needs no row.
  - The broadcast byte: every boot starts at `PAL480i50.00` (the BIOS),
    so the flash holds a PAL byte. The byte itself was never printed:
    the new `[flash]` line of HW run 1 shows it.
  - 10 of the 23 games of the old logs stay at 50 Hz in every boot, not
    3 as P59 says: Rayman 2 (8 boots), Soul Calibur (6), Space Channel 5
    (4), Border Down (3), Capcom vs. SNK 2 (3), Ikaruga (3), Sonic
    Shuffle (3), Spider-Man (3), Sega Rally 2 (2), Shenmue (2). Power
    Stone 2 is at 50 Hz in 3 of 5 boots, Rez in 2 of 3. Crazy Taxi,
    Sonic Adventure, Sonic Adventure 2, Looney Tunes, ChuChu Rocket,
    Quake III Arena, Phantasy Star Online, Jet Grind Radio and Ecco the
    Dolphin switch to NTSC by themselves (a boot counts once: the logs
    are split per `[menu] launching` line and per RTC value).
  - Each disc image of the old logs lists the area `JUE`, and the flash
    region is 1 (USA). So for Matt's discs DISC gives what NTSC gives.
  - The BIOS settings block is in physical block 224 to 241 of the 254
    of the user partition, a new one at each BIOS save. For GOALS.md
    G11: the dial-up blocks 0x80-0xEA need about 107 free blocks, and
    Flycast's `alloc_block` takes a block again when a newer copy of its
    data exists. Ours has no such rule (not built here: it changes the
    old path for a flash with no settings block).
- The code:
  - `dc/flash_fix.cpp` (header `dc/flash_fix.h`): `dc_flash_build()`
    (Flycast's `Validate()` on an empty chip, then its Region 1,
    Language 1 and Broadcast 0), `dc_flash_set_time()` (as before, with
    the language of a new block and the row's language),
    `dc_flash_fixup()` (the rows' factory bytes, the console ID),
    `dc_flash_set_region()` (as before, with the DISC rule),
    `dc_flash_report()` and `dc_flash_restore_region()`.
    `LoadBiosFiles()` (`dc/dc.cpp`) calls them in that order.
  - The `[flash]` line, new, at each boot with every row OFF too:
    `[flash] region=R language=L broadcast=B (flash file: R L B) syscfg
    language=N (flash file: N) console id=XXXXXXXXXXXX checksum=ok|bad
    version=XX user blocks=N/254`. The first values are what the BIOS
    and the game read. The values after "flash file" are the file's own.
  - `dc/mem/sh4_mem.cpp`: `mem_SaveFlashForced()` for the monitor
    thread (`ps3/ps3_main.cpp`). It saves only a flash that
    `LoadBiosFiles()` marked as loaded (`g_flash_loaded`), and one lock
    lets one thread write the file at a time. Log: `[flash] forced exit:
    flash saved`.
  - `dc/aica/aica_if.cpp` `GetRTC_now()` and `ps3/ps3_main.cpp`
    `ps3_local_offset_seconds()`
    (`sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE)` and
    `..._SUMMERTIME`). Log at each boot, with the row OFF too: `[rtc]
    PS3 time zone N min (rc X), summer time N (rc X)`.
  - `nullDC.cpp`: a missing `dc_flash.bin` ends the boot only at Flash
    Defaults OLD.
  - Byte order: none. `misc/flash_check/run.sh` fails if
    `dc/flash_fix.cpp` holds a `(u16*)` or a `(u32*)` cast.
- My choices, not in the plan text and not Matt's words:
  - Row RTC Local Time. P12's one line cannot make the clock right on
    the PS3 (the fact above), so the row takes the offset from the XMB.
    The unit of the summer-time value is not verified (minutes are
    assumed; a value of 1 counts as 60). The `[rtc]` line of HW run 1
    shows the two raw values before the row is ON.
  - No row's value reaches `dc_flash_wb.bin`, but the console ID: the
    factory bytes and the settings block's language go back to the
    flash's own before each save, as the region did. A language that
    the user picks in the BIOS in the same run stays. So a row acts
    again at each boot, and CONSOLE gives back the flash's own value.
  - Flash Defaults holds two items of P85 in one row: the built flash
    and the English default of a new settings block. Flycast's
    `Validate()` runs only when no file loads: a flash that loads is
    not repaired, and its partition 1 is not erased (Flycast erases it
    at each boot).
  - The new console ID comes from an own generator with the RTC as
    seed, not from `rand()`.
  - Exit Flash Save is a row and is live, with the lock and the
    "loaded" flag: a forced exit before the flash loads must not write
    an empty flash over the saved one.
  - Not taken: Flycast's PAL-M and PAL-N values and its per-game
    broadcast lists (a list per game); the `SaveSettings()` call at an
    RTC write (P85's "Also" line, no effect).
- PC checks (WSL):
  - `misc/flash_check/run.sh`: PASS. It builds the real
    `dc/flash_fix.cpp` and three sections cut from `sh4_mem.cpp`,
    `aica_if.cpp` and `ps3_main.cpp`. 400 flashes with every row OFF:
    the bytes and the log lines are those of the code of commit 977df14
    (`ref_flash_fix.inc`). 400 flashes with random rows: the bytes are
    those of Flycast's `fixUpDCFlash()`, written in the check in
    Flycast's own form; after `dc_flash_restore_region()` the flash is
    the one of a boot with Broadcast and BIOS Language at CONSOLE. Then
    the built flash, the DISC table, the save lock, the zone and the
    RTC (on a host with a CET zone too). 58 source line checks.
  - `misc/flash_check/mutate.py`: 94 of 94 mutants fail `run.sh`.
  - The older checks that name a changed file, `run.sh` only: PASS for
    mmu_check, pad_check, rtt_check, screen_check, glpath_check,
    timebase_check, clock_check, sysgate_check, jit8_check and
    g2dma_check. `misc/g2dma_check/g2_check.cpp` and
    `misc/sysgate_check/stubs.inc` have two new stubs
    (`get_rtc_local_preset`, `ps3_local_offset_seconds`): four checks
    link `aica_if.cpp`.
  - Seen, not repaired (not from this bundle): one mutant text of
    `misc/timebase_check/mutate.py` is stale since bundle 11 ("TMU Fine
    TCNT in the live list": its line of `k_live_opts` is no longer the
    last one).
- Link: `.got` is 0xEAB0 of 0x10000, 0 `r2off`. 5.3 KB of the TOC are
  left: watch it at the next large addition.
- HW steps (UDP listener on port 18194, only when Matt says so; SH4
  Clock 200; install `build/notdc_ps3.pkg` of 10:05):
  1. Rows OFF, one boot of Rayman 2. Read `[flashcfg] boot:` (every
     value 0, JitExit=1), `[rtc] PS3 time zone`, `[nullDC] RTC=` and
     the `[flash] region=` line. Expected: broadcast=1 (PAL), VPS 50.
  2. Broadcast NTSC. Rayman 2, then Capcom vs. SNK 2 and Border Down:
     `NTSC480i59.95`, VPS about 60, SPEED, the menus, the sound. Soul
     Calibur is a fourth 50 Hz game. Crazy Taxi: unchanged.
  3. BIOS Language at a language that is not the flash's. A boot with
     no disc shows the BIOS menus in it; the `[flash]` line shows
     `language=` and `syscfg language=N (flash file: M)`.
  4. RTC Local Time ON. `[nullDC] RTC=` minus `time()` minus 631152000
     must be the zone's seconds (7200 in Italy until 2026-10-25). The
     BIOS clock shows the local time.
  5. Console ID FLYCAST. The `[flash]` line: `checksum=ok version=FE`,
     and the ID of the boot before if the flash had one. No effect on
     a game is expected.
  6. Exit Flash Save ON with JIT Exit OFF (EMULATION page): with JIT
     Exit OFF each XMB quit takes the forced exit. Expected: `[flash]
     forced exit: flash saved`, then `[ps3] SH4 did not stop in 3 s,
     forcing process exit`. Then JIT Exit ON again.
  7. Optional (it needs two file renames on the PS3): Flash Defaults
     FLYCAST with `dc_flash.bin` and `dc_flash_wb.bin` renamed in
     `/dev_hdd0/data/NotDC/data/`. Expected: `[flash] no flash file:
     built a new flash (USA, English, NTSC)` and a BIOS in English.
     Then the two names back.

**GOALS.md G14 (CHD disc images) built 2026-10-13 (Matt: "Can you add CHD
support?"). Package of 09:35 on 2026-10-13 (`build/notdc_ps3.pkg`,
1673008 bytes) = the package of 01:15 + the source defaults that waited +
G14. The HW test is open.** GOALS.md G14 has the whole state (files, PC
check, what is not done). In short:
- `.chd` files show in the browser and open through
  `plugs/ImgReader/chd.cpp` (chdr, LzmaDec and libFLAC's decoder in
  `deps/`). CHD v1-v5; codecs cdlz, cdzl, cdfl, zlib and none. Out, with a
  `[chd]` log line: zstd (`cdzs`), parent files.
- PC check `misc/chd_check`: PASS, 49 of 49 mutants. Three real GD-ROM
  images (Naomi, chdman v5) decode to the SHA1 of their header.
- HW test (Matt makes the files: the PC has no chdman, and none of his
  discs is a `.chd`): a GD-ROM `.chd` and a MIL-CD `.chd` boot and play;
  CD-DA sounds right; loads are as with the GDI/CDI. Read `[chd] hunks`
  (file and decode time per hunk) and `[gdr]` (fill times) in the log.
- Open after the HW run: a read-ahead buffer, only if loads stall.
- Link: `.got` 0xE9C0 of 0x10000, 0 `r2off`.
- Another session edited the tree from 09:36 on (flash_fix, sh4_mem,
  aica_if, presets, menu): its edits are NOT in the package of 09:35. A
  copy of that package is `notdc_ps3_g14_0935.pkg` in the scratchpad of
  session 74c76729.

**Bundle 14 (Render-thread draw cost) built 2026-10-13 (Matt: "Let's work
on bundle 14!"), HW SUCCESS 2026-10-13 (Matt: "Yes, bundle 14 is a
success. Keep it ON.") after HW runs 1-3 with the three rows ON: Crazy
Taxi (render busy 47.2% -> 29.2% in 9K+ vertex scenes; Matt: "Done. No
broken geometry."), Sonic Adventure 2 ("Everything looked right.") and
Rayman 2 ("ran fine too"). No fault in the three logs. HW run 4 (Sonic
Adventure, named after the verdict) is clean too: render busy 18.0% ->
12.0%. Package of 01:15
on 2026-10-13 (`build/notdc_ps3.pkg`, 1605232 bytes) = commit 977df14 +
this bundle. **The three rows of the new DRAW COST page are default ON
in source since 2026-10-13 (Tex Bind Table ON, GL State Compare ON, TR
First Pass + GAME ORDER), not packaged: the package of 01:15 has them
OFF, and Matt's settings file has them ON.** No value changes the
picture. GOALS.md says that G14 (CHD disc images) starts after this
bundle (Matt, 2026-10-13).**
- Left after the verdict, nothing asked for yet:
  - No pause (render to texture) ran with the rows ON in the three logs.
  - The texture lifetime: 60-68% of the decodes in Crazy Taxi and Sonic
    Adventure 2 are textures that the 180-frame rule freed (the
    `[texlife]` counter). A longer life saves them. Not built.
  - The merge key mask: 13-20% fewer opaque draws in Crazy Taxi, none in
    Sonic Adventure 2 and Rayman 2 (the `[cost] key` counter). Not built.
  - The index list: not built.
  - One Sonic Adventure 2 boot with the three rows OFF on this package:
    it says if the lighter render thread gives the SH4 thread time (HW
    run 2 has no such half).
- **HW run 1, 2026-10-13 01:51 to 01:59: Crazy Taxi with Tex Bind Table
  ON, GL State Compare ON and TR First Pass + GAME ORDER from the boot.**
  Log: `sa_anim1.txt` of session d13d658e (its listener still holds the
  port), lines 393736-403210; the cut is `b14_run1.txt` in the scratchpad
  of session 6e520dca. `[costcfg] boot: TexBindTable=1 GlCompare=1
  TrFirstPass=2 TrTriSort=3`. The other settings are those of the
  baseline boot (the table further down). 14995 renders, 0 dropped, no
  fault line, a clean exit to the XMB. Two in-game menu opens, no row
  changed.
  - Against the baseline, per vertex band (windows of 5 s):

    | Scene | busy | param (tex, of it gl) | glEnd | sort | vtx+other | Draws per render (OP + TR) | us per draw / per bind | SPEED mean / min |
    |---|---|---|---|---|---|---|---|---|
    | 9K+ vertices: 28 windows (10.6K) | 29.2% (47.2) | 10.2 (6.3, 2.9) | 6.0 (13.7) | 3.6 | 6.5 (5.2) | 314 + 615 (309 + 1033) | 2.91 / 0.53 (4.42 / 1.47) | 99.7% / 96.1% (99.1 / 88.0) |
    | 11-13K vertices: 3 windows | 30.3% (51.7) | 10.9 (6.7, 3.2) | 6.4 (14.8) | 3.8 | 6.7 (5.7) | 402 + 598 (337 + 1124) | 2.87 / 0.53 (4.47 / 1.51) | 100.1% / 100.0% (98.2 / 88.0) |
    | 13K+ vertices: 2 windows (15.8K) | 33.5% (49.0) | 11.7 (7.2, 3.4) | 6.9 (14.2) | 4.0 | 7.0 (5.3) | 481 + 641 (467 + 972) | 2.82 / 0.53 (4.43 / 1.49) | 98.1% / 96.1% (96.8 / 94.7) |

  - Per row (9K+ band). The three rows were ON together, so each row is
    read from its own number:
    - Tex Bind Table (with the parameter compare): a bind costs 0.53 us,
      not 1.47. `gl` is 2.9 points, not 11.7; at the baseline's bind
      count it would be 4.2.
    - GL State Compare: `glEnd` per draw is 1.08 us, not 1.71.
    - TR First Pass + GAME ORDER: 615 TR draws per render, not 1033, for
      the same sorted triangles (1169, baseline 1204). The estimate was
      750. Its cost is in `vtx+other`: 6.5 points, not 5.2 (the first
      pass's own corners).
    - The sort (3.6) and the volumes (1.3) are as before.
  - SPEED: the slow windows of the baseline (88.0% at 11.6K vertices) are
    not in this run; the lowest window is 96.1% at 15.8K. The sample is
    small (3 and 2 windows in the two high bands), and the route is not
    the same.
  - The counters:
    - `[texlife]`: 3939 textures freed for their age, 2739 came back,
      2733 of them with the same texels. The run had 4047 decodes, so 68%
      of the decodes are textures that the 180-frame rule freed (180
      frames are 3 s at Crazy Taxi's 60 renders per second). At most 335
      textures live. A longer life saves these decodes (0.7 points of
      busy, and their time on the SPU).
    - `[cost] key`: opaque state changes per render by the key of today >
      by the bits a draw reads: 304>252, 225>193, 337>281, 531>451,
      157>129 (14 samples read, 13-20% less). TR: 105>95, 111>106,
      122>105 (2-14% less, and the sorted triangles do not run in this
      order). So a key mask saves about 50 of the 930 draws per render.
      The sample is the one of `[rendz]`: some windows print the sample
      of the window before.
    - `nodepth` is 0 or 1 of 80-170 first-pass runs: SKIP alone has
      nothing to leave out in Crazy Taxi, as P73 says.
  - Not in this run: a pause (`rtt=0/0` in each window, so no render to
    texture ran with the rows ON); a second game for TR First Pass (Sonic
    Adventure or Rayman 2).
- **HW run 4, 2026-10-13 02:11 to 02:17: Sonic Adventure with the three
  rows ON, JIT IDLE RULE X3 BACKWARD, Screen Format 4:3** (Matt: "I also
  did a Sonic Adventure (not 2) run.", then "The run looked fine"). Log:
  `sa_anim1.txt` lines 413705-422419; the cut is `b14_run4.txt`
  (scratchpad of session 6e520dca, the boot is its line 301). 8209
  renders, 0 dropped, no fault line, a clean exit, no menu open, no
  pause. Baseline: the Sonic Adventure boot of bundle 13's HW run 2
  (`b13_run2.txt` from line 21543; the same code with the rows OFF, X3
  BACKWARD; it had 16:9 WIDE).

  | Scene | busy: run 4 (baseline) | bind `gl` | `glEnd` | TR draws per render | us per draw / per bind | SPEED mean / min |
  |---|---|---|---|---|---|---|
  | 2K+ vertices: 44 windows (10.6K) | 12.0% (18.0) | 1.0 (4.5) | 2.0 (4.7) | 413 (714) | 2.69 / 0.49 (4.41 / 1.59) | 100.1% / 98.3% (99.7 / 93.6) |
  | 8-12K: 19 | 11.2% (16.9) | 0.9 (4.0) | 1.8 (4.5) | 377 (677) | 2.77 / 0.49 (4.28 / 1.45) | 99.8% / 98.3% (100.1 / 98.0) |
  | 12-16K: 10 | 13.0% (20.7) | 1.1 (5.3) | 2.2 (5.3) | 427 (786) | 2.71 / 0.49 (4.53 / 1.66) | 100.2% / 99.1% (99.4 / 98.1) |
  | 16K+: 4 (18.0K) | 20.5% (35.3) | 1.6 (11.6) | 3.4 (8.9) | 786 (1377) | 2.66 / 0.48 (5.22 / 2.24) | 100.8% / 99.9% (99.4 / 98.3) |

  - The game is at full speed in both boots, so the gain is render-thread
    time only: 6 points on average, 15 points in the 16K+ scenes. The TR
    draws go from 714 to 413 per render for the same sorted triangles
    (1587, baseline 1567).
  - TR First Pass: 52 first-pass runs per render, 4.6 of them write no
    depth. `[cost] key`: no gain (294>294, 365>365). `[texlife]`: 1905
    freed for their age, 910 came back, 901 with the same texels, of 2276
    decodes (40%).
- **HW run 3, 2026-10-13 02:08 to 02:10: Rayman 2 with the three rows ON**
  (Matt: "Also Rayman 2 ran fine too."). Log: `sa_anim1.txt` lines
  410495-413404; the cut is `b14_run3.txt` (scratchpad of session
  6e520dca, the boot is its line 345). A short run: 2088 renders, 0
  dropped, no fault line, a clean exit. Render busy 4-7% (2-8K vertices);
  a bind 0.42-0.51 us. TR First Pass acts: 34-82 first-pass runs per
  render, 0-12 of them write no depth; 143-190 TR draws per render.
  `[cost] key`: no gain (77>77, 70>70). `[texlife]`: 277 freed, 56 back.
  One menu change in the run: Screen Format 16:9 WIDE -> 4:3. No row of
  this bundle changed. No pause.
  - Side fact for bundle 19: the disc is a 3-track GDI set (`GDI : 3
    tracks`), so this is a GDI boot on hardware. Side fact for bundle 15:
    the US disc runs at `PAL480i50.00` (the broadcast byte).
- **HW run 2, 2026-10-13 02:00 to 02:07: Sonic Adventure 2 (Europe) with
  the three rows ON, JIT IDLE RULE X3 BACKWARD** (Matt: "Mind if I try
  Sonic Adventure 2? That game also was bottlenecked by rendering.", then
  "Done", then "Everything looked right."). Log: `sa_anim1.txt` lines
  403688-410150; the cut is `b14_run2.txt` (scratchpad of session
  6e520dca, the boot is its line 478). 10276 renders, 0 dropped, no fault
  line, a clean exit. No menu open: the boot has no half with the rows
  OFF.
  - So the baseline is the two old boots of 2026-10-12 (`b8_hash_sa2.txt`
    of 02:01 and `b8_sqcopy_sa2.txt` of 02:45, session 1c8776dd): the same
    disc image, X3 BACKWARD, TR Triangle Sort + OPAQUE DEPTH. Not the
    same: an older package (before bundles 9-13), 4:3, no Fog row, no FPS
    overlay, JIT PR MOVES and JIT IDLE HASH ON, and JIT SQ COPY ON in the
    second one. The compare is a rough one.

    | Scene | busy: run 2 / old / old + SQ COPY | us per draw / per bind: run 2 (old) | SPEED mean (min): run 2 | old | old + SQ COPY |
    |---|---|---|---|---|---|
    | 10-14K vertices | 21.1% / 28.3 / 26.1 | 3.38 / 0.61 (4.81 / 1.43) | 99.8% (98.3), 7 windows | 98.3% (90.5), 10 | 99.0% (95.0), 8 |
    | 14-16K | 25.0% / 28.5 / 30.0 | 3.15 / 0.59 (4.33 / 1.35) | 94.3% (90.6), 3 | 89.6% (88.3), 2 | 94.9% (91.0), 3 |
    | 16-18K | 26.3% / 32.6 / 32.0 | 3.13 / 0.59 (4.32 / 1.31) | 100.9% (93.2), 3 | 94.5% (91.7), 5 | 99.2% (95.7), 3 |
    | 18K+ | 29.0% / 37.8 / 34.3 | 3.08 / 0.59 (4.54 / 1.45) | 94.4% (88.4), 2 | 83.8% (74.8), 4 | 91.4% (87.8), 3 |

  - Windows under SPEED 97% (of those with 2K+ vertices): 4 of 30, mean
    91.7%. Old: 14 of 33, mean 90.0%. Old + SQ COPY: 8 of 31, mean 93.0%.
  - Reading: the render thread is 7 to 9 points lighter at 16K+ vertices
    (a draw 3.1 us, not 4.3-4.5; a bind 0.59 us, not 1.3-1.45), with more
    work in it than the old boots had (16:9 WIDE, fog, overlay). The SH4
    thread is still the limit in the heavy windows (`[pace] slept` 0-9 of
    57-61 at 14K+). SPEED is higher than in the old boot without SQ COPY
    in every band, but the samples are 2-3 windows and the settings are
    not the same: this run does not prove that the lighter render thread
    gives the SH4 thread time (the two share the PPE core). A boot with
    the three rows OFF on this package decides it.
  - TR First Pass acts in this game: 50-73 first-pass runs per render, 10
    to 27 of them write no depth (SKIP leaves them out; Crazy Taxi had
    none). TR draws 144-198 per render (old boots: 162-339, other scenes).
  - The counters: `[cost] key op=316>313 tr=57>57`: a key mask saves
    nothing in this game. `[texlife]`: 1787 freed for their age, 1211 came
    back, 1210 with the same texels, of 2010 decodes (60%).
- The DRAW COST page (page 12 of 12; a `[costcfg]` line at boot and at a
  menu change; all three rows are live):
  - (The first value of each row is the old path; the defaults are ON
    since the verdict.)
  - Tex Bind Table (`TexBindTable`): OFF / ON. ps3gl finds the slot of a
    texture name in a table of 4096 entries, indexed by the name's low
    bits. An entry holds the slot of the last texture with those bits; a
    hit must still hold the name, a miss takes the old scan. OFF scans up
    to 4096 slots at each bind (the 11.1 points of HW run 5 of 3k).
  - GL State Compare (`GlCompare`): OFF / ON. ON:
    - A state setter (blend, alpha, depth, cull, scissor, viewport, colour
      mask, polygon offset, shade model, stencil) marks its group only
      when a value changes. OFF marks it at each call, and the next draw
      sends the whole group again.
    - A texture parameter that keeps its value does not mark the texture.
      OFF marks it at each call: the 5 parameters of each bind send 4 RSX
      calls at the next draw.
    - At a texture change, the unit's control, filter and wrap words go
      out only when they differ from the words the unit holds. The
      texture itself always goes out.
    - The 4 vertex attribute binds stay when the next draw has the same
      block, stride, colour shift and texcoord1 form. So in a scene they
      go out at the first draw, and again when a bump or fog program (the
      texcoord1 form) or the colour shift comes or goes.
  - TR First Pass (`TrFirstPass`): OLD / SKIP / + GAME ORDER. It acts on
    the first pass of TR Triangle Sort + OPAQUE DEPTH only.
    - SKIP (Flycast's depth pass): a run that writes no depth (its blend
      hides nothing, or the polygon has ZWriteDis) is not applied and not
      drawn. OLD applies each run, and draws a ZWriteDis run with every
      mask off.
    - + GAME ORDER: the first pass goes out before the sort, in submission
      order, so its runs are as long as the merged strips. The depth it
      leaves does not depend on the draw order. It writes its own corners
      (only the runs it draws); when the scene's vertex block is too short
      for them, the rest of the scene goes to a second block.
- Counters only, as the bundle says (no row, no change of the draws):
  - The merge key: `[cost] key op=A>B pt=A>B tr=A>B`. A = state changes
    from strip to strip by the merge key of today; B = by the bits a draw
    reads. A - B is the draws a mask on the key can save. It is the last
    render of each 5 s window, in submission order.
  - The texture lifetime: `[texlife] aged=N back=N same=N` per window.
    aged = textures freed after 180 frames with no draw; back = cache
    misses whose key one of them had; same = of those, the ones with the
    same texels (a decode that Flycast's rule saves).
- Not built: the index list for the sorted triangles. The bundle has it
  last, with a ceiling of 4.6 points for all vertex work; it needs an RSX
  draw call that NotDC never ran (`rsxDrawIndexArray`), and FIRST VERTEX
  flat shading cannot use it. Read `vtx+other` of the first HW run first.
- Log:
  - `[costcfg] boot: TexBindTable=1 GlCompare=1 TrFirstPass=2 TrTriSort=3
    StripMerge=1 VertexArrays=1 SpeBuild=1`, and `[costcfg] menu:` at a
    change.
  - `[cost] bind= compare= first_pass= | fp_runs= nodepth= | key ...` per
    window. fp_runs = runs of the first pass per render; nodepth = those
    that write no depth (what SKIP leaves out; Crazy Taxi had none in the
    3k runs).
  - The measure is in the old lines: `[rend] busy=`, `draw split %: param
    (tex) glEnd sort`, `tex split %: gl` (the bind), `draws=merge ... tr=`.
- Code: `ps3/ps3gl/ps3gl.h` (`PS3GL_FAST_BIND`, `PS3GL_FAST_COMPARE`,
  `ps3gl_fast_path()`), `ps3gl_main.c` (`ps3gl_fast_path()`, the resets in
  `ps3gl_begin_frame()`), `ps3gl_textures.c` (`ps3gl_texture_find()`,
  `tex_param()`, `ps3gl_apply_textures()`), `ps3gl_states.c` (`rs_mark()`
  and each setter), `ps3gl_vertices.c` (`bind_vertex_attribs()`),
  `ps3gl_draw.c` (`glDrawElements`), `ps3/ps3_rend.cpp`
  (`tri_first_draws()`, `tri_runs()`, `tri_game_room()`,
  `draw_tr_tris()`, `strip_read_equivalent()`, `rth_job_draw()`),
  `ps3/ps3_tex.cpp` (`TexGhost`), `ps3/ps3_menu.cpp`,
  `ps3/ps3_presets.cpp`. The long comments of `ps3gl_main.c`,
  `ps3gl_states.c` and `ps3gl_draw.c` are cut to 2 lines (no code change).
- Traps:
  - `ps3gl_begin_frame()` marks every group and drops the unit and
    attribute records. A new RSX call outside ps3gl that changes a
    register ps3gl holds must come before it, or mark the group. Today
    the calls outside ps3gl are the surface, the depth format and the
    transfers, and ps3gl holds none of them.
  - ps3gl has one dirty flag per texture. A texture on two units misses a
    parameter change on the second one, on both paths. NotDC has no such
    texture (the fog table is on unit 1 only).
  - With GL State Compare ON, ps3gl's GL_POLYGON batch takes in more
    polygons (a state that stays is no longer marked). NotDC has no
    GL_POLYGON draw.
  - A key that is not in an installed `nullDC.cfg` takes the source
    default: OFF in the package of 01:15, ON from the next package on.
  - `.got` is 0xE378 of 0x10000 after this bundle.
- PC checks, all PASS in WSL:
  - `misc/glpath_check` (new): the real ps3gl files over a model of the
    RSX registers, 12 seeds of 400 frames of random GL calls of NotDC's
    kinds. Rows OFF: the RSX calls are those of the files of commit
    977df14, call for call. Rows ON (each, both, and changed in mid run):
    the registers at each draw and clear, and after each frame, are those
    of the run with the rows OFF; no sampler word and no set of attribute
    binds goes out twice. Before each frame the registers take a poison
    value. Source lines: the rows have their default in the source (ON
    since the verdict), are saved, in the menu, live and read by the
    renderer. `mutate.py`: 73 of 73 planted bugs fail the check.
  - `misc/rend4_check` (extended): a third build, the file of commit
    977df14 (`ref_b13_ps3_rend.inc`): with TR First Pass at OLD the GL
    calls and the vertex block are the old ones in 1357 scenes with the
    other rows random. The model has the two new levels, the corners'
    W and colour, the order of the first pass, and the need for a second
    vertex block. `mutate.py`: 88 of 88 planted bugs fail the check (4
    of its texts were stale since bundles 11 and 3k; repaired).
  - The 14 other checks that read a changed file pass (ta4, bump, fog,
    rtt, screen, tex3, texslot, spetex, spepix, tr4, mmu, pad, timebase,
    clock). `misc/screen_check/mutate.py`: 2 texts that were stale since
    bundle 13 are repaired.
- HW steps (Crazy Taxi, TR Triangle Sort at + OPAQUE DEPTH, one boot; the
  rows are on the SELECT menu, page DRAW COST):
  1. Install the package. Boot Crazy Taxi and start a drive. Keep the
     three rows OFF for about 1 minute in a dense street: the baseline.
  2. Set Tex Bind Table ON. Drive the same street for 1 minute.
  3. Set GL State Compare ON too. Drive 1 minute.
  4. Set TR First Pass to + GAME ORDER too. Drive 1 minute.
  5. In each step the picture must be as before: no wrong or missing
     texture, no flicker, no freeze. Pause once (START) with the rows ON.
  6. Second game for TR First Pass: Sonic Adventure or Rayman 2 (both need
     + OPAQUE DEPTH) at SKIP, then at + GAME ORDER: the models and the
     water must look as before.
  7. Read per step: `[rend] busy=`, `tex split %: gl`, `draw split %:
     glEnd`, `draws=... tr=`, SPEED at 11-13K vertices. HW run 5 of 3k
     had busy 45.2%, gl 11.1, glEnd 12.5, 1064 TR draws per render.
- Baseline with Matt's settings of today (Matt 2026-10-13: "Can I do the
  compare with it on all the time?", then "Can you check the run from
  bundle 13?"): the Crazy Taxi boot of bundle 13's HW run 2 (2026-10-12
  23:48, package of 23:15, `b13_run2.txt` lines 189-9837, scratchpad of
  session ff1f423e). Its code is this package with the three rows OFF.
  Settings: TR Triangle Sort + OPAQUE DEPTH, Fog TABLE, 16:9 WIDE, Screen
  Scaler ON, FPS Overlay ON, Region Scissor ON. No menu change in the boot.
  So one boot with the three rows ON can be read against it, per vertex
  band (the route is not the same).

  | Windows | Renders/s | busy | param (tex, of it gl) | glEnd | sort | vtx+other | Draws per render (OP + TR) | us per draw / per bind | SPEED mean / min |
  |---|---|---|---|---|---|---|---|---|---|
  | 9K+ vertices: 33 (10.6K) | 59.7 | 47.2% | 21.7 (16.2, 11.7) | 13.7 | 3.7 | 5.2 | 309 + 1033 | 4.42 / 1.47 | 99.1% / 88.0% |
  | 11-13K vertices: 6 | 59.2 | 51.7% | 23.9 (18.0, 13.1) | 14.8 | 4.0 | 5.7 | 337 + 1124 | 4.47 / 1.51 | 98.2% / 88.0% |
  | 13K+ vertices: 3 (14.6K) | 57.9 | 49.0% | 22.7 (17.0, 12.4) | 14.2 | 3.5 | 5.3 | 467 + 972 | 4.43 / 1.49 | 96.8% / 94.7% |

  The numbers come from `b13_base.py` (scratchpad of session 6e520dca):
  windows of 5 s with 25 or more renders; us per draw = (param + glEnd) /
  all draws; us per bind = gl / `[texfmt] binds=`.

**Bundle 13 (MMU and memory) built 2026-10-12 (Matt: "We will now work on
Bundle 13."), HW SUCCESS 2026-10-13 after HW runs 1 and 2 (Matt: "Mark it
as a success, and we will check the Ikaruga and Rez issues later on.").
Those issues are the SH4-bound heavy scenes of the two games: now item 7
of bundle 21 (section 7). HW run 1, 2026-10-12: with MMUCR Bits FIXED,
Border Down, Sonic Shuffle, Ikaruga and Rez all get past the BIOS logo and
play (Matt: "The game ran fine!", "Ikaruga runs!", "Rez ran fine!").
Package of 23:15
on 2026-10-12 (`build/notdc_ps3.pkg`, 1602352 bytes) = commit 27f2a65 +
this bundle. **MMUCR Bits is default FIXED in source since 2026-10-12
(Matt: "Let's keep the MMUCR Bits default in FIXED."), not packaged: the
package of 23:15 has OLD, and Matt's settings file has FIXED.** The other
three rows stay at their old path (no default named). HW run 2: Crazy
Taxi, Looney Tunes and Sonic Adventure are unchanged with FIXED. Not run
yet: FB Write Watch ON (Ikaruga), the other three rows. LMMODE has no
test: no game in the logs sends a store-queue block to VRAM.**
- **HW run 2, 2026-10-12 23:48 to 2026-10-13 00:18: no change in the
  three regression games** (Matt on Looney Tunes: "It worked right.").
  Same log file, lines 362471-393377; the cut is `b13_run2.txt` in the
  same scratchpad. Package of 23:15, MMUCR Bits FIXED, the other three
  rows OFF. No fault line, 0 `Write to Boot ROM`, 0 `[p4]` access, `[ocr]`
  and `DMAC: Ch2` lines in the three boots.
  - None of the three turns the MMU on. Crazy Taxi writes MMUCR 3 times,
    each time 0; Looney Tunes and Sonic Adventure have only the BIOS's two
    writes of 0. Looney Tunes' 100 `MEM remap [0]` lines are there as
    before. So the fix has no path into these games.
  - Crazy Taxi (X30): 16373 renders, 0 dropped, SPEED 99.6% as the mean.
    By the survey's rule (2K+ vertices, 5+ renders/s): 21 of 257 windows
    slow (8.2%), 93.6% in them at 14.4K vertices. The survey of P86 has
    3-9% and 92.6-94.8% for Crazy Taxi at X30.
  - Looney Tunes (X30): 8815 renders, 0 dropped, the intro video decoded
    (YUV DMA in 12 windows), SPEED 99.6% as the mean. 12 of 249 windows
    slow (4.8%): 4 at 90-97% with 28.4K vertices, 8 at 85-89% with 32.2K.
    Of the 166 race windows (20K+ vertices), 11 have no sleep of the SH4
    thread (the host limit), and 130 are at SPEED 100% with 25-27
    renders/s and sleep in about half of the pace calls (the X30 rule's
    frame drops in emulated time). Matt: "I'm not sure if we can still
    pinpoint about the stutters": the log still separates the two kinds;
    both are bundle 21's.
  - Sonic Adventure (X3 BACKWARD): 9838 renders, 0 dropped, SPEED 99.5%
    as the mean, 30.6 renders/s in 230 game windows, none slow.
- **HW run 1, 2026-10-12 ~23:24-23:45: the fix works** (Matt, per game,
  above). Log: `sa_anim1.txt` of session d13d658e (its listener held the
  port), lines 344370-362470; the cut is `b13_run1.txt` in the scratchpad
  of session ff1f423e. Six boots, each with `[memcfg] boot: MmuBits=1
  P4Mirror=0 OcrBanks=0 Ch2DmaFix=0 Lmmode=0 VramDirect=0 DmaFix=1`.
  - The self-test line in each boot: `bitfield AT=0 TI=0 SV=0 URC=2, masks
    AT=1 TI=1 SV=1 URC=42`. So the PS3 build reads the bitfield as
    `bitfield_ppu.sh` says.
  - The MMUCR writes, the same in the four games: two writes of 0 by the
    BIOS, then one write of `00040005` by the game (AT, TI, URB = 1; pc
    8C03D336 Border Down, 8C05C8C6 Sonic Shuffle, 8C044386 Ikaruga,
    8C0517FA Rez). Then `MMU address translation ENABLED`, `TLB
    invalidate`, a `recSh4: cache clear` at the same pc, and after that
    the 50 `SQ remap` lines, each one with V=1. So TI comes before the
    entries: the "TI keeps the remap table" rule and the V = 0 skip had
    nothing to do in these games (0 `not valid` lines).
  - 0 `Write to Boot ROM` lines in the four games (old logs: Border Down
    11592 and 45848, Sonic Shuffle 50048). 0 `[p4]` access lines, 0 `[ocr]` lines, 0 `DMAC: Ch2`
    lines in the six boots: none of these games uses a P4 mirror page, an
    on-chip RAM address that changes bank, or a ch2 DMA that the rules
    change. Each game left with a clean exit to the XMB.
  - Border Down (JIT IDLE RULE X3 BACKWARD, set by Matt: "Speed is wrong
    because I put the speed at x3 REVERSE", so the speed does not count):
    66 of 76 windows with renders, 34 renders/s; about 390K store-queue
    flushes/s to RAM.
  - Sonic Shuffle at X30: 97 of 112 windows with renders, SPEED 99.2% as
    the mean; 36.5K flushes/s to the TA and 77.7K to RAM. 24-25 renders/s,
    19.9 in the scenes over 4K vertices (at SPEED 100%). A second boot at
    X3 BACKWARD: SPEED 80% in the menus and 56% in scenes of 0.5-4K
    vertices, 14 renders per wall second there. Matt: "It runs better at
    x30. But It's too heavy for now." One more game for bundle 21 (the
    idle rule, heavy scenes).
  - Ikaruga at X30: 3068 renders, 0 dropped, 47 renders/s, SPEED 99.1% as
    the mean. 5 of 67 windows are slow (75-91%) at 11.7-20.1K vertices:
    `[pace] slept=0`, 952K flushes/s (543K to the TA, 409K to RAM) at 336
    ns each, `sq path 15.2%`. LMMODE1 = 1 from the game, but no SQ block
    and no DMA went to VRAM by the 32-bit bus; `FbWatch=0`, `fb=1 fbw=0`.
  - Rez at X30 (NTSC 60): 105 of 110 windows with renders, 43 renders/s,
    SPEED 99.0% as the mean. 11 windows are slow; 8 of them are 87-95% at
    10.8-12.3K vertices and 55-57 renders/s: `[pace] slept=0`, 849K
    flushes/s, all to RAM, at 174 ns each, `path 11.2%`; the TA data goes
    by ch2 DMA (15 MB/s). No SQ block to VRAM in this boot (`v64=0 v32=0`),
    VRAM DMA in 7 windows with LMMODE0 = 0 (the old path). So LMMODE had
    nothing to change in Rez here.
  - Sega Rally 2 (Windows CE): `MMUCR write 00000005`, `ldtlb 0/0`, `MEM
    remap [0] : virt=0x00005800 -> phys=0x0C131800`, then `Unimplemented
    opcode` and boot ROM writes: it needs the full MMU (P38), as the trap
    below says. Matt: "Windows CE games still hang uh."
  - The plan's condition for P74 is met: Ikaruga and Rez are SH4-bound in
    their heavy scenes, and the old chain takes 15.2% and 11.2% of the SH4
    thread there. Not built: Matt 2026-10-13, "we will check the Ikaruga
    and Rez issues later on" (bundle 21, item 7).
- The cause is confirmed with the PS3 compiler
  (`misc/mmu_check/bitfield_ppu.sh`, the build image's ppu-g++ at -O2):
  the MMUCR bitfield reads AT from bit 4, TI from bit 6, SV from bit 12,
  SQMD from bit 13, URC from bits 19-14 and URB from bits 25-20. Only LRUI
  is at its bits. So the emulator never sees a game turn the MMU on.
- A new MEMORY page (page 11 of 11; a `[memcfg]` line at boot and at a
  menu change):
  - MMUCR Bits: OLD / FIXED (set before boot). FIXED:
    - The fields come from masks on the register word, at the manual's
      bits (AT 0, TI 2, URC 15-10, URB 23-18). The readers are the MMUCR
      write, `ldtlb`, the interpreters' PREF and the JIT (PREF target,
      JIT PREFETCH, JIT GUARD REUSE). OLD reads the bitfield as before.
    - A write keeps the real bits only (0xFCFCFF05, Flycast).
    - An AT change clears the JIT code cache, on both edges (Flycast): the
      JIT reads AT when it compiles a block.
    - TI clears the V bit of each TLB entry and keeps the store-queue
      remap table (Flycast's `mmu_flush_table`). OLD clears the table too.
      The reason: no TLB miss comes to the emulator, so nothing loads a
      cleared slot again. P58's text counted "the table is not cleared" as
      a fault; that part is not taken.
    - A UTLB entry in the store-queue area with V = 0 leaves its remap
      slot as it is (Flycast's `UTLB_Sync` without FAST_MMU).
    - A UTLB or ITLB data-array write with address bit 23 goes to the
      assistance word (data array 2), not into PPN and V (Flycast,
      Reicast). The reads are as before (Flycast returns data array 1).
  - P4 Mirrors: OFF / ON (set before boot). ON maps VRAM at 0xE4-0xE7 and
    main RAM at 0xEC-0xEF, after the P4 handler (Flycast 156aafe95). OFF:
    those pages read 0 and drop writes, as before. The other pages of
    Flycast's mirror hold nothing in our map (areas 2, 4, 5, 6).
  - Cache RAM Banks: OLD / FLYCAST (set before boot). FLYCAST: the on-chip
    RAM is two 4 KB banks; the bank is address bit 13, or bit 25 when
    CCR.OIX is set. OLD takes the low 13 address bits, so 0x7C000000 and
    0x7C002000 are the same bytes (on the SH4 they are bank 1 and bank 2).
  - Ch2 DMA Rules: OLD / FLYCAST (live). FLYCAST:
    - The target is (SB_C2DSTAT & 0x03FFFFE0) | 0x10000000 and the length
      is SB_C2DLEN & 0x00FFFFE0, applied when the DMA starts (Flycast
      masks at the register write; the registers' read-back is not
      changed here).
    - Nothing is refused: an odd length is cut, a zero length ends at
      once, both with TE and the end interrupt. OLD returns with no
      interrupt and SB_C2DST still 1.
    - 0x12xxxxxx is the TA's mirror (polygons and YUV). OLD moves no data
      there.
    - A DMAOR write keeps bits 15, 9, 8 and 2-0 (0x8307). This also
      helps the PVR and GD-ROM DMAs, which test the same register.
- Bundle 1's rows for these games stay on VIDEO / DEBUG, with no code
  change: LMMODE (test: Rez), FB Write Watch (test: Ikaruga), VRAM Direct.
- Not built: the flat store-queue flush for AT = 1 (P74). The plan takes
  it only if one of the four games is SH4-bound after the fix. Until then
  a flush with AT = 1 takes the old chain (`do_sqw_mmu`): TA SQ Direct and
  RAM SQ Direct do not act for these games.
- Log:
  - `[mmu] MMUCR Bits OLD | self-test 0000A905: bitfield AT=0 TI=0 SV=0
    URC=2, masks AT=1 TI=1 SV=1 URC=42` at boot. These numbers are the
    proof of P58 on the PS3 itself; other bitfield numbers mean that the
    compiler's layout is not the one of `bitfield_ppu.sh`.
  - `CCN: MMUCR write V (was W): AT= TI= SV= SQMD= URC= URB= LRUI=, acts
    on AT= TI= (pc=)`: the first 64 writes, in both row states. The values
    and their order in the four games were not known.
  - `CCN: MMU address translation ENABLED` and `recSh4: cache clear` at an
    AT edge with FIXED.
  - `SQ remap [n] slot= : virt= -> phys= V=` (V is new), and with FIXED
    `SQ remap [n] slot= : virt= not valid, slot stays 0x...`.
  - `[p4] P4 Mirrors OFF` at boot, and `[p4] read32 at 0xEC......: P4
    Mirrors is OFF, no memory there (pc=)`: the first 8 accesses to a page
    that the row maps. No old log has this (the old build was silent).
  - `[ocr] 0x7C002000 OIX=0: offset 0x0000 with OLD, 0x1000 with FLYCAST;
    Cache RAM Banks is OLD (pc=)`: the first 8 accesses where the two rules
    differ.
  - `DMAC: Ch2 length cut`, `Ch2 ends at once`, `Ch2 target 0x12...... is
    the TA mirror`: the first of each kind (each one with OLD, as before).
- Code: `dc/sh4/ccn.h` (the masks, `mmucr_at()`, `mmucr_urc()`,
  `mmucr_urb()`), `dc/sh4/ccn.cpp` (`CCN_MMUCR_write()`,
  `ccn_mmucr_selftest()`), `dc/mem/mmu.cpp` and `mmu.h`
  (`mmu_InvalidateTLB()`, `UTLB_Sync()`, the assistance word),
  `dc/mem/sh4_internal_reg.cpp` (the F3 and F7 writes, `p4_stray_note()`,
  `ocr_offset()`), `dc/mem/sh4_mem.cpp` (the mirror), `dc/sh4/dmac.cpp`
  (`DMAC_Ch2St()`, `WriteDMAOR()`), `dc/sh4/sh4_cpu_misc.h`,
  `dc/sh4/rec_v2/shil_canonical.h`, `ps3/ps3_driver.cpp` (the readers),
  `ps3/ps3_menu.cpp`, `ps3/ps3_presets.cpp`.
- Traps:
  - A game that turns AT on then sends every store-queue flush through
    its TLB entries. A game with AT = 1 and no entry for an address
    writes to area 0 (`Write to Boot ROM`).
  - After an AT edge the running block goes on, and the blocks it is
    linked to are still the old code until the next block lookup (how
    long that is was not measured). A PREF there takes the old target.
  - Windows CE games (Sega Rally 2 in the logs: `ldtlb 0/0`) need the
    full MMU (P38). FIXED does not make them run.
  - Looney Tunes writes UTLB entry 0 with zeros 100 times at boot (`MEM
    remap [0]`). Its MMUCR writes are not known: the regression run with
    FIXED tells.
- PC checks, WSL:
  - `misc/mmu_check/run.sh`: PASS. It builds the marked sections of the
    five real sources twice: the new code, and the code of commit 27f2a65
    (`ref_*.inc`). 300 cases of 400 register operations (MMUCR writes,
    TLB array writes and reads, store-queue addresses, on-chip RAM, ch2
    DMAs): with every row OFF the two give the same state after each
    operation, and both agree with a model. With the rows ON at random
    (the live rows also change in play) the new code agrees with the
    model: 4549 AT edges, 3898 TI writes, 1937 V = 0 entries kept, 3053
    data array 2 writes, 2610 on-chip RAM accesses that change bank, 2729
    ch2 DMAs to the TA (1124 to 0x12), 715 odd and 736 zero lengths. Then
    45 source line checks (the JIT and interpreter readers, the mirror,
    the rows, the defaults).
  - `misc/mmu_check/mutate.py`: 75 of 75 mutants fail.
  - `misc/mmu_check/bitfield_ppu.sh` (Git Bash with Docker, not WSL):
    PASS, 19 values.
  - The 14 older checks that name a changed file: PASS (`div32_check`,
    `carry_check`, `constprop_check`, `clock_check`, `exit_check`,
    `jit8_check`, `sqstore_check`, `sysgate_check`, `tcopy_check`,
    `timebase_check`, `pad_check`, `rtt_check`, `screen_check`,
    `ta4_check`). Their mutation runs were not repeated. `ramsq_check`
    names `sh4_mem.cpp` and does not build since bundle 4 (not repaired).
  - The PC is little endian: there the old bitfield has AT and TI at the
    right bits. So the PC check proves "OFF is the old code" and "ON is
    the model"; the wrong bits of the PS3 build are proved by
    `bitfield_ppu.sh` and by the boot self-test line.
- Link: `r2off` 0, `.got` 0xE2F8 (0xE180 before; the limit is 0x10000). No
  warning in the changed files.
- Not checked on the PC: the JIT cache clear inside a running block (the
  same call as the CCR I-cache invalidate, which each boot runs), the
  flushes of the four games through `do_sqw_mmu`, the memory map of P4
  Mirrors, the menu page.
- HW steps (UDP listener on port 18194, only when Matt says so). An
  install keeps the settings; every new row starts at its first value.
  1. Front menu, OPTIONS, page MEMORY (the last page): MMUCR Bits FIXED.
     Leave the other three rows as they are.
  2. Boot Border Down. Expected: a picture after the BIOS logo. Log:
     `[mmu] MMUCR Bits FIXED | self-test ...` with the numbers above, 50
     `SQ remap` lines (the old logs have them), `CCN: MMUCR write` lines
     of which one has `acts on AT=1`, then `CCN: MMU address translation
     ENABLED` and a `recSh4: cache clear`. No `Write to Boot ROM` line
     (the old logs have 11592 and 45848 of them). Not known: if the
     writes come before or after the `SQ remap` lines.
  3. The same with Rez, Ikaruga and Sonic Shuffle (old logs: 50 `SQ
     remap` lines each; Sonic Shuffle 50048 `Write to Boot ROM` lines).
  4. No change expected, MMUCR Bits still FIXED: Crazy Taxi, Sonic
     Adventure, Looney Tunes (boot, the intro video, a race).
  5. Bundle 1's rows, when their game runs. Rez: VIDEO / DEBUG -> LMMODE
     ON (live); the textures must stay right; log `[vram] LMMODE0=
     LMMODE1=` and `sq/s ... v64= v32=` above 0. Ikaruga: FB Write Watch
     ON; the loading screens; log `fbw=`.
  6. No test game is known for the other three rows. The old logs have no
     refused ch2 DMA in any game, and the old build did not log the P4
     pages or the on-chip RAM. A boot of any game with the rows OFF now
     shows `[p4]` and `[ocr]` lines if the game uses those paths. Flycast
     names NHL 2K2 for P4 Mirrors and Cache RAM Banks.
  7. On a fault: the row back to its first value; note the game and
     when.

**Bundle 12 (Screen format and test tools) built 2026-10-12 (Matt: "Work
on Bundle 12"), HW SUCCESS 2026-10-12 after HW run 1 (Matt: "Success!").
Package of 21:37 on 2026-10-12 (`build/notdc_ps3.pkg`, 1599472 bytes) =
commit 701647a + this bundle. Matt named no default: every row is at its
old path by default, so with the defaults the build acts as before. The
one thing that is always there: SQUARE in the in-game menu takes a
screenshot. No HW run yet: the screenshot, 16:9 STRETCH, Cheats, a render
that WIDE keeps at 4:3, a game that Screen Scaler changes.**
- **HW run 1, 2026-10-12 ~22:22-22:28: SUCCESS** (Matt: "Success!").
  Log: `sa_anim1.txt` of session d13d658e (its listener held the port),
  lines 333514-344369; the cut is `b12_run1.txt` in the scratchpad of
  session 65dd1ebb. One boot, Sonic Adventure, 311 s, a clean exit, no
  fault line.
  - `[scrcfg] boot: FpsOverlay=1 ScreenFormat=2 ScreenScaler=1 Cheats=0`.
    `[outcfg]`: Region Scissor ON, Keep Last Frame ON, RTT Read-back
    FIXED, the other rows as in bundle 11's runs.
  - FPS Overlay ON at boot: the fonts were loaded a second time at the
    renderer's start (`[ui] ready` twice), and the text was on the screen
    from the first frame.
  - 16:9 WIDE: `[scr] format=2 scaler=1 fps=1 | wide=150 narrow=0
    canvas=640x480 shots=0` in play (61 windows): every render was drawn
    wide, none was kept at 4:3. 9443 renders, 0 dropped. Region Scissor
    cut none (`scissor=0`).
  - Screen Scaler ON: the canvas stayed 640 x 480. The game does not use
    the scaler's vertical factor or pixel double, so this is the no-harm
    run.
  - SPEED median 99.99%; 267 of 311 seconds at 99% or more. Render busy
    median 14%, largest 37%. Seconds 27-32 are at 66-90%: the boot of the
    old build in the same log file (line 251699, the package of 19:52)
    has the same dip (68-88% for 4 s), and the render thread is at 2%
    busy there. So it is not from this bundle.
  - Two in-game menu visits, no row changed, no SQUARE: `shots=0`.
- A new SCREEN page (page 10 of 10; a `[scrcfg]` line at boot and at a
  menu change):
  - FPS Overlay: OFF / ON (P50, live). The text that SPG builds once a
    second ("FPS" and "SPEED") is drawn at the top of the left side bar,
    as four short lines over a black box. It is on every frame that goes
    to the TV: a render and a 2D screen. With the row ON at boot the menu
    fonts are loaded again at the renderer's start (the front end frees
    them at launch); a change in the in-game menu acts at once.
  - Screen Format: 4:3 / 16:9 STRETCH / 16:9 WIDE (live).
    - STRETCH (P69, and P36's stretch): the same picture on every column
      of the TV, renders and 2D screens. This is the format for a game
      that a widescreen cheat makes anamorphic.
    - WIDE (P53, Flycast `rend.WideScreen`): the 4:3 picture stays where
      it is, and the canvas gets more columns at both sides (at 720p:
      -106.7 to 746.7). The scissor is the whole TV. A render whose own
      clip narrows x (Flycast `isClipped()`: the region tiles and the FB
      clip times the scaler's X factor do not span the TA's width) keeps
      the 4:3 picture; if it also keeps the last frame's colour, its side
      bars are cleared. 2D screens and renders to texture stay 4:3.
    - Region Scissor (bundle 11) acts as before in every format; in WIDE
      it leaves the sides open and cuts only the lines.
  - Screen Scaler: OFF / ON (P10, live). At each render the canvas also
    follows SCALER_CTL's vertical factor when the mode has no interlace
    (Flycast `getPvrFramebufferSize`: a 480-line render into a 240-line
    screen draws 480 lines) and VO_CONTROL bit 8 (Reicast: pixel double,
    the canvas is half as wide). X Scaler and FB Line Double act as
    before.
  - Cheats: OFF / ON (P36, set before boot). ON reads
    `/dev_hdd0/data/NotDC/cheats/<image name with no extension>.cht` (or
    `.txt`) at launch and applies it at each vblank. The file is
    RetroArch's form, as Flycast reads it: `cheats`, and per cheat
    `cheatN_address`, `_value`, `_cheat_type` (1 set, 2 increase, 3
    decrease, 4-7 run the next cheat if equal, not equal, greater, less,
    8 copy), `_memory_search_size` (5 = 32 bits), `_enable`,
    `_repeat_count`, `_repeat_add_to_value`, `_repeat_add_to_address`,
    `_dest_address`, `_address_bit_position`. Addresses are RAM offsets
    (0x8C000000 + address). Values can be decimal or 0x hex. The folder is
    made at the first launch with the row ON.
- Screenshot (P51, no row): SQUARE in the in-game menu (not in the disc
  browser) closes the menu; the next frame that is drawn for the TV is
  read back before the FPS text and the flip, and written as
  `/dev_hdd0/data/NotDC/screenshots/shot_NNNN.png` (the first free number;
  RGB, fast deflate). The area is the picture: 960 x 720 at 4:3, 1280 x
  720 where the picture takes the whole TV. The read-back is always the
  one that returns (bundle 11's FIXED path), whatever RTT Read-back says.
- Log: `[scr] format= scaler= fps= | wide= narrow= canvas= shots=` per
  stats window when a row is on; `[shot] saved <file> (WxH, read-back
  done, N ms)`; `[cheat] <file>: N cheat(s), M enabled`, one line per
  cheat (16 at most), and `[cheat] N write(s) in 300 frames`.
- Code: `ps3/ps3_screen.h` (the view, the clip rule, the scaler, the text
  lines, the PNG rows), `ps3/ps3_cheats.h` (the file and the apply loop),
  `ps3/ps3_cheats.cpp`, `ps3/ps3_shot.cpp` (libpng write, with the 512-byte
  jmp_buf), `ps3/ps3_rend.cpp` (`draw_frame()`, `present_fb_draw()`,
  `frame_extras()`, `SetFpsText()`), `ps3/ps3_ui.cpp`
  (`ps3ui_overlay_begin()`), `ps3/ps3_video.c` (`ps3_video_grab()`),
  `ps3/ps3_menu.cpp`, `ps3/ps3_presets.cpp`, `plugs/drkPvr/SPG.cpp` (the
  cheats' vblank call), `ps3/ps3_main.cpp` (the load).
- Not built, and why:
  - P10's FB clip as a scissor: it is Region Scissor of bundle 11 (the
    same Flycast rule, `intersectTileAndFBScissor`). Nothing was left.
  - Flycast's built-in widescreen table and its other per-game cheats: a
    list per game (Matt's rule). A code from that table works as a file
    (HW step 5).
  - A cheat list in the menu, GameShark code entry: the file's `enable`
    key is the switch.
  - Our two limits that Flycast does not have: 256 cheats per file, and a
    repeat count of 65536 at most.
- Traps:
  - WIDE: games cull at 4:3, so the sides can show missing geometry and
    2D parts that end at 640.
  - Cheats that patch code: the JIT does not see a change in a block it
    has compiled. A code that patches data (the widescreen codes write
    floats) acts at once; the JIT reads no memory at compile time.
  - A screenshot holds the render thread for the read-back and the file
    write; frames drop for that time.
  - On a 1080-line display (the fallback mode) the picture is over the
    4 MB staging buffer: `[shot] FAILED`, no file.
- PC checks, WSL:
  - `misc/screen_check/run.sh`: PASS. The view on six displays (WIDE keeps
    every column of the 4:3 picture at its place, and is Flycast's matrix
    on a 16:9 display); the clip rule against Flycast's `isClipped()`,
    200 K cases; the scaler against `getPvrFramebufferSize`; the text
    lines; the PNG rows; the cheat file against Flycast's `IniFile` and
    `loadCheatFile` (written there in Flycast's form), 60 K random files
    and Flycast's own test file; the apply loop against Flycast's
    `apply()`, 60 K random cheat lists over two vblanks; 113 source line
    checks. `mutate.py`: 178 of 178 mutants fail.
  - `misc/rtt_check/run.sh`: PASS (one of its lines names the new
    `clip_view_cut` call), `mutate.py`: 107 of 107. `pad_check`, `rend4_check`, `ta4_check`,
    `tr4_check`, `bump_check`, `fog_check`, `spepix_check`,
    `sysgate_check`, `timebase_check`, `clock_check`: PASS (one stub line
    for the cheats' vblank call).
  - Two older checks do not build, and not from this bundle:
    `misc/ramsq_check` (`s_flat_ok`, the TA Data Gate of bundle 4) and
    `misc/spevb_check` (its `ClipView` has no base scissor, bundle 11).
    Not repaired here.
- Link: `r2off` 0, `.got` 0xE180 (0xDB10 before: libpng's writer and
  deflate; the limit is 0x10000).
- Not checked on the PC: every RSX and GL call (the overlay inside a game
  frame, the read-back in the middle of a frame, the wide viewport), the
  PNG file, the cheat file's read, the memory writes.
- HW steps (UDP listener on port 18194, only when Matt says so). An
  install keeps the settings; every new row starts at its first value.
  1. FPS Overlay. Front menu, OPTIONS, page SCREEN: FPS Overlay ON. Boot
     Crazy Taxi. Expected: "FPS", a number, "SPEED", a number in the left
     black bar, new once a second, and the game as before. Then SELECT:
     the menu opens and closes as before. Log: `[scr] ... fps=1`.
  2. Screenshot. In the game: SELECT, then SQUARE. Expected: the menu
     closes, the picture stops for a moment, the game goes on. Log:
     `[shot] saved /dev_hdd0/data/NotDC/screenshots/shot_0001.png (960x720,
     read-back done, N ms)`. Then get the file (FTP) and look at it: the
     game's picture, right colours, no FPS text.
  3. Screen Format, same boot: SELECT, SCREEN, 16:9 STRETCH, back.
     Expected: the same picture over the whole TV. Then 16:9 WIDE:
     expected: the middle as at 4:3, more scene at the sides. Crazy Taxi
     and Sonic Adventure. Log: `[scr] format=2 ... wide=N narrow=0`. A
     pause in Crazy Taxi (RTT Read-back FIXED first) and a 2D screen
     (the boot logo) must look as before.
  4. Screen Scaler ON, same games. No game that needs it is known (Flycast
     names Cho - Hatsumei Boy Kanipan), so this is a no-harm run: the
     picture as before, and `[scr] ... canvas=640x480`.
  5. Cheats, with Looney Tunes - Space Race (US). The file
     `/dev_hdd0/data/NotDC/cheats/Looney Tunes - Space Race (US)[DCRES].cht`
     (the code is Flycast's widescreen code for T15116N):
     ```
     cheats = 1
     cheat0_desc = "Widescreen"
     cheat0_enable = true
     cheat0_cheat_type = 1
     cheat0_memory_search_size = 5
     cheat0_address = 0x2198EC
     cheat0_value = 0x3F400000
     ```
     Cheats ON and Screen Format 16:9 STRETCH, then boot the game. Log:
     `[cheat] ...: 1 cheat(s), 1 enabled` and `[cheat] N write(s)`.
     Expected: the 3D scene has the right shape on the 16:9 TV (at 4:3 it
     looks thin). If the picture does not change, the rip's code is at
     another address: the row then works (the log) but the code does not.
  6. On a fault: the row back to its first value; note the game and
     when.

**Bundles 10 (Controllers) and 11 (render to texture and present) built
2026-10-12 (Matt: "Let's go with Bundle 10 and 11 together."), HW SUCCESS
2026-10-12 after HW runs 1 and 2 (Matt: "It's okay on that. We can
consider this a success."). Package of 17:40 on 2026-10-12
(`build/notdc_ps3.pkg`, 1553328 bytes) = commit 68def3c + both bundles.
Matt named no default for these rows: each one keeps its old path as the
default (the one exception is Keep Last Frame, in the next paragraph),
so with the defaults the build acts as before, and the Crazy Taxi pause
still freezes until OUTPUT -> "RTT Read-back" is FIXED. Left, not
planned: the Looney Tunes boot videos (cause not known), the rows with no
HW run (Present Rule, RTT Size TILE CLIP, Stick Dead Zone RADIAL, Rumble
Pack, a button-map change). Next in the recommended order: 13.**

**The fix for the Looney Tunes race loading screen, built 2026-10-12
after the verdict (Matt: "Wait, about the Looney Tunes loading screen...
Fix it. It's in this bundle, right?"): HW SUCCESS 2026-10-12 (Matt:
"Success! The image stays."). Row OUTPUT -> "Keep Last Frame" (OFF / ON,
live), it acts with Region Scissor ON. Package of 19:52 on 2026-10-12
(1553584 bytes) = the package of 17:40 + this row. Default ON in source
since 2026-10-12 (Matt: "Success! Keep it up as default."), not
packaged: the package of 19:52 has OFF. Region Scissor is still default
OFF, so with the defaults alone the row does nothing; Matt has not named
a default for Region Scissor. The entry "Keep Last Frame" at the end of
this block has the code, the checks and the HW runs.**
- Bundle 10, the CONTROLS page (it scrolls now; a `[padcfg]` line at
  boot):
  - Controller: STANDARD PAD / FULL (2 STICKS) (GOALS.md G12). FULL is
    Flycast's full controller: capability word 0xffff3f00, the name
    "Dreamcast PantherDC Controller", X2/Y2 from the DS3 right stick, the
    two VMU slots. `[ps3] Controller` is 0 or 1; `get_controller_type()`
    gives 5 for FULL. The 0xF901 button mask stays (G12 step 3). Set
    before boot.
  - Stick Dead Zone: AXIS (each axis cut below 20, as before) / RADIAL
    (P84: a 10% radial zone with a rescale, then the magnitude limit of
    128; on both sticks). Read at each poll.
  - Rumble Pack: OFF / ON (P13). ON plugs Flycast's Puru Puru device in
    slot 2 of each port, so the second VMU is not plugged. The DS3's
    large motor takes the power; the stop time and the falling slope run
    at the controller polls; the in-game menu and the exit stop the
    motors. `[pad] dcN rumble power= slope= ms= motor=` shows the first
    16 commands. Set before boot.
  - 15 button-map rows, Cross to R3 (GOALS.md G10 step 3): `[ps3]` keys
    MapCross ... MapR3 with the values NONE, A, B, X, Y, START, the four
    D-pad directions, L TRIGGER, R TRIGGER. The defaults are the old
    fixed map. L2/R2 keep their pressure on a trigger. C, Z and D are not
    offered (P84: "If Matt wants them"; the mask would have to go).
  - Code: `ps3/ps3_padmap.h` (the map and the stick),
    `dc/maple/maple_puru.h` (the SetCondition decode),
    `ps3/ps3_input.cpp`, `dc/maple/maple_devs.cpp`, `maple_cfg.cpp`.
- Bundle 11, a new OUTPUT page (an `[outcfg]` line at boot and at a menu
  change):
  - RTT Read-back: OLD / FIXED (P75). FIXED: the three straight-line
    transfer calls, then a wait on a new reference value with a 2 s limit
    (`[vid] the RSX did not reach reference` on a miss). OLD is PSL1GHT's
    `rsxSetTransferData`, which never returns.
  - RTT Scaler: OFF / ON (P66, Flycast `8d4188159`): SCALER_CTL scales a
    render to texture (hscale halves the width, vscalefactor / 1024
    divides the height).
  - RTT Size: FB CLIP / TILE CLIP (P76). TILE CLIP is Flycast's rule: the
    scene is the region array's tiles inside TA_GLOB_TILE_CLIP, and the
    FB clip cuts the copy.
  - RTT Pack: OLD / FLYCAST (P54, P66, P76): the 1555 alpha bit from
    fb_alpha_threshold, channels rounded, each pixel at its clip
    position, the row cut at FB_W_LINESTRIDE, no copy in pack modes 4-7,
    the target cleared to alpha 0.
  - Draw Retry: OFF / ON (P76): a render to texture waits up to 250 ms
    for a free buffer; a 2D present that found none is tried again at the
    next vblank.
  - FB Line Double: OFF / ON (P76): the bit halves the canvas height. It
    acts at the next video mode write: set before boot.
  - Blank Video: OFF / ON (P76): a 2D present with VO_CONTROL bit 3 shows
    VO_BORDER_COL.
  - Present Rule: ALL / SHOWN / + KEEP COLOUR (P60). SHOWN: a render is
    flipped at its end only if FB_R_SOF1 holds its FB_W_SOF1; else it
    stays in the back buffer, and the game's FB_R_SOF1/2 write flips it
    (`FbShown()`, called from `plugs/drkPvr/regs.cpp`). + KEEP COLOUR: a
    render over a frame that waits does not clear the colour, unless its
    buffer is not one of the last two (Flycast's rule). With the rule on,
    a render to texture draws on its own RSX surface (3.7 MB of RSX
    memory, always allocated), not in the back buffer. Frame Hold places
    no flip for a frame that waits.
  - Region Scissor: OFF / ON (P60): a render draws only inside the region
    array's tiles and the FB clip (the clip times the scaler's factors).
  - Keep Last Frame: OFF / ON (P60, added in the package of 19:52): a
    render that Region Scissor cuts starts from a copy of the last flipped
    frame, not from black (the entry at the end of this block).
  - FB Reads: OLD / FLYCAST (P26): two interlaced fields one line apart
    are read as one image, else the shown field; 240 lines at the 13.5
    MHz clock; 24-bit frames are converted (on the PPU).
  - Log: `[rtt]` lines give the registers and the geometry of the first
    render to texture and of each new set (24 lines at most), and each
    read-back's start and end (8 times). `[out] rtt_wait= held= shown=
    keep= scissor= retry= blank=` comes per stats window when a row is on.
  - Code: `ps3/ps3_rtt.h` (geometry, packing, the 2D read),
    `ps3/ps3_video.c`, `ps3/ps3_rend.cpp`, `plugs/drkPvr/regs.cpp`,
    `SPG.cpp`.
- Not built, and why:
  - P26's fb_concat bits: the SPU FB convert has no such step, and a rule
    on the PPU only would break SPE FB Convert CHECK.
  - P54's writers for pack modes 4 and 5: P76 found that no reference
    renders to texture in modes 4-7.
  - P60's colour that stays from one shown frame to the next: Flycast has
    one target, the PS3 flips three buffers. The colour stays only over a
    frame that waits, as the P60 outline says. Built later the same day
    for the renders that Region Scissor cuts: row "Keep Last Frame" (the
    entry at the end of this block).
- Found in the old logs (every `[rend]` window of every scratchpad log,
  split per boot):
  - `rtt=` is 0 in all games. The Crazy Taxi pause is the only render to
    texture seen.
  - Looney Tunes (62 of 66 boots, at about t=72 s) and Quake3.cdi print
    `24-bit framebuffer read not handled`: both have a 24-bit 2D screen
    that is not shown today. That is the HW test for FB Reads.
  - Most 2D presents: Rayman 2 (368), Looney Tunes (206), Crazy Taxi
    (154, one per boot).
- PC checks, WSL:
  - `misc/pad_check/run.sh`: PASS. The default map against the old code
    for every set of buttons; the stick against Flycast's code (1 step
    apart at most, 2 at the rim: Flycast cuts the 16-bit value, ours
    rounds); the pack decode against Flycast's code, 250 M words; 45
    source lines. `mutate.py`: 37 of 37 mutants fail.
  - `misc/rtt_check/run.sh`: PASS. The packing against Flycast's line
    writers; the geometry against the old code, Flycast's size, matrix
    and copy rules, and a model of the scaler and the FB clip, 3 M cases;
    the 2D read against Flycast's ReadFramebuffer, 2 M cases; 24-bit
    pixels; 80 source line checks. `mutate.py`: 88 of 88 mutants fail.
  - `misc/rend4_check`, `timebase_check`, `clock_check`, `sysgate_check`:
    PASS with the new code. The last three did not link since bundle 9
    (no stub for `arm_p49_dump`); one stub line added.
- Link: `r2off` 0, `.got` 0xDB10.
- Not checked on the PC: every RSX call (the transfer, the RTT surface),
  the render thread's flip protocol, the pad and motor calls.
- HW steps (UDP listener on port 18194, only when Matt says so). An
  install keeps the settings; every new row starts at its first value.
  1. Run 1, the freeze. OUTPUT -> RTT Read-back FIXED. Crazy Taxi: START
     in a drive, resume, START again. Expected: no freeze, both times;
     the pause background can be wrong. Log: the `[rtt]` lines (the
     registers), `read-back done in N ms`, `rtt=N/N` in `[rend]`.
  2. Same boot, SELECT menu (the rows are live): RTT Scaler ON, pause
     again. If the background is still wrong: RTT Size TILE CLIP, then
     RTT Pack FLYCAST; one change per pause.
  3. Run 2, present. Present Rule SHOWN, then + KEEP COLOUR, Region
     Scissor ON. Crazy Taxi: the play looks as before, and `[out] held=`
     equals `shown=`. Quake3.cdi, split screen with 2 pads: both halves
     stay on the screen.
  4. Run 3, 2D screens. FB Reads FLYCAST, Draw Retry ON, Blank Video ON.
     Looney Tunes and Quake3.cdi from boot: a screen that was not shown
     appears, and the line `24-bit framebuffer read not handled` is gone.
  5. Run 4, controllers. CONTROLS -> Stick Dead Zone RADIAL: Crazy Taxi,
     the steering. Controller FULL (2 STICKS): Quake3.cdi, the right
     stick. Rumble Pack ON: Crazy Taxi (not known if it sends commands:
     the `[pad] dc0 rumble` lines tell). One map change, for example L3
     to START.
  6. On a fault: the row back to its first value; note the game and
     when. RTT Read-back OLD at a render to texture is the old freeze.
- **HW run 1, 2026-10-12 ~17:57-18:13: the Crazy Taxi pause is a
  SUCCESS** (Matt: "Tested Crazy Taxi and it works as intended. Looney
  Tunes still doesn't display the videos. Various games crashed. I still
  have to try Quake 3 Arena with the dual stick setup."). Package of
  17:40. Log: `sa_anim1.txt` of session d13d658e (its listener held the
  port) from line 196291; the cut is `b1011_run.txt` in the scratchpad of
  session 27a662ce. Five boots, each with a clean exit and no fault line.
  - `[outcfg]`, the same in all five: RTT Read-back FIXED, RTT Scaler ON,
    RTT Pack FLYCAST, Draw Retry ON, FB Line Double ON, Region Scissor
    ON, FB Reads FLYCAST; RTT Size FB CLIP, Blank Video OFF, Present Rule
    ALL. `[padcfg]`: every CONTROLS row at its default.
  - Crazy Taxi: two pauses, no freeze. `[rtt] sof1=01530080
    ctrl=00000009 stride=1024 fbclip=0..511,0..480 glob=640x480
    tiles=0..640,0..480 scaler=00401`: a 512 x 481 target in 565, no
    scaling (the factor is 1025). `read-back done in 0.5 ms` and `0.7
    ms`. SPEED median 100.0% over the 71 s. Region Scissor cut 40
    renders in two windows; Matt named no picture fault.
  - Looney Tunes (PAL 50 at boot), what the log shows:
    - The 24-bit screen is presented now: 2 presents by the PPU convert
      at second 13, and no `24-bit framebuffer read not handled` line.
      It is one still screen just before the intro video.
    - The intro video (seconds 14-25): 640 x 480 in a 1024 x 512 YUV
      texture, 16800 macroblocks/s = 14 frames/s, 64 decodes and 249
      renders per window. The boot of 02:45 (the old build,
      `b8_hash2_lt_x30.txt`) has 60-67 decodes and 250-253 renders
      there: the same.
    - Seconds 96-135: 2-3 renders per second, each one cut by Region
      Scissor (31 of 31 in a window), 32 translucent strips with a tile
      clip, the disc worker at 25 fills/s, no texture decode, no YUV
      DMA, no store queue or DMA to VRAM, `fb=0`. My reading (not
      proven): the game writes a video straight into the framebuffer
      and the PVR draws only an overlay; our renderer never shows that
      framebuffer. FB Write Watch (bundle 1, never exercised) was OFF
      (`FbWatch=0`). Open: which videos Matt means; a run with FB Write
      Watch ON.
  - Capcom vs. SNK 2: 10 renders/s after the boot, 25 s, then the exit.
    Ikaruga and Border Down: 0 renders after the BIOS logo, the SH4
    runs on. These are the open faults of bundle 21 (the menus) and
    bundle 13 (MMU). Not known: if Matt means these three by "crashed",
    and if there were more games (the log holds five boots).
  - Not run yet: Present Rule, RTT Size TILE CLIP, Blank Video, every
    CONTROLS row (Quake III Arena with FULL: Matt, later).
- **HW run 2, 2026-10-12 ~19:15-19:29: the second stick is a SUCCESS**
  (Matt: "Quake 3 double sticks works"). Same log file, lines
  224133-237900; cuts `b1011_run2.txt` and `b1011_lt2.txt`. Package of
  17:40.
  - Quake3.cdi, Controller FULL (`[padcfg] Controller=1`): 18 `joy2=`
    lines, the right stick from -60 to 112 and from -90 to 46. No
    `24-bit framebuffer read not handled` line. SPEED median 99.9%. This
    is GOALS.md G12 step 5; Crazy Taxi and Looney Tunes ran with STANDARD
    PAD as before.
  - Looney Tunes, FB Write Watch ON, Blank Video ON, FB Reads FLYCAST
    (the test for the videos; Matt: "It's at boot."):
    - 7 2D presents in the boot; 6 of them were blanked (`blank=3`, `1`,
      `2`): VO_CONTROL has the video blanked while the game shows those
      frames, the 24-bit ones too. So the 24-bit screen of run 1 is not
      a picture. The watch caused 2 presents (`fbw=2`) in 130 s.
    - So the game writes no video into the framebuffer at boot: my
      reading of run 1 was wrong for the boot. The boot videos are the
      YUV frames of seconds 16-27 (`dma KB/s yuv=` 3500-7500, 50 renders
      per second, 53 SPU decodes per window, one opaque strip over
      0..640, 0..480 with the YUV texture, 230-251 flips per window).
      The same lines as in `ps3log_p42_run1.txt` of 2026-10-07, when
      Matt saw the video ("Perfect."). The log does not show why the TV
      has no video. Not known: what the TV shows there, and since which
      build. On this path since 2026-10-07: SPE Tex Decode (the SPU
      decodes this texture), Anisotropic 16x, Fog TABLE, and today's
      rows.
  - Looney Tunes, a second fault (Matt: "when loading a race it would
    display a picture and then a black frame (except the loading bar)
    until getting in-game"). The log fits: while the race loads, the
    game sends 2-3 renders per second and Region Scissor cuts each one
    (run 1: 31 of 31 in a window; run 2: 18, 8, 26). The game redraws
    only the tiles of the bar and counts on the rest of the framebuffer.
    We clear each new back buffer. This is the part of P60 that is not
    built (the colour that stays from one shown frame to the next); with
    Region Scissor OFF the background plane paints the whole screen, so
    the old builds must show the same (inferred). A fix needs the last
    shown frame under such a render, for example one RSX copy into the
    back buffer before a render that Region Scissor cuts. Not built:
    Matt 2026-10-12, on the question to build it now: "It's okay on
    that."
  - "The three games that crashed: frozen at boot. Couldn't exit to
    XMB." (Matt). The log has a quit request and a clean exit for each of
    Capcom vs. SNK 2, Ikaruga and Border Down. Likely (not checked): a
    frozen game sends no new frame, and the PS3 draws its PS-button menu
    over new frames only. The freezes are bundle 13's and bundle 21's.
- **Verdict, Matt 2026-10-12: "It's okay on that. We can consider this a
  success."** ("It's okay on that" answered the question to build the
  loading-screen fix now; Matt asked for that fix in his next message,
  see the entry below.) Both bundles are done. No default changes: Matt
  named none, so every row stays at its old path in source. Validated on
  HW: RTT Read-back FIXED (the Crazy Taxi pause) and Controller FULL
  (Quake III Arena). Ran with no fault that Matt named, in the same runs:
  RTT Scaler, RTT Pack FLYCAST, Draw Retry, FB Line Double, Region
  Scissor (but see the race loading screen above), FB Reads FLYCAST,
  Blank Video. No HW run: Present Rule, RTT Size TILE CLIP, Stick Dead
  Zone RADIAL, Rumble Pack, a button-map change. The Looney Tunes boot
  videos stay open with no cause.
- **Keep Last Frame, built 2026-10-12 after the verdict (Matt: "Wait,
  about the Looney Tunes loading screen... Fix it. It's in this bundle,
  right?"). It is: the part of P60 that was not built. Package of 19:52
  (`build/notdc_ps3.pkg`, 1553584 bytes). HW SUCCESS 2026-10-12 (HW run
  3, the last item of this entry).**
  - The fault (HW runs 1 and 2): while the race loads, the game sends 2-3
    renders per second that draw only the tiles of the loading bar. On a
    Dreamcast the rest of the framebuffer keeps its picture. Region
    Scissor cuts each of these renders, and each new back buffer is
    cleared, so the screen is black but the bar.
  - The fix: row OUTPUT -> "Keep Last Frame" (OFF / ON, live, `[ps3]
    KeepFrame`; default OFF in the package of 19:52, ON in source since
    the HW runs). ON: when Region Scissor cuts a render, the
    RSX copies the last flipped display buffer into the new back buffer
    (`ps3_video_carry()`: one transfer inside RSX memory with the three
    straight-line calls of the read-back, mode LOCAL_TO_LOCAL, the whole
    buffer), and the colour clear is left out. The depth and the stencil
    are cleared as before. A render with no pixel left in its scissor
    then shows the old frame again.
  - Not a source: a menu frame (`ps3ui_end()` forgets the last frame, so
    the first cut render after the menu starts from black, as today) and
    a render to texture (it never flips). A 2D present is a source.
  - It does nothing with Region Scissor OFF (no render is cut), for a
    render of the whole screen, and over a frame that waits (Present Rule
    + KEEP COLOUR draws in place there).
  - Cost: one copy of 3.7 MB (1280 x 720) inside RSX memory per cut
    render. Not measured. Crazy Taxi had 40 cut renders in two stats
    windows of HW run 1.
  - Log: `[outcfg]` has `KeepFrame=`; the `[out]` line has `carry=` (cut
    renders that started from the last frame) after `blank=`.
  - Code: `ps3/ps3_video.c` (`s_last_fb`, `ps3_video_carry()`,
    `ps3_video_forget_frame()`), `ps3/ps3_rend.cpp` (`draw_frame()`: the
    region box is found before the clear), `ps3/ps3_ui.cpp`, the row and
    the key.
  - PC checks, WSL: `misc/rtt_check/run.sh` PASS (95 source line checks
    now); `mutate.py`: 107 of 107 mutants fail (19 new). `misc/rend4_check`
    PASS. Link: `r2off` 0, `.got` 0xDB28. Not checked on the PC: the RSX
    copy itself.
  - HW steps. An install keeps the settings, so RTT Read-back FIXED and
    Region Scissor ON are still set from HW run 2.
    1. OUTPUT -> Keep Last Frame ON. Region Scissor stays ON.
    2. Looney Tunes: start a race. Expected: the loading picture stays
       under the loading bar until the race starts.
    3. Crazy Taxi, a short drive and one pause: the picture as before.
    4. Log: in the `[out]` lines while the race loads, `carry=` equals
       `scissor=` (one less for the first cut render after the menu).
    5. On a fault: Keep Last Frame OFF; note the game and when.
  - **HW run 3, 2026-10-12 ~20:00: Looney Tunes, Keep Last Frame ON,
    Region Scissor ON: SUCCESS (Matt: "Success! The image stays.").**
    Package of 19:52 (`[outcfg] ... RegionScissor=1 FbReads=1
    KeepFrame=1`). Log: `sa_anim1.txt` of session d13d658e from line
    237901; the cut is `b1011_run3.txt` in the scratchpad of session
    27a662ce. Three stats windows have cut renders: `scissor=9 carry=9`,
    `12` and `12`, `31` and `31`, so each cut render started from the last
    frame. `job_max` is 2-3 ms in these windows. No fault line, no `[vid]
    the RSX did not reach reference` line. Five more windows of the same
    boot, read later: `scissor=` and `carry=` are 15, 12, 11, 11, 15.
  - **HW run 4, 2026-10-12 ~20:10: Crazy Taxi, Keep Last Frame ON, Region
    Scissor ON: SUCCESS (Matt: "Success! Keep it up as default.").** Same
    log file, lines 244773-251489; the cut is `b1011_run4.txt`. A boot of
    149 s with a clean exit: cut renders in two windows (`scissor=1
    carry=1`, `scissor=58 carry=58`), two pauses (`read-back done in 0.3
    ms`, twice), no fault line.
  - **Default ON in source since 2026-10-12 (Matt), not packaged.**
    `misc/rtt_check/run.sh` checks the new default. An install keeps the
    old settings, so the default acts only on a new install. Region
    Scissor stays default OFF (Matt named no default for it), and Keep
    Last Frame does nothing without it.

**Built 2026-10-12, HW SUCCESS on the picture (Ecco the Dolphin: run 2 at
+ VERTEX, Matt: "Everything looked right!"; run 3 at TABLE, Matt: "No
graphical glitches."); runs 1-3 have no fault. Default TABLE in source
since 2026-10-12 (Matt: "Let's set it to TABLE."), not packaged: the
package of 12:33 and the other session's later ones have OFF.
3a, fog (section 3; reference
`OPTIMIZATION_POTENTIAL.md` P14). Matt 2026-10-12: "In the distance,
objects often have a "pop-in". Ecco the Dolphin heavily uses fogs though.
I suppose It's tied? Work on those, in case." Package of 12:33 on
2026-10-12 (`build/notdc_ps3.pkg`, 1541872 bytes) = commit a7c19df + the
fog. Row RENDERER -> "Fog": OFF (default in those packages) / TABLE /
+ VERTEX, live.**
- **HW run 1, 2026-10-12 ~14:04-14:10: Sonic Adventure, Fog TABLE from the
  boot, then + VERTEX from the in-game menu** (Matt: "I ran a fog test with
  Sonic Adventure if you wanna check it."; `sa_anim1.txt`, scratchpad of
  session d13d658e, from line 109356; the package is the other session's of
  14:02, 1542320 bytes, which holds the fog and its ARM7 changes).
  - It runs: `Shaders loaded: 24 modes`, 375 s, 54 windows (26 at TABLE, 28
    at + VERTEX), `drop=0`, no fault line, clean JIT EXIT.
  - The game: in the level 1394-2164 strips per render have FogCtrl 0
    (97%), the HUD and the menus have FogCtrl 2. `fogv=0`, `ppu=0`: no
    vertex fog, so + VERTEX changes nothing here.
  - The registers in the level: FOG_COL_RAM 000000, FOG_DENSITY FA0C =
    8000 exactly (the decode gives a round number), table `FF9F 9F4B ..
    0000 0000`. Entries 0 and 1 fit a linear fog from W 6750 to W 8000
    (my fit from two entries; the log holds no other). The farthest vertex
    of each window is at W 2300-4950. So the fog value is 0 on all the
    geometry: no change of the picture is expected, and this run cannot
    show if the fog looks right. It also says that the pop-in in this
    level is not the fog's: the objects appear at W under 5000.
  - Cost, level windows (4000+ vertices): SPEED mean 100.0%, lowest 98.3%.
    Render thread busy 16.5% with 897 draws per render; the 7 boots before
    it in the same log (older packages, no fog, 874-920 draws) have
    14.2-15.6%. State + draw cost per 1000 draws: 6.8 against 5.6-6.2. So
    about +1 to +2 points; the settings of the boots differ, not a
    same-load compare. 3 table uploads in the whole run.
  - Open after run 1: a game whose fog reaches its geometry (run 2).
- **HW run 2, 2026-10-12 ~14:22-14:25: Ecco the Dolphin, Fog + VERTEX from
  the boot** (Matt: "I did an Ecco the Dolphin run, in the meantime.";
  same log file, lines 125542-129569; the package of 14:02). 133 s, no
  fault line, `drop=0`, clean JIT EXIT. No Fog OFF part and no word from
  Matt on the picture, so the log shows what the fog must do, not how it
  looks.
  - The level (from t=106 s, about 27 s): the opaque strips (670-1973 per
    render, all but 0-2) and 457-775 of the 478-1318 translucent strips
    have FogCtrl 0.
    FOG_COL_RAM 3CA09E, FOG_DENSITY FF07 = 255, table `FFFF FFFF .. 0000
    0000`. The fog is full from z = 1.125 down, that is from W 227 on; the
    opaque geometry goes to W 850-950, the translucent to W 1876. So
    all that is farther than W 227 must be the plain fog colour: objects
    must come out of the haze, not pop in. Here the fog reaches the
    geometry; Matt's "heavily uses fogs" is right.
  - The 3D scene before it (t=73-86 s): FOG_COL_RAM 066E64, the same
    density and table ends; geometry at W 11-180, so partly fogged.
  - Vertex fog: only the logo and menu screens have it (1-10 strips per
    render with FogCtrl 1 and the Offset bit, W 4.4-5.1; FOG_COL_VERT
    7F7F7F, then 000000). Those renders went to the PPU replay (`ppu=` 64,
    251, 141, 1, 205 per window), so the vertex-fog programs ran too. The
    level has none (`fogv=0`, `ppu=0`): TABLE is enough for it.
  - Cost: SPEED 99.7-100.5% in every 3D window. Render thread busy 19-31%
    at 15-20K vertices and 1080-2120 draws per render (no OFF number for
    this game). 4 table uploads in the run.
  - Seen beside the fog, not looked into: the level draws 18-27 frames
    per second at SPEED 100% (RPS), with JIT IDLE RULE X30.
  - Matt, asked what the far water, the logos and the menus looked
    like: "Everything looked right!" So table fog (the level) and vertex
    fog (the logo and menu screens) are right on hardware in this game.
  - Open after run 2: the default level (Matt), and one Fog OFF minute
    in the same scene for the cost (run 3 has 37 renders of it).
- **HW run 3, 2026-10-12 ~15:19-15:23: Ecco the Dolphin, Fog TABLE from
  the boot; in the level Fog OFF for about 2 s of play, then TABLE again**
  (Matt: "Gonna test Ecco the Dolphin with TABLE now. So we can check it
  out", then "Done. No graphical glitches."; same log file, lines
  187856-193301; the package of 14:02). 237 s, no fault line, `drop=0`,
  clean JIT EXIT, SPEED 99.0-101.1% in the 3D scenes.
  - The same scenes and numbers as run 2: `fog=` 1127-2733 opaque and
    translucent strips with table fog, FOG_COL_RAM 066E64 then 3CA09E.
  - `ppu=0` in all 27 windows: at TABLE no scene goes to the PPU replay.
    The logo and menu screens still have their 1-10 vertex-fog strips
    (`fogv=`); they draw with no vertex fog, and Matt saw no glitch.
  - + VERTEX against TABLE, the same scenes of runs 2 and 3: the state +
    draw cost per 1000 draws is the same (2.22 / 2.22, 2.24 / 2.18, 2.13 /
    2.14, 2.05 / 2.06 ms). The level has no vertex fog, so + VERTEX costs
    nothing there.
  - Fog OFF against TABLE: the OFF part is one window with 37 renders
    (the menu was open for most of it): 2.02 ms per 1000 draws, with the
    log's rounding +-0.16. The 17 fog windows with 800+ draws have
    2.05-2.35 (mean 2.17). So the fog's cost is under what this sample
    can show: at most about 0.15 ms per 1000 draws, under 1 point of the
    render thread at 1300 draws and 25 renders per second.
  - Smoothness (Matt: "Did the game run smoothly or"), the 49 s of the
    level: SPEED 99.0-100.5%, sound with no silent, dropped or late
    block, no dropped render, render jobs 4-18 ms. But the game draws
    17.8-33.9 frames per second, mean 25.4: its frames are a mix of 2 and
    3 vblanks (`[vid] held`, for example `2=75 3=45`, `2=37 3=73`), so
    the motion cannot be even. Not the fog: 22.7 with Fog OFF, 22-24
    beside it. Not the PS3: `[pace] slept` 36-61 of 60 calls, render
    thread 5-26%. The game needs more emulated SH4 time than 2 vblanks
    give. Not known: what a real Dreamcast does in this scene. I named
    JIT IDLE RULE X3 BACKWARD as a row to try (this run has X30; `[jit8]
    idle rule 0 billed=5500`), as for Sonic Adventure and PSO.
  - **HW run 4, 2026-10-12 ~15:29-15:31: Ecco the Dolphin, JIT IDLE RULE
    X3 BACKWARD, Fog TABLE** (Matt: "x3 backward runs worse."; same log
    file, lines 193304-196288, 87 s, the first 3D scene only). The log
    agrees: SPEED 47-51% (VPS 28-30.5) for the 22 s of the scene, 15.6-17
    frames per second, `[pace] slept=0`, the sound silent in 94-96 of 95
    blocks per second. The same scene at X30 (run 3): SPEED 100%, 29-34
    frames per second, `slept` 59-61. So Ecco is a game that X3 breaks,
    as Looney Tunes and Quake 3: it has no host time for it. X30 stays
    its level. My hint was wrong for this game: the count of sleeps at
    X30 did not show how little host time was left. The uneven frame
    rate at X30 stays open, with no row to name for it.
  - Close: `g_fog_preset = 1` in `ps3/ps3_presets.cpp` (Matt: "Let's set
    it to TABLE."). An install that has the `Fog` key keeps its value.
    No build by this session after the change.
- Not known yet: if the pop-in Matt sees is the missing fog. No saved log
  holds a fog count (no code read `FogCtrl` before). From this package the
  third `[rendz]` line of each list shows it, with the row OFF too:
  `fog=a/b/c/d fogv=n` = strips per FogCtrl (0 table, 1 vertex, 2 none,
  3 table as the base colour) and the strips the vertex fog acts on.
- What the row draws (Flycast `core/rend/dx9/d3d_shaders.cpp` pp_FogCtrl;
  Reicast `glesrend.cpp` is the same):
  - TABLE. FogCtrl 0: colour = lerp(colour, FOG_COL_RAM, table(FOG_DENSITY
    / W)), per pixel, after the texture. FogCtrl 3: (FOG_COL_RAM, table
    value) is the base colour before the texture. Bump polygons take the
    FogCtrl 0 fog too (Rayman 2's overlay).
  - + VERTEX. FogCtrl 1 with a texture, the Offset bit and no bump map:
    colour = lerp(colour, FOG_COL_VERT, offset alpha). Only the PPU replay
    keeps offset colours (3j's design A), so a scene with such a polygon
    decodes and builds on the PPU, as Rayman 2's bump scenes do
    (`[fog] ppu=`). That is why it is a level of its own.
  - Not built: the offset colour add (3b), the colour clamp
    (FOG_CLAMP_MIN/MAX), FogCtrl 3 on a bump polygon (it draws as before).
- How:
  - `RenderRegs` takes FOG_COL_RAM, FOG_COL_VERT, FOG_DENSITY and the 128
    table words at STARTRENDER, only with the row on. They are native u32.
  - One 512 x 4 linear texture on TMU 1 (`fog_upload()` in
    `ps3/ps3_rend.cpp`; written again only when a register changes). Rows
    0-1: FOG_COL_RAM and the table's value at z = 2^(8 (k + 0.5) / 512).
    Rows 2-3: FOG_COL_VERT. So the fragment program reads the table at
    s = saturate(-log2(W / density) / 8): one LG2, no floor, no pow, and
    no fragment constant to patch. The saturate is the PVR's z clamp.
  - `fog_vp.vcg` is `bump_vp` + texcoord0.z = position.z x fogp.x.
    position.z is W; fogp.x = 1 / FOG_DENSITY, a VP constant per scene.
    The RSX interpolates it with perspective: that is the PVR's 1/W per
    pixel. The SPU's vertices need no change.
  - 14 fragment programs from one source (`fog_fp.fcg`, `compile_fog.sh`,
    `ps3gl_shader_fog.h`; `ps3gl_shader_data.h` is not touched): table fog
    x (no texture, modulate, replace, decal, 3 bump), vertex fog x 3, base
    colour x 4. `ps3gl_dc_fog(mode)` picks the key (`ps3gl_shaders.c`
    `fog_key()`); `apply_param_body()` sets the mode per polygon.
  - New on hardware: LG2, a second texture unit, texcoord0 with 3
    components. The rest is what the bump programs use. The binaries were
    decoded (opcodes, units, masks, the `fogp` constant at c[4]).
- PC checks (WSL): new `misc/fog_check/run.sh` PASS: the tree's texture
  and scale code, read as the two programs read them, against Flycast's
  `fog_mode2` (3000 tables, 1.2M lookups; worst difference 1.15 of 255 on
  fog curves; a jump in the table blurs over one texel = 1/64 octave of
  z). `misc/fog_check/mutate.py` 14 of 14. `bump_check`, `spevb_check`,
  `rend4_check` (rows off = the old GL calls), `ta4_check` PASS with the
  row off. `misc/bump_check/mutate.py` 31 of 31 again. The suites of
  `rend4_check`, `ta4_check` and `ramsq_check` were not run again: each
  of their 109 patterns on `ps3_rend.cpp` is still there once.
- Build: no warning.
- HW steps (package of 12:33; the install keeps the settings; UDP listener
  on port 18194, only when Matt says so):
  1. Ecco the Dolphin, Fog OFF: play to a scene with the pop-in, stay
     10 s. The log gives `fog=` for the scene.
  2. Same scene: SELECT -> RENDERER -> Fog TABLE. Look at the distance.
  3. Same scene: Fog + VERTEX. Look again.
  4. One game Matt knows well (Crazy Taxi or Sonic Adventure) with
     Fog + VERTEX for 1-2 minutes, and Rayman 2's first level (bump maps
     with fog): nothing must change for the worse.
  From the log: `fog=` and `fogv=` (which level the game needs), `[fog]`
  (registers, `uploads=`, `ppu=`), `[rend] busy=` and SPEED against the
  Fog OFF minute.
- Risks for the first run, none seen on a PC: the RSX with the table on
  unit 1; one VP switch per change between a fog and a no-fog polygon
  (cost not measured); a table write while queued draws still read it
  (one frame of the new fog, early).

**Bundle 9 CLOSED 2026-10-12, HW SUCCESS. F1 FIXED (Matt: "THE SOUND IS
FIXED"): the ChuChu Rocket slow effects and echo end with AUDIO -> "ARM7 STR
Align" ON (section 7 bundle 9, 6.8's unaligned store). The row is default
ON in source since 2026-10-12 (Matt: "If it yields positive results, we
close this and we set it as default ON"), not packaged: the package of
09:34 on 2026-10-12 (`build/notdc_ps3.pkg`, 1537712 bytes) has it OFF;
Matt's install has it ON. HW runs 1-5: ChuChu Rocket (OFF, ON), Crazy Taxi,
Sonic Adventure, Looney Tunes. The P49 fix is not built: the census found
no P49 form in the four games. The census and the op tests stay in.**
- **HW run 3, 2026-10-12 ~11:21: ChuChu Rocket, ARM7 STR Align ON, JIT IDLE
  RULE X30** (`b9_run1.txt`, from line 18184; scratchpad of session
  d13d658e). `[sndcfg] ... Arm7StrAlign=1`. Matt: "THE SOUND IS FIXED".
  - The boot op tests pass with the row ON too (816 checks, 0 failed):
    the aligned path of `arm_mem.cpp` is right on hardware.
  - The driver itself acts in another way: `unaligned str` is 113, 271,
    954, 1334 in the first four 5 s windows, where run 1 (OFF) had 119,
    1923, 7461, 15948 and then ~1930 per second. So with OFF a scattered
    store broke the driver's own data, and it then stored there on each
    tick. The first addresses are the same (0x0001103A).
  - SPEED: 39 windows so far, mean 99.52%, none under 97% after the boot.
    No fault line. Census still A 0, B 0.
- Cause of F1, as found: an ARM7 word store to an address that is not a
  multiple of 4. The ARM7DI ignores the low 2 address bits and writes the
  aligned word (Flycast `arm_mem.h` `writeMem`: the same). Our three
  engines wrote 4 bytes from the unaligned address (nullDC's rule), so
  2 bytes went to the wrong place and 2 bytes of the next word were lost.
  The read side was already right (aligned word, rotated). Not an engine
  decode fault: P40's tests pass on all three engines.
- **HW run 4, 2026-10-12 ~11:24-11:31: Sonic Adventure, ARM7 STR Align ON,
  JIT IDLE RULE X3 BACKWARD** (same log, lines 22288-44177). Matt: "Yes,
  Sonic Adventure sounded correct. I noticed that some animations looked
  off, though." (the animations: point 12 of the list under Bundle 8 in
  section 7, CLOSED the same day by Matt: no fault, the game looks
  clunky as it is). Boot lines as before (0 failed). Census: A 0,
  B 0, **unaligned str 0** over 7237 instructions: this driver makes no
  unaligned word store, so the row cannot change this game. SPEED: 365
  windows, mean 99.18%; the windows under 97% are at boot (10 in the first
  27 s) and at loads with no frame drawn (one window at 19.4% at t=250 s:
  a stall of about 0.8 s, cause not looked for). No fault line, clean JIT
  EXIT.
- **HW run 5, 2026-10-12 ~11:32-11:35: Looney Tunes, ARM7 STR Align ON,
  JIT IDLE RULE X30** (same log, lines 44418-55804). Matt: "Done." (no
  word on the sound; "Last but not least I will also test Looney Tunes").
  Boot lines as before (0 failed). Census: A 0, B 0, **unaligned str 0**
  over 5127 instructions: the row cannot change this game. SPEED: 198
  windows, mean 99.73%; under 97%: the boot and loads (6 windows in the
  first 43 s), the demo at 28K vertices (4 windows, 90.7-96.3%) and 4 race
  windows at 22-34K vertices (92.6-97.0%): the known SH4-bound scenes
  (point 11 under Bundle 8). No fault line, clean JIT EXIT.
- Close: `g_arm7_str_align_preset = 1` in `ps3/ps3_presets.cpp`. An
  install that has the key keeps its value. Of the four games only ChuChu
  Rocket makes unaligned word stores; the other three cannot change with
  the row. Left as they are: the `[p49]` census (log only, for the next
  games) and the boot op tests. The listener of this session is stopped.

**Built 2026-10-12, HW runs 1-2 done (ChuChu Rocket, Crazy Taxi): section 7
bundle 9 (ARM7 engines), first part (Matt: "proceed with bundle 9"). Package
of 09:34 on 2026-10-12. It also holds the source defaults of the blocks
below (JIT IDLE HASH and JIT SQ COPY stay OFF).**
- **HW run 1, 2026-10-12 ~11:12-11:15: ChuChu Rocket, ARM7 STR Align OFF,
  SPE ARM7 ON, JIT IDLE RULE X3 BACKWARD (Matt: "Just for curiosity"; his
  install also has `idlehash=1 sqcopy=1`)** (`b9_run1.txt`, scratchpad of
  session d13d658e). Matt: "Done." No fault line, clean JIT EXIT.
  - Boot lines: `[ARM7TEST] PASS: all 45 tests passed.`, `op tests ...
    816 checks in 338 runs, 0 failed`, `[ARM7JIT] self-test PASS`. The PPU
    ARM7 JIT passes P40's tests and the STR cases on hardware. So all
    three engines pass: P40 found no engine fault.
  - Census: A 0 and B 0 over 42278 instructions looked at. ChuChu
    Rocket's sound driver has no P49 form: the P49 fix has no target here.
  - **Unaligned word stores: 91282 in about 65 s, a steady ~1930 per
    second, about 2 per FIQ (FIQ 996/s). The first 4 are all at 0x0001103A
    (address & 3 = 2).** With the row OFF each one scatters to bytes
    3A-3D; the ARM7DI and Flycast write the word at 0x11038. This is the
    first difference from Flycast found in what this driver's code does,
    and it is in or near its 1 kHz tick: a candidate for F1 (section 6).
    Not known yet: the storing instruction and what the word holds.
  - SPEED at X3 BACKWARD: 80 windows, mean 99.46%; the 4 windows under
    97% are all in the first 17 s (BIOS, load, the PAL to NTSC switch).
    No dip in play.
  - **HW run 2, 2026-10-12 ~11:17-11:21: Crazy Taxi, ARM7 STR Align OFF,
    JIT IDLE RULE X30** (same log file, lines 5908-17630). Matt: "Crazy
    Taxi is done." Boot lines as in run 1 (0 failed). Census: A 0, B 0,
    unaligned str 0 over 7197 instructions (no new decode after the first
    minute). Crazy Taxi's driver has no P49 form and no unaligned store:
    the row changes nothing there. SPEED: 213 windows, mean 99.71%; under
    97% only at boot (2), once at 38 s and in one 17-20K vertex scene (4
    windows, 87.9-95.8%), the known dense-scene cost. No fault line.
  - Next: one boot of ChuChu Rocket with ARM7 STR Align ON. In the log:
    `Arm7StrAlign=1` in `[sndcfg]`; the `unaligned str` count rises as
    before (the stores are counted, then aligned).
- Built:
  1. The P49 census, log only, in the three decoders. `a7_p49_form()`
     (`plugs/vbaARM/arm7_p49.h`) is the one rule; the hooks are in
     `arm7_cached.cpp` (block decode), `arm7_jit.cpp` (`compile_block`) and
     `ps3/spu/arm7_spu.h` (`a7_decode`). Form A: data processing, shift
     amount in a register, Rn = PC (MOV and MVN have no Rn). Form B: STR or
     STRB of PC. NV never runs and is not counted. The SPU puts its counts
     in a new block (`ArmCensus`, `ArmSetup.census_ea`) when a count moves
     or each 1024 instructions; `arm_spe_stats` prints them.
  2. P40 as self-tests: `plugs/vbaARM/arm7_optest.h`, Flycast's
     `AicaArmTest.cpp` line by line as a script (13 groups), and a 14th
     group for 6.8: STR and STRB forms, one byte lane each, unaligned STR.
     Two passes: step (one instruction per slice) and block (the ops in one
     slice, which ends in a `b .`; a PC write is stepped in both). 408
     checks in 169 runs per pass. `arm_RunSelfTests()` runs both passes
     after the 45 binaries, so the PPU ARM7 JIT gets them at boot.
  3. Row AUDIO -> "ARM7 STR Align" (`Arm7StrAlign`), default OFF, set
     before boot: an unaligned ARM7 word store writes the aligned word, as
     the ARM7DI and Flycast (`arm_mem.h` `writeMem`). OFF scatters 4 bytes
     (today). In `arm_mem.cpp` `WriteMemArm` (cached interpreter, and the
     JIT's slow store path) and in `spe_arm7.cpp` `a7_wr32`
     (`ARM_SETUP_STR_ALIGN`). The unaligned stores are counted with the
     census, and the first 4 addresses kept.
- Not built, as the plan says: the P49 fix and its two self-tests. They
  wait for the census. The 45 binaries and the op tests hold no form A or B
  instruction (PC check: 0 decodes), so the boot self-test does not touch
  the census.
- Log:
  - Boot: `[ARM7TEST] PASS: all 45 tests passed.`, then `[ARM7TEST] op
    tests (Flycast AicaArmTest + STR cases), step and block: 816 checks in
    338 runs, 0 failed`, then `[ARM7JIT] self-test PASS`. A failed check
    prints `[ARM7TEST]   FAIL <group> run <n>, op <opcode>, step|block:
    ...` (first 8), and the JIT falls back to the cached interpreter (old
    rule).
  - Every ~5 s when something moved (at least once a minute while the
    ARM7 decodes new code): `[p49] spu census: scanned <n> insns | A <n>
    decodes (<sites> sites) | B <n> (<sites>) | unaligned str <n>`. Each
    new site once: `[p49] spu form A (dp, Rn=pc, shift by reg): pc
    <addr> op <opcode>`, `[p49] spu form B (str pc): ...`, `[p49] spu
    unaligned str: addr <addr>`. With SPE ARM7 OFF the same lines say
    `ppu`. `scanned` above 0 shows that the census runs.
  - `[sndcfg]` has `Arm7StrAlign=`.
- PC checks, all PASS:
  - `misc/arm7spu_check/run.sh` (WSL): the op tests on arm7.cpp +
    arm7_cached.cpp and on arm7_spu.h, 0 failures over 6528 checks (2
    cores x step/block x 2 reset modes x the row off/on). The census:
    `a7_p49_form` against a second reading of the rule over 20M random
    words and 17 fixed words; the cached decoder's census equals the SPU
    core's (full map); the list rules. The 8192-entry build has the same 3
    known mismatches as before.
  - `misc/arm7spe_check/run.sh` (WSL, after `build_spu.sh`): the real SPU
    image in the simulator, 50 runs: the 46 old seeds (the census block is
    now compared after each batch), seed 7 with the row ON (2525 unaligned
    stores) and seed 9 with the row ON and the three Looney Tunes fixes
    (2382), `optest 0` and `optest 130`: the 45 conformance binaries (for
    the first time on the SPU image) and the op tests, 816 checks, through
    the SPU's command ring.
  - Mutations: `misc/arm7spu_check/mutate_ops.sh` 26 of 26 (15 engine
    faults with no random program: 8 of them only the op tests catch, the
    45 binaries do not: HI, BL's link, STM of PC, STR writeback before and
    after, a rotated immediate's carry, STRB, a store with a false
    condition; 11 census faults); `misc/arm7spe_check/mutate.sh`, the 9 new
    ones (`try9`), 9 of 9; `misc/arm7spu_check/mutate.sh` 10 of 10 (its
    copy of arm7_spu.h now keeps the tree's shape for the new include).
  - No PC check runs the PPU ARM7 JIT: its op tests are the boot lines.
- Result so far: Flycast's tests pass on the cached interpreter and on the
  SPU engine (x86 build and the real SPU image). P40 found no engine fault
  there. Open: the JIT (boot lines) and the census.
- Build: no warning in the files compiled; `.got` 0xD780, 0 `r2off`; the
  ARM7 SPU image uses 224920 of 262144 bytes of LS (0.5 KB more).
- HW test (rows as they are, ARM7 STR Align OFF, SPE ARM7 ON):
  1. Boot ChuChu Rocket, play 1-2 minutes with music and effects.
  2. Boot Crazy Taxi, drive 1-2 minutes.
  3. If there is time: Sonic Adventure and Looney Tunes (more sound
     drivers for the census).
  Then, from the log: A or B above 0 in a game -> build the P49 fix (own
  row, 2-3 lines per engine, `gen_arm7_cached.py`, the two self-tests
  `orr r0,pc,r1,lsl r2` and `str pc,[r1]`). `unaligned str` above 0 in a
  game -> one boot of that game with ARM7 STR Align ON. All 0 -> the P49
  fix has no target in these games; Matt decides if it is built.

**Built 2026-10-12, HW run 25 done, no verdict: 5.46 JIT SQ COPY, the
Sonic Adventure 2 fix (Matt: "Work on the Sonic Adventure 2 fix. Looney Tunes is still
better at x30 sadly."). Package of 02:30 on 2026-10-12
(`build/notdc_ps3.pkg`, 1531696 bytes); it also holds the second Looney
Tunes entry and the `[idlehash]` log fix of the block below.**
- Matt on Looney Tunes with the package of 02:17 or 02:30 (2026-10-12):
  "x30 is still preferred for Looney Tunes, although it has microstutters
  that didn't appear while on x3 backwards." My reading: the stutter at
  X30 is the frames the game drops in emulated time (27.3 of 30 per
  second, runs 6 and 20); X3 draws 29.9 but runs 12% slow (run 19).
- Matt runs Sonic Adventure 2 with JIT SQ COPY ON only ("We already have
  enough samples with it off by default."): the compare is against runs
  21 (90.6% at 14K+) and the X3 run of 2026-10-11 (92.5%).
- **HW run 25, 2026-10-12 ~02:40-02:45: Sonic Adventure 2, X3 BACKWARD,
  JIT SQ COPY ON, JIT HOTBLOCKS OFF, package of 02:30** (`[jitcfg]
  sqcopy=1`, `[ta-split] sqcp=25` sites; `b8_sqcopy_sa2.txt`, scratchpad
  of session 1c8776dd). Matt: "Done." No fault line, clean JIT EXIT. No
  word from Matt on how it looked. The OFF runs are run 21 (today) and
  the X3 run of 2026-10-11, other play each time:

  | SPEED by vertices (windows) | SQ COPY ON | OFF, run 21 | OFF, 2026-10-11 |
  |---|---|---|---|
  | 3-8K | 100.0% (52) | 100.3% (44) | 100.2% (85) |
  | 8-14K | 100.0%, sleep 71 ms/s (58) | 99.2%, 50 (61) | 99.8%, 55 (52) |
  | 14-16K | 98.8% (7) | 95.6% (10) | 100.9% (4) |
  | 16-18K | 97.3%, sleep 56 ms/s (22) | 93.2%, 3 (21) | 95.6%, 0 (15) |
  | 18-20K | 90.8% (9) | 88.8% (15) | 90.9% (10) |
  | 20K+ | 89.0% (8) | 84.8% (13) | 87.5% (13) |
  | 14K+, all | 94.8%, min 85.2, sleep 27 (46) | 90.6%, min 72.8, sleep 2 (59) | 92.5%, min 83.4, sleep 1 (42) |

  - Reading: a gain of about 1.5 to 3 SPEED points at the same load,
    under the 3 to 5 of the estimate. The two OFF runs differ by about 2
    points from each other, so the gain is at the edge of the run-to-run
    spread; the sleep at 16-18K (56 ms/s against 0-3) is the clearest
    sign. Above 18K the SH4 thread still has no time left (sleep 0).
  - Not done: a JIT HOTBLOCKS run with the row ON, to see `coldin/s` of
    8C141BBA at 0. No verdict from Matt on the default.
- **HW run 24, 2026-10-12 ~02:20-02:35, package of 02:17: two Looney
  Tunes boots, X3 BACKWARD and X30, JIT IDLE HASH ON** (`b8_hash2_lt_x3.txt`,
  `b8_hash2_lt_x30.txt`; Matt's verdict on Looney Tunes above came
  after them). The log fix works: both entries print (`8C19D168` and
  `8C014544`, `our list: ends the slice`, `listed=2`). No fault line,
  clean JIT EXITs.
  - The demo at X3: SPEED 96.9% and 99.8% in its 2 windows (27-28K
    vertices; Matt left the demo then), 67.7% before the second entry.
    The menus after it: 99-101%. A small sample.
  - The demo at X30: 92.7-96.5% in 4 windows (89.1% in run 20, 96.9% in
    run 6). The races: X3 85.8% (35 windows, 27.4K vertices), X30 99.8%,
    26.9 frames per second (92 windows): as runs 19 and 20.
- Matt 2026-10-12, after run 25: "So for Sonic Adventure 2 there's no
  hope? And what about Space Race's stutters?" Read from the logs, no new
  run (`frame_cmp.py`, scratchpad of session 1c8776dd):
  - The Looney Tunes race costs the same SH4 work per frame at both
    levels: 23.0M JIT host instructions per rendered frame at X30
    (`run_prof.txt`, 2026-10-08, census 4 to 25) and 23.2M at X3 + hash
    (run 22, census 2 to 11); no block of the X3 top 64 runs more often
    per frame. So the level only picks what the shortfall looks like:
    X30 drops frames (27.3 of 30, the stutter), X3 slows the game (88%).
  - Not explained: at X30 the SH4 thread sleeps 55-108 ms/s in the race
    while frames drop, and the host time per frame is 33-35 ms at X30
    against 37 ms at X3 (no profile). The sound control step (`[sys]
    arm`) is 29 ms/s at X30 and 50 ms/s at X3. A bill between the two
    levels could turn the sleep into frames: not tried.
  - Where the SH4 thread's time goes when it is host-bound (Sonic
    Adventure 2 at 18K+, SPEED 89.9%, 17 windows of run 25): JIT code
    about 77%, the store-queue call into the TA 11.4% (1.24M calls/s,
    170 ns each; 7.8-8.1% in the Looney Tunes race), `[sys]` 10.6% (of
    it the sound control step 6.1%). Sonic Adventure 2 above 18K lacks
    about 10 SPEED points at X3; the Looney Tunes race lacks about 12.
  - Candidates named to Matt, none built: a number row for the idle
    rule's factor (a test for the Looney Tunes stutter), the store-queue
    call per vertex (both games), 5.23 `fmov.s @Rm+` pair fusion (both
    vertex loops read with it).
  - **OPEN ISSUE, PARKED by Matt 2026-10-12: "Save this for the future
    as an open issue. We'll get back to it at the end."** It is point 11
    of the list under "Bundle 8" in section 7. Do not start a candidate
    before Matt comes back to it. Left as they are: JIT IDLE HASH and
    JIT SQ COPY default OFF, no verdict on either default; the package
    of 02:30; no commit.
- Why: HW run 23 (below). The game's vertex loop 8C141BBA is 22-26% of
  the JIT's hot instructions at 14K+ vertices, and each of its 5
  store-queue stores per vertex fails the RAM test and goes out of line
  (`coldin/s` 5.0 per turn). JIT SQ STORES wants a `pref` on the store's
  own register in the block; JIT SQ MISS's look-ahead wants it in the
  first 6 ops of a successor. This loop stores through r6 and does `pref
  @r3; mov r6,r3`.
- What: row JIT SPEED -> "JIT SQ COPY" (`JitSqCopy`), default OFF, set
  before boot. `sqcp_guess()` (`ps3/ps3_jit_sqla.h`): the block PREFs
  through Rm, and Rm's only writer in the block is `mov Rn,Rm` with Rn
  the store's base; no interpreter op and no SR sync in the block. It
  only picks the shape (store-queue test inline, RAM in the cold path):
  `emit_sq_check` still tests the address, so a wrong guess costs speed,
  not correctness. It needs JIT SQ STORES ON. Both guess sites take it
  (`gr_prepare` and the `shop_writem` case of `ps3/ps3_driver.cpp`).
- Log: `[jitcfg] sqcopy=`, `[ta-split] ... sqcp=<sites since boot>`.
  With JIT HOTBLOCKS: `coldin/s` of 8C141BBA must fall from 5 per turn
  to 0.
- Checks: `misc/sqla_check` PASS (the Sonic Adventure 2 block: 5 of 5
  stores by the copy rule, 0 by the look-ahead at either level; copy
  fuzz 100000 blocks against a per-register reference, 0 wrong);
  `mutate.py 100000 "cp: "` 17 of 17; `misc/jit8_check` PASS. Build clean
  (the 7 old warnings).
- HW test: Sonic Adventure 2, the same heavy scenes, X3 BACKWARD, JIT
  HOTBLOCKS OFF, one boot with JIT SQ COPY OFF and one with ON. Compare
  by vertex load against each other (and run 21: 90.6% at 14K+). My
  estimate, not measured: 3 to 5 SPEED points at 14K+. Then Crazy Taxi
  and Looney Tunes with the row ON for no change (their loops have no
  copy).

**Built 2026-10-12, HW runs 19 to 23 done, no verdict from Matt yet: JIT
IDLE HASH, way (B) of bundle 8's list "more friendly for other users"
(Matt: "In Bundle 8, let's go with Reicast's approach for the JIT. It's
worth a shot."). Runs 19 to 23 used the package of 01:37 on 2026-10-12
(one entry). Latest package: 02:17 on 2026-10-12 (`build/notdc_ps3.pkg`,
1530800 bytes): a second Looney Tunes entry (the demo's wait, run 22)
and the log fix; no HW run with it. The detail is in section 7, Bundle 8,
point 3.**
- **HW run 19, 2026-10-12 ~01:43-01:47: Looney Tunes, one race, JIT IDLE
  RULE X3 BACKWARD + JIT IDLE HASH ON** (`[jitcfg] idlerule=1 idlehash=1
  bcache=1 finetcnt=0`; `b8_hash_run1.txt`, first boot, scratchpad of
  session 1c8776dd; `ltcmp.py` of session 55d01e77). Matt: "Done." No
  fault line, clean JIT EXIT. `[jit8] idle hash 1 listed=1`: our block
  compiled once.

  | Looney Tunes race | X30 (run 6) | X3 (run 2) | X3 + HASH ON (89 windows) |
  |---|---|---|---|
  | SPEED | 99.95% | 59.8% | 88.4% (min 72.5; 14% of the windows at full speed) |
  | Frames per emulated second | 27.2 | 29.95 | 29.9 |
  | Frames per wall second | 27.2 | 17.9 | 26.4 |
  | `[stall]` sleep | 58.6 ms/s | 0.3 | 8.8 |
  | SPEED at 10-20K / 25-30K / 30K+ vertices | - | - | 98.8% / 87.3% / 81.7% |

  - The listed wait loop was most of X3's cost in this game (SPEED 60 ->
    88%). The rest is not waiting in that loop: the host has no time
    left (sleep 8.8 ms/s), so the game and its sound run 12% slow, and
    the frames per wall second are as at X30. X30 stays the better level
    for this game. Runs 2 and 6 had other settings (TMU Fine TCNT ON, run
    2 JIT BCACHE OFF): the compare is not same-settings.
  - The demo scene (60 frames kind) did not move: SPEED 67.7% (67.8% in
    run 2, 96.9% at X30). Its wait is another loop; no profile of it at
    X3 with the row ON.
  - Reicast's list on HW, log only: in the 48 lines that the log holds
    (the boot up to the menus), its hashes matched 40 blocks, 32 of them
    through `04:DB35BCA0` (the function return). That confirms the PC
    check.
  - Defect of the build: the 48 `[idlehash]` lines were all used by the
    log-only hits, so the line of our own block is not in the log. Fixed
    in source after the run (24 lines per list; `misc/jit8_check` PASS,
    331 checks). **Not packaged**: the package of 01:37 has the old log
    limit.
- **HW run 20, 2026-10-12 ~01:47-01:52: Looney Tunes, one race, JIT IDLE
  RULE X30 + JIT IDLE HASH ON** (`idlerule=0 idlehash=1`, the other
  settings as run 19; the second boot of `b8_hash_run1.txt`, cut as
  `b8_hash_x30.txt`). Matt: "I'm done with the x30 + idle hash on." No
  fault line, clean JIT EXIT, `listed=1`.
  - Race, 104 windows: SPEED 99.90% (min 92.5), 27.3 frames per second,
    sleep 108.2 ms/s. Run 6 (X30, no hash): 99.95%, 27.2, sleep 58.6. So
    at X30 the row changes no frame rate here (the loop is 0.28% of the
    JIT weight at X30). The sleep is not a same-settings compare (run 6
    had TMU Fine TCNT ON).
  - Next, proposed to Matt: one profile run (X3 BACKWARD + JIT IDLE HASH
    ON + JIT HOTBLOCKS ON, the demo for 30 s, then one race), to find
    the demo's wait loop and what costs the race its last 12 points. Not
    run: Matt took Sonic Adventure 2 ("It's the only one with the
    unknown cost"), without the profile ("We'll see the speed.").
- **HW run 21, 2026-10-12 ~01:57-02:01: Sonic Adventure 2 at X3 BACKWARD,
  JIT IDLE HASH ON, JIT HOTBLOCKS OFF** (`b8_hash_sa2.txt`, cut of the
  same file; `binspeed.py` of session 55d01e77, `hbprof.py` of session
  1c8776dd). Matt: "SA's run is done." No fault line, clean JIT EXIT.
  `listed=0`: the row does nothing in this game, so this is a second X3
  run.

  | Sonic Adventure 2, X3 | 2026-10-11 (`b8_sa2_x3.txt`) | this run |
  |---|---|---|
  | 3-8K vertices | SPEED 100.2%, sleep 226 ms/s (85 windows) | 100.3%, 211 (44) |
  | 8-14K | 99.8%, 55 (52) | 99.2%, 50 (61) |
  | 14K+ | 92.5%, min 83.4, sleep 1 (42) | 90.6%, min 72.8, sleep 2 (59) |
  | Frames per emulated second | 59.9 | 59.7-59.9 in each range |

  - The same result as the first X3 run (other play, more heavy
    windows).
  - A reading without a profile, from the `[hb]` pc samples (one per
    second, the pc of the last slice end, so a sample of emulated
    cycles) and the `[idle]` lines, the same in both X3 runs: the
    library wait loop (8C12CB98-8C12CBC0, Reicast's SC entry) is
    skipped by JIT IDLE SKIP (72K checks/s, 72K skips/s) and holds
    36-39% of the samples at 3-8K, 7-10% at 8-14K, 0-7% at 14K+. One pc,
    8C141BBA, holds 0% at 3-8K and 17% at 14K+ in both runs (3% at X30,
    `sa2_step2.txt`). A share that grows with the load is work, not a
    wait. So the cost at 14K+ is game work that the host cannot pay: no
    list entry can give it back. Not proven: no log holds the code of
    8C141BBA (59 and 42 samples); a JIT HOTBLOCKS run would show it.
- **HW run 22, 2026-10-12 ~02:03-02:10: the Looney Tunes profile. X3
  BACKWARD, JIT IDLE HASH ON, JIT HOTBLOCKS ON** (`b8_hash_lt_hot.txt`,
  11 censuses, 128 block dumps; `census_phase.py` of session 1c8776dd
  takes one census from another, so each phase has its own numbers).
  Matt: "Looney Tunes' run is done." No fault line, clean JIT EXIT. With
  the counters: race SPEED 83.4% (91 windows), demo 65.3% (10).
  - **The race (census 2 to 11, 91 s) has no wait loop left.** The
    profile is flat: the first block is 5.5% of the JIT's host
    instructions, the first 30 are 44%. On top is the vertex loop
    8C08738C / 8C0873A6 (`ftrv`, `fdiv`, `fipr`, store-queue `pref`,
    `dt; bf.s`; 267-320K turns/s, 10.8%), then large blocks at
    8C0688xx-8C0699xx. The listed frame wait is out of the 64 rows. So
    the 12 points that X3 still costs in the race are game work: the
    host draws about 26-27 race frames per wall second at either level.
  - **The demo (census 1, 10 s) is one wait loop: over 51% of the JIT's
    host instructions, 1.03M turns/s**, in 12 blocks (8C19D160,
    8C19D164, 8C19D168, 8C19CC0E-8C19CC72, 8C1ADD60, 8C1ADD7A,
    8C1ABDE0): `do { hook(7); bit = find_event(); } while (bit == -1);`.
    `hook(7)` is the library dispatcher 8C1ADD60 (Reicast's `0D:DF0C1754`
    entry) and calls an empty function; `find_event` scans a mask that
    interrupt handlers set. JIT IDLE SKIP cannot take it: the dispatcher
    pushes on the stack (a store). It runs 0.83M turns/s in the menus
    after the demo and 344/s in the race.
  - **New entry, in source: 8C19D168**, the block that closes that loop
    (`cmp/eq #-1,r0; bt.s 8C19D160; mov r0,r13`,
    `>:1:8C19D168:03:D81E7C3D:...`). `misc/jit8_check` PASS (376 checks).
    **Package of 02:17 on 2026-10-12 (1530800 bytes), with the log fix;
    no HW run.** Expected, not measured: the demo scene near full speed
    at X3 (67.7% now), and a gain at X30 too (the loop's small blocks
    are billed x30 there).
- **HW run 23, 2026-10-12 ~02:10-02:15: the Sonic Adventure 2 profile.
  X3 BACKWARD, JIT IDLE HASH ON, JIT HOTBLOCKS ON** (`b8_hash_sa2_hot.txt`,
  18 censuses, 128 block dumps). Matt: "Done with both." No fault line,
  clean JIT EXIT. With the counters: SPEED 82.8% at 14K+ (71 windows).
  - **The cost at 14K+ is game work; run 21's reading holds.** 8C141BBA
    is the game's vertex loop (4 `fmov.s @Rm+`, `ftrv`, `fsrra`, `fmul`,
    `fmac`, 5 `fmov.s FRm,@-R6` into the store queue, 2 `pref`, `dt;
    bf.s`; 29 ops). It is the first block of every census: 22-26% of
    the JIT's hot instructions in the heavy phases (525-656K turns/s,
    census 2 to 3 and 16 to 17), 2.5% in a light one (45K turns/s,
    census 10 to 11). The library wait 8C12CBB8 is 1.1-1.2% and skipped.
    No block of this game is a wait loop that a list entry could take.
  - **Lead for SH4 speed, not examined:** this loop enters its
    out-of-line paths 5.0 times per turn (`coldin/s` 1.86M against
    371K entries/s; 73% of the game's 2.55M cold entries/s). It has 5
    store-queue stores per turn, and its `pref @r3` takes r3 from a copy
    of r6 made one turn before, so JIT SQ STORES cannot tie the stores
    to a `pref`. The vertex loops of Looney Tunes (8C08738C, 8C0873A6,
    `pref @r6` after the stores) have `coldin/s` 0. Estimate from the
    block sizes (688 B hot, 1040 B cold in 9 pieces): the cold paths
    add about 145 instructions to the 172 of a turn.
- The same file holds a Space Channel 5 boot at X3 BACKWARD (23 windows,
  no word from Matt, `b8_hash_sc5.txt`): its last 9 windows ran at SPEED
  62-70%, 0.01K vertices, about 17 frames per wall second (PAL mode).
  Not looked at further.
- Row JIT SPEED -> "JIT IDLE HASH" (`JitIdleHash`, `[jitcfg] idlehash=`),
  set before boot: OFF (default) / ON / + REICAST LIST. A listed block
  drops the rest of its timeslice. ON acts on our list (one entry: the
  Looney Tunes frame wait, block 8C014544) and only logs a block of
  Reicast's list. + REICAST LIST acts on Reicast's 12 hashes too; two of
  them match plain function returns (point 3), so it is for a test only.
- The package holds the whole source: the verdicts below (JIT BCACHE ON,
  no FPU DN row, TMU Fine TCNT OFF) and the cfg name `NotDC.cfg`. The
  first boot starts from the source defaults (the old `nullDC.cfg` is not
  read).
- HW test: Looney Tunes, one race, JIT IDLE RULE X3 BACKWARD + JIT IDLE
  HASH ON. References: run 2 (X3: SPEED 61%), run 6 (X30: 26.4 frames/s
  at SPEED 100%). Then the games that X3 helps (Sonic Adventure, Phantasy
  Star Online) for no change.
- Checks: `misc/jit8_check` PASS (18 source lines; 400048, 798767 and 331
  checks; the new `hash8_check`), `mutate.py 2000 "idle hash"` 31 of 31
  before the log fix, its 2 log mutants caught after it. Build clean (the
  7 old warnings).

**HW SUCCESS (Matt 2026-10-12: "still mark this as a success", and after
the last run and the checks: "SUCCESS."): bundle 8,
the JIT items (section 7; Matt 2026-10-11: "Work on Bundle 8. Ignore the
Interpreter parts. I want JIT/Dynarec stuff."). Six items built: `bcache`
and the `JitMac` fix (5.3), 5.39, 5.40, the P57 RM verify and P71. HW runs
1 to 17 on 2026-10-11 and 12 with the package of 22:43 (below). The
bundle 7 state is below this block.**

**Matt's verdicts (2026-10-12) and the state in source:**
- **JIT BCACHE: default ON** (2026-10-11: "You can keep BCACHE on then").
  The Looney Tunes race is back at full speed with it (run 6).
- **JIT IDLE RULE: default X30, X3 BACKWARD stays as a level** ("Mark x30
  as the default, with a toggleable x3 backward. And in case some games
  get too slow I'll test them with x3 backward in this case."). X3
  BACKWARD gives Phantasy Star Online and Sonic Adventure their 30
  frames, and costs speed in Looney Tunes, Quake3.cdi, Sonic Adventure 2
  (heavy scenes) and a little in Crazy Taxi. The level OFF is still in
  the row (worse in every run). Note for a later test: X3 helps a game
  that drops frames at SPEED 100% with sleep to spare; it does not speed
  up a game whose SPEED is low.
- **FPU DN: the row is removed** ("Since it doesn't matter anymore, remove
  FPU DN row."): the preset, the `FpuDn` key, the getter, the NI forms in
  `SetFloatStatusReg()` (the function is as before the bundle) and
  `fpudn=` on `[jitcfg]`. 5.40 is closed, no effect (run 1). The `[ni]`
  boot probe is still in `ps3/ps3_driver.cpp` (not asked for).
- **TMU Fine TCNT (bundle 7): default OFF again** ("If it was FINE
  before, I'd keep it that way for the TMU Fine and TCNT OFF."):
  `g_tmu_fine_tcnt_preset` 0; it cost Quake3.cdi 10 to 17 SPEED points
  (runs 15 and 17). `misc/timebase_check` follows the new default.
- JIT PR MOVES stays OFF: run 15 (Quake3.cdi) compiled 20 sites with no
  fault in the log; no word from Matt on how it looked. JIT MAC and JIT
  RM 2/3 stay OFF: no game is known that uses them.
- **Open, by Matt's word ("Mark as a 'what to do to make the bundle 8
  more friendly for other users'"): the list under "Bundle 8" in section
  7.** Today a user must know which games gain from X3 BACKWARD.
- UDP listener: every run of this bundle went into the listener of the
  bundle 5 session (PID 6248, started 2026-10-11 15:08, file
  `ps3log_b5_run2.txt`, scratchpad of session 01bbad3e). Stopped at
  Matt's request on 2026-10-12 ("Stop the listener!"); port 18194 is
  free. The cuts per run (`b8_*.txt`) are in the scratchpad of session
  55d01e77.
- Source after the verdicts: builds (the 7 old warnings; `notdc_ps3.elf`
  of 00:53 on 2026-10-12). `misc/jit8_check` PASS (13 source lines,
  400048 and 798767 checks; `mutate.py` 75 of 75: the NI mutants went
  out with the row, two guards against an NI form came in);
  `misc/timebase_check` PASS with the default line at 0 (7776305
  checks, 24 source lines; `mutate.py` 63 of 63, its default mutant now
  plants ON). **Not packaged**: the package of 22:43 on 2026-10-11
  (`build/notdc_ps3.pkg`, 1529712 bytes) has JIT BCACHE OFF, the FPU DN
  row and TMU Fine TCNT ON. Matt's `nullDC.cfg` holds JIT BCACHE ON, TMU
  Fine TCNT OFF, JIT IDLE RULE X30, JIT PR MOVES ON, JIT C-Calls ON.

**The build (2026-10-11): five new rows at the end of the JIT SPEED page,
all at the old path; JIT MAC (EMULATION page) stays OFF. Package of 22:43
on 2026-10-11. It also holds bundle 7's four defaults, as the package of
22:16 does.**
- Not built, Matt's scope: P46 (the SH4 op tests) and P32 (the
  interpreter's fneg with PR=1 and mac.w with S=1) are interpreter parts.
  P78's RFCR is a BSC register, not JIT. 5.37 was done on 2026-10-10.
- The rows (the key is in `nullDC.cfg` [ps3]; the first value is the old
  path; all are set before boot):

  | Row | Key | Values | Item | Default in source |
  |---|---|---|---|---|
  | JIT BCACHE | `JitBcache` | OFF / ON | 5.3 `bcache` | ON (1) since 2026-10-11 (HW run 6); OFF in the package of 22:43 |
  | JIT PR MOVES | `JitPrMoves` | OFF / ON | 5.39 | OFF (0) |
  | JIT RM 2/3 | `JitRmAny` | VERIFY / ACCEPT | P57 | VERIFY (0) |
  | JIT IDLE RULE | `JitIdleRule` | X30 / X3 BACKWARD / OFF | P71 | X30 (0), Matt's verdict 2026-10-12 |
  | FPU DN | `FpuDn` | OFF / ON | 5.40 | removed from source 2026-10-12 (Matt); still in the package of 22:43 |
  | JIT MAC (EMULATION, old row) | `JitMac` | OFF / ON | 5.3 `JitMac` | OFF (0) |

- JIT BCACHE (5.3): a dynamic block end (rts at the default DYN IC level,
  and each DYN IC miss) looks up `bm_bcache[]`, 8-byte {addr, code}
  entries: 9 instructions, two loads, no store (`ps3/ps3_jit_bcache.h`).
  The old path is about 16 with a `lookups++` load and store.
  `BlockCacheFlatEntry.code` is now a u32 (it was a pointer: 16 bytes on
  PPC64, so the path could not run). `loop_no_update` still calls
  `bm_GetCode` in C after a timeslice. Known side effect: a hit does not
  count `lookups`, so two hot blocks in one bucket can take turns in the
  cached slot (each turn is a few C lookups). Expected gain: noise level.
- JIT MAC fix (5.3): the S = 0 result is one 64-bit add of `extsw`,
  `extsw`, `mulld` on MACH:MACL (`ps3/ps3_jit_mac.h`, 7 instructions; every
  one is in the HW validated 5.2 and 5.38 forms). The old form took the
  carry of a 64-bit `addc`: on the PC model it is wrong in 26.2% of random
  mac.l cases, 6.2% of mac.w, and in 1 of Flycast's 4 mac cases (mac.w
  -21 + 35). The S = 1 call to `sh4_mac_l` / `sh4_mac_w` is not changed.
  **No game in any old `[IFB]` log runs mac.l or mac.w** (Crazy Taxi,
  Sonic Adventure, Soul Calibur, Rayman 2, ChuChu Rocket, Looney Tunes), so
  no HW test game is known: `[jit8] mac=` shows if a game compiles one.
- JIT PR MOVES (5.39, Flycast `62085539a`): in a PR=1 block the fmov
  forms, flds and fsts stay native. **fneg is left out**: the plan ties it
  to the P32 interpreter fix, which is out of scope. In the JIT fneg flips
  FR(n), as Flycast does, so it can go in with one line if Matt wants it.
  Old logs, **corrected 2026-10-11 after HW run 5** (the first scan took
  each log's first game; `ifb_scan2.py` splits per boot): PR=1 FPU ops run
  in Quake3.cdi (homebrew; `ps3log_spevb_run1.txt`: fmov forms up to
  3.5K/s each in one window, fcnvsd, float, fmul, fsub about 500/s, then
  1-4/s), Space Channel 5 (fmov forms 35-62/s, `ps3log_p16_run1.txt`) and
  Rayman 2 (1-3/s). **Looney Tunes, Soul Calibur and ChuChu Rocket show
  none.** Expected gain: none that a run can show.
- JIT RM 2/3 (P57): ACCEPT compiles a block with FPSCR.RM 2 or 3 as round
  to nearest, as `SetFloatStatusReg()` already runs it; VERIFY keeps the
  `verify(fpu_cfg.RM<2)` hang. No game that boots here sets RM 2 or 3 (the
  WinCE games need P38): `[jit8] rm23=` counts such blocks.
- JIT IDLE RULE (P71): X30 is today's bill. X3 BACKWARD is Reicast's rule
  (x3; a bt block only when it branches back; the x10 self-loop and the
  x5 / x15 syscall bills stay). OFF bills each block its own cycles
  (Flycast). `block.idle_loop` is set at every level, so JIT IDLE SKIP
  works at every level. It is the only row with a visible effect, and it
  can cost SPEED (the host runs more SH4 ops per emulated second).
- FPU DN (5.40): `SetFloatStatusReg()` sets `mtfsfi 7,4|RN` while FPSCR.DN
  is set (NI). The render thread and the menus keep their own FPSCR. The
  plan's "check NI on the PPE first" is a probe in the same package: two
  `[ni]` lines at each boot (dynarec), with the row ON or OFF. They show a
  denormal result (1e-30 x 1e-10), a denormal operand (x 1.0, and + itself)
  as bits, and ns per `fmuls` for a normal op, a denormal result and a
  denormal operand, with NI = 0 and NI = 1. The row is worth a run only if
  NI = 1 changes a value or a time.
- Log: `[jitcfg] idlerule= prmoves= rmany= fpudn=` (a fifth line at boot);
  `bcache=` on the third `[jitcfg]` line now follows its row. `[jit8]`
  after new compiles: `sites: bcache= mac= prmoves=` and `blocks: idle
  rule N billed= rm23=` (billed = blocks whose cycles the rule raised).
  `[ni]` at boot.
- PC (WSL), new `misc/jit8_check` (`run.sh`, `cut.py`, `gen.py`,
  `mutate.py`): PASS. `cut.py` cuts the code under test out of the real
  sources and checks 15 source lines of the driver and the presets.
  `jit8_check`: both headers against 41 ppu-as words; mac.l and mac.w on a
  PPC64 model against `shil.cpp`'s `sh4_mac_l` / `sh4_mac_w` and 64-bit
  math, with junk above the low words, and Flycast's 4 mac cases; the
  lookup against a table that `blockmanager.cpp`'s own mirror fills (hits,
  misses on a shared index, empty buckets, 4 table addresses): 400048
  checks. `dec8_check`: the PR list for all 4096 FPU opcodes (equal to
  Flycast's list less fneg) and the gate; the cycle block at the three
  levels (X30 equal to the block before the bundle, `ref_cycle_old.inc`;
  X3 equal to Reicast's text; OFF plain); the RM rows; the host FPSCR
  field for each RM, DN and row: 798775 checks. `mutate.py`: 78 of 78
  planted bugs fail (29 of the 30 header bugs on the model alone; `mullw`
  for `mulld` gives the same values there, so only the words see it).
- Other checks after the edits: `fschg_check`, `clock_check`,
  `timebase_check`, `carry_check`, `div32_check`, `idle_check` PASS.
- Build: the 7 old warnings, `.got` 0xD608, 0 `r2off`; `bm_bcache` at
  0x10B92800 (the lookup needs it below 0x7FFF8000).
- HW steps (package of 22:43; SH4 Clock 200; one row per run; a row acts
  at the next boot). An install keeps `nullDC.cfg`: the new keys start at
  0. UDP listener on port 18194 only when Matt says so.
  1. Any game, rows OFF, 60 s: the two `[ni]` lines and `[jitcfg]
     idlerule=0 prmoves=0 rmany=0 fpudn=0`. This run answers 5.40's open
     question.
  2. JIT IDLE RULE X3 BACKWARD, then OFF, one boot each: the same Looney
     Tunes race (RPS against 26.4, SPEED, `[stall]` sleep), then Phantasy
     Star Online's 20 fps scene and a dense Crazy Taxi drive (SPEED must
     hold). `[jit8]` shows `idle rule N`.
  3. JIT IDLE RULE back to the level Matt keeps. JIT BCACHE ON: Crazy Taxi
     and Sonic Adventure. `[jit8] bcache=` above 0, no fault, same-load
     VPS.
  4. JIT PR MOVES ON: Space Channel 5 (corrected after HW run 5; the step
     named Looney Tunes, which has no PR=1 FPU op). `[jit8] prmoves=` above
     0; with IFB Probe ON the fmov lines leave `[IFB]`. Any image or
     physics fault?
  5. FPU DN ON, only if step 1 shows a change with NI = 1: Crazy Taxi and
     Sonic Adventure. Any geometry or physics fault? `fpudn=1`.
  6. JIT MAC ON and JIT RM 2/3 ACCEPT: no known game uses them. They can
     ride on any run; `[jit8] mac=` and `rm23=` tell if a game does.
- HW run 1, 2026-10-11 (package of 22:43, Crazy Taxi, rows OFF; Matt:
  "Testing Crazy Taxi."; the listener of the bundle 5 session wrote it, cut
  into `b8_run1_ct.txt`, scratchpad of session 55d01e77). Matt:
  "Perfect." Next (Matt): Looney Tunes with JIT IDLE RULE X3 BACKWARD.
  - `[jitcfg] idlerule=0 prmoves=0 rmany=0 fpudn=0`, `bcache=0`, `Mac=0`.
  - **5.40's question is answered: NI does nothing on the PPE.**
    `[ni] NI=0: 1e-30*1e-10=000116C2 den*1=00000123 den+den=00000246 |
    ns/op: normal 5.4, den result 4.8, den operand 47.8`; `NI=1:` the same
    three values, `normal 4.7, den result 4.7, den operand 45.8`. So NI = 1
    flushes no denormal result and zeroes no denormal operand, and it
    saves no time. A denormal result costs nothing. A denormal operand
    costs about 10 times a normal op (about 43 ns more) with NI = 0 and
    with NI = 1; the probe loads the operand with `lfs` in each turn, so
    the cost can be the load (not split). FPU DN has no effect to test:
    HW step 5 falls away. **5.40 CLOSED by Matt 2026-10-11 ("Perfect. You
    can close 5.40."), no effect.** FPU DN was never run ON. The row went
    out of the source on 2026-10-12 (Matt); the `[ni]` probe is still
    there.
  - The boot is as the earlier Crazy Taxi boots (`ct_last.txt`, `run4b.txt`:
    the same vertex timeline up to the drive). The drive's first 30 s:
    4.9-11.8K vertices at SPEED 99.7-100.2%, no fault line. `[jit8]`:
    `bcache=0 mac=0 prmoves=0`, `idle rule 0 billed=4733`, `rm23=0`.
- HW run 2, 2026-10-11 ~22:56-23:03 (package of 22:43, Looney Tunes, JIT
  IDLE RULE X3 BACKWARD, two boots: `b8_lt_x3_a.txt` up to the main menu,
  `b8_lt_x3_b.txt` with one race and JIT C-Calls ON; scratchpad of session
  55d01e77, `ltcmp.py` there; `step2cmp.py` and `rpshist.py` of the older
  sessions). **Matt: "It runs much worse".** `[jitcfg] idlerule=1`, the
  other rows OFF; no fault line, two clean JIT EXITs. No X30 race with
  this package: the reference is `lt_step2.txt` (2026-10-10, X30, other
  settings: `dyn_ic=2 dve=1 greuse=1`, and the defaults of bundles 2-7
  were not in).

  | Scene | X30 (`lt_step2.txt`) | X3 BACKWARD |
  |---|---|---|
  | Race: SPEED | 100.0% | 60.9% (55.5-89.0), 89 windows |
  | Race: frames per wall second | 26.4 | 18.2 |
  | Race: frames per emulated second | 26.4 | 29.95 |
  | Race: `[stall]` sleep | 99.5 ms/s | 0.3 ms/s |
  | Demo (60 fps kind, 26-27K vertices): SPEED | 99.8% | 67.8-69.7% |
  | Demo: frames per wall / emulated second | 59.8 / 59.95 | 40-41 / 58.5-58.7 |
  | Boot 2D screen (50 Hz, 50 frames/s): SPEED | 99.3-102.8% | 62.5-71.8% |

  - **P71's claim holds in emulated time**: at X3 the race draws 29.95
    frames per emulated second (62 of 87 windows at 28-30, 20 at 30-32),
    against 26.4. The game gets its 30 frames when the rule does not bill
    its work blocks x30.
  - **The host cannot pay for it.** One emulated second costs 1.64 times
    the host time in the race, 1.46 in the demo, about 1.47 on the 2D boot
    screen. P71's estimate was +14% SH4 work. The demo and the boot screen
    draw the same frames per emulated second as before, so the extra time
    there is not frame work: it is the game's wait loops, which now spin
    10 times more turns per slice (my reading; no profile). JIT IDLE SKIP
    skips none of them in the race (`[idle]` skips/s 0, checks/s 13-15K).
  - OFF bills less than X3 (no x3, no x10 self-loop, no syscall bill), so
    it is expected to cost more host time. Matt ran it next: HW run 3.
- HW run 3, 2026-10-11 ~23:03-23:07 (package of 22:43, JIT IDLE RULE OFF,
  `[jitcfg] idlerule=2`; `b8_ct_off.txt`: Crazy Taxi, left after 27 s, no
  drive; `b8_lt_off.txt`: Looney Tunes, one short race, JIT C-Calls ON).
  **Matt: "OFF is much worse."** No fault line, two clean JIT EXITs.

  | Looney Tunes | X30 (`lt_step2.txt`) | X3 BACKWARD | OFF |
  |---|---|---|---|
  | Race at 29.5K+ vertices: SPEED | 100.2% | 58.0% (31 windows) | 56.4% (53.1-58.7, 16 windows) |
  | Race: frames per wall second | 25.8 | 17.4 | 16.9 |
  | Race: frames per emulated second | 25.7 | 30.1 | 30.0 |
  | Demo (60 fps kind): SPEED | 99.8% | 67.8-69.7% | 60.1% |
  | Menus and select screens: SPEED | 100% | 89-100% | 76-93% |
  | After the BIOS logo (about 10 s, both games) | 100% | 100% | 12.8% |

  - The race gains nothing over X3: the same 30 frames per emulated second
    (the game's cap) for a little more host time. So the step that counts
    is X30 -> X3.
  - New at OFF: about 10 windows at SPEED 12.8% after the BIOS logo, in
    Crazy Taxi and in Looney Tunes. That code is at 8C000000-8C00FFFF: OFF
    drops its x5 / x15 bill (my reading; no profile).
  - Matt's `nullDC.cfg` now holds `JitIdleRule=2`: set the row back to X30.
  - Reading of runs 2 and 3: the x30 is a speed trade this host needs. X30
    stays the default. P71 has no verdict from Matt yet.
- HW run 4, 2026-10-11 ~23:08-23:14 (package of 22:43, Crazy Taxi, JIT
  BCACHE ON, JIT IDLE RULE back at X30, JIT C-Calls ON; `b8_ct_bcache.txt`,
  `binspeed.py`, scratchpad of session 55d01e77). Matt: "Look and sounded
  fine". No word on the default: the row stays OFF. Sonic Adventure not
  run. `[jitcfg] bcache=1 ... idlerule=0`;
  `[jit8] bcache=9255` sites; no fault line, clean JIT EXIT; 257 windows.
  - Same package, by load, against run 1 (rows OFF, JIT C-Calls OFF) and
    `run4b.txt` (bundle 5 session, 2026-10-11, `bcache=0`, the same
    `[jitcfg]` values):

    | Vertices | Run 1, OFF | `run4b.txt`, OFF | BCACHE ON |
    |---|---|---|---|
    | 6-9K | 100.31% | 99.96% | 100.00% |
    | 9-12K | 100.08% | 100.10% | 100.10% |
    | 12K+ (full-speed windows) | 96.84% (14%, 21 windows) | 98.06% (56%, 16) | 98.13% (60%, 25) |
    | 6K+ | 99.58% | 99.78% | 99.77% |

  - No gain to see, no loss: BCACHE ON is inside the spread of the two
    OFF runs. As expected (noise level). `[stall]` sleep with BCACHE ON:
    210 / 122 / 49 ms/s at 6-9K / 9-12K / 12K+; no OFF run of this
    package has JIT C-Calls ON, so the sleep has no reference.
  - Matt 2026-10-11: "You can keep BCACHE on then, but I'd like to know
    what it does first." Explained; default ON after HW run 6 (below).
- HW run 5, 2026-10-11 ~23:15-23:19 (package of 22:43, Looney Tunes, JIT
  PR MOVES ON, JIT BCACHE OFF, JIT C-Calls ON; `b8_lt_prmoves.txt`). Matt:
  "the Looney Tunes race is done". No fault line, clean JIT EXIT.
  - **The run did not test the row: `[jitcfg] prmoves=1`, but `[jit8]
    prmoves=0` to the end.** No PR=1 move was compiled, because Looney
    Tunes has none. The HW step named the wrong game: the PR=1 fallbacks of
    `ps3log_spevb_run1.txt` are from its Quake3.cdi boot, not from its
    Looney Tunes boots (this session's error; the game list above is
    corrected). JIT PR MOVES still has no HW run. Games that hit it: Space
    Channel 5, Quake3.cdi, Rayman 2 (1-3/s).
  - So the run is a rows-OFF race of this package: 87 race windows, SPEED
    97.62% (31% at full speed), 26.10 frames per wall second, 26.77 per
    emulated second, `[stall]` sleep 24.1 ms/s, sys 82.7 ms/s.
  - **Side finding, not split: the race is slower than on 2026-10-10**
    (`lt_step2.txt`: SPEED 98.96-100.0%, 86-94% of windows at full speed,
    sleep 91-99.5, sys 71-72.6). Per wall second the SH4 code takes about
    874 ms (819) and sys 82.7 (71.0) (`stallparts.py`). Interrupts are the
    same (16.3K/s, 15.6K/s); HBlank is mode 0. Between the two runs: the
    settings went back to the source defaults (`dyn_ic` 2 -> 1, `dve` 1 ->
    0, `greuse` 1 -> 0; getcode 81K/s, was 64K/s), and the defaults of
    bundles 2 to 7 came in (`finetcnt=1`, SPE AICA Sync 3, the TIMING
    rows). No Looney Tunes race with today's settings exists from before
    bundle 8, so the logs cannot say which change it is. Bundle 8's rows
    were all at the old path in this run. Crazy Taxi with the rows OFF is
    as `run4b.txt` (run 4's table).
- **HW run 6, 2026-10-11 ~23:20-23:26: JIT BCACHE HW SUCCESS in Looney
  Tunes** (package of 22:43, JIT BCACHE ON, the other settings as run 5;
  `b8_lt_bcache.txt`). Matt: "Check Looney Tune's framerate with BCACHE.
  It's much better." `[jitcfg] bcache=1`, `[jit8] bcache=32235` sites, no
  fault line. Against run 5 (the same package and settings, BCACHE OFF),
  one race each, the numbers at 123 race windows (the run went on):

  | Looney Tunes race | BCACHE OFF (run 5, 87 windows) | BCACHE ON (123 windows) |
  |---|---|---|
  | SPEED | 97.62% | 99.95% |
  | Windows at full speed | 31% | 85% |
  | Frames per wall second | 26.10 | 27.20 |
  | `[stall]` sleep | 24.1 ms/s | 58.6 ms/s |
  | SH4 code (rest of `[stall]`) | 874 ms/s | 842 ms/s |
  | sys | 82.7 ms/s | 80.8 ms/s |
  | getcode C calls | 81.2K/s | 63.7K/s |
  | 10-20K vertices: SPEED (full, sleep) | 100.84% (80%, 32) | 99.88% (92%, 85) |
  | 20-25K | 96.03% (7%, 36) | 99.95% (91%, 46) |
  | 25-30K | 96.35% (12%, 23) | 100.11% (86%, 53) |
  | 30K+ | 96.51% (14%, 8) | 99.92% (65%, 22) |

  - The gain is about 35 ms/s of SH4 thread time, 4% of the SH4 code time,
    in every load bin. It is more than the plan's "noise level": this game
    takes far more dynamic block ends than Crazy Taxi (32K sites against
    9K), and its race had no spare time, so the gain shows as SPEED. The
    frames per emulated second do not change (27.2, 26.8): the 26-27 of
    the idle rule stays.
  - It also takes back most of run 5's side finding: sleep 58.6 ms/s
    against 91 on 2026-10-10 (SH4 code 842 against 819).
  - **JIT BCACHE defaults to ON in source since 2026-10-11**
    (`g_jit_bcache_preset` 1; Matt: "You can keep BCACHE on then", after
    the explanation he asked for, and this run). Not packaged: the package
    of 22:43 has it OFF. Matt's `nullDC.cfg` holds it ON from his run.
    `misc/jit8_check` PASS with the new default line.
  - Matt after the run: "Looney tunes looked fine." Clean JIT EXIT.
- HW run 7, 2026-10-11 ~23:27-23:32 (package of 22:43, Sonic Adventure,
  JIT BCACHE ON, JIT PR MOVES ON, X30, JIT C-Calls ON;
  `b8_sonic_bcache.txt`, `game_history.py`, `binspeed.py`). **Matt: "It
  ran slightly better, but still not smooth."** `[jit8] bcache=21088`
  sites, `prmoves=0` (Sonic Adventure compiles no PR=1 move either); no
  fault line, clean JIT EXIT.
  - 172 play windows (3K+ vertices): SPEED 100.00% (min 97.0, 99% at full
    speed), 28.65 frames/s, 77% of the windows at 29.4 or more, `[stall]`
    sleep 387 ms/s. By load: 3-8K sleep 482, 8-14K 365, 14K+ 277 ms/s
    (26.3 frames/s at SPEED 99.96%).
  - Against the old logs (`bcache=0`): the same frame rate. 2026-10-07 and
    08 (`dve=1 dyn_ic=2 greuse=1`): SPEED 99.99%, 28.5-29.0 frames/s,
    72-79% of the windows at 29.4+, sleep 394-417 ms/s. 2026-10-10 and 11
    (bundle 3 and 4 runs, today's JIT settings): SPEED 99.15-99.64% with
    dips to 33-38%, 27.7-28.4 frames/s. So BCACHE shows no gain here that
    the log can prove: the game has spare time with and without it.
  - **Why it is not smooth: 33 of the 172 windows are below 29 frames/s,
    all at SPEED 100%, with the SH4 thread asleep 318 ms/s (233-492).** The
    game drops these frames in emulated time while the host has a third
    of its time spare. This is the Looney Tunes and Phantasy Star Online
    pattern (the cycle bill, P71), plus Sonic's own 3-vblank frames
    (5.41). Lead, no run: JIT IDLE RULE X3 BACKWARD in this game. Looney
    Tunes paid x1.46-1.64 host time for it and had none spare; Sonic
    Adventure's SH4 thread is 52-72% busy, so most scenes can pay. A
    global X3 is not possible (run 2), so the lead needs a per-game
    setting (Matt's parked idea) or a rule that tells wait loops from
    work blocks.
  - Next (Matt): Phantasy Star Online, "and then we can get a verdict."
- HW run 8, 2026-10-11 ~23:33-23:36 (package of 22:43, Phantasy Star
  Online Ver. 2, the settings of run 7; `b8_pso_bcache.txt`). Matt:
  "Phantasy Star is done." `[jit8] bcache=16550` sites, `prmoves=0`; no
  fault line, clean JIT EXIT.
  - SPEED 100% in every load bin (94-100% of the windows at full speed).
    Frames per second: 30.0 at 1-4K vertices, 28.6 at 4-8K, **20.2 at
    8-14K and 19.2 at 14K+**, with `[stall]` sleep 716 / 583 / 541 / 413
    ms/s. So the 20 fps scene is as in `pso.txt` (bundle 4 session,
    `bcache=0`: 20.0 and 19.7 at SPEED 100%): BCACHE does not change it,
    and the SH4 thread sleeps 41-54% of the time in it. It is the same
    emulated-time pattern as run 7, with more spare host time.
  - Next (Matt): "I'll test the X3 backward with Sonic Adventure as last."
- **HW run 9, 2026-10-11 ~23:37-23:41: JIT IDLE RULE X3 BACKWARD in Sonic
  Adventure** (package of 22:43, the settings of run 7 but `idlerule=1`,
  the same first level; `b8_sonic_x3.txt`). **Matt: "Sonic Adventure was
  VERY smooth."** No fault line, clean JIT EXIT.

  | Sonic Adventure, play windows (3K+ vertices) | X30 (run 7, 172) | X3 BACKWARD (168) |
  |---|---|---|
  | Frames per second | 28.65 | 29.69 |
  | Windows at 29.4 frames/s or more | 77% | 97% |
  | Windows below 29 frames/s | 33 | 5 |
  | 14K+ vertices: frames per second | 26.31 | 29.64 |
  | SPEED (mean) | 100.00% | 99.98% |
  | `[stall]` sleep, all / 14K+ | 387 / 277 ms/s | 343 / 209 ms/s |
  | Slowest frame of each second (mean) | 22.3 ms | 32.9 ms |

  - The lead of run 7 holds: the frames this game dropped in emulated
    time come back, and the host pays with about 45-70 ms/s of its spare
    time. The 14K+ scenes go from 26.3 to 29.6 frames/s.
  - Cost seen in the log: (1) SPEED per window moves between 98.1% and
    101.7% (36 windows below 99.5%, 36 above 100.5%; at X30 2 and 7); the
    mean is 100.0%. One vblank interval per frame holds the frame's work
    and takes up to 33 ms, the next one is short. Matt saw and heard no
    fault. (2) Three windows of a boot logo screen at SPEED 65-72% (X30:
    88-91% there). (3) One scene start at 91.3% (X30: 97.0%).
  - **State of P71 after runs 2, 3 and 9: the right level depends on the
    game.** Looney Tunes has no spare host time: X3 gives SPEED 61%.
    Sonic Adventure has it: X3 gives its 30 frames. Phantasy Star Online
    has more spare time than Sonic (run 8); no X3 run. So X30 stays the
    global default, and X3 BACKWARD is a win per game. How to offer it is
    open (Matt's call): the row by hand, a per-game setting (his parked
    idea of a per-game ID checker), or a rule that follows the host's
    spare time.
  - Matt's `nullDC.cfg` now holds `JitIdleRule=1`.
- **HW run 10, 2026-10-11 ~23:42-23:45: JIT IDLE RULE X3 BACKWARD in
  Phantasy Star Online** (package of 22:43, the settings of run 9;
  `b8_pso_x3.txt`). Matt: "I'm done with Phantasy Star Online." (no word
  yet on how it looked). `[jitcfg] idlerule=1`; no fault line, clean JIT
  EXIT. **The 20 fps scene runs at 30.**

  | Phantasy Star Online, by vertices | X30 (run 8): frames/s, sleep | X3 BACKWARD: frames/s, sleep |
  |---|---|---|
  | 1-4K | 29.97, 716 ms/s | 29.92, 731 ms/s |
  | 4-8K | 28.55, 583 | 29.97, 481 |
  | 8-14K | 20.20, 541 | 29.99, 368 |
  | 14K+ | 19.17, 413 | 29.97, 234 |

  - SPEED 100.0% in every bin at both levels (X3: min 98.1%, 93% of the
    4K+ windows at full speed; X30: 94%). No more window jitter than at
    X30 (7 and 8 windows outside 99.5-100.5%, 5 and 7 at X30). The
    slowest frame of each second at 8K+: 29.7 ms (26.7).
  - The host pays 110-180 ms/s of sleep for 10 more frames per second and
    keeps 234 ms/s spare in the heaviest scene.
  - This gives the cause of the 20 fps scene that Matt parked on
    2026-10-11: it is the x30 bill. The two ideas of that day (SH4 Clock
    250, the 50 Hz mode) are not needed.
- HW run 11, 2026-10-11 ~23:47-23:58: JIT IDLE RULE X3 BACKWARD in Sonic
  Adventure 2 (Europe), the settings of run 9 (`b8_sa2_x3.txt`). No word
  from Matt on how it looked. No fault line, clean JIT EXIT. No X30 run
  with this package: the references are `ps3log_step2_run1.txt`
  (2026-10-10) and `ps3log_bump_run1.txt` (2026-10-08), X30, older
  settings. **The middle case: 60 frames where the host can pay, a
  slow-down where it cannot.**

  | Sonic Adventure 2, play windows | X30 (2026-10-10 / 10-08) | X3 BACKWARD (179 windows) |
  |---|---|---|
  | Frames per emulated second | 52.3 / 49.6 | 59.9 (178 windows at 58.5+) |
  | SPEED | 99.98% / 100.05% | 98.28% (min 83.4, 68% at full speed) |
  | Frames per wall second | 52.3 / 49.6 | 58.9 |
  | 3-8K vertices | - | SPEED 100.2%, 60.0 frames/s, sleep 226 ms/s |
  | 8-14K | - | 99.76%, 59.8, sleep 55 |
  | 14K+ | 100.0%, 55.1 / 99.9%, 49.7 | 92.51% (83.4-), 55.5 per wall second, sleep 1 |
  | `[stall]` sleep, all | 324 / 276 ms/s | 123.5 ms/s |

  - The game reaches its 60 frames in emulated time at X3. In the 14K+
    scenes (42 windows) the host has no time left: SPEED 92.5%, so the
    game and its sound run slow there.
- Matt 2026-10-11: "Reicast has an Idle skill loop detection. How about we
  take that one?" Read (`libswirl/hw/sh4/dyna/decoder.cpp:30-63`,
  `:1147-1188`, `driver.cpp:171-207`; Minicast the same): (1) the x3 rule,
  which is our X3 BACKWARD; (2) `idle_hash`, a fixed list of 12 block
  hashes (4 BIOS, 3 "SC", 2 "HH", 3 more) that bill `max_cycles`, so the
  slice ends. Looney Tunes is not in the list, and our JIT IDLE SKIP is the
  general form of (2) (0 skips in the Looney Tunes race). Trap for a port:
  its `hash()` reads the code as bytes (`u8* ptr`, `u16 data=ptr[i]`), so
  it hashes the first `guest_opcodes` bytes, each as 2 bytes, and the
  `0xD` reloc mask never acts. Proposed and accepted (Matt: "I'll do the
  JIT HOTBLOCKS run now."): a profile of Looney Tunes at X3 BACKWARD, then
  make its wait loops end the slice (a hash list or a better detector), if
  the extra host time is wait loops.
- **HW run 12, 2026-10-12 ~00:02-00:06: the profile. Looney Tunes, X3
  BACKWARD, JIT HOTBLOCKS ON, JIT BCACHE ON** (`[jitcfg] hotblocks=1 ...
  idlerule=1`; `b8_lt_x3_hot.txt`, 10 censuses, 128 block dumps). Matt:
  "Run's done." Clean JIT EXIT. Race: 90 windows, SPEED 56.2%.
  - **One wait loop takes 46.6% of the JIT's host instructions** (census
    10, 100 dense windows, 101.4 s): the blocks 8C01453E, 8C014544 and
    8C0713D0, 4.67M turns/s each (16.7 + 15.5 + 14.4% of the weight). In
    the X30 profile (`run_prof.txt`, 2026-10-08, session 898d8ac1) the
    same loop ran 43K turns/s, 0.28% of the weight. So at X3 the game ends
    its frame early in emulated time and waits here, 108 times more.
  - The loop: `8C01453E: mov.l @(PC),r0; jsr @r0; mov #6,r4` ->
    `8C0713D0: mov.l @(PC),r0; shll2 r4; add r0,r4; rts; fmov.s @r4,fr0`
    -> `8C014544: ftrc fr0,FPUL; sts FPUL,r1; mov.l @(PC),r0; mov.l
    @r0,r0; cmp/gt r1,r0; bt 8C01453E`. That is `while (*counter >
    (int)table[6]) ;`: the game's own frame wait on a counter in memory. No
    store, 14 ops, 12 billed cycles per turn at every level (none of the
    three blocks falls under the idle rule: jsr and rts ends, FPU ops).
  - So P71's cost in this game is waiting, not game work (the reading of
    run 2, now with a profile). JIT IDLE SKIP does not catch the loop
    (`[idle]` skips/s 0): its candidates are blocks of the idle rule (no
    FPU op, under 6 opcodes), and this loop closes in a block of 6 opcodes
    with `ftrc`. Reicast's small rule misses it for the same reasons.
  - Estimate, not measured: without this loop the X3 race needs between
    0.88 and 1.07 times the host time of the X30 race (the loop's share
    counted in instructions; its time share is not known). So 30 frames/s
    at about full speed is in reach.
  - Two ways, open (Matt's call): (A) widen JIT IDLE SKIP's candidates to
    this shape (a closing block with FPU reads, a few more opcodes; the
    run-time proof stays: no store since the last check, the same
    register hash, plus the block's FPU inputs); general, no list. (B) a
    list of known wait loops as Reicast's `idle_hash`, with this loop as
    its first entry; per game, each new game needs a profile run.
- HW run 13, 2026-10-12 ~00:07-00:13: Crazy Taxi at X3 BACKWARD, "out of
  curiosity" (Matt), **with JIT HOTBLOCKS still ON** (`b8_ct_x3_hot.txt`).
  Matt: "The Crazy taxi run is done." No fault line, clean JIT EXIT.
  - 6K+ vertices: SPEED 96.40% (99.77% in run 4 at X30), 57.8 frames/s
    (59.8), sleep 27 ms/s (142). 12K+: 89.8% (98.1%).
  - Not a verdict on X3 in this game: the profile's block counters cost
    host time by themselves (Looney Tunes at X3: SPEED 60.9% without the
    profile, 56.2% with it), and the run has no partner at X3 without the
    profile. Crazy Taxi already draws its 60 frames at X30, so X3 has no
    frame to give here.
  - Its census has no wait loop on top (the first block is 4.1% of the
    weight): JIT IDLE SKIP catches this game's wait loop, 0C15A678, 77-83K
    skips/s.
- HW run 14, 2026-10-12 ~00:15-00:19: Crazy Taxi at X3 BACKWARD, JIT
  HOTBLOCKS OFF, JIT BCACHE ON, JIT C-Calls ON (`b8_ct_x3.txt`), against
  run 4 (X30, the same package and settings). Matt: "Done." No fault
  line, clean JIT EXIT.

  | Crazy Taxi, by vertices | X30 (run 4): SPEED, sleep | X3 BACKWARD: SPEED, sleep |
  |---|---|---|
  | 6-9K | 100.00%, 210 ms/s | 100.09%, 177 ms/s |
  | 9-12K | 100.10%, 122 | 100.16%, 102 |
  | 12K+ (full-speed windows) | 98.13% (60%), 49 | 95.67% (26%), 4 |
  | 6K+ | 99.77%, 142 | 99.40%, 108 |

  - X3 costs this game about 35 ms/s of SH4 thread time (SH4 code about
    763 -> 802 ms/s, +5%) and gives it no frame (59.8 and 59.5 frames/s:
    it draws its 60 at X30). It shows as SPEED only at 12K+: -2.5 points.
  - Its wait loop 0C15A678 is skipped at both levels (98.7K and 77.9K
    skips/s). At X3 three more candidates run checks with no skip
    (0C0679F8, 0C02971A, 0C067930: 2.7-6.5K checks/s). Where the +5% goes
    is not split: run 13's census has no block above 4.1%.
- **HW run 15, 2026-10-12 ~00:21-00:27: JIT PR MOVES hit for the first
  time, in Quake3.cdi** (homebrew; X30, JIT BCACHE ON, JIT PR MOVES ON, JIT
  HOTBLOCKS OFF, JIT C-Calls ON; `b8_q3_prmoves.txt`). Matt: "I did a test
  on Quake 3 Arena. Can you check it?" `[jit8] prmoves=20` sites, no fault
  line, clean JIT EXIT; the boot goes through the same scenes as the old
  boot. No word from Matt yet on how it looked.
  - Speed against the old Quake3.cdi boot (`ps3log_spevb_run1.txt`,
    2026-10-07, cut as `q3_old.txt`; `dyn_ic=2 dve=1 greuse=1`, no
    bundle 2-8): **slower in every scene.** Loading screens SPEED
    31.8-32.3% (45.1-45.7%), the screen at 0.55K vertices 33.6% (47.1%),
    the demo 49.7-50.1% (61-62%), the menu 34.1% (48.2%), play at 1K+
    vertices 78.5% (94.6%).
  - **Cause, by the log and the code, no A/B run: TMU Fine TCNT (bundle 7,
    default ON since 2026-10-11), not bundle 8.** The game polls TCNT in a
    loop: `[CC] readmem a7` 113.2K per emulated vblank in both logs,
    `[tmu] tcnt reads 2188581/s, 2188581/s inside a slice`. The SH4 thread
    is busy all the time in these scenes (`slept=0`), so the time per loop
    turn is 1 s / call-outs: 325 ns then (3.14M/s), 465 ns now (2.22M/s).
    With the row ON `tmu_tcnt_now()` (`dc/sh4/tmu.cpp`) takes its long
    path on each read (`part != 0`: `tmu_units()`, a 64-bit run, the
    `tmu_step` loop, three counters). Bundle 7's note already said that
    the row had no run with a game in play.
  - Check that is open: TMU Fine TCNT OFF (TIMING page, next boot), the
    same Quake 3 load: SPEED about 45% is expected there. Then the fix is
    a short path for the read (a closed form, no counters) or the row OFF.
  - Second lead from the same log: these scenes are a wait on the timer
    (6.8M TCNT reads per emulated second). The area 7 wait loop of Capcom
    vs. SNK 2's menus is the same class.
- **HW run 17, 2026-10-12 ~00:29-00:34: the A/B. TMU Fine TCNT OFF in
  Quake3.cdi confirms run 15's cause** (`[timecfg] TmuFineTcnt=0`,
  `finetcnt=0`, X30, the other settings as run 15; `b8_q3_nofine.txt`).
  Matt: "Done." No fault line, clean JIT EXIT.

  | Quake3.cdi, SPEED | 2026-10-07 (old settings) | Fine TCNT ON (run 15) | Fine TCNT OFF (run 17) |
  |---|---|---|---|
  | Loading screens | 45.1-45.7% | 31.8-32.3% | 41.2-41.6% |
  | Screen at 0.55K vertices | 47.1% | 33.6% | 43.3% |
  | Demo (6.6K) | 61-62% | 49.7-50.1% | 61.2-61.3% |
  | Menu (0.65K) | 48.2% | 34.1% | 43.8-44.1% |
  | Play, 3-6K | 94.9% | 76.7% | 93.6% |
  | Play, 6K+ | 94.4% | 82.4% | 97.7% |
  | Host time per timer-poll turn (loading) | 318 ns | 452 ns | 357 ns |

  - TMU Fine TCNT costs about 95 ns per TCNT read. In a game that polls
    the timer 2-3M times/s that is 10 SPEED points in the loading screens
    and 15-17 in play. The rest of the gap to 2026-10-07 (357 against 318
    ns) is not split.
  - Its default is Matt's call ("Keep them as default, they seem
    harmless." on 2026-10-11, before a game that polls the timer ran). The
    ways: the row OFF by default, or a short read path in
    `tmu_tcnt_now()`.
- HW run 16, 2026-10-12 ~00:28 (between the two): Quake3.cdi at X3
  BACKWARD, TMU Fine TCNT ON (`b8_q3_x3_fine.txt`; not announced by Matt,
  the row was still at X3). **X3 is bad in this game too**: demo SPEED
  37-38% (50% at X30), play 42.5% (78.5%), 12 frames per wall second (20).
  The game waits by polling the timer: `readmem a7` 58.7K per emulated
  vblank in play at X3, 18.0K at X30. JIT IDLE SKIP has two candidates in
  that loop (8C08467C, 8C08468A, 98K and 86K checks/s) and skips none,
  with Fine TCNT ON and with it OFF. So a third kind of wait loop: a
  timer poll, where way (A)'s proof (same registers) cannot hold while
  the read value moves.
  - Reading after runs 2-17: X3 BACKWARD pays in games that wait in a loop
    the emulator does not skip, and each game waits in its own way (a
    memory counter in Looney Tunes, the timer in Quake3.cdi, not known in
    Sonic Adventure 2). Way (A) covers the first kind only, so X3 as the
    global default needs more than way (A).
- HW run 18, 2026-10-12 ~00:55-01:00, after the verdicts: Jet Grind Radio
  (US) at X3 BACKWARD (package of 22:43; JIT BCACHE ON, TMU Fine TCNT OFF,
  JIT C-Calls ON; `b8_jgr_x3.txt`), and a short boot at X30 before it
  (`b8_jgr_x30.txt`, 20 play windows). Matt: "can you check my Jet Set
  Radio run at x3 backwards? The average framerate and such." No fault
  line.

  | Jet Grind Radio, play windows (2K+ vertices) | X30 (20 windows) | X3 BACKWARD (476 windows, the whole run, clean JIT EXIT) |
  |---|---|---|
  | Frames per second, mean (lowest window) | 24.57 (19.5) | 29.96 (26.7) |
  | Windows at 29.4 frames/s or more | 25% | 99% |
  | Windows below 25 frames/s | 11 | 0 |
  | 12K+ vertices: frames per second | 23.7 (16 windows) | 30.0 (56 windows) |
  | SPEED, mean | 99.92% | 99.99% (one scene start at 89.2%) |
  | `[stall]` sleep, all / 12K+ | 305 / 275 ms/s | 320 / 180 ms/s |

  - A fourth game of the kind that gains: 30 frames in every scene at
    full speed, with 180 ms/s of sleep left in the heaviest scenes. The
    older X30 log (`ps3log_b4_run1.txt`, 2026-10-11): 28.93 frames/s over
    84 windows, 19.9 at 14K+.
- Matt 2026-10-12: "Is the new level on the JIT IDLE SKIP row risky,
  though?" Answered in the session: one fault kind (a work loop taken for a
  wait loop: that place loses frame rate; no wrong data, no crash), the
  5.25 history (Sonic Adventure 30 -> 20 frames/s before the register
  hash), a new level so the old path stays. No go yet for (A) or (B).
  - Lead, not built, no word from Matt: a rule that keeps the x30 on wait
    loops only and bills work blocks x1. It needs a JIT HOTBLOCKS profile
    at X3 first (which blocks take the new host time). Its best case is
    +14% SH4 work in the race, which has 5-16% of spare time: the same
    wall as SH4 Clock 220 (5.45, SPEED 97.5%, "basically the same").

**Update 2026-10-12 to the bundle 7 block below: TMU Fine TCNT is default
OFF again in source (Matt: "If it was FINE before, I'd keep it that way
for the TMU Fine and TCNT OFF."). It costs about 95 ns per TCNT read;
Quake3.cdi lost 10 to 17 SPEED points with it (bundle 8, HW runs 15 and
17, above). The three SPG rows stay ON. The packages of 22:16 and 22:43
have TMU Fine TCNT ON.**

**Done but TMU Clock: bundle 7, Time base (section 7; Matt 2026-10-11: "We
will now work at bundle 7."). HW runs 1 and 2 on 2026-10-11 (Crazy Taxi,
ChuChu Rocket; below). Verdict (Matt 2026-10-11): "Keep them as default,
they seem harmless." So TMU Fine TCNT ON, SPG Field Bit FRAME, HBlank IRQ
MODES and VBLANK INT Reset FLYCAST are the defaults in source since
2026-10-11 (`ps3/ps3_presets.cpp`) and in the package of 22:16
(`build/notdc_ps3.pkg`, 1528496 bytes; Matt 2026-10-11 after the source
change: "Place them on by default?", read as the package too). It differs
from the package of 21:15 only in these four defaults. Matt's `nullDC.cfg`
holds the rows ON from his runs. "Them" is read as the four rows he ran. TMU Clock stays SH4: it has no HW run,
because it acts only below SH4 Clock 200 and Matt keeps 200. TMU Fine TCNT
is the default with one run of 57 s (boot, load and a menu), no game in
play. Open: 6.4's HW test (SH4 Clock 150), if Matt wants it.**

**The build: five rows on a new options
page, TIMING (page 8), all at the old path in the package of 21:15
on 2026-10-11 (`build/notdc_ps3.pkg`, 1528496 bytes). Matt's picks
before the build (2026-10-11, asked once): all four items (6.4; P11 with
the TimerTest; P28's field bit and HBlank modes; P28's reset value); a TCNT
read inside a slice gives the value only (UNF and the IRQ stay at the slice
end); TimerTest "smallTCOR" stays as it is; a new page TIMING, not the
EMULATION page. The same package has one source edit that is not from this
session: `g_speed_limiter_preset` 0 -> 1 in `ps3/ps3_presets.cpp` (21:09);
an install keeps `nullDC.cfg`, so it acts on a new install only.**
- The rows (the key is in `nullDC.cfg` [ps3]; the first value is the old
  path):

  | Row | Key | Values | Acts | Default in source |
  |---|---|---|---|---|
  | TMU Clock | `TmuClock` | SH4 / EFF | live | SH4 (0), no HW run |
  | TMU Fine TCNT | `TmuFineTcnt` | OFF / ON | next boot, dynarec only | ON (1) |
  | SPG Field Bit | `SpgFieldBit` | LINE / FRAME | live | FRAME (1) |
  | HBlank IRQ | `HblankIrq` | VBLANK / MODES | live | MODES (1) |
  | VBLANK INT Reset | `VblankIntReset` | OLD / FLYCAST | next boot | FLYCAST (1) |

- TMU Clock EFF (6.4): below SH4 Clock 200 a slice counts x 200 / clock in
  the TMU, so the timers keep 50 MHz of Pclk per emulated second. **At SH4
  Clock 200 and above it changes nothing: its HW test needs SH4 Clock
  150.** The TMU now counts in 1/4096 SH4 cycle (`TMU_FRAC` 12,
  `dc/sh4/tmu.cpp`) in every mode. The slice's count is scaled once, with
  no carried remainder, so each slice has the same count and the SYS GATE's
  closed forms (5.36, 5.42) hold. Rate error at 150 MHz: 0.14 ppm or less.
  The rate is latched at a slice update (`tmu_clock_latch()`).
- TMU Fine TCNT (P11, Flycast `b31f92f8e`): a TCNT read adds the SH4
  cycles run inside the slice. The JIT stores `ppc_cycles` in
  `Sh4cntx.jit_cycles` (`emit_cycles_store()`, `ps3/ps3_driver.cpp`, 6
  places): before each cold memory C call, before each interpreter
  fallback, at the loop entry, at each slice end and on both RESUME paths.
  A block takes its cycles at its entry, so the resolution is one block, as
  in Flycast. The read works on a copy: an underflow inside the slice
  gives the reloaded count; UNF and the interrupt come at the slice end
  (Matt's pick). A write that changes a channel (TCNT, TSTR, a new TCR
  prescaler) first commits the cycles run so far (`tmu_commit()`), so a
  write and a read in one slice measure the time between them. A commit
  can set UNF. The interpreter engines keep the slice resolution. With
  SH4 Clock above 200 the SH4's longer slice is scaled to the module slice.
- SPG Field Bit FRAME (P28): `SPG_STATUS.fieldnum` flips once per frame, at
  line 0, as Flycast `spg.cpp:158-163`; LINE flips on each line. HBlank IRQ
  MODES (P28): mode 0 raises `holly_HBLank` at `SPG_HBLANK_INT`'s compare
  line, mode 2 on each line, modes 1 and 3 never (Flycast `spg.cpp:140-155`);
  VBLANK raises one at each vblank. With the reset value (mode 0, line 0)
  MODES moves the interrupt from the vblank line to line 0. A write to
  `SPG_HBLANK_INT` now syncs the SYS GATE (both values). VBLANK INT Reset
  FLYCAST: `SPG_VBLANK_INT` resets to 0x00150104 (SCANINT2 at line 21, not
  336); the BIOS writes the register.
- Log: `[timecfg]` at boot and at a menu close (SH4Clock, Accuracy and the
  5 keys). `[jitcfg]` has `finetcnt=`. `[tmu]` in the 1 s block, only with
  TMU Fine TCNT on or a rate that is not x1: the rate, TCNT reads/s, those
  with cycles inside a slice (and their average), commits/s. `[spg]`, only
  with one of the two SPG rows on: field flips/s, HBlank interrupts/s and
  the game's mode and compare line.
- PC (WSL), all on the real `tmu.cpp`, `SPG.cpp`, `regs.cpp` and
  `sh4_interpreter.cpp`:
  - Rows OFF: 24 timelines of `misc/sysgate_check` give the same trace,
    byte for byte, as the sources before the bundle. The new TMU step
    equals the old one count by count (7 clock and row sets, 48000
    slices each, TCOR down to 0).
  - `misc/sysgate_check` takes the rows from the environment (`TMUEFF=1`
    with `CLOCK=150`, `FINE=1`, `FIELD=1`, `HBLANK=1`); its guest also
    writes `SPG_HBLANK_INT`. PASS: 300 of 300 timelines equal in the 3
    gate modes with the rows OFF, and with each of 7 row sets (FINE; FIELD;
    HBLANK; TMUEFF at 150; all four at 150; FINE at 250; FINE with
    RACEFIX, G2SMALL and STALL). Its `mutate.py`: 63 of 63 planted bugs
    fail (2 patterns follow the new TMU text).
  - New `misc/timebase_check` (`run.sh`, `mutate.py`): each row and all
    rows against the three gate modes; Flycast's TimerTest; TCNT against a
    model that counts step by step in continuous time (reads, TCNT, TSTR,
    TCR and TCOR writes at random cycles, slice ends with a block's
    overshoot, clocks 150 to 250, both TMU Clock values, the three gate
    modes and slices); the TMU rate; the SPG rows; 24 source-line checks
    for the code that does not build on the PC. PASS: 7776305 checks, with
    0 of about 8300 TCNT reads per run different from the model (row OFF:
    60 to 75% differ). Its `mutate.py`: 62 of 62 planted bugs fail. One of
    them needed a directed case: a write past a slice end, then slices the
    gate skips, then a write early in a slice (the batch must not run over
    the committed part; `tmu_slices_quiet_run()` refuses it).
  - `misc/clock_check` PASS (2878 checks, 7 source lines), its `mutate.py`
    22 of 22; `misc/g2dma_check` ALL PASS (1280 runs); `misc/div32_check`
    PASS (it builds `sh4_registers.h`, which has the new `jit_cycles`).
- Flycast's TimerTest on our slices (27 asserts), with TMU Fine TCNT ON:
  22 pass as written (init 10 of 10, precision 8 of 9, underflow 4 of 4).
  1 of precision is the UNF flag, which comes at the slice end. smallTCOR
  0 of 4: TCOR 2 underflows more than once inside one slice, where ours
  takes a modulo and Flycast clamps (left as it is, Matt). With the row
  OFF: 16 of 27 (precision 5 of 9, underflow 1 of 4), as the plan said.
- Seen, not changed: at an underflow ours and Flycast both go to TCOR - 1,
  so a period is TCOR counts. The SH7750 manual loads TCOR, so the period
  is TCOR + 1 there (from memory of the manual, not checked on HW).
- HW steps as planned before the runs (package of 21:15; one row per run).
  The runs below did not follow them: the three SPG rows ran together, and
  TMU Fine TCNT ran on top of them:
  1. Crazy Taxi, SH4 Clock 200, rows OFF, 60 s: the baseline, and
     `[timecfg] boot:` in the log.
  2. SELECT menu, TIMING: SPG Field Bit FRAME (live). `[spg]` shows
     `field flips` (about 60/s when interlaced). Any image or speed fault?
  3. SPG Field Bit back to LINE, HBlank IRQ MODES (live). `[spg]` shows
     `hblank irq` and the game's mode and line. Any fault?
  4. TMU Fine TCNT ON, then boot again (it is a next-boot row). Check
     `finetcnt=1` on `[jitcfg]` and the `[tmu]` line. Crazy Taxi, Sonic
     Adventure and Soul Calibur (not for speed; Flycast issue #2093 was its
     randomness).
  5. VBLANK INT Reset FLYCAST, then boot the BIOS and one game: no change
     is expected.
  6. ChuChu Rocket at SH4 Clock 200 (Matt 2026-10-11: "Also keep ChuChu
     Rocket at 200."), so no SH4 Clock 150 run. At 200 TMU Clock EFF
     changes nothing and `[tmu]` shows `rate x1.0000`: the run checks the
     other rows on ChuChu Rocket, and TMU Clock EFF stays without a HW test.
- Listener (Matt 2026-10-11 21:47: "Boot the Listener. I'll test Crazy
  Taxi."): the listener of the bundle 5 session still runs (pid 6248) and
  owns port 18194, so no second one was started. The log is
  `ps3log_b5_run2.txt` (scratchpad of session 01bbad3e); the application
  start of this test is at line 408307. Its banner (`NotDC Oct 11 2026
  19:14:58`) does not name the package: a launch from the package of 21:15
  prints `[timecfg] boot:` and `finetcnt=` on `[jitcfg]`.
- HW run 1, 2026-10-11 21:48, Crazy Taxi, the package of 21:15, SH4 Clock
  200 (`ps3log_b5_run2.txt`, lines 408307-414765; `runlog.py` in the
  scratchpad of session e70b315a reads a run). Matt: "Crazy Taxi is done."
  (no fault reported). `[timecfg] boot: SH4Clock=200 Accuracy=0 TmuClock=0
  TmuFineTcnt=0 SpgFieldBit=1 HblankIrq=1 VblankIntReset=1`: the three SPG
  rows ON from boot in one run, the two TMU rows OFF. So SPG Field Bit
  FRAME, HBlank IRQ MODES and VBLANK INT Reset FLYCAST ran together; no
  run has one of them alone.
  - 294 s, clean XMB exit (JIT EXIT), no fault line (13 lines of the usual
    kind: I-cache invalidations, one unimplemented register read per boot).
  - `[spg] field flips 60/s | hblank irq 60/s (mode 0, line 0)` in 196 of
    199 windows (50/s in the BIOS). The compare line is 37 in 3 windows at
    boot, so the BIOS or the game writes `SPG_HBLANK_INT`. With mode 0 the
    row raises one interrupt per frame, as the old path, at another line.
  - SPEED in the drive (181 windows, NTSC 480i): 5-11K vertices 100.02%
    (79 windows), 11-13K 100.08% (22), 13K+ 96.73% (16 windows, 16-21K
    vertices, lowest 85.02%). The 13K+ dips are the known dense scenes
    (HW run 4 of bundle 5: 97.6%); no base run of the same scenes, so the
    log shows no cost and proves none. The 6.38% window is the SELECT menu.
  - Matt set TR Triangle Sort 3 -> 0 in the SELECT menu at line 410075; his
    `nullDC.cfg` now holds 0.
  - `[gate]` medians: runs 28341/s, `tmu` 0, `spg` 266: Crazy Taxi's
    timers do not underflow in play, and the log has no TCNT read count
    with TMU Fine TCNT off.
- HW run 2, 2026-10-11 21:54, ChuChu Rocket, the package of 21:15, SH4
  Clock 200 (same log, lines 414766-418589, two application sessions).
  Matt: "Went to the menu. Closing now." (no fault reported).
  - Session 1 (lines 415353-416654): `TmuFineTcnt=0`, the three SPG rows
    ON. 20 windows, up to the BIOS and the load; Matt set TMU Fine TCNT in
    the SELECT menu and left through the XMB (clean).
  - Session 2 (lines 416769-418589): `[timecfg] boot: ... TmuFineTcnt=1
    SpgFieldBit=1 HblankIrq=1 VblankIntReset=1` and `finetcnt=1` on
    `[jitcfg]`. First HW run of TMU Fine TCNT: the BIOS, the load and
    about 15 s of the game's menu (0.11K vertices); no stage. 57 s, clean
    XMB exit (JIT EXIT), no fault line.
  - The row acts: `[tmu]` in all 44 windows, and each TCNT read has cycles
    inside a slice (the two counts are equal in every window). The average
    position is 790-990 cycles of the 1792-cycle slice in 42 windows (the
    middle, as a read at a random point gives), so the JIT's stores hold
    real counts. ChuChu Rocket reads TCNT 360/s in its menu (6 per frame)
    and 300/s in the BIOS; the boot and the load have bursts of 6.5K to
    133.6K reads/s (wait loops on TCNT). Commits (TMU writes inside a
    slice): 13/s, 3/s and 3/s in three windows of the boot, 0 after.
  - SPEED 99.85% on average. The 83.90% window is the change from PAL to
    NTSC (50.29 vblanks against 59.95), not a slow window. `[spg]` 60/s
    in the menu, mode 0, line 0.
  - Not shown by this run: a stage, and a game in play with the row ON.
    The sound fault F1 of ChuChu Rocket (section 6, parked): no word from
    Matt on it in this run.
- Verdict and defaults, 2026-10-11 (Matt, asked what he saw and whether to
  run a game in play: "Keep them as default, they seem harmless."):
  `g_tmu_fine_tcnt_preset`, `g_spg_field_bit_preset`, `g_hblank_irq_preset`
  and `g_vblank_int_reset_preset` are 1 in `ps3/ps3_presets.cpp`;
  `g_tmu_clock_preset` stays 0. Packaged at 22:16 (only
  `ps3_presets.cpp` compiled again). An install keeps
  `nullDC.cfg`, so a source default reaches a new install only. The source
  lines of `misc/timebase_check/run.sh` hold the new defaults (PASS, 7776305
  checks and 24 lines); its `mutate.py`: 63 of 63 planted bugs fail (a
  wrong default of TMU Clock and of TMU Fine TCNT are two of them).
  `misc/sysgate_check` keeps its rows OFF unless the environment sets them.

**PARKED by Matt 2026-10-11 ("Let's park this for now. I will test it
with Bangai-O and Nomad Soul later on."): bundle 6, G2 DMA (section 7;
Matt 2026-10-11: "Work on bundle 6."). HW run 1 done, no fix for the
ChuChu Rocket sound fault (F1; Matt after the run: "Neither fixes the
issue."). The rows work and stay OFF. Open: Matt's test of Bangai-O and
Nomad Soul, the games P24 names (step 5 below). Two rows at the end of the
AUDIO page, both OFF by default, both live: G2 DMA Small (P24,
`G2DmaSmall`) and G2 DMA Read (P9, `G2DmaRead`). The package of 19:22 on
2026-10-11 has them
(`build/notdc_ps3.pkg`, 1525680 bytes); it differs from the package of
18:46 only in these two rows and their two log counts. Matt 2026-10-11
20:20: "Start the listener." The listener of the bundle 5 session still
runs (pid 6248) and owns port 18194, so no second one was started: HW run
1 of bundle 6 is in `ps3log_b5_run2.txt` (scratchpad of session 01bbad3e)
from line 392820, a ChuChu Rocket boot with both rows OFF.**
- HW run 1, 2026-10-11 ~20:20, the package of 19:22, ChuChu Rocket, SH4
  Clock 200 (`ps3log_b5_run2.txt`, lines 392820-394994): 14 `[aicafix]`
  windows (~70 s), SPEED 100%, clean XMB exit, no fault line.
  - Rows: both OFF at boot; both ON from line 394132; Small ON, Read OFF
    at 394713; Small OFF, Read ON at 394796.
  - G2 DMA Small acted: 12 transfers ended in the start call (`small` 2,
    1, 7, 2, each equal to or below `g2 starts`); 7 of them in the window
    of the 1846 KB load (12 starts). `small` reads 0 with the row OFF.
  - G2 DMA Read did not act: `rd` 0 in every window. ChuChu Rocket sends
    no SB_ADDIR = 1 transfer, so this row cannot change it.
  - The run has no stage traffic: at most 12 starts per window, against
    70-134 in the stage of `ps3log_b2_run1.txt`. So the row ran on the
    boot, the load and the menus only.
  - Matt: "Neither fixes the issue." Read here as F1 (the cues that loop
    and play slowly). So the end time of the small G2 DMAs is not F1's
    cause in the menus. F1 stays parked (section 6).
- G2 DMA Small ON: an AICA G2 DMA of 512 SH4 cycles or less (128 bytes or
  less at SH4 Clock 200) ends in its start call: the registers, then the
  end IRQ (Flycast `aica_if.cpp:405-409`). Longer ones stay scheduled. It
  acts only with DMA Fix ON (with DMA Fix OFF every transfer ends at once
  already). A start over a running transfer (possible only with AICA Race
  Fix OFF) keeps the scheduled path, so each start still has one end. The
  Read_SB_ADST guard is not changed.
- G2 DMA Read ON: SB_ADDIR = 1 copies from SB_ADSTAG (wave RAM) to
  SB_ADSTAR (system RAM), word by word through the memory handlers, so no
  byte order change. The registers and the end are those of a write. OFF:
  the message box and no end, as before.
- Where: `Write_SB_ADST` in `dc/aica/aica_if.cpp`. `[aicafix]` has two new
  counts after `end31`: `small` (ends in the start call) and `rd`
  (SB_ADDIR = 1 transfers). `[sndcfg]` prints both keys.
- PC (WSL): `misc/g2dma_check` now has 32 configurations (AICA Race Fix,
  DMA Fix, Scheduler, G2 DMA Small, G2 DMA Read) x 3 seeds, 40 runs each:
  PASS, 3840 of 3840 runs, with 45373 small ends, 21687 SB_ADDIR = 1
  transfers and 37936 refused ones; the two new `[aicafix]` counts equal
  the model's. One length in five is at the 128-byte limit. `mutate.sh`:
  21 of 21 planted bugs fail (9 new: the row tests, the limit one step
  low and high, an end over a running transfer, the copy direction; the
  "guard read returns 0" one named a line number and did not apply any
  more, now a pattern). `misc/sysgate_check` with `G2SMALL=1`: 1200
  timelines equal in the 3 gate modes, also with `RACEFIX=1` (about 1
  timeline in 12 has a small end); `misc/clock_check` and
  `misc/aicactl_check` (64 seeds) PASS. `sysgate_check` did not link
  since bundle 4 (three names missing in its `stubs.inc`): added.
- Test game: ChuChu Rocket, not Looney Tunes (Matt 2026-10-11: "Wait
  Looney Tunes? And not ChuChu Rocket?"). The first HW steps named Looney
  Tunes from the bundle text, with no look at the transfer sizes. The old
  logs give the sizes (`[aicafix]`: starts and KB per 5 s window):
  - ChuChu Rocket (`ps3log_b2_run1.txt`, lines 14474-20061): 470 starts
    in 16 windows. Two loads are 1881 and 1846 KB. In the stage the
    windows read 16 starts / 1 KB, 70 / 2, 88 / 4, 134 / 10, 107 / 6: 30
    to 75 bytes each on average, below the 128-byte limit. Each one has
    bit 31 of SB_ADLEN (`end31` = starts) and one guard read (`irqrd`).
  - Looney Tunes (`ps3log_step3_run1.txt`): 340-363 starts / 168-181 KB
    in a race, so 512 bytes each. They stay scheduled: the row does
    nothing in a race.
  - Crazy Taxi (`ps3log_b2_run1.txt`): 0 starts in 70 windows.
  ChuChu Rocket also has the open echo fault (F1, section 6, parked). No
  evidence links it to this row.
- HW steps (package of 19:22, SH4 Clock 200, both rows OFF at boot):
  1. ChuChu Rocket, a stage, 60 s: note `g2 starts` on `[aicafix]` and
     the sound (the echo of F1 is there today).
  2. SELECT menu, AUDIO: G2 DMA Small ON (live). 60 s more in a stage.
     `small` shows the transfers that ended at once. Sound as before,
     better or worse?
  3. G2 DMA Read ON: `rd` shows SB_ADDIR = 1 transfers (0 expected).
  4. Boot ChuChu Rocket with G2 DMA Small ON: the two loads and the menus.
  5. Bangai-O and Nomad Soul, if Matt has them: boot with G2 DMA Small
     OFF (startup freeze?), then ON.

**Done: the Crazy Taxi crackle (F2, 6.6). HW SUCCESS 2026-10-11 with SPE
AICA Sync at + SAMPLE SYNC (HW run 4; Matt, menus: "At +SAMPLE seems to
work well? No crunch."; in the drive: "Pretty good!", then "Fully
gone."). Verdict (Matt 2026-10-11): "Let's set it as default." So + SAMPLE
SYNC is the default since the package of 18:46 on 2026-10-11
(`build/notdc_ps3.pkg`, 1525296 bytes; it differs from the package of
16:55, the one of HW run 4, only in this default). Matt's `nullDC.cfg`
has the level from his run. The missed tests were the cause, not the
clipping. The two levels came as the quick proof of the missed tests
(step (b) of HW run 2; Matt after HW run 3: "Yes, go with b"). The row is
OFF / ON / + BLOCK SYNC / + SAMPLE SYNC, live. Not built, not needed now:
step (a), the test on the SH4 thread (it only removes the SH4 wait of
1-5% of wall, which no SPEED figure shows). + BLOCK SYNC is not tested on
HW. SPE AICA Gain stays at 100%. The listener runs (Matt: "Start it."),
into `ps3log_b5_run2.txt`.**
- HW run 4, ~18:36, the package of 16:55, Crazy Taxi, + SAMPLE SYNC from
  boot, SPE AICA Gain 100%, SH4 Clock 200 (`ps3log_b5_run2.txt` from line
  386809; `synclog.py` in the scratchpad lists the windows). Clean XMB
  exit, `bad=0`, timeouts 0, no fault line.
  - Drive (23 windows): `miss/s` 0 in every window, `tests/s` 0, 61-99
    syncs/s (average 77), sync wait 119-285 us, SH4 wait 1.1-4.6% of wall
    (average 2.6%). Hits 330-2240/s, less than at ON (4000-7800/s): the
    map is made again with lag 0 after each sync, so it marks fewer pages.
  - SPEED by vertices per render: below 11K 99.95-100.04% (182 s); 11-13K
    100.2% (22 s, lowest 99.7%); 13K+ 97.6% (13 s, lowest 92.4%, 5 s
    below 97%). HW run 2 at ON: 100.3% and 98.0%. No cost that this log
    can show; the 13K+ sets are too short to tell 0.4 points.
  - The output still clips: `clip/s` 20-99 in most drive windows, `peak`
    125-167%, and Matt hears no crackle. So the clipping is not audible
    as a crackle, and the reading of HW run 3 that the high peaks are the
    crackle itself was wrong: the mix is that loud.
  - Sound SPU busy 17-22%, ARM7 SPU 23-26%.
  - A boot before it (from line 12569) has no test data: 45 s of game,
    then 26 minutes in the SELECT menu, then a reset into the BIOS (360K
    `[vmem] Write16 to unmapped` lines in 19 s) and the exit. Matt: "I did
    something wrong, sorry."
  - After the run, Matt: "Fully gone. Let's set it as default."
- + BLOCK SYNC (`SpeAicaSync=2`): the first write into a marked page in a
  64-sample block syncs with no test. Other writes at that sample pass
  free; writes at other samples of the block are a `miss`, as before.
  This is step (b) as offered.
- + SAMPLE SYNC (`SpeAicaSync=3`): each sample with a write into a marked
  page syncs, no test, no limit per block. Added because a burst of
  writes can go over 2-3 samples (not measured): + BLOCK SYNC then still
  misses the later ones, and a crackle that stays would prove nothing.
  With this level `miss` stays at 0, so the proof is clean.
- Where: `hz_write` in `ps3/spu/spe_arm7.cpp` (ARM_CFG_BLOCK, ARM_CFG_ALL
  in the CFG command), `arm_spe_hz_hit` in `ps3/ps3_arm_spe.cpp` (level 2
  asks once per block, level 3 at each sample with a hit), and the PPU
  twin's copy in `ps3/ps3_aica_spe.cpp`.
- Cost, estimate only: one sync is ~240 us of SH4 wait in the logs; a
  drive has ~55 blocks/s with hits, so 1-2% of wall at + BLOCK SYNC and
  up to 3 times that at + SAMPLE SYNC. `[aram-hz]` shows it: `syncs/s`,
  `sync wait`, `sh4 wait % of wall`; `tests/s` reads 0 at both levels.
- PC (WSL): `misc/arm7spe_check` has 9 new seeds (148-151 + BLOCK SYNC,
  180-190 + SAMPLE SYNC; the harness is the PPU and the sound SPU): the
  log, the map, the counts and the answers equal the x86 reference. Two
  planted bugs fail (+ SAMPLE SYNC as + BLOCK SYNC; the levels as ON).
- HW steps (package of 16:55; SPE AICA Gain back at 100%; SH4 Clock 200):
  1. Crazy Taxi, a dense drive, SPE AICA Sync ON: 60 s, note the crackles.
  2. SELECT menu, AUDIO: + SAMPLE SYNC (live). 60 s. Crackle gone: the
     missed tests are the cause, and step (a) is the fix without the cost.
     Crackle still there with `miss/s` at 0: the cause is somewhere else.
  3. If it is gone: + BLOCK SYNC for 60 s, to see if the cheaper level is
     enough.
  4. Log: `syncs/s`, `miss/s`, `sh4 wait`, SPEED by vertices.

**HW run 3 done, no verdict yet: SPE AICA Gain, the SPU output gain that
6.6 names as its next step (Matt 2026-10-11, after HW run 2 below: "My
bet's on the output clipping."). The row works, but the log does not
support clipping as the cause of the crackle that is left (HW run 3
below). A 7th row at the end of the AUDIO page: 50% to 100% in steps of
5, live, default 100% (no change). The package of 15:36 on 2026-10-11 has
it (`build/notdc_ps3.pkg`, 1525680 bytes). Matt's last level in the run
was 65%. The listener runs since HW run 2 (Matt: "Start it."), into
`ps3log_b5_run2.txt`.**
- HW run 3, 2026-10-11 ~16:08, the package of 15:36, Crazy Taxi, SPE AICA
  Sync ON, SH4 Clock 200 (`ps3log_b5_run2.txt` from line 7221;
  `gainlog.py` in the scratchpad lists the windows). Levels: 80% from
  boot (menus only), 100% (2 windows, menu), 90% (3 drive windows), 70%
  (9), 60% (2), 65% (4). Clean XMB exit, no fault line, timeouts 0.
  - Matt: "The crackles get weaker once I reach 65-60%."
  - `peak` (before the gain): 68-77% in the menu, 110-181% in the drive.
  - Clipped samples in the drive: HW run 2 at 100%: 5-60/s, up to 130.
    This run: 90%: under 3 per window (`clip/s` 0/0); 70%: 0-2/s; 65%:
    0-3/s; 60%: 0. So from 100% to 70% the clipping fell by about 98%,
    and Matt heard no change before 65-60%.
  - Reading (not proven then; HW run 4 above confirms the first part):
    the clipping is not the cause. A crackle made by the clamp stops when
    the clamp stops; this one only gets weaker, as every sound does at a
    lower level. The second part was wrong: the peaks up to 181% are not
    the crackle itself (HW run 4 has such peaks and no crackle).
  - The other candidate of HW run 2 stands: miss/s 2400-6300 in the
    drive (writes into marked pages with no test), tests ~220/s, 5
    hazards found. SH4 wait 0.03-4.9% of wall.
  - SPEED: no cost seen (the gain is 2 multiplies per sample on the SPU).
  - Offered: step (b) of HW run 2 as a quick proof (one sync per block
    that has a hit, no test), then (a) if it proves the cause. Matt
    2026-10-11: "Yes, go with b". Built (above).
- What it does: the sound SPU multiplies each output sample by the level
  (in 1/256: 90% = 230, -0.93 dB) after MVOL and DAC18B and before the
  16-bit clamp (`AicaModel::out_gain`, in `final_mix` and `run`). nullAICA
  does the same with its fixed Volume of 90, so 90% is nullAICA's level at
  an even MVOL step. SPE AICA Sound only; nullAICA's mixer is not changed.
- Live path: the PPU writes the level into the sound SPU's bell line
  (`AicaBell::gain`, `gain_tick` in `ps3/ps3_aica_spe.cpp`, at each CHECK);
  the SPU reads it at each wake, before the next batch of records
  (`am_batch`). No record, no twin: the same with SPE ARM7 ON or OFF. The
  key is `SpeAicaGain` in `nullDC.cfg` [ps3]; `[sndcfg]` shows it.
- Log, on the `[aica-spe] sound` line: `gain=` (the row), `peak=` (the
  loudest output sample of the ~5 s window before the gain, 100% = full
  scale), and `clip/s` as before (counted after the gain). No clip when
  peak x gain is below 100%: the level to pick is 100 / peak.
- PC (WSL), all PASS: `misc/spu_sim/run_ring.sh` has 3 new runs on the real
  SPU image (a fixed level; a change at half the run; the same with the
  DSP): every frame equal to the x86 model with the same level, clip 43 ->
  7 at 230 in the mix scenario (peak 124%). The 40 older runs of that
  script, `run.sh`, `run_dsp.sh`, `diff_seeds.sh` (`diff_adv`, `diff_a3`,
  8 seeds), `misc/hz_check` (8 seeds), `misc/arm7spe_check` (37 seeds),
  `misc/a2_check`, `misc/a3_check`: as before.
- LS: sound SPU image end 0x3D9D0 (was 0x3D7D0); stack use 4.8 KB in the
  simulator, 4.9 KB spare (was 5.4). ARM7 SPU end 0x36770, no change.
  Link: `.got` 0xD458, no warning.
- HW steps (package of 15:36; an install keeps `nullDC.cfg`; SPE AICA Sync
  ON, SH4 Clock 200):
  1. Crazy Taxi, a dense drive, SPE AICA Gain at 100%: about 60 s, note
     the crackles.
  2. SELECT menu, AUDIO, last row: SPE AICA Gain 90% (live). 60 s.
  3. If it still crackles: 80%, then 70%.
  4. Log: `clip/s`, `gain=`, `peak=` per window. The crackle gone with
     `clip/s` at 0: clipping was the cause. The crackle still there with
     `clip/s` at 0: it is the missed tests of HW run 2, step (a) there.

**Bundle 5, AICA RAM coherency (section 7; Matt 2026-10-11: "We now work
on bundle 5."): SPE AICA Sync (6.6) is HW SUCCESS 2026-10-11 in the Crazy
Taxi main menu (Matt: "With AICA Sync ON the sound is fixed."), and DSP
Ring RAM (P20 G2) ran with no fault in Phantasy Star Online (Matt: "PSO is
okay"). Verdict (Matt 2026-10-11): "If they ran with no fault place them
ON then." So SPE AICA Sync and DSP Ring RAM are ON by default since the
package of 15:02 on 2026-10-11 (`build/notdc_ps3.pkg`, 1524720 bytes; it
differs from the package of 04:16, the one of HW run 1, only in these two
defaults). Matt's `nullDC.cfg` has both ON, and Mono Mix Fix ON, from his
run. The other 4 rows stay OFF: Mono Mix Fix ran in Looney Tunes and PSO
with no word on it; DSP Ring 64K, DSP Reg Reads and LFORE Keep are not on
HW yet. Matt's picks
before the build (asked once): all four items (6.6, P20 G2 + G3, P20 G4,
P31 Mono + LFORE), and for 6.6 the hazard test on a page map that the twin
makes. 6 new rows at the end of the AUDIO page. The package of 04:16 was
also the first one with 3k's default (TR Triangle Sort at + OPAQUE DEPTH).
An install keeps `nullDC.cfg`.**
- Open after HW run 1, not bundle 5 items:
  - Looney Tunes: the balance of voices and music is wrong (Matt: "Voices
    and music is just unbalanced."), with every row of the run ON or OFF.
    Parked by Matt 2026-10-11: "We'll investigate the Looney Tunes stuff
    later on."
    Which one is too loud is not known. A lead from the code, not
    measured: the voices reach the output only through the DSP (EFREG at
    EFSDL / EFPAN) and the music is CD audio (EXTS); caique marks both
    output levels as not verified on a console (P20, "Output stage").
  - The SPU output clips: Crazy Taxi drive up to 55 samples/s, one ChuChu
    Rocket window ~1300/s. 6.6 names the cause (0.9-1.4 dB louder than
    nullAICA) and the next step (an SPU output gain). Built 2026-10-11 as
    SPE AICA Gain (above), HW not run.
  - The cost of Sync in a dense Crazy Taxi drive: measured in HW run 2
    (below), no SPEED loss.
- HW run 2, 2026-10-11 ~15:10, the package of 04:16 (the one installed),
  Crazy Taxi drive, SH4 Clock 200 (`ps3log_b5_run2.txt`, same scratchpad).
  Rows of the boot: SPE AICA Sync ON, DSP Ring RAM OFF, Mono Mix Fix OFF,
  TR Triangle Sort OFF (Matt's file). Sync went OFF twice for 1-2 s.
  - Matt: "The audio crackle with the settings you saw in the log happens
    but It's much less prevalent now. Not entirely gone, though."
  - SPEED with Sync ON, by vertices per render: below 11K 100.0-100.2%
    (210 s); 11-13K 100.3% (25 s, lowest 99.5%); 13K+ 98.0% (14 s, lowest
    91.8% at 19.9K). The boots of 2026-10-10 without Sync and with the
    same TR Triangle Sort OFF: 11-13K 99.8-100.1%, 13K+ 97.4-99.5%. So
    Sync costs no SPEED that this log can show. SH4 wait for the tests
    0.1-4.6% of wall per window, about 1% on average.
  - Why the crackle is not all gone, from the log: in the drive the SH4
    writes 4000-7800 times/s into marked pages. The limit of 4 tests per
    block lets about 220 tests/s through (so ~55 blocks/s have hits: one
    burst of ~100 writes per frame, inferred). The other 96% are `miss`
    (4000-7600/s), with no test and no sync. The tests found 15 hazards
    in the drive; how many the missed writes hold is not counted. In the
    menu the bursts are small, so most hazards got their test: that is
    why the menu sounded fixed.
  - Second candidate, not separable in this log: the SPU output clips in
    the drive in 33 of 48 windows, up to 130/120 samples/s.
  - The steps offered after the run: (a) the SH4 thread tests a write
    against the slots' spans itself (the twin sends a table per CHECK), so
    only a likely hazard asks the ARM7 SPU: no limit on tests, and less
    SH4 wait than now; (b) a quick rule: after 4 tests without a hazard in
    a block, one sync without a test, ~55 syncs/s in a drive, ~2% of wall
    (estimate); (c) the SPU output gain of 6.6, as a row of its own.
  - Matt 2026-10-11: "My bet's on the output clipping." So (c) is built
    (SPE AICA Gain, above). (a) and (b) are not built.
- HW run 1, 2026-10-11 ~14:00, the package of 04:16, Crazy Taxi, SH4 Clock
  200 (`ps3log_b5_run1.txt`, scratchpad of session 01bbad3e).
  - Boot 1: Matt's `nullDC.cfg` had SPE AICA Sound OFF (`SpeAicaSound=0`,
    cause not known), so nullAICA played and SPE ARM7 stayed off. Matt:
    "With it off it works properly." `nullAICA clip 0/0`. No test of 6.6.
  - Boot 2: SPE AICA Sound ON (DSP, DSP JIT and SPE ARM7 ON), main menu
    with music. Sync OFF, Matt: "Cracks are about 3-4 within 10 secs."
    Sync ON from the SELECT menu, Matt: "With AICA Sync ON the sound is
    fixed."
  - Log, Sync ON, 15 windows of ~5 s: SH4 writes into marked pages 236/s
    (no DMA, no ARM7 store); 28 exact tests/s; 15 hazards in all, 13 of
    them in one window, each 64-255 positions behind the twin's play
    point, each with one sync (53-154 us of sound SPU wait); `miss` 202/s
    (hits over the block's limit, not tested); timeouts 0; no fault line.
    So the cause 6.6 gave is the right one, and the writer is the SH4.
  - Not the cause: clipping. `clip/s` 0/0 in every menu window (2/0 and
    5/4 in two windows before the menu).
  - Cost: SH4 wait for the tests 1.0% of wall on average, 4.9% in the
    worst window. SPEED stayed at 100% (the menu leaves the SH4 spare
    time). About 1 test in 100 finds a hazard, and each test waits ~0.4 ms
    for the ARM7 SPU.
  - Then a drive of ~30 s with Sync ON (4.5-12.5K vertices per render,
    one second above 11K). Matt: "It worked fine". SPEED 99.3-100.7% in
    every second, clean XMB exit. Per 5 s window: SH4 writes into marked
    pages 1400-4600/s, 140-217 tests/s, hazards 0.6-5.4/s, `miss` up to
    4342/s, SH4 wait 1.4-4.0% of wall, timeouts 0. A dense drive (11-13K)
    with Sync ON is not measured yet.
  - New fact: the SPU output clips in the drive: `clip/s` 34/16, 31/25,
    54/55 in its three windows (0/0 in the menu). No word from Matt on it.
    6.6 names the cause (the SPU path is 0.9-1.4 dB louder than nullAICA)
    and the next step (an SPU output gain).
  - Boot 3, ChuChu Rocket, Sync ON (kept in `nullDC.cfg`). Matt: "ChuChu
    still has the slowed sound cues issue" (F1, section 6, parked: no row
    of bundle 5 targets it). Log: 0 hazards, 0 DSP register reads; one
    window with `clip/s` 1269/1320, the others 0/0.
  - Boot 4, Looney Tunes, Sync + DSP Ring RAM + Mono Mix Fix ON (Matt
    before the boot: "Looney Tunes also had weird audio balances"; after
    it: "The Looney Tunes balance is still wrong." Open: which part is
    too loud or too quiet is not known yet; no row of bundle 5 changed
    it). Log: 6 hazards, all from G2 DMA writes (2 under 16 positions, 4
    under 64), each with a sync; the ring is idle (0 lines in, 0 out: the
    DSP program has 2 live steps and no ring access, so DSP Ring RAM
    cannot change the load echo here); `clip/s` 0/0; SPEED 100.1%, 15 s at
    11K+ vertices at 100.4%; sound SPU busy 4.8%.
  - Boot 5, Phantasy Star Online Ver. 2, the same rows, 241 s. Matt: "PSO
    is okay." Log: DSP
    Ring RAM is in use: 7680 ring lines loaded, 3.04M written back (~18
    per CHECK, as planned for a real program), no guest write into the
    ring, `slow=0`, `bad=0`; sound SPU busy 18.4% (max 23.0%; no boot
    without the row in this log to compare). 22 hazards from SH4 writes
    (16 under 64 positions, 6 under 256), each with a sync; SH4 wait 1.75%
    of wall (max 4.0%); SPEED 99.98%, 28 s at 11K+ at 100.0%.
  - All boots: timeouts 0, no fault line, clean XMB exits, 0 DSP register
    reads (DSP Reg Reads has no use in these 4 games). The log cannot show
    if a game sets MONO, so it does not tell if Mono Mix Fix acted.
- Why 6.6 changed: 6.6 put the hazard test on the PPU, with the PPU twin's
  CA. With SPE ARM7 ON (the default since 2026-10-05) the PPU runs no twin:
  the twin is on the ARM7 SPU. So the twin makes a map of wave RAM, and the
  SH4 thread tests its writes against the map.
- The rows (options page 2, AUDIO; the key is in `nullDC.cfg` [ps3]). All
  need SPE AICA Sound; the DSP rows need SPE AICA DSP too. `[sndcfg]` shows
  the six keys.
  - SPE AICA Sync (`SpeAicaSync`, 6.6): OFF / ON, live. A wave RAM write
    waits for the sound SPU when a slot has yet to play that byte. Since
    the package of 16:55 also + BLOCK SYNC and + SAMPLE SYNC (top of this
    section).
  - DSP Ring RAM (`DspRingRam`, P20 G2): OFF / ON, set before boot. The
    sound SPU's DSP ring (up to 32K words) is wave RAM for the guest too,
    both ways.
  - DSP Ring 64K (`DspRing64K`, P20 G3): OFF / ON, set before boot. DSP
    writes of a 64K-word ring, and TABLE writes past the ring, reach wave
    RAM.
  - DSP Reg Reads (`DspRegRead`, P20 G4): OFF / ON, set before boot. A read
    of TEMP, MEMS, MIXS or EFREG (0x4000-0x45BF) returns the DSP's value.
  - Mono Mix Fix (`AicaMonoFix`, P31): OFF / ON, set before boot. MONO
    output is (L + R) / 2 (Flycast 136ce267d), in the SPU model and in
    nullAICA's mixer.
  - LFORE Keep (`AicaLfore`, P31): OFF / ON, set before boot. A write to
    the LFO register leaves LFORE set (Flycast 27fd0d426; caique keeps it).
    Only the register read-back changes.
- 6.6, how it works:
  - The map: 1 KB, one bit per 256 bytes of wave RAM. At each CHECK (64
    samples) the twin marks, for each slot that plays, the bytes from the
    sound SPU's play point (its lag + 8 samples behind the twin's CA) to
    256 samples ahead. Loops wrap; a slot with LEA at or below LSA, or with
    CA past LEA, gets all its positions. The model does it
    (`AicaModel::hz_mark`, `hz_hit` in `plugs/caiqueAICA/aica_model.cpp`).
  - The SH4 thread tests each wave RAM write (`aram_hz_write` in
    `libAICA_WriteMem_aica_ram`; a G2 DMA goes the same way). A hit sends
    `ARM_CMD_SYNC` and waits for the ARM7 SPU. That SPU makes the exact
    test of 6.6 (`hz_hit`: the byte is between the sound SPU's play point
    and the twin's). Only then it sends `AICA_OP_SYNC` and waits for the
    sound SPU, which plays up to that sample, then drops its cache. Then
    the write lands. The ARM7 SPU tests its own stores the same way
    (`aram_store`).
  - Limits per 64-sample block: 4 exact tests, 1 sync. More are counted as
    `miss`. After a sync, other writes at the same sample pass free. The
    SH4 also asks when it is more than 192 samples ahead of the map
    (`stale`).
  - No answer from the sound SPU in 50 ms (PPU twin: 100 ms): sync goes
    off for the session (`timeouts`).
  - Always on, as 6.6 says: the sound SPU drops its wave RAM cache at the
    start of each batch of records.
- P20 G2: while a DSP program is set, the LS ring is a copy of the ring in
  wave RAM. The SPU loads it when the program, RBP or RBL changes. At each
  CHECK it writes back the lines (128 bytes) that the round's MWT steps
  wrote (worked out from the program: MADRS + MDEC_CT; an ADRS-relative
  step marks 2048 words each way). A guest write into the ring syncs first
  (the ring's pages are on the map), then goes to the SPU as an
  `AICA_OP_RING` record, and the SPU loads those lines again.
- P20 G3: a DSP write through the line cache marks the line; the SPU puts
  it back before the line leaves the cache (at each CHECK, at a sync, when
  another line takes its place).
- P20 G4: a read sends `AICA_OP_DSPRD` and waits for the sound SPU's value
  (one sync per read). With the row OFF the reads are only counted.
- Where it runs: the twin's side is in `ps3/spu/spe_arm7.cpp` (SPE ARM7 ON)
  and again in `ps3/ps3_aica_spe.cpp` for the PPU twin (SPE ARM7 OFF); the
  SH4 side is in `ps3/ps3_arm_spe.cpp`; the sound SPU's in
  `ps3/spu/spe_aica_model.cpp`, `spe_aica_ram.h`, `spe_aica.c`.
- Cost. Rows OFF: one byte test per SH4 wave RAM write and per ARM7 store.
  Sync: bound by the limits (689 syncs/s at most); 6.6 expects 0.01-0.3%.
  PC, sound SPU, with random DSP programs (much more ring traffic than a
  game: 160 ring lines written back per block): DSP Ring RAM +3.8 points
  of real time (48.4 -> 52.2%), DSP Ring 64K +1.3 points.
- Logs: `[aram-hz]` every ~5 s while a row is on or a count moved (map
  pages, hits by writer sh4 / dma / arm, tests, hazards with their distance
  behind the play point, ring hits and records, syncs/s, miss/s, timeouts,
  sync wait, SH4 wait % of wall, DSP register reads by sh4 / arm, ring and
  cache lines of the SPU); `clip/s=L/R` on the `[aica-spe] sound` line;
  `nullAICA clip L/R` on the `[aicafix]` line. The count of DSP register
  reads works with every row OFF: it tells if a game reads them at all.
- PC (WSL), all PASS:
  - New `misc/hz_check/run.sh`: the map and the exact test against the
    wave RAM bytes that a second model's render pass reads. 8 seeds, 18.7K
    tests, 700M reads, none missed; 1.10-1.25 pages marked per page needed.
    `mutate.py`: 24 of 24 planted bugs fail.
  - New `misc/spu_sim/run_ring.sh`: the real sound SPU image. Scenarios
    ring, ringrand, ring64, ringwrap in the 3 DSP modes (30 runs): guest
    writes into the ring behind SYNC, RING records, DSP register reads,
    program and ring changes. Every frame equal to a model whose ring is
    plain RAM, wave RAM equal after each CHECK, every read equal. Also
    mono (fix on and off) and the old scenarios with MVOL set.
  - `misc/arm7spe_check`: 15 new seeds (100-131, the rows as bits) with the
    harness as PPU and as sound SPU: the log (SYNC, RING, DSPRD records and
    their samples), the map, the counts and the answers equal the x86
    reference. The 22 old seeds as before.
  - `misc/aicactl_check`: 64 seeds (LFORE Keep is seed bit 5; the DSP read
    hook on both sides).
  - As before: `misc/spu_sim/run.sh` and `run_dsp.sh`, `diff_seeds.sh`
    (`diff_adv` and `diff_a3`, 8 seeds), `misc/a2_check`, `misc/a3_check`,
    `misc/dspjit_check`, `misc/g2dma_check`, `misc/arm7spu_check`.
- A fact found on the way: the old `run_a2` scenarios never set MVOL, so
  their frames were all silent (the frame compare was empty). With MVOL
  set (in `run_ring.sh`) every frame is equal: the SPU sound path holds on
  real audio.
- Not under a PC check: the PPU twin's copy of the code (SPE ARM7 OFF); the
  SH4 thread's part (`arm_spe_hz_hit`, the stale rule); DMA order on the
  real MFC (the simulator completes a DMA at once).
- Known limits:
  - A slot that stops inside the lag window has no mark for its last
    bytes. A slot whose loop end moves below its CA walks back in steps;
    the map does not cover that walk once it is over.
  - SPE ARM7 OFF with the ARM7 JIT: the JIT's inline stores are not
    tested (SH4, DMA and interpreter stores are). With Sound Thread ON the
    rows do nothing.
  - A guest write into the ring that gets no sync (over the block's limit)
    wins over DSP writes not yet written back to that 128-byte line.
  - A slot that plays from the ring's memory sees DSP output up to one
    block late.
- LS: sound SPU image end 0x3D7D0 (was 0x3C750); stack use 4.8 KB in the
  simulator, 5.4 KB spare (was 9.6). ARM7 SPU end 0x36770, 39 KB spare.
- Link: `.got` 0xD440, no warning.
- HW steps (package of 04:16; SH4 Clock 200; the other rows as they are):
  1. AUDIO page: the 6 new rows OFF. Boot Crazy Taxi, go to the main menu
     with music, SPEED at 99.5% or more.
  2. 60 s with SPE AICA Sync OFF: count the crackles per 10 s.
  3. SELECT menu, SPE AICA Sync ON (live): 60 s, count again. Then OFF for
     30 s.
  4. Log: `[aram-hz]` (hits by writer, hazards, syncs/s, miss/s, SH4 wait),
     `clip/s` on the `sound` line. If ON still crackles with hazards at 0
     and clip above 0: the next step is an SPU output gain (6.6).
  5. One boot per other row, ON: DSP Ring RAM in Crazy Taxi (the reverb
     must sound the same; ring lines in / out, `spu busy`) and at a Looney
     Tunes load (the echo); DSP Ring 64K only with a game whose `dsp jit`
     line shows `slow=` above 0; DSP Reg Reads only if `dsp reads/s` is
     above 0 with the row OFF; Mono Mix Fix with a game set to mono (Sega
     Bass Fishing if Matt has it); LFORE Keep: no audible change expected.

**Done: 3k, TR Triangle Sort, with 4 levels (Matt 2026-10-11, at the
bundle 4 verdict: "Build it next"; then, after HW run 2, "Two more
levels"). HW SUCCESS 2026-10-11 at + OPAQUE DEPTH in Rayman 2 ("They look
fine now!") and Sonic Adventure ("Perfect!"), HW run 4 below. Verdict
(Matt 2026-10-11, asked once): the default is + OPAQUE DEPTH, in the
source since 2026-10-11; TR Depth Write stays GAME. Not packaged: the
package of 01:41 on 2026-10-11 (`build/notdc_ps3.pkg`, 1510896 bytes) has
the row OFF; Matt's `nullDC.cfg` has + OPAQUE DEPTH and GAME from his
run. Crazy Taxi ran with the level after the verdict (HW run 5 below):
clean log, but render `busy` 20% -> 45% and SPEED 97.9% in its 11-13K
vertex scenes (was 99.8-100%); Matt: "I consider this a success", the
cost is parked for later. Not run with the level
yet: Jet Grind Radio (ON on his word). One row, "TR Triangle Sort", the
last one of the RENDERER page:
OFF / ON / + DEPTH WRITE / + OPAQUE DEPTH. The package of 01:41 has
bundle 4's defaults (the block below).**
- Why: HW run 1 of bundle 4. With depth write, Rayman 2's water hides the
  translucent polygons behind it. Without it (TR Depth Write at OFF IF
  SORTED), Sonic Adventure's models are wrong. The sort was per strip (the
  average 1/W of a strip).
- HW run 2, 2026-10-11 ~01:05, the package of 01:01 (the row was OFF / ON
  then), Sonic Adventure with the row ON and TR Depth Write at GAME. Matt:
  "Sonic still looks wrong". The log: up to 942 sorted triangles per
  render, SPEED 100%, render `busy` 5%, sort 1.0%, no fault line.
  - So the first analysis was wrong for Sonic Adventure: the order is not
    the cause there. Its translucent list (209-936 strips) is the models:
    blended (SRC_ALPHA, INV_SRC_ALPHA), every strip with cull mode 1 (both
    faces drawn) and Z write on. Only a depth write picks the front face
    per pixel; no sort per triangle can. Rayman 2's water is the other
    case: a large surface that must not hide what is behind it.
- HW run 3, 2026-10-11 ~01:40, the package of 01:16, SH4 Clock 200, TR
  Depth Write at GAME:
  - Sonic Adventure. Matt: "Depth write: Looked fine. Opaque depth:
    Looked fine." Log: + DEPTH WRITE 51 s (boot to play), + OPAQUE DEPTH
    27 s at SPEED 100.0% (min 99.8), up to 954 sorted triangles per
    render, render `busy` 13% at most, sort 2.3%, no fault line.
  - Rayman 2. Matt: "With Opaque depth, some elements are broken but
    water is fine. Opposite happens with depth write." And: "OFF IF
    SORTED + Depth WRITE fixes both issues", but that pair "gives issue
    to Sonic Adventure 1". The pair is the ON level: TR Depth Write at OFF
    IF SORTED also switches the depth write of + DEPTH WRITE off.
  - The table of the run (models = Sonic Adventure; water, elements =
    Rayman 2):
    - ON (no depth write): models wrong, water right, elements right.
    - + DEPTH WRITE: models right, water wrong, elements right.
    - + OPAQUE DEPTH (package of 01:16): models right, water right, some
      elements wrong.
  - Cause of the wrong elements, from the log: 71 of Rayman 2's 253
    translucent strips are additive (DstInstr ONE) and use 565 textures,
    which have no alpha (alpha 1 in every texel). The first pass of
    + OPAQUE DEPTH wrote the depth of those whole quads, so a glow hid
    what was behind it. Sonic Adventure has only SRC_ALPHA with
    INV_SRC_ALPHA, so it did not show.
  - Fix, in the package of 01:41: the first pass takes only a polygon
    whose blend hides what is behind it: SRC_ALPHA with INV_SRC_ALPHA
    (where alpha is 240 or more), or ONE with ZERO (every texel). A
    polygon of any other blend is not in the first pass.
- HW run 4, 2026-10-11 ~01:51, the package of 01:41, SH4 Clock 200, TR
  Depth Write at GAME, TR Triangle Sort at + OPAQUE DEPTH from the boot:
  - Rayman 2 (the water and the elements of run 3). Matt: "They look fine
    now!" Log: 82 s, SPEED 100% but one second at 98.5%, up to 930 sorted
    triangles per render, render `busy` 15% at most, sort 1.3%, no fault
    line, clean XMB exit.
  - Sonic Adventure (the models). Matt: "Perfect!" Log: 80 s, up to 2115
    sorted triangles per render, render `busy` 32% at most, sort 3.1%, no
    fault line, clean XMB exit. SPEED 100% in play; the slow seconds are
    the intro FMV (89.5-94.7%, the known SH4-bound FMV) and a load.
  - So + OPAQUE DEPTH is right in both games: models, water, elements.
    Not run with it yet: Jet Grind Radio.
- HW run 5, 2026-10-11 ~01:55, Crazy Taxi, same package and settings
  (+ OPAQUE DEPTH from the boot), 383 s, clean XMB exit. No word from Matt
  on the picture yet. Log: no fault line, `drop=0`, fences `bad=0`, SPE
  Build `fail=0`, up to 1936 sorted triangles per render. The level costs
  render time here. Same load (9K+ vertices per render), against the
  Crazy Taxi boots of 2026-10-10 (bundle 3 and Step 2 logs, no level):
  - Render `busy` 45% (was 20%), 67% at most (was 29%). TR draws per
    render 1064 (was 215-228): param 21.0% (was 8.3-9.0), glEnd 12.5%
    (was 4.8-5.0), sort 3.6%. Every TR polygon of the game is SRC_ALPHA
    with INV_SRC_ALPHA, so the first pass draws the full list.
  - SPEED by load: full below 11K vertices (as before). 11-13K: 97.9%
    average, 14 of 57 s below 97%, lowest 87.4% (was 99.8-100.1%, 1 of
    79 s below 97%). 13K+: 97.4% (was 97.4-99.5%, the same range).
    `slept` at 11-13K falls from 43-54 to 27 of 60.
  - Sonic Adventure for reference, 9K+ vertices: `busy` 5% -> 20%, TR
    draws 182 -> about 1000 per render, SPEED not changed (the game draws
    20-30 fps). Rayman 2: `busy` 3.5% -> 4.0%.
  - Verdict (Matt 2026-10-11): "I consider this a success, but can we go
    back to it later?" The default stays + OPAQUE DEPTH. The cost and the
    per-game question are parked (the "Parked for later" block below).
- The row (`TrTriSort` in `nullDC.cfg` [ps3], live), for a translucent
  list the PVR sorts (autosort):
  - ON: Flycast's default path (`sortTriangles`, `drawSorted`). Each
    strip is cut into triangles (one with an inf corner is left out). The
    key is the farthest corner (the smallest 1/W; ours compares the
    largest W). A stable sort, far to near. The triangles go out as
    GL_TRIANGLES in runs of one state, with no depth write.
  - + DEPTH WRITE: the same sort and draw, but each polygon writes depth
    as the game asks (and as the TR Depth Write row says). Expected: Sonic
    Adventure right; Rayman 2's water not sure.
  - + OPAQUE DEPTH (our own design, no reference): two passes over the
    same sorted triangles. First, depth only: a texel whose alpha is 240
    of 255 or more writes its depth (alpha test, no colour, no blend).
    Then the colour, far to near, with no depth write. A texel that hides
    what is behind it does so in any order (the models); the rest blend
    in the sorted order (the water). It draws the list twice. Only a
    polygon whose blend can hide something is in the first pass (HW run
    3): an additive one is not.
  - In all three: a strip's odd triangles have their first two corners
    swapped, so each triangle winds as its strip does and its newest
    corner stays last (flat shading); a triangle of the other cull
    direction joins the run with the two corners swapped again. A
    presorted list, and the opaque and punch-through lists, do not change.
- Where it runs: the render thread (PPU): `tri_collect`, `tri_sort` (a
  radix sort), `tri_runs` and `draw_tr_tris` in `ps3/ps3_rend.cpp`. With
  SPE Build the request flag `SPEVB_NO_TR` makes the SPU answer TR_PPU at
  once: it builds the opaque and punch-through runs only
  (`spe_vb_build.h`, one SPU image changed). It needs Vertex Arrays and
  Strip Merge (the defaults); without them the sort stays per strip.
- Cost: the PPU writes 3 vertices per triangle into RSX memory, and the
  SPU no longer builds the translucent runs. `[rend] busy=`, `draw split:
  sort`, `[rend4] tris=` (sorted triangles per render), `[spevb] trppu`.
- PC (WSL), all PASS:
  - `misc/rend4_check/run.sh`: the rows-off phase is unchanged (4000
    scenes, the old code's GL calls). Rows on, the 4 levels at random:
    73373 sorted triangles against the model (each triangle of each strip
    once, its corners and their order, the draw order by W falling with
    the submission order at equal W, the state of each run, strips with
    an inf corner), 18103 of them also in an opaque depth pass (only the
    polygons whose blend hides, its state, the alpha level, each triangle
    once, before the pass's colour). `mutate.py 1500 3k`: 29 of 29 planted bugs fail.
  - `misc/spevb_check/run.sh`: the builder with `SPEVB_NO_TR` at random
    (3000 scenes, 1129 with the list left to the PPU).
  - `ta4_check`, `tr4_check`, `bump_check` again on the final files.
- Not under a check: the RSX itself (the depth of the first pass must be
  the depth of the colour pass, bit for bit, or + OPAQUE DEPTH flickers),
  the alpha level of 240 (a pick), the speed.
- Link: `.got` 0xD288, no warning.
- HW steps (package of 01:41; the install keeps `nullDC.cfg`; SH4 Clock
  200). TR Depth Write at GAME (Matt's file has OFF IF SORTED from run 3),
  TR Triangle Sort at + OPAQUE DEPTH:
  1. Rayman 2: the water and the elements that were wrong in run 3.
  2. Sonic Adventure: the models must still be right.
  3. Crazy Taxi and Jet Grind Radio, for the image and for SPEED.
  4. On a fault: note the game, the scene and what the element is.

**Parked for later (Matt 2026-10-11: "we can bench these improvements
later"): Phantasy Star Online Ver. 2 (US) draws 20 fps in a heavy scene at
SPEED 100%.** Log of 2026-10-11 (`ps3log_b4_run1.txt`, the last boot): 30
fps in light scenes; 20 fps (19.7-21.0) with 11-17K vertices per render;
no dropped render, render `busy` 10%, `[pace] slept=60` (the SH4 thread
sleeps in each frame); the idle loop at 8C3C53F8 skips 15-26K slices of
111.6K per second, so the game works 39 ms or more per frame and takes 3
refreshes. Two ideas, not tested: SH4 Clock 250 (30 fps if 39 ms is the
true value and the PS3 holds SPEED; one boot, the row is read at boot);
the 50 Hz mode with a PAL disc (2 refreshes = 40 ms, 25 fps, no host
cost; a forced 50 Hz on the US disc is slow motion).
Sonic Adventure has the same case, less often (Matt asked 2026-10-11): in
the 5 boots of this log it draws 30 fps in play and 20-27 fps for a few
seconds with 12-20K vertices per render, at SPEED 100% (the 3-vblank
frames of 5.41). The Looney Tunes race is the third game of the kind.

**Parked for later (Matt 2026-10-11, after HW run 5 of 3k: "can we go
back to it later? Not all games use this and probably we need to see it
for case by case."): the cost of TR Triangle Sort, and settings per
game.** Two items, nothing designed, nothing built:
- A cheaper + OPAQUE DEPTH: the first pass (depth only) needs no sorted
  order, so it can draw the merged strips in the game's order. Estimate
  for Crazy Taxi: 1064 -> about 750 TR draws per render, render `busy`
  45% -> about 37% (20% with the level OFF). Not measured. **Built
  2026-10-13 in bundle 14: row TR First Pass at + GAME ORDER (DRAW COST
  page). HW SUCCESS 2026-10-13 (Crazy Taxi: 1033 -> 615 TR draws per
  render), default ON in source (the first block of this section).**
- Matt's idea: "Maybe an ID checker based off a Dreamcast romset that
  will automatically set the right flags on depending on the game." A
  table keyed by the disc's product ID (IP.BIN header), which sets rows
  such as TR Triangle Sort per game. Flycast does this by product ID
  (`loadGameSpecificSettings`). This changes the old rule "No per-game
  lists" (2026-09-20, 3g/3i): ask Matt for the scope before a design.
  Known cases so far: Sonic Adventure and Rayman 2 need + OPAQUE DEPTH;
  Crazy Taxi pays 25 points of render thread for it.

**Done: bundle 4, TA decode and draw rules (section 7; Matt 2026-10-10:
"Go with bundle 4."), HW SUCCESS 2026-10-11 in Crazy Taxi, Sonic
Adventure, Jet Grind Radio and Rayman 2 (HW run 1 below). Verdict (Matt
2026-10-11, asked once): the defaults are the values of his run: every row
ON, Volume Cap 8192, TA Bad Records SKIP, List Passes ON, Tile Clip
INSIDE; TR Depth Write stays GAME (OFF IF SORTED fixes Rayman 2's water
and breaks Sonic Adventure's models); Flat Shading stays OFF ("No
difference seen" between its levels: the RSX vertex question is open).
The defaults are in the source since 2026-10-11, not in the package of
00:08 (it has every row OFF). Matt's `nullDC.cfg` has the values of his
last boot: TR Depth Write OFF IF SORTED and Flat Shading LAST VERTEX
(not the defaults). No game of the run used Sprite Cull, Tile Clip, List
Passes, the TR volume draw or the TA Data Gate: they are ON on his word.
Next (Matt: "Build it next"): the sort per triangle, 3k, the block above.**

**The build: the package of
00:08 on 2026-10-11 has it: `build/notdc_ps3.pkg`, 1508720 bytes, 16 new
rows, all OFF. Matt's picks before the build (2026-10-10, asked once): every
item of the bundle but Empty Frame (only Reicast has that rule; a screen
that is only a background would freeze); added from the fifth review: P63,
and from P66 the invalid TA types, the volume cap of 8192 and "no data
before list init"; a new options page, "RENDERER".**
- The same package is the first one with bundle 3's defaults (Mipmaps ON,
  Anisotropic 16x, VQ Formats ON). An install keeps `nullDC.cfg`.
- The rows (options page 7, RENDERER; the key is in `nullDC.cfg` [ps3]).
  The first value is the old path. All 16 are live (they act from the
  SELECT menu): a decode row acts from the next TA_LIST_INIT, a draw row
  from the next render.
  - Intensity Clamp (`IntClamp`, P8): OFF / ON. Decode. The face colour
    becomes saturated bytes at the header, the intensity a saturated byte,
    then each channel is face x intensity / 256; alpha is the face alpha
    (Flycast `vert_face_base_color`, Reicast "Fixes red dog"). OFF
    multiplies the floats and clamps after. Both decoders. On the SPU the 3
    double multiplies per intensity vertex go away.
  - Face Reset (`FaceReset`, P63): OFF / ON. Decode. The face colours (and
    the face offset colour) are white at each list init (Flycast
    `ta_parse_reset`; NHL 2K, NHL 2K2). Also: a TA_LIST_CONT closes the
    store stage's open list (TA Fast Store only).
  - TR Volumes (`TrVolumes`, P27): OFF / ON. Decode + draw. The
    translucent modifier-volume list (list 3) is kept, tagged in the free
    byte of the volume record. The draw takes list 3's volumes in place of
    list 1's when a pass of the render has one pointer for both lists in
    the region array (words +8 and +16 equal, bit 31 clear) and list 3 has
    more volumes with triangles (Flycast dev `f2013c652`, Xtreme Sports).
  - Volume Cap (`VolumeCap`, P66): 4096 / 8192 triangles per frame (Jet
    Grind Radio). The arrays hold 8192 from this build on (+1.2 MB).
  - TA Bad Records (`TaBadRec`, P9 + P66): FATAL / SKIP. The PPU splitter
    (`ta.h`) skips Object_List_Set, the reserved types 3 and 6, and a
    polygon header with Volume = 1 and Col_Type = 1 (it has no type, and
    its ids ran off two tables). FATAL calls `die()`, which never returns.
    The SPU decoder always skipped them, so the two now agree.
  - TA Data Gate (`TaDataGate`, P66): OFF / ON. Store stage. TA records
    are dropped while no list init is current: before the first one, and
    from a STARTRENDER of the TA's own context to the next TA_LIST_INIT or
    TA_LIST_CONT (Flycast `ta_ctx == NULL`; Vigilante 8 FMV). No copy, no
    list-end interrupt. The flat SQ path pays nothing for it.
  - PT Depth Write (`PtDepth`, P25): OFF / ON. Draw. Punch-through always
    writes depth (Worms World Party, Bust-a-Move 4, Re-Volt).
  - TR Depth Write (`TrSortDepth`, P25): GAME / OFF IF SORTED. Draw. An
    autosorted translucent polygon writes no depth (Flycast). Soul Calibur
    sends ~616 such strips with Z write on: look at it first.
  - PT Blend (`PtBlend`, P25): OFF / ON. Draw. Punch-through blends with
    its TSP factors (Flycast `1a019b79b`).
  - Sprite Cull (`SpriteCull`, P25): OFF / ON. Draw + merge. A sprite
    culls the other face (Flycast `6ffe4c028`; South Park Rally and Wacky
    Races menus). The PPU merge and the SPU builder flip the strip parity
    the same way.
  - Flat Shading (`FlatShade`, P25): OFF / LAST VERTEX / FIRST VERTEX. Draw.
    A polygon without Gouraud takes one colour per triangle, the colour of
    the triangle's last vertex on the DC. LAST VERTEX is right if the RSX
    takes the last vertex of a triangle. FIRST VERTEX is right if it takes
    the first: the colour attribute then starts 2 vertices later, so the
    first vertex reads the last one's colour. One of the two is wrong on
    the RSX: the HW run tells which. The background stays smooth.
  - Region Array (`RegionArray`, P10): OFF / ON. At STARTRENDER. The sort
    mode comes from the region array as Flycast reads it: a first entry
    whose list pointers are all empty is skipped; an entry is 6 words when
    its pre-sort bit is set. With List Passes each pass takes its own
    entry.
  - List Passes (`ListPasses`, P10): OFF / ON / + TR DEPTH. Draw. A render
    with TA_LIST_CONT draws opaque, punch-through, volumes, translucent
    once per pass (Flycast `drawStrips`). Opaque polygons blend after pass
    0. A later pass with no strip is not drawn. + TR DEPTH also draws each
    non-final sorted pass's translucent strips again, depth only (Flycast
    `TranslucentPolygonDepthMask`, its Cosmic Smash setting). Such a render
    goes to the PPU replay and the PPU draw (the SPU lists have no pass
    marks): `[rend4] pass_ppu=` counts them.
  - Tile Clip (`TileClip`, P10): OFF / INSIDE / + OUTSIDE. Decode + merge
    + draw. The decoders pack the last User Tile Clip rect into each
    header's reserved bits (ISP 19:0, PCW 9:8), so no list layout changed.
    INSIDE (clip mode 2) is a scissor per run. OUTSIDE (mode 3) draws the
    run once per side of the rect (up to 4 draws). A rect over the whole TA
    area is no clip. Volumes are not clipped. Needs Strip Merge and Vertex
    Arrays (the defaults).
  - TA List Regs (`TaListRegs`, P10): OFF / ON. TA_LIST_INIT sets
    TA_ITP_CURRENT = TA_ISP_BASE and TA_NEXT_OPB = TA_NEXT_OPB_INIT
    (Flycast; Reicast's TA_ISP_CURRENT, for KOS's background polygon).
  - Core Reset (`CoreReset`, P10): OFF / ON. SOFTRESET bit 1 cancels the
    render-done interrupts still pending (Minicast `9970b0e`).
- How the options travel: a context takes its decode options at its list
  init (`TaContext.opt`); the SPU decoder gets the same word with the RESET
  command, and each fence says which options its lists have. The draw
  rules are read once per render job. SPE Build gets Sprite Cull and the
  clip key as request flags.
- Log:
  - `[rendcfg] boot: IntClamp= FaceReset= TrVolumes= VolumeCap= TaBadRec=
    TaDataGate= PtDepth= TrSortDepth= PtBlend= SpriteCull= FlatShade=
    RegionArray= ListPasses= TileClip= TaListRegs= CoreReset=`, and a
    `[rendcfg] menu:` line when one changes.
  - `[rend4] cont= multi= pass_ppu= trvol= gate_drop= bad_rec= opt=`, per
    5 s window: TA_LIST_CONT writes, renders drawn by passes, renders sent
    to the PPU replay for their passes, renders that drew list 3's
    volumes, TA records the gate dropped, bad records skipped, the decode
    option bits of the last lists.
  - A third `[rendz]` line per list: `flat= sprcull= clip=in/out blend=`:
    the strips of the window's last frame a row can act on (no Gouraud;
    sprites with a cull direction; clip modes 2 and 3; blend factors that
    are not ONE, ZERO). The first line's `zwd=` gives the Z-write counts.
    They tell if a game uses a rule, with the row ON or OFF.
- Code: `ps3/ps3_rend.cpp` (the decoder, the store gate, `apply_param_body`,
  `clip_apply`, `draw_vb`, `eff_cull`, `mv_pick_list`, `draw_modvols`,
  `tr_depth_pass`, `draw_scene`, `snapshot_region`), `ps3/ps3_rend_regs.h`,
  `ps3/spu/spe_dec.c` + `spe_bell.h` + `spe_bell.c` (the SPU decoder),
  `ps3/spu/spe_vb_build.h` + `spe_vb.h` (the SPU builder's merge),
  `ps3/ps3_spe.cpp`, `ps3/ps3_spevb.cpp`, `ps3/ps3gl/ps3gl_vertices.c`
  (`ps3gl_draw_vb_shift`), `plugs/drkPvr/ta.h`, `regs.cpp`, `SPG.cpp`
  (`spg_CancelRenderEnd`), `ps3/ps3_presets.cpp`, `ps3/ps3_menu.cpp`. Two
  SPU images changed (the decoder and the builder).
- PC (WSL), all PASS:
  - `misc/ta4_check/run.sh` (new, ASan + UBSan): the PPU replay
    (`ps3_rend.cpp`'s decoder through the real `ta.h`) and the real
    `spe_dec.c` on the SPU shims, with the MFC model of `spevb_check`. It
    is the first PC run of the SPU decoder.
    - Options off: 6000 frames give the lists of the frozen files of before
      bundle 4 (`ref_*.inc`), on both decoders.
    - Any options: 6000 frames, the PPU lists equal the SPU lists word for
      word, and both follow a model of the ported rules (P8 colours, P63
      reset, P27 tags, the two caps, the clip rect in the params, the pass
      marks). 6000 more with the records that have no meaning and TA Bad
      Records at SKIP.
    - `mutate.py`: 41 of 41 planted bugs fail.
  - `misc/rend4_check/run.sh` (new, ASan + UBSan): `ps3_rend.cpp` from
    `bg_eval` to the end of `draw_scene` over a GL that records every call.
    - Rows off: 4000 scenes make the GL calls of the frozen file, call for
      call, with the same vertex block (1.3 million calls).
    - Rows on: 4000 scenes against a model of each rule: depth write,
      blend, cull, flat shading and its colour shift, the clip scissors
      (the outside draws cover the viewport less the rect, once), the pass
      order, the depth pass, the volumes drawn and how many times.
    - `mutate.py`: 43 of 43 planted bugs fail. 1 is left out as equivalent
      (named in the file).
  - `misc/spevb_check/run.sh` (extended): the SPU builder against the PPU
    merge with Sprite Cull and the clip key at random, sprite headers in
    the params: 3000 scenes, equal. 8 of 8 planted bugs fail (scratchpad
    `mut_spevb.sh`, not in the tree).
  - The older checks that read the changed files: `tr4_check`,
    `bump_check` (their harnesses got the new symbols), `spetex_check`,
    `spepix_check`, `depth_check`, `texslot_check`, `yuv_check`, `tex3_check`.
  - The SPU builder in the simulator (`misc/spevb_check/sim.sh`, the Looney
    Tunes race frame): 2.22 ms per build, 2.21 before (+0.3%).
- Not under a check: the store gate and the store reset at TA_LIST_CONT,
  `snapshot_region` (the region array reads), `regs.cpp` and
  `spg_CancelRenderEnd`, the option latch in `rth_job_decode`, `draw_runs`
  (the SPU runs take the same `apply_param` and `draw_vb` as the PPU runs
  under the check), `ps3gl_draw_vb_shift`, the menu page, the big-endian
  build, and the RSX itself (which vertex gives a flat colour).
- Found by the PC checks, fixed before the package:
  - The SPU decoder took the clip rect one word early (words 3 to 6; the
    TA sends it in words 4 to 7).
  - An outside clip with an inverted rect drew the run twice over a band.
- Found, not changed:
  - `spe_dec.c` `recip()` reads a double's sign word at index 0: right on
    the SPU, wrong on x86. It has a `DEC_HI` macro now (0 on the SPU, the
    code is the same), as `VB_HI` in the builder.
  - `ta.h`: an END_OF_LIST with no list open takes its list type as an
    index of 5 flags (`ListIsFinished`); a type of 5 to 7 writes past them.
    A normal END_OF_LIST is all zero.
  - The PPU keeps a vertex record inside an open strip whatever its type;
    the SPU ends the strip (`odd=`). The face colours and the clip rect
    are one state for all contexts in both decoders, and the PPU replays
    only the renders the SPU does not draw: after such a render the PPU's
    state is the older one.
  - A volume group's area sum draws every triangle from the group's first
    one (Flycast's too): a run of the other list inside a group would go
    with it. Lists do not interleave like that in a normal frame.
- Comments over 2 lines in `plugs/drkPvr/ta.h` and `spe_vb_build.h` are
  cut to 2 (Matt's comment rule for edited files): the two copies of the
  TA type table in `ta.h` are now 2 lines that point at the helpers.
- Not taken from Flycast: the clip of modifier volumes (its own TODO for
  inside), the per-pass Z clear flag (no Flycast renderer uses it), the
  `throw` on types 3 and 6 (ours skips one record, as Reicast and the SPU).
- Link: `.got` 0xD258, no warning.
- UDP listener: started at Matt's request (2026-10-11 00:25, PID 27628,
  port 18194, into `ps3log_b4_run1.txt`, scratchpad of session c485c4b0).
- HW run 1 as a whole (4 boots, 627 s): 4 clean XMB exits, no fault
  line, `fail=0`. No slow second in play but the known ones (the Sonic
  Adventure intro FMV at 91.6-92.4%, one Crazy Taxi second at 98.7%); the
  other slow seconds are the SELECT menu and the boots.
  - What the 4 games use (`[rendz]`, `[rend4]`): strips without Gouraud
    in all four (up to 360 per frame), so Flat Shading acted; a
    punch-through list only in Jet Grind Radio (148 strips, all with blend
    factors, none with Z write off), so PT Blend acted there and PT Depth
    Write changed nothing.
  - Not used by any of the 4: sprites with a cull direction (Sprite Cull),
    a user clip (Tile Clip), TA_LIST_CONT (List Passes, `cont=0`), the
    shared volume pointer (`trvol=0`), `gate_drop=0`, `bad_rec=0`. No
    counter exists for Intensity Clamp, Face Reset, Region Array, TA List
    Regs and Core Reset.
- HW run 1, 2026-10-11 from 00:25 (`ps3log_b4_run1.txt`):
  - Crazy Taxi, rows set ON in the SELECT menu (all but Volume Cap, TA Bad
    Records and TR Depth Write; Flat Shading LAST VERTEX, Tile Clip
    INSIDE). Matt: "It looked better, actually!" No fault line.
  - Sonic Adventure, the same rows. TR Depth Write at OFF IF SORTED:
    Matt: "it gave me issues on the models." HW FAILURE for this row; it
    stays at GAME. Log: the translucent list has 469 strips, all with Z
    write on, `sort=auto`. Our sort is per strip (average 1/W), so with no
    depth write the faces of one model draw in the wrong order. Flycast's
    default sorts per triangle. With the row at GAME and the other rows
    ON, Matt saw no transparency issue ("it only displays with OFF IF
    SORTED").
  - Jet Grind Radio (US), 131 s, every row ON but TR Depth Write (Volume
    Cap 8192, TA Bad Records SKIP, Flat Shading LAST VERTEX, Tile Clip
    INSIDE). Matt: "looked fine with those settings."
    - SPEED 100% in 128 of 131 s; the game draws 30 fps. The 3 slow
      seconds: 1 at boot, 2 at the SELECT menu. Render `busy` 18% at most.
    - Volume triangles per render: up to 3305 in list 1 and 2870 in list
      3, so 6175 with TR Volumes ON: over the old cap of 4096, under 8192.
      `trvol=0`: the game's own list 1 was the one drawn.
    - `rtt=0/0` (no render to texture in this run), `cont=0`, `gate_drop=0`,
      `bad_rec=0`, no fault line, clean XMB exit.
  - Rayman 2, the same rows (the bump levels run on the PPU replay,
    `ppu=251` per window). Matt: "Rayman 2 has the transparency on water
    fixed using OFF IF SORTED". SPEED 100% in 101 of 103 s, no fault line.
    Log: 107-254 translucent strips per frame, all with Z write on,
    `sort=auto`, 11-34 of them cover most of the screen (the water).
  - So TR Depth Write is right for Rayman 2 and wrong for Sonic Adventure.
    Both games send every translucent strip with Z write on under
    autosort. Cause: the sort is per strip. With Z write, a large strip
    (water) hides what the game draws behind it; without it, the strips of
    one model come out in the wrong order. Flycast's default is no depth
    write with a sort per triangle; ours has no such sort. Open for Matt's
    verdict.
- Risks for the HW run:
  - Flat Shading: one of the two levels shows wrong colours on flat
    polygons (the colour of another corner). `[rendz] flat=` tells if the
    scene has such strips.
  - TR Depth Write: Soul Calibur. List Passes: a game with TA_LIST_CONT in
    every frame loses the SPU decode and build for those renders (`[rend]
    busy=`, `[rend4] pass_ppu=`).
  - TA Data Gate: a game that sends TA data after STARTRENDER with no new
    list init loses that data.
  - Tile Clip OUTSIDE: up to 4 draws per run.
- HW steps (package of 00:08; every row OFF after the install; SH4 Clock
  200). All rows are live, so one boot per game. Do not press START in a
  Crazy Taxi drive (the open RTT freeze below).
  1. Crazy Taxi to a drive, rows OFF: nothing must change. Read `[rendcfg]
     boot:` and `[rend4]`.
  2. SELECT, RENDERER: the rows ON one at a time, top to bottom, with a
     look at the scene after each. Flat Shading: LAST VERTEX, then FIRST
     VERTEX. Note the row at the first wrong image.
  3. Sonic Adventure and Soul Calibur the same way (Soul Calibur: TR Depth
     Write first).
  4. The named games only if Matt has them: Red Dog (Intensity Clamp),
     Xtreme Sports (TR Volumes), Worms World Party, Bust-a-Move 4, Re-Volt
     (PT Depth Write), South Park Rally, Wacky Races (Sprite Cull), Cosmic
     Smash (List Passes), NHL 2K2 (Face Reset), Vigilante 8 (TA Data Gate),
     a KOS homebrew (TA List Regs).
  5. Optional, one more boot: SPE Doorbell ON and SPE Draw OFF (VIDEO /
     DEBUG, set before boot) with the decode rows ON: `[spe] ... bad=0`,
     the two decoders on the PS3 itself.
  6. On a fault: the row OFF in the SELECT menu; note the game and the
     scene.

**Done: bundle 3, the texture path (section 7; Matt 2026-10-10: "Let's
work with Bundle 3 now."), HW SUCCESS 2026-10-10 in Crazy Taxi and Sonic
Adventure (Matt: "The roads looked right. Same for Sonic Adventure 1.",
then "Perfect. It's a success then."). Defaults in the source since
2026-10-10 (Matt's picks, asked once): Mipmaps ON, Anisotropic 16x, VQ
Formats ON; Texture Filter stays GAME. Not packaged: the package of 22:06
on 2026-10-10 has every row OFF. Matt's `nullDC.cfg` has the HW run's
values (the same ones). Not tested on HW: Rayman 2 (Matt: "Can we skip
Rayman 2?"; it is the only game with bump-map textures, `bump=0` in both
logs), a 4-bit palette texture (`p4=0` in both logs), a game with a
texture of the two VQ forms, the CHECK boot. Matt's picks before the build
(2026-10-10, asked once): the items P45, P52 and the P25 VQ formats; the
P25 trilinear weights are left out; P1 steps 2-3 are dropped; the texture
hash covers the top level only; a new options page, "TEXTURES".**
- Defaults (`ps3/ps3_presets.cpp`): `Mipmaps` 1, `Aniso` 4, `TexFilter`
  0, `VqFormats` 1. In the package of 22:06 they are all 0.
- Why the picks:
  - P1 step 2 has no target: SPE Tex Decode (5.12 #4, default ON) already
    writes the RSX order and skips `convert_pixels()`. Step 3 gives no
    image and no measured speed (decode is 0.4% of the render thread, the
    RSX waits on nothing).
  - Trilinear weights: without a shader they scale the vertex colour only
    (no effect on decal textures), and the SPU vertex builder must change.
  - Hash: Flycast also watches the top level and the codebook only. So the
    SPU hash image did not change.
- The rows (options page 6, TEXTURES; the key is in `nullDC.cfg` [ps3]).
  All four are live (they act from the SELECT menu). The first value is
  the old path.
  - Mipmaps (`Mipmaps`, P45): OFF / ON. A mipmapped texture (TCW
    MipMapped, not in scan order) gets every level down to 1x1 in RSX
    memory, by Flycast's rules (`TexCache.cpp` `Update()`): the VQ 1x1
    level is texel (1,1) of a 2x2 block (YUV: the block of the 2x2
    level); the 1x1 level of a YUV texture is one 565 texel.
    - Not on a mirrored texture: it is linear in RSX memory, and the
      linear chain never ran on HW (the first-build rule of section 7).
    - Filter (Flycast `SetGPState`): LINEAR_MIPMAP_NEAREST on bilinear
      polygons; level 0 only in a punch-through list and on point-sampled
      polygons; LINEAR_MIPMAP_LINEAR when Anisotropic is above 1x. The
      sampler's maxlod is levels - 1. No LoD bias from MipMapD.
    - Decode: with SPE Tex Decode ON the SPU decodes each level of 8x8
      and up as one job (the SPU image did not change). The PPU decodes
      4x4, 2x2 and 1x1 (21 texels). A twiddled YUV texture with a chain
      decodes on the PPU at every level (see "Found").
    - A switch clears the texture cache (the chain is fixed at
      allocation). RSX memory of these textures: +33%.
    - A write to a lower level alone keeps that level's old texels (the
      hash pick). The next change of the top level decodes the chain.
  - Anisotropic (`Aniso`, P52): 1x / 2x / 4x / 8x / 16x, on every texture
    that is not point-sampled. Sampler state only.
  - Texture Filter (`TexFilter`, P52): GAME / NEAREST / LINEAR. NEAREST
    and LINEAR force one filter on every texture (Flycast
    TextureFiltering). Sampler state only.
  - VQ Formats (`VqFormats`, P25): OFF / ON. ON reads two forms as Flycast
    does. Palette VQ (a palette texture with VQ_Comp): a codebook entry
    is the 8 index bytes of one twiddled block (4x4 texels for 4-bit, 2x4
    for 8-bit). Scan-order VQ (VQ_Comp with ScanOrder, not mipmapped): an
    index byte is 4 texels of one row, with the row stride. OFF ignores
    VQ on a palette texture and reads every VQ texture as twiddled. The
    PPU hashes and decodes the two forms: the SPUs describe a texture the
    old way. A switch clears the texture cache.
- Log:
  - `[texcfg] boot: Mipmaps= Aniso= TexFilter= VqFormats= SpeTexHash=
    SpeTexDecode= TexSlotHash= YuvBlockDecode= BumpMap=`, and a `[texcfg]
    menu:` line when one changes.
  - `[tex] mipmaps ON, vq formats OFF, cache cleared`; `[tex] anisotropic
    4x, filter GAME`.
  - `[texfmt]` has four new counts per window: `chains=` (textures decoded
    with a chain), `chainlv=` (levels written under level 0), `palvq=` and
    `plvq=` (fills of the two VQ forms by their TCW bits, with the row ON
    or OFF: they tell if a game has such textures).
  - `[ps3gl] mip uploads` goes above 0. `[spepix] tex jobs` counts one job
    per level of 8x8 and up.
- Code: `ps3/ps3_tex.cpp` (`describe()`, `level_desc()`, `decode()`,
  `upload()`, `upload_chain()`, `set_tex_params()`; `ps3tex_bind()` takes
  the punch-through flag from `apply_param_body()`),
  `ps3/ps3gl/ps3gl_textures.c` (`ps3gl_texture_hint_mipmaps()`,
  `ps3gl_tex_chain_levels()`, `ps3gl_tex_direct_level()` and `_end()`,
  `GL_TEXTURE_MAX_ANISOTROPY_EXT`), `ps3/ps3gl/ps3gl.h`,
  `ps3/ps3_presets.cpp`, `ps3/ps3_menu.cpp`. No SPU image changed.
- PC (WSL), all PASS:
  - `misc/tex3_check/run.sh` (new, ASan + UBSan): the real `ps3_tex.cpp`
    and `ps3gl_textures.c` as whole files, with the real SPU decode
    (`spe_pix_job.h` on `misc/spepix_check`'s shims) and RSX memory as an
    arena with guards.
    - Trace: with every option 0 the two files give what their frozen
      copies give (`ref_ps3_tex.inc`, `ref_ps3gl_textures.inc`, the tree
      before bundle 3): 12 seeds of 25 epochs equal. Each bind folds the
      whole texture block, the sampler state, every SPU job and every
      RSX allocation.
    - Model: 6 seeds of 40 epochs with random options, SPE modes (OFF, ON,
      CHECK), late SPU jobs and SELECT-menu switches: 24649 binds, 370
      million texels, each one against a per-texel model of Flycast's
      rules, at its place in the swizzled or linear memory. 6794 binds
      with a chain (33963 lower levels), 2525 palette VQ, 1798 scan-order
      VQ. Also: the level counts, `gcmTexture`, maxlod, the filters, the
      anisotropy, no word written outside a level, no SPE CHECK mismatch,
      and ps3gl's mip-upload count.
    - The lower-level rule (the hash pick), in the three SPE modes.
    - `mutate.py`: 67 of 67 planted bugs fail (53 in `ps3_tex.cpp`, 14 in
      `ps3gl_textures.c`). 3 are left out as equivalent (named in the
      file).
  - The older checks that read the changed files: `spetex_check`,
    `spepix_check`, `yuv_check`, `texslot_check`, `bump_check`,
    `spevb_check`, `tr4_check`.
- Not under a check: the RSX itself (the swizzled chain in RSX memory,
  maxlod, the mip filters, anisotropy: the ps3gl mip path never ran on HW
  in this tree), the menu page, the big-endian build (the new decode
  paths use the `vram8`/`vram16` accessors only).
- Found:
  - The SPU decode has no swizzled form for a twiddled YUV texture
    (`tex_tile()` sends it to the 16-bit kernel, which has no YUV case).
    Before P45 no YUV texture was swizzled, so it had no effect. The PC
    check found it on the first model run. `spu_layout()` in
    `ps3_tex.cpp` keeps these textures on the PPU. A YUV kernel on the SPU
    is an SPU image change: not done.
  - Not changed: the trilinear passes (FilterMode 2 and 3) still draw at
    full weight. A texture whose source passes the end of VRAM wraps
    (Flycast refuses it).
- Not taken from Flycast: the LoD bias from MipMapD (desktop GL 3 only
  there; the RSX bias format is unverified), the GPU palette.
- Link: `.got` 0xD0D8, 0 `r2off`, no warning.
- The package of 22:06 holds the whole tree, so it is also the first one
  with the defaults that sections below call "not packaged" (bundle 2's
  14 rows ON, and the older ones). An install keeps `nullDC.cfg`: Matt's
  values stay, and only a key that his file does not have takes a default.
- Risks for the HW run:
  - The first RSX use of a swizzled mip chain in this tree. A fault shows
    as scrambled or black textures at a distance, or a render freeze.
    Mipmaps OFF in the SELECT menu clears it (the cache is cleared).
  - One SPU job per level: a frame that decodes many mipmapped textures
    waits longer (`[spepix] tex jobs`, `wait total`, `max`). A chain job
    on the SPU would cut it; that is an SPU image change.
  - RSX memory: `[ps3gl] WARNING: failed to allocate` must not show.
- UDP listener: started at Matt's request (2026-10-10 22:23, PID 30384,
  port 18194, into `ps3log_b3_run1.txt`, scratchpad of session 39900053),
  stopped at his request at 22:43. Port 18194 is free.
  Matt: "I'll just do the tests with everything ON." So the run has the 4
  rows ON from the start, not the steps below.
- HW boot 1 2026-10-10 ~22:23-22:28, Crazy Taxi (cut as `ct_b3.txt`).
  `[texcfg] boot: Mipmaps=1 Aniso=4 TexFilter=0 VqFormats=1 SpeTexHash=1
  SpeTexDecode=1 TexSlotHash=1 YuvBlockDecode=1 BumpMap=1`. Matt: "The
  roads looked right. Same for Sonic Adventure 1."
  - 294 s, no fault line, no dropped render, `fail=0` on the three
    texture and vertex SPUs, no allocation warning, clean XMB exit.
  - The chain reaches the RSX: 1272 textures decoded with a chain (6781
    lower levels); up to 184 of 285 live textures have one (avg 6.4
    levels, max 9); 65-75% of the binds in the drive have maxlod above 0.
    180 of the 1452 mipmapped fills got no chain (mirrored).
  - `palvq=0 plvq=0` in every window: Crazy Taxi has no texture of the
    two VQ forms, so VQ Formats had nothing to act on.
  - Texture Filter went NEAREST, LINEAR and back to GAME in the SELECT
    menu; each switch is in the log.
  - Speed: 100% in the drive but for 5 windows at 93-97% in one scene of
    17-20K vertices. That scene is SH4-bound (render `busy` 29%, `rend`
    stall 0.0-0.1 ms per frame), as in the bundle 2 runs.
  - Cost against the two Crazy Taxi runs of bundle 2 (other scenes, so
    not same-load): decode 0.43% of the render thread (0.31-0.36), `gl`
    3.3% (2.4-3.0), `job_max` 16 ms at most (13). SPU decode jobs 5200
    (1614-2329); the render thread waits for them 3.2 ms at most per 5 s
    window.
- HW boot 2 2026-10-10 ~22:31-22:37, Sonic Adventure (cut as
  `sonic_b3.txt`). The same 4 values (`YuvBlockDecode=0` in this boot).
  Matt: the image is right (his words are in boot 1).
  - 216 s, no fault line, no dropped render, `fail=0`, no allocation
    warning, clean XMB exit.
  - 560 textures decoded with a chain (3135 lower levels); up to 185 of
    258 live textures have one (avg 6.8 levels, max 9).
  - `palvq=0 plvq=0` in every window here too.
  - Texture Filter went NEAREST, LINEAR and back to GAME in the SELECT
    menu.
  - Speed: no window of play under 99%. The windows under it are the
    SELECT menu, the boot, one load, and the intro FMV (91.5-93.0%, the
    known SH4-bound FMV).
  - Render `busy` 13% at most, decode 0.21%, `job_max` 21 ms at most (a
    level load).
- State after the two boots: Mipmaps, Anisotropic 16x and Texture Filter
  work on HW in Crazy Taxi and Sonic Adventure (Matt's eyes, clean logs).
  VQ Formats ON changed nothing in them: no game with a palette VQ or a
  scan-order VQ texture has run. Not run: Rayman 2, and the CHECK boot.
- Verdict (Matt 2026-10-10): "Perfect. It's a success then." Rayman 2 is
  skipped at his word. Asked once for the defaults: Mipmaps ON,
  Anisotropic 16x (the only value that ran on HW), VQ Formats ON (OFF
  decodes the two forms as garbage; only the PC check covers ON), no
  package.
- Left for bundle 3: a package with the defaults (when Matt asks for
  one). If a later game shows a wrong texture at a distance, try Mipmaps
  OFF first, then Anisotropic 1x; for a wrong texture close up in a game
  with `palvq=` or `plvq=` above 0, VQ Formats OFF.
- HW steps (package of 22:06; the install keeps `nullDC.cfg`, the 4 rows
  come up OFF; SH4 Clock 200). All rows are live, so one boot per game:
  1. Crazy Taxi, all rows OFF, to a drive: nothing must change. Read
     `[texcfg] boot:`.
  2. SELECT, TEXTURES, Mipmaps ON. Look at the road and the buildings far
     away: less shimmer, no wrong texture. Log: `[tex] mipmaps ON`,
     `[texfmt] chains=`, `[ps3gl] mip uploads` above 0, `[spepix] ...
     fail=0`, the render `busy` against step 1.
  3. Anisotropic 16x: the far road is sharper. Then Texture Filter
     NEAREST, LINEAR, and back to GAME.
  4. VQ Formats ON: nothing must change unless `palvq=` or `plvq=` is
     above 0.
  5. Sonic Adventure (floors at a distance) and Rayman 2 (mip and VQ
     textures), the same way.
  6. Optional, one more boot: SPE Tex Decode and SPE Tex Hash at CHECK
     (VIDEO / DEBUG, set before boot) with Mipmaps ON: `check tex ok=N
     bad=0`.
  7. On a fault: the row OFF in the SELECT menu; note the game and the
     scene.

**Done: bundle 2, GD-ROM and disc images (section 7; Matt 2026-10-10: "Go
with bundle 2."), HW SUCCESS 2026-10-10 (Matt), every row default ON in
the source since 2026-10-10 (Matt: "apply them as ON by default."; GD-ROM
Speed default REAL, his pick). The package of 19:26 (the build of 18:48,
packed again) still has every row OFF; the package of 22:06 (bundle 3,
above) is the first one with these defaults. HW: ChuChu Rocket
and Crazy Taxi with every row ON, and a Crazy Taxi load with GD-ROM Speed
REAL. Not tested on HW (Matt: "I'll do it later on"): Disc Swap (a
two-disc game) and the two GDI rows (a GDI set); they are default ON too.
Crazy Taxi freezes at its pause: the open item below, not a bundle 2
fault. Matt's picks before the build (2026-10-10, asked once): all items
in one package; P61 and P62 added (they were not in the bundle's list);
the CD-DA miss fix as 3 levels (OFF / PLAY AHEAD / + SILENCE); a new
options page, "GD-ROM". 14 rows; the first value of each is the old path.**
- Defaults (`ps3/ps3_presets.cpp`): `GdStatus` 2, `CddaRules` 1,
  `GdPosition` 2, `GdScdQ` 1, `TocFields` 1, `CddaMiss` 2, `GdSpeed` 1,
  `GdDmaFix` 1, `GdCmdFix` 1, `GdSetMode` 1, `Sector2340` 1, `GdiParser` 1,
  `GdiPaths` 1, `DiscSwap` 1. An install keeps `nullDC.cfg`: Matt's file
  has these values from the HW run (GdSpeed 1 too).
- The rows (options page 5, GD-ROM; the key is in `nullDC.cfg` [ps3]).
  "Live" rows act from the SELECT menu; the rest are set before boot.
  - GD Status (`GdStatus`, P21): OLD / READ STOPS CD-DA / + TIMING. Level
    1: SPI_CD_READ stops CD-DA, and a PLAY status becomes PAUSE (Hydro
    Thunder, NFL Blitz 2001). Level 2: Flycast dev's timing (`e6707469d`,
    `fff68b3f5`): PLAY while a data read runs and for 67 ms after it;
    CD-DA starts 5 ms after CD_PLAY; a seek shows SEEK with DSC clear for
    20 ms, then PAUSE; a stop shows BUSY for 5 ms, then STANDBY; a resume
    shows BUSY for 20 ms. Level 2 also: a resume acts only on a paused
    play; GET_SCD takes its audio status from the CD-DA state (0x11, 0x12,
    0x13, 0x15); the status registers show the real DSC bit (the old path
    always reads it as set).
  - CD-DA Rules (`CddaRules`, P61): (a) the end test is >=; (b) an end FAD
    of 0 is the end of the disc (REQ_SES session 0); (c) a resume acts only
    on a paused play, and past the end it stops (STANDBY); (d) only the two
    start forms take a repeat count; (e) the end of a play leaves PAUSE and
    audio status 0x13; (f) a disc event stops CD-DA.
  - GD Position (`GdPosition`, P47, live): OLD / REQ_STAT / + GET_SCD.
    REQ_STAT sends the FAD by shifts (the old union bytes are FAD >> 8 on
    the PS3), the track and control/ADR from the TOC, index 1. The FAD is
    the CD-DA position while it plays or is paused, else the last sector a
    data read took. Level 2 puts the same position, track (BCD) and
    control/ADR in GET_SCD format 0, with the time inside the track.
  - GET_SCD Q Data (`GdScdQ`, P62, live): format 1 is 14 bytes (control/ADR,
    track, index, FADs in the track, 0, FAD), cut at the asked length;
    format 0 is cut at its 100 bytes (the old path copies past the buffer).
  - TOC Fields (`TocFields`, P23): ADR 1 in every used TOC entry; the
    lead-out is a data entry with ADR 1; a GDI's low-density lead-out is
    the FAD after track 2 (from its file size), not 13085.
  - CD-DA Miss (`CddaMiss`, 5.34's parked item, live): PLAY AHEAD: CD_PLAY
    and a resume tell the worker to read the play's window at once
    (`gda_cdda_seek`). + SILENCE: a ring miss gives silence and the play
    position waits; the SH4 thread reads no CD-DA sector. After 150 silent
    pulls in a row (2 s) it reads the sector itself. Needs GD-ROM Async at
    + CD-DA.
  - GD-ROM Speed (`GdSpeed`, P22, live): FAST / REAL. REAL is Flycast's
    pacing: 10 KB chunks at 1.8 MB/s, a last chunk of 10 KB or less at 25
    MB/s; only a transfer under 448 cycles moves inside the SB_GDST write.
    The waits count down in SlowUpdate (each chunk is 80 SlowUpdates, 0.8%
    over). Loads take as long as on a Dreamcast.
  - GD DMA Fix (`GdDmaFix`, P22 + P9, live): SB_GDLEN 0 is 32 MB (Pro
    Pinball Trilogy); a DMA longer than its read stops where the read
    ends, the read completes, and the DMA goes on with the next read. The
    two verifies that hang are not reached.
  - GD Command Fix (`GdCmdFix`, P22, live): an unknown ATA command aborts
    with an interrupt (sense 5); ATA_EXEC_DIAG completes; a command to the
    slave drive does nothing and its status reads 0; a command write of the
    wrong size does nothing; CD_READ2 takes its length from bytes 6-7;
    SET_MODE stops at the 10 writable bytes and GET_TOC at the TOC's 408;
    IDENTIFY_DEV sends its 0x50 bytes from offset 0.
  - SET_MODE Fix (`GdSetMode`, P48, live): the write goes to the byte
    offset, where REQ_MODE reads.
  - 2340-byte Sectors (`Sector2340`, P9, live): a read with header and
    subheader gets the raw sector from byte 12.
  - GDI Parser (`GdiParser`, P23): a track name in quotes may hold spaces;
    a line that does not parse fails the load with a message; REQ_SES
    gives session 2 at track 3 (`SessionStart[1]`).
  - GDI Backslash (`GdiPaths`, P57): a `\` in a track path opens as `/`.
  - Disc Swap (`DiscSwap`, P34, live): TRIANGLE in the SELECT menu opens
    the disc browser (X inserts, O goes up or back). The next image opens
    after the menu closes; the drive is BUSY with sense 2/04/01 for 1
    emulated second, then reports unit attention once (sense 6, ASC 0x28;
    0x29 when no reader took the image). The swap clears the command in
    flight, the buffers and CD-DA (fourth review). ON also makes ATA_NOP
    clear DRQ (Sentimental Graffiti 2).
- Log: `[gdcfg] boot: GdAsync= GdStatus= ...` with every row, and a
  `[gdcfg] menu:` line when one changes; `[swap] <path>: open, drive busy
  for 1 s`, then `[swap] drive ready: status N, disc format N`. With +
  SILENCE the `[gdr]` line shows `cdda 0/0` (no SH4 read) while `ra miss`
  counts the silent pulls. A GDI track that does not open now logs `GDI :
  cannot open track file <path>` (every mode).
- Code: `dc/gdrom/gdromv3.cpp` (the drive: all but the image rows),
  `ps3/ps3_gdasync.cpp` (`gda_cdda_seek`, `gda_cdda_on`, `gda_cdda_reset`),
  `plugs/ImgReader/common.cpp` (2340, TOC fields, the track table for
  P47), `iso9660.cpp` (GDI parser, paths, lead-out; `iso_term` now closes
  the track files), `ImgReader.cpp` (`libGDR_SwapDisc`, the track
  lookups), `ps3/ps3_menu.cpp` (the page, the in-game browser),
  `ps3/ps3_presets.cpp`. The status changes, the DMA waits and the swap's
  second count down in `gdrom_spinup_tick` (SlowUpdate), so the SYS GATE
  needs no change.
- Not taken from Flycast: the raw 96-byte Q data of GET_SCD format 0 (ours
  keeps its 18-byte form), format 2, the stop at the lead-out, sense 5 on
  an unknown SPI command, the soft-reset rework, an open-lid state.
- PC (WSL), three checks, all PASS:
  - `misc/gd2_check/run.sh` (new, one thread): the file with every option
    at 0 against the frozen old file (`ref_gdromv3.inc`, HEAD 93fca79): 40
    seeds of 20000 random guest operations (ATA and SPI commands, PIO and
    DMA reads, CD-DA pulls, ticks, disc events), every reply and register
    hashed: equal. Then 11 models, one per option, against the ported
    rule (P21 step 1, step 2, P47, P62, P61, commands, P48, DMA fix with
    the full 32 MB, REAL pacing, PLAY AHEAD, the swap's sense). `mutate.py`:
    89 of 89 planted bugs fail.
  - `misc/gdasync_check/run.sh` (the 5.34 check, extended): 25 runs, all
    PASS, no ThreadSanitizer report. New in it: CD-DA Miss at PLAY AHEAD
    and + SILENCE, the 150-pull limit with a blocked worker, a DMA of
    length 0 with the worker, GD-ROM Speed REAL with the worker, and 24
    disc swaps per run (the ring gives no sector of the old disc; a DMA in
    flight does not hang). SILENCE: 0 SH4 reads in steady play, seeks and
    loops; about 6 silent pulls per seek or loop turn in the check's
    timing. PLAY AHEAD: 0 misses at the start of 40 plays (80 without it).
    `mutate.py`: 34 of 34 planted bugs fail (17 of 5.34, 17 new;
    the new ones run `run_b2.sh`, the bundle 2 runs alone).
  - `misc/gdi_check/run.sh` (new, ASan + UBSan): the real `iso9660.cpp` and
    `common.cpp` on GDI sets in the 4 layouts of Flycast's
    `tests/files/test_gdis`, broken GDIs, the TOC conversion against the
    old formula (2000 random TOCs), the lead-outs, the track table, 2340
    from 2352 and 2448, 1300 disc changes (no file left open).
    `mutate.py`: 36 of 36 planted bugs fail.
- Not under a check: the menu (the page, the in-game browser), the real
  `libGDR_SwapDisc` with CDI and MDS images (its three steps are compared
  by text), the big-endian build of the new replies (they are built by
  shifts and bytes).
- Found, not changed (not in the bundle):
  - The GDI reader takes FAD 150 as LBA 150 (`if (StartSector>150)`,
    `iso9660.cpp`), so the first sector of track 1 reads wrong.
  - A loop turn or a seek still costs either one SH4 stall (OFF, PLAY
    AHEAD) or 1-2 silent sectors (+ SILENCE): the ring holds one run, so
    it cannot hold the loop start before the turn.
  - Flycast dev keeps CD-DA playing for the 20 ms of a seek, from the new
    position. + TIMING ports it as it is.
  - A soft reset leaves DRQ set (old path).
- Link: `.got` 0xD030, 0 `r2off`, the old warning only.
- UDP listener: started at Matt's request (2026-10-10 20:18, PID 22112,
  port 18194, into `ps3log_b2_run1.txt`, scratchpad of session 12aaf4cf),
  stopped at his request at 20:59. Port 18194 is free.
- Short HW procedure (Matt 2026-10-10: "Any way to reduce the boots?"), in
  place of the 10 steps below: every row ON at its top level, set one
  time on the main menu, GD-ROM Speed FAST. Boot 1 ChuChu Rocket (music,
  pause, resume, loops). Boot 2 Crazy Taxi (load, drive; then GD-ROM Speed
  REAL in the SELECT menu for one load). Boot 3, if Matt has one: a
  two-disc game for Disc Swap. On a fault: the live rows OFF first, then
  one boot each for GD Status, CD-DA Rules and TOC Fields.
- HW boot 1 2026-10-10 ~20:17-20:19, ChuChu Rocket. Matt: "Music ran
  fine." The game ran before the listener did, so the log has 58 s and no
  `[gdcfg] boot:` line: the rows of this boot are not on record (boot 2
  will show them; the install keeps them).
  - No fault line, SPEED 100%, clean XMB exit.
  - CD-DA: 1369 sectors from the ring, 16 ring misses, 0 SH4 CD-DA reads.
    So + SILENCE was on and acted: a miss was 13 ms of silence, not an SH4
    stall. The misses fell in 6 of the 27 `[gdr]` seconds, 4 of them
    seconds with data fills (the worker reads the data first).
  - Disc data: 177 fills, all on the worker.
  - Matt also: "ChuChu Rocket still has the slow reverb"; "sound cues that
    play fast instead here they loop and play slowly". That is F1 (section
    6, parked 2026-10-04), not a bundle 2 item: no row acts on the AICA.
- HW boot 2 2026-10-10 ~20:20-20:24, Crazy Taxi (cut as `ct_b2.txt`).
  `[gdcfg] boot: GdAsync=2 GdStatus=2 CddaRules=0 GdPosition=2 GdScdQ=0
  TocFields=0 CddaMiss=2 GdSpeed=0 GdDmaFix=1 GdCmdFix=1 GdSetMode=1
  Sector2340=1 GdiParser=1 GdiPaths=1 DiscSwap=0`. So 4 rows were OFF and
  are not tested: CD-DA Rules, GET_SCD Q Data, TOC Fields, Disc Swap (most
  likely in boot 1 too).
  - Boot, menus, load and a drive: no fault line. 194 s, mean SPEED 99.8%.
    850 data fills (18910 sectors), all on the worker; 0 SH4 disc reads.
  - The run ended in a freeze at START in the drive: the open item below.
    It is in the render-to-texture path, not in the GD-ROM.
  - Not run: the load with GD-ROM Speed REAL (no row changed in the SELECT
    menu before the freeze).
- HW boot 3 2026-10-10 ~20:26-20:28, ChuChu Rocket with every row ON
  (cut as `chu_b2_boot3.txt`). `[gdcfg] boot: GdAsync=2 GdStatus=2
  CddaRules=1 GdPosition=2 GdScdQ=1 TocFields=1 CddaMiss=2 GdSpeed=0
  GdDmaFix=1 GdCmdFix=1 GdSetMode=1 Sector2340=1 GdiParser=1 GdiPaths=1
  DiscSwap=1`. Matt: "Then It's a success." (the sound cues are F1, not
  this bundle).
  - 84 s, mean SPEED 99.7%, no fault line, clean XMB exit.
  - CD-DA: 4324 sectors from the ring, 23 ring misses, 0 SH4 CD-DA reads.
- HW boot 4 2026-10-10 ~20:31-20:36, Crazy Taxi with every row ON, then
  GD-ROM Speed REAL from the title on (`[menu] GD-ROM Speed: 0 -> 1`; cut
  as `ct_b2_real.txt`; timelines `tl_real.txt`, `tl_fast.txt` by
  `tl.awk`). Matt: "Success".
  - No fault line, clean XMB exit, 0 SH4 disc reads. The first Crazy Taxi
    boot with TOC Fields, CD-DA Rules and GET_SCD Q Data ON: it boots and
    plays.
  - Loads: the worker's peak is 640 sectors/s (1.3 MB/s) with REAL, 1184
    to 1824 with FAST (boot 2). The load to the drive reads ~2100 sectors
    in ~4.5 s (REAL) against ~2.5 s (FAST). So REAL adds about 2 s to that
    load: FAST is already bound by the PS3 disk, near the drive's rate.
  - ~30 s with no vertices and no disc read come before that load in both
    boots (30 and 31 s): not from REAL.
  - Drive: the same stream rate (46 against 47 sectors/s); SPEED 99.8%
    (77 s) against 99.9% (117 s); the dips of the REAL run (90-98%, 6 s)
    are in scenes of 15-20K vertices, which boot 2 did not have.
  - The second SELECT menu visit changed no row, so `nullDC.cfg` still has
    GdSpeed=1 (REAL).
- Verdict (Matt 2026-10-10): "apply them as ON by default." All 14 rows;
  for GD-ROM Speed he picked REAL (asked: FAST or REAL).
- Left for bundle 2 on HW, for later (Matt): Disc Swap with a two-disc
  game; a GDI set for the two GDI rows and for the GDI lead-out of TOC
  Fields. The 4 rows that act only on rare input (GD DMA Fix, GD Command
  Fix, SET_MODE Fix, 2340-byte Sectors) showed no change in the two
  games; no game that needs them ran.
- HW steps (package of 19:26). The install keeps `nullDC.cfg`; the 14 rows
  come up OFF. SH4 Clock 200. Each boot: read the `[gdcfg] boot:` line.
  1. Boot 1, all rows OFF: Crazy Taxi to a drive, ChuChu Rocket with
     music. Nothing must change against the last package.
  2. Boot 2, the fixes that must change nothing in a good game (all live,
     set in the SELECT menu): GD DMA Fix, GD Command Fix, SET_MODE Fix,
     2340-byte Sectors ON. Crazy Taxi load and drive, Sonic Adventure,
     Looney Tunes to a race.
  3. Boot 3, CD-DA Miss: ChuChu Rocket and Looney Tunes with CD-DA. PLAY
     AHEAD first, then + SILENCE in the SELECT menu. Listen at song starts
     and loop turns. Log: `[gdr] ... cdda N/N` (0/0 with SILENCE), `ra hit
     / miss`, `[stall] gd` at the misses.
  4. Boot 4, GD Status = READ STOPS CD-DA; boot 5, + TIMING; boot 6, CD-DA
     Rules ON: ChuChu Rocket and Looney Tunes CD-DA (pause, resume, looped
     tracks), the BIOS CD player, Crazy Taxi loading.
  5. Boot 7, GD Position = + GET_SCD and GET_SCD Q Data ON (live): the
     same games; the BIOS CD player's track and time display.
  6. Boot 8, TOC Fields ON: every game must still boot; a GD-ROM image if
     Matt has one (the lead-out change is GDI only).
  7. GD-ROM Speed REAL (live): Crazy Taxi from the menu to a drive. Loads
     take longer; nothing must hang.
  8. GDI Parser and GDI Backslash: a GDI set with spaces in its track
     names (quoted) or `\` paths, if Matt has one.
  9. Disc Swap ON (live): a two-disc game at its disc 2 prompt; SELECT,
     TRIANGLE, X on disc 2. Log: the two `[swap]` lines.
  10. On a fault: the row OFF (live rows at once); note the game and when.

**Open, to investigate (Matt 2026-10-10: "Also Crazy Taxi crashes when
pushing start. Add it to the things to investigate."): Crazy Taxi freezes
when START is pressed in a drive (the pause). Evidence from one log only;
nothing analysed further, nothing built.**
- HW 2026-10-10 ~20:24, package of 19:26, boot 2 of the bundle 2 run
  (`ct_b2.txt`, cut from `ps3log_b2_run1.txt`, scratchpad of session
  12aaf4cf; the `[gdcfg]` line is in the bundle 2 block above).
- The log:
  - START at the title and in the menus is fine (3 presses, `[pad] dc0
    kcode=FFF7`). The freeze is at the first START in the drive, after 194
    s at SPEED ~100%.
  - Right after that `[pad]` line: `[hb] t=240s pc=0C159AE4 vbl=11541
    (+54) blk=0 (+0) phase=rtt seq=17264 rend=rtt seq=47870`. Every later
    `[hb]` line is the same, with `vbl ... (+0)`; `[aud] out=0/s`. No
    fault line.
  - XMB exit: `[ps3] SH4 did not stop in 3 s, forcing process exit`.
- Reading: this is the first render to texture seen on HW (`rtt=` read
  0/0 in every `[rend]` line before it, in all earlier logs too). The
  pause takes the frame as a texture: `StartRender()` sees FB_W_SOF1 bit
  24 and the SH4 thread waits in `rth_run(JOB_RTT)` (`ps3/ps3_rend.cpp`,
  `SH4_PHASE(PH_RTT)`); the render thread is in `rtt_render()`
  (`REND_PHASE(PH_RTT)`) and does not return.
- Not known: if an older package pauses Crazy Taxi (no earlier log has an
  RTT, so it may never have worked); which call in `rtt_render()` does
  not return; if SPE Draw, SPE Build, SPE Tex Decode or Depth Float have a
  part in it (all ON here).
- Not a bundle 2 fault: no bundle 2 row is in the renderer.
- Related: the RTT note of section 3 and P54 (render-to-texture pixel
  packing), bundle 11 (output stage, RTT games), TR1/TR3 of TArender.MD
  (their RTT checks were never run).

**Step 3 of the SH4-bound Looney Tunes race (Matt 2026-10-10: "We will now
work on Step 3."): 5.45, an SH4 Clock above 200 MHz, is built and PC
tested, package of 16:30 on 2026-10-10. HW 2026-10-10, Looney Tunes at 220
and 250 MHz: the logs are clean and the game draws more frames per
emulated second (26.4 -> 28.0 at 220, 28.2 at 250), but the PS3 does not
hold the clock in the race (SPEED 97.5% at 220, 86.6% at 250). Matt: 220
"was basically the same", "250 feels worse. Stopping now." Boot 3 (Crazy
Taxi) was not run. Matt's verdict (2026-10-10): "Keep at 200 MHz. Clearly
not the right approach on this issue.", then "We can close the step 3."
Step 3 is closed. The default stays
200 MHz; the 150-250 range stays in the menu (no word from Matt on it).
The Looney Tunes stutter stays open: its cause is not known.**
- Matt's two choices (2026-10-10): the mechanism "CPU cycles per slice"
  and the menu limit 250 MHz.
- EMULATION -> SH4 Clock is now 150-250 MHz in steps of 5 (`SH4Clock`), set
  before boot.
  - Up to 200: as before. The time bases (SPG lines, AICA samples, RTC, DMA
    delays) follow the clock through `SH4_CLOCK_EFF`.
  - Above 200: `SH4_CLOCK_EFF` stays at 200 MHz (`sh4_clock_eff_mhz`,
    `plugins/plugin_types.h`). The SH4 gets clock/200 times the cycles per
    timeslice: `s_cpu_timeslice` (`ApplyAccuracyPreset`,
    `dc/sh4/sh4_interpreter.cpp`), to the nearest cycle. Accuracy FAST:
    1971 at 220, 2240 at 250 (1792 at 200). The modules still advance by
    `s_timeslice`, so the TMU, the video, the sound, the ARM7, the disc
    and the SYS GATE keep 200 MHz time. No more slices per second and no
    more ARM7 work. 6.4 (TMU Clock EFF) is not needed for it.
  - The SH4's slice goes to the JIT main loop (`jit_timeslice`: its `li`,
    its `addi` and the RESUME `addi`, `ps3/ps3_driver.cpp`), to
    `Sh4_int_Run` and to the cached interpreter. A clock above 250 in
    `nullDC.cfg` acts as 250.
  - Reference: Flycast scales each block's cycles by 200/clock
    (`core/hw/sh4/dyna/decoder.cpp:1064`, slider 100-300) and leaves its
    modules at 200 MHz time: the same effect. Its rounding per block gives
    short blocks no gain at 205-220; the slice form has no such loss.
  - Why not the menu range alone: the TMU counts raw SH4 cycles, so at 220
    it runs 10% fast against the video, and the host runs 10% more slices
    and ARM7 cycles.
- Log: `[jitcfg] gbatch= ramsq= fschg= clk= slice= cpuslice=`; `[sndcfg]
  boot: ... SH4Clock=`. SPEED reads against 200 MHz time at every clock
  above 200 (100% = full speed).
- PC (WSL): `misc/clock_check/run.sh` (new) PASS, 2878 checks, on
  `sysgate_check`'s harness (its clock stub is now a variable):
  - The module trace and the batch counts of 60 timelines (gate off, gate,
    gate + batch) at 205, 220 and 250 MHz equal those at 200 MHz. All 60
    traces change at 150 MHz, so the traces do see the time bases.
  - Both slices and `SH4_CLOCK_EFF` for each clock 100-300 and each
    Accuracy, against `lround(slice x clock / 200)`.
  - The real `Sh4_int_Run` on one counted opcode, 3000 slices: the ops
    follow the clock, each AICA tick keeps its cycles.
  - 7 source lines (the JIT main loop, the cached interpreter, the sound
    samples, the menu range): that code does not build on the PC.
  - `mutate.py`: 22 of 22 planted bugs fail (4 of them only on the source
    lines).
- `misc/sysgate_check/run.sh` PASS with the same counts as on 2026-10-10
  before the change (300 seeds: 169263 batches, 512502 slices, 27245 runs
  fell back, 32.4M plan checks, 0 differ); also with STALL=1 and RACEFIX=1.
  Its `mutate.py`: 63 of 63 planted bugs fail.
- Not checked on the PC: the emitted JIT main loop with the larger slice.
  The compiled `ApplyAccuracyPreset` was read in the disassembly (the 250
  limit, the multiply, the +100, the divide by 200). The HW run is the
  test.
- Link: `.got` 0xCEA8, 0 `r2off`, the old warnings only.
- UDP listener: started at Matt's request (2026-10-10 17:13, PID 18064,
  port 18194, into `ps3log_step3_run1.txt`, scratchpad of session
  dcef45af), stopped at his request at 17:23. Port 18194 is free.
- HW boot 1 2026-10-10 ~17:13-17:17, Looney Tunes at 220 MHz, one race
  (cut as `lt_step3_220.txt`; `step2cmp.py` of session fda9f847 and
  `rpshist.py`, scratchpad of session dcef45af), against `lt_step2.txt`
  (200 MHz, the same settings). `[jitcfg] ... clk=220 slice=1792
  cpuslice=1971`; `[gate]` slices 111.6K/s as at 200 MHz; no fault line,
  clean XMB exit. Matt: "It was basically the same."
  - 108 race windows. The game draws 28.0 frames per emulated second
    (26.4): 2.14 vblanks per frame (2.27). Most windows are now at 28-30
    (53 of 108; before 26-28, 36 of 78), 14 at 30 or more (1). So RPS
    follows the clock: +6% frames for +10% cycles.
  - The host does not hold it: SPEED 97.5% (100.0%), full-speed windows
    35% (94%), sleep 27 ms/s (99.5), sys 73.4 (72.6). By load, SPEED and
    sleep: 10-20K 100.2%, 40 ms/s; 20-25K 98.5%, 37; 25-30K 96.8%, 30;
    30K+ 95.1%, 5. Wall RPS 27.3 (26.4).
- HW boot 2 2026-10-10 ~17:18-17:21, Looney Tunes at 250 MHz, a shorter
  race (cut as `lt_step3_250.txt`). `[jitcfg] ... clk=250 slice=1792
  cpuslice=2240`; no fault line, clean XMB exit. Matt: "250 feels worse.
  Stopping now."
  - 35 race windows. 28.2 frames per emulated second, 2.12 vblanks per
    frame: almost no gain over 220 MHz (28.0). 23 of 35 windows at 28-30,
    3 at 30 or more.
  - SPEED 86.6%, full-speed windows 6%, wall RPS 24.3 (26.4 at 200 MHz):
    the race runs slower in real time. 30K+ vertices: SPEED 84.6%.
- Reading of the two boots:
  - The working assumption of 2026-10-08 holds in part. +10% SH4 cycles
    remove about half of the game's 3-vblank frames (27% of the frames
    at 200 MHz, 14% at 220). +25% removes no more (12%). So the rest of
    the stutter is not the SH4 cycle budget; cause not known.
  - The PS3 lacks ~2.5% of SH4 thread time for 220 MHz in the race (5% at
    30K+ vertices) and ~13% for 250 MHz.
  - Not tested: 205-215 MHz (the room of the Step 2 run), Crazy Taxi.
- Estimate from the Step 2 run: the race sleeps 99.5 ms/s, 51 ms/s at 30K+
  vertices. 220 MHz asks for up to ~10% more JIT work (~80 ms/s): it fits
  on average, not in the heaviest scenes. 250 MHz will not hold SPEED 100%
  in the race; it shows if RPS follows the clock.
- HW steps (package of 16:30):
  1. Install. The install keeps `nullDC.cfg`. On the main menu, before the
     boot: EMULATION -> SH4 Clock 220 MHz. The rest as in the Step 2 run
     (JIT C-Calls ON, JIT HOTBLOCKS OFF).
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1, Looney Tunes at 220: the intro FMV, then one full race (3
     minutes or more). Look at the stutter, the sound and the game speed
     (timers and music must run as at 200).
  4. Boot 2, Looney Tunes at 250: the same race.
  5. Boot 3, Crazy Taxi at 220: a dense drive of 2 minutes, then the XMB
     exit.
  6. Log: `[jitcfg] ... clk=220 slice=1792 cpuslice=1971` (2240 at 250);
     `[gate] slices=` as at 200 MHz; race RPS against 26.4, SPEED,
     `[stall]` sleep against 99.5 ms/s.
  7. Reading: RPS up at SPEED ~100% -> the working assumption holds (the
     game's work per frame does not fit the 200 MHz cycles of 2 vblanks).
     RPS the same -> it does not hold, and the clock is not the answer.
  8. On a fault: SH4 Clock 200.

**Step 2 of the SH4-bound Looney Tunes race: HW SUCCESS 2026-10-10 (Matt:
"Success: everything looked like before."). The four Step 2 candidates
(Matt 2026-10-10: "Resume with the next steps.") are built and PC tested,
package of 14:26 on 2026-10-10, each with its own setting. HW run (one
boot per game, all four ON): the logs are clean and the SH4 thread has
more spare time (below). Matt then: "But Crazy Taxi felt smoother. Looney
Tunes still had the stutter." All four are default ON since the package
of 15:49 on 2026-10-10 (Matt: "keep the settings default on since they
seem to be mostly massive wins."): SYS GATE BATCH ON, JIT SQ MISS + LA
STEPS, JIT FSCHG + NO BLOCK END, RAM SQ Direct ON. Step 3 (an SH4 Clock
above 200 MHz) is the block above (Matt then: "do not start Step 3 yet. I
will start it in a separate session.").**
- Defaults (`ps3/ps3_presets.cpp`): `SysGateBatch` 1, `RamSqDirect` 1,
  `JitFschg` 2, `JitSqMiss` 3. The last one also makes 5.27's SLOW PATH
  and LOOK-AHEAD default ON (they were default OFF; Matt's `nullDC.cfg`
  had 2). Package of 15:49: `.got` 0xCEA0, 0 `r2off`. An install keeps
  `nullDC.cfg`, and Matt's file has these values from the HW run.
- The Looney Tunes stutter is not SH4 host speed any more: the race runs
  at SPEED 100.00% and the game still draws 26.4 frames/s, a mix of 2- and
  3-vblank frames. Working assumption of 2026-10-08, not proven: the
  game's work per frame does not fit the emulated cycles of 2 vblanks (the
  JIT bills 1 cycle per non-FPU op, `CPU_RATIO` 1, and 0 per FPU op). Step
  3 is the test: if RPS rises with the clock, it holds. Step 2 only added
  spare time: sleep 99.5 ms/s in the race, 51 ms/s at 30K+ vertices. That
  is the room for Step 3: ~10% (220 MHz) on average, ~5% (210 MHz) in the
  heaviest scenes.
- Sonic Adventure 2 (Europe, CDI; Matt 2026-10-10: "as last test: I'm
  going to do Sonic Adventure 2 with these settings."), the same package
  and settings; cut as `sa2_step2.txt`, against the boot of 2026-10-08
  (`sa2_bump.txt`, cut from `ps3log_bump_run1.txt`, session d0cd44fe;
  `sqmiss=2`, no batch). No fault line, clean XMB exit. Windows at 6K+
  vertices (144 against 63): SPEED 100.00% (100.05%), full-speed windows
  96% (86%), sleep 278 ms/s (239), sys 64.0 (67.8); sleep at 6-9K 447
  (345), at 9-12K 345 (266), at 12K+ 186 (195, other scenes). Batch 27.7K
  of 28.6K runs/s, `miss=0`; RAM flush write 22 ns. The game was at full
  speed before too. Matt gave no remark on this run.
- Step 3 facts (read before the build of 5.45): SH4 Clock scales the SPG
  line cycles, the AICA sample cycles, the RTC and the DMA delays through
  `SH4_CLOCK_EFF`; the TMU does not scale (6.4 "TMU Clock EFF" is open),
  so above 200 MHz the game timers would run fast against the video. The
  menu range was 150-200 (`ps3/ps3_menu.cpp`). 5.45 (the block above)
  takes the other way: above 200 the time bases stay and the SH4 gets
  more cycles per slice.
- UDP listener: started at Matt's request (2026-10-10 ~14:40, PID 16720,
  into `ps3log_step2_run1.txt`, scratchpad of session fda9f847), stopped
  at his request at ~15:48. Port 18194 is free.
- 5.42 SYS GATE BATCH (candidate 1). JIT SPEED -> "SYS GATE BATCH"
  (`SysGateBatch`), set before boot, acts only with JIT SYS GATE.
  - `UpdateSystem_gate` (`dc/sh4/sh4_interpreter.cpp`) runs the k - 1
    slices it skipped as one step per module, then the last slice whole
    (the plan puts the event there). `sys_gate_sync_slow` batches all its
    slices. Batches of 2 slices or more only.
  - `sys_slices_quiet(n)` first proves that the n slices hold no medium
    tick, no TMU underflow, no SPG event line or countdown end, and no AICA
    DMA end. If one does, nothing moves and the slices run one by one. So
    the batch equals the old gate in every case, also after a kick with no
    sync. The Scheduler and a pending list IRQ (Render Delay) keep the old
    path.
  - TMU (`tmu_slices_quiet_run`, `dc/sh4/tmu.cpp`): n slices are one step
    of n x timeslice cycles, exact while no channel underflows. SPG
    (`spg_slices_quiet` / `spg_slices_run`, `plugs/drkPvr/SPG.cpp`): one
    compare when no line ends in the run; else the slice loop on copies,
    then the line, the field (one flip per line) and SPG_STATUS in one word
    store. AICA DMA (`dc/aica/aica_if.cpp`): one subtract.
  - `sys_plan` takes `tmu_slices_to_event_fast`: per running channel one
    multiply, shift and compare; a divide only when an underflow is in
    reach. The SPG plan keeps its loop (cheap).
  - With Accuracy FAST (Matt's setting) the medium tick ends every 4th
    slice, so a gate run is 3 batched slices and 1 whole slice. A line is
    ~3.5 slices (6355 cycles at 200 MHz), so most batches hold a line
    change: the batch handles lines that are not event lines.
  - Log: `[gate] ... | batch runs= slices= miss=` (miss = runs that fell
    back to the slice loop), a `quiet` part in `[sys]`.
  - PC (WSL): `misc/sysgate_check/run.sh` PASS, 300 seeds x 4000 slices,
    now 3 modes per seed (gate off, gate, gate + batch): the same trace in
    all three; 169263 batches (512502 slices), 27245 runs fell back; the
    closed-form TMU plan against the slice loop on the live state, 32.4M
    checks, 0 differ. Also PASS with STALL=1 and RACEFIX=1. New guest
    actions: k slices in one sync past the plan (events land anywhere in a
    run the batch tries), a countdown that ends exactly on a slice end, a
    list IRQ pending in such a run, TCNT writes a few steps from underflow.
    The check now registers the Scheduler's G2-DMA end (`aica_sb_Init`).
    `mutate.py`: 63 of 63 planted bugs fail (30 of 5.36, 33 of the batch).
- 5.43 JIT SQ MISS "+ LA STEPS" (candidate 2). JIT SPEED -> JIT SQ MISS has
  a 4th value (`JitSqMiss=3`), set before boot.
  - `sqla_class` (`ps3/ps3_jit_sqla.h`) takes a `steps` flag: `mov.x
    Rm,@-Rn`, `fmov FRm,@-Rn` and `add #imm,Rn` pass when they move the
    followed register. Nothing else changes; `sts.l`/`stc.l X,@-Rn` and
    `@Rm+` loads still stop the scan. The guess only picks the store's
    shape, as before.
  - The race loop: 8C0873B4 (7 stores through R6, ends at `fschg`) runs
    into 8C0873CA = `mov.l R7,@-R6; dt R0; bf.s; pref @R6` (the dump of
    `run_prof.txt`). The scan now passes the first op and hits the PREF in
    the delay slot. Matt's `nullDC.cfg` has `sqmiss=2` (the profile run),
    so level 3 is one step up.
  - Log: `[ta-split] ... sqla=` (look-ahead store sites) and `sqcold`.
  - PC (WSL): `misc/sqla_check/run.sh` PASS, seeds 1 and 7: the class for
    all 1048576 (opcode, register) pairs in both modes against the opcode
    list (with steps exactly 5120 pairs change, all to "pass"; `gen.py` now
    also gives the registers an opcode only moves by a constant), 2M
    random scans per mode, the Looney Tunes words and block as directed
    cases. `mutate.py`: 35 of 35 planted bugs fail.
- 5.44 RAM SQ Direct (candidate 3). VIDEO / DEBUG -> "RAM SQ Direct"
  (`RamSqDirect`), live.
  - `do_sqw_nommu` (`ps3/ps3_rend.cpp`): a flush whose QACR area is 3
    copies its 32 bytes to `mem_b[dest & 0x00FFFFE0]` (the address the JIT
    fastmem uses) and counts `sq_mem`; no call, no stack frame (checked in
    the disassembly: 4 `ld`, 4 `std`). The sampled probe call takes the
    same copy, so `ram ns` times the new path.
  - Old path kept when the cached interpreter has blocks
    (`g_sh4_cached_live`, set where `s_tables_init` is set): then
    `sh4_cached_invalidate_range` must run. Under the JIT it is false.
  - Log: `[ta-split] ... direct= ramsq=`.
  - PC (WSL): `misc/ramsq_check/run.sh` (new) PASS, 4M random flushes: the
    SQ flush code cut from `ps3_rend.cpp` against the old chain cut from
    `sh4_sqw.h`, `sh4_mem.cpp` (`WriteMemBlock_nommu_ptr`) and `_vmem.cpp`
    (`_vmem_GetBlockPtr`, RAM mapped as `sh4_mem.cpp` maps it), each side
    on its own 16 MB: the same RAM image, the same TA / VRAM / MMIO calls,
    `sq_mem` and census counts; 1.08M flushes took the direct path (no
    block write call), 0.54M RAM flushes the old chain (toggle off or
    cached interpreter), 25548 through the probe. `mutate.py`: 18 of 18
    planted bugs fail.
- 5.37 `fschg` without a sync or a block end (candidate 4). JIT SPEED ->
  JIT FSCHG has a 3rd value "+ NO BLOCK END" (`JitFschg=2`), set before
  boot.
  - Decoder (`dc/sh4/rec_v2/decoder.cpp`): at level 2 `fschg` is the `xor`
    on FPSCR and the decoder's own SZ flip, as in Flycast. No
    `shop_sync_fpscr`, no `dec_End`. Levels 0 and 1 emit what they did.
  - `old_fpscr` then keeps a stale SZ. Its readers: `UpdateFPSCR` (FR only)
    and JIT FRCHG's `emit_frchg_test`, which at level 2 leaves SZ out
    (`oris` 0x10, `xoris` 0x30; no record form, no wrapped mask). The swap
    path stores the whole FPSCR into `old_fpscr`.
  - Nothing in the backend reads SZ per block (checked: only the decoder
    reads `state.cpu.FSZ64`, per op). JIT DEAD VALUES keeps the `xor`:
    FPSCR is live at the block end and at every interpreter fallback.
  - Effect on the race loop: 8C0873A6, 8C0873B4 and 8C0873CA become one
    block. Its 7 stores then see the PREF in their own block (JIT SQ
    STORES), so 5.43 is not needed for this loop when 5.37 is on.
  - PC (WSL): `misc/fschg_check/run.sh` (new) PASS: the handler cut from
    `decoder.cpp` on a mock decoder state, levels 0-3, in and out of a
    delay slot, both SZ values; `mutate.py` 11 of 11. `misc/frpref_check`
    PASS (1000073 checks): the SZ-free test's 6 words against ppu-as, and
    on the PPC64 model: FR alone or FR + SZ changed -> the inline swap,
    `old_fpscr` = FPSCR; anything else (SZ alone too) -> the C path with
    the state untouched; `mutate.py` 29 of 29, all on the model alone.
  - Not checked on the PC: a whole block decoded across an `fschg` (the
    real decoder does not build there). The HW run is the test.
- Other PC checks run again after the edits: `g2dma_check` ALL PASS,
  `tr4_check` PASS, `bump_check` PASS, `spevb_check` PASS (3000 scenes),
  `spepix_check` PASS.
- Link: `.got` 0xCEA0, 0 `r2off`, the old warnings only
  (`g_jit_blocks_entered`, the unused `fm_*`).
- New `[jitcfg]` line: `gbatch= ramsq= fschg=`; `sqmiss=` is in the long
  line as before.
- Baseline for the HW run: `run1_off.txt` (2026-10-08 ~20:40, scratchpad
  of session 898d8ac1; package of 19:22; Accuracy FAST, `sysgate=2`,
  `sqmiss=2`, `dve=1`, `greuse=1`, `dyn_ic=2`, `hotblocks=0`; gate runs
  28K/s, 27.9K of them at the medium tick): 127 race windows,
  SPEED 100.0%, RPS 26.5, `[stall]` sys 80 ms/s, sleep 63 ms/s; `[sys]`
  tmu 12.5, pvr 18.1, aicadma 3.8, gate 4.3, plan 13.0 ms/s; `ram ns`
  write ~125; 3.74M cold store entries/s at 8C0873B4 (profile run).
  Estimates (% of the SH4 thread in the race): 5.42 ~3, 5.43 ~1.4, 5.44
  ~1.6-2, 5.37 ~1.
- HW run 2026-10-10 15:27-15:37 (package of 14:26; `ps3log_step2_run1.txt`,
  cut as `lt_step2.txt` and `ct_step2.txt`; `step2cmp.py`, `ctcmp.py`,
  scratchpad of session fda9f847). `[jitcfg]` of both boots: `sqmiss=3`,
  `gbatch=1 ramsq=1 fschg=2`, the rest as the baselines. No fault line,
  two clean XMB exits. The four settings were ON together, so the gain
  is not split per setting.
  - Looney Tunes, one race, against `run1_off.txt`. 78 race windows (the
    pause screen left out): SPEED 100.00% (99.99%), RPS 26.4 (26.5),
    full-speed windows 94% (81%), `[stall]` sleep 99.5 ms/s (63.2), sys
    72.6 (80.0). Sleep by load: 10-20K 159 (118), 20-25K 93 (68), 25-30K
    87 (28), 30K+ 51 (20); 30K+ full-speed windows 96% (64%). 60 fps
    scenes (6 windows): SPEED 99.8% (95.3%), RPS 59.8 (53.9).
  - `[gate]`: 27.8K of 28.1K runs/s batched (83K of 111.5K slices/s),
    `miss=0`. `[sys]` ms/s: tmu 5.3 (12.5), pvr 7.0 (18.1), aicadma 1.9
    (3.8), gate 1.3 (4.3), plan 11.6 (13.0), quiet 14.8 (new): 538 ns per
    batch, more than planned. The slice that is left costs more per call
    (tmu 187 ns, pvr 247): it is the one after the medium tick, so the
    old averages hid ~87 and ~135 ns warm slices. Leads, not built: the
    3 statics between `spg_slices_quiet` and `spg_slices_run` (store
    then load), the `%` per line, the plan (still 413 ns).
  - RAM flush: write 23 ns (128), whole flush 156 ns (259), path 0.89%
    (1.75%). SQ store sites: `sqst=380 sqla=3` (296 and 3): with 5.37 the
    vertex loop is one block, so its stores are in-block sites; 5.43
    added no site in this game.
  - The pause screen before the exit runs at 85-90% (7 windows, vt
    constant, 7.6-7.9M DYN_IC `rts` misses/s). Not new: `lt_spepix.txt`
    (2026-10-08) has the same windows at 83-87%. A lead for later: the
    return stack for `rts`.
  - Crazy Taxi, against `ct_b1.txt` (2026-10-08, the same `[jitcfg]` but
    `sqmiss=2`). Windows at 6K+ (217 against 177): SPEED 99.96% (99.81%),
    full-speed windows 98% (90%), sleep 203.6 ms/s (140.6), sys 61.3
    (65.4). Sleep by load: 6-9K 277 (219), 9-12K 187 (125), 12K+ 73 (28);
    12K+ SPEED 99.65% (97.81%), full-speed 87% (43%). Same-load VPS +0.1%
    (at 60 in both); 13K+ bins +1.1 to +5.3% (few windows). Batch 27.7K
    of 28.4K runs/s, `miss=0`. `sqst=411 sqla=57` (371 and 105).
- HW steps (package of 14:26):
  1. Install. The install keeps `nullDC.cfg`. SH4 Clock 200. On the main
     menu, before the boot: JIT SPEED -> SYS GATE BATCH ON, JIT SQ MISS
     + LA STEPS, JIT FSCHG + NO BLOCK END; VIDEO / DEBUG -> RAM SQ Direct
     ON, JIT C-Calls ON, JIT HOTBLOCKS OFF. The rest as now.
  2. UDP listener on port 18194, only when Matt says so.
  3. Looney Tunes: the intro FMV, then one full race (3 minutes or more).
     Look at the geometry (the vertex loop's stores change shape), the
     sound and the frame pace.
  4. Crazy Taxi: a dense drive of 2 minutes (same-load VPS), then the XMB
     exit.
  5. Log: `[jitcfg] ... sqmiss=3` and `[jitcfg] gbatch=1 ramsq=1 fschg=2`;
     `[gate]` `batch runs=` near `runs=`, `miss=` small; `[sys]` tmu, pvr,
     aicadma, gate and plan below the baseline, `quiet` new; `[stall]` sys
     below 80 ms/s and sleep above 63 ms/s in the race; `ram ns` write
     below 125; `sqcold` and `sqla=`; SPEED and RPS against the baseline.
  6. On a fault: RAM SQ Direct OFF in the SELECT menu (live). Then one boot
     each with JIT FSCHG ON, JIT SQ MISS + LOOK-AHEAD, SYS GATE BATCH OFF.
  7. For the gain of each setting alone (if Matt wants it): one boot per
     setting ON with the other three OFF.

**Step 1, done: the Looney Tunes race is SH4-bound (Matt 2026-10-08: "Let's work
with the The Looney Tunes race being SH4-bound.", then "Profile + 2
probes"). Step 1 done 2026-10-08: the race profile and both probes ran on
HW (package of 20:57, which also holds a JIT link fix, HW confirmed).
Step 2 = the four candidates at the end of this block ("Next steps"; Matt
2026-10-08: "Can you save them in the OPTIMIZATION_PLAN.md as next
steps?"): all four built 2026-10-10, the block above.** The
agreed order (the open item below): faster SH4 thread on the race first,
then an SH4 Clock above 200 MHz. At 21:10 this session's UDP listener
(PID 24700, `ps3log_lt_run1.txt`) still held port 18194.
- HW boot 1 2026-10-08 ~20:24 (JIT HOTBLOCKS ON; `ps3log_lt_run1.txt`,
  scratchpad of session 898d8ac1): FAILURE, the boot froze in the BIOS.
  Right after the first code cache clear (CCR write at A0000016), every BSC
  write landed 0x10 too high (RTCOR -> +0x34, RFCR -> +0x38, unimplemented),
  so the RFCR wait loop at 800000A2 spun (~32K unimplemented reads/s,
  226 MB of log). Clean XMB exit at 20:27. The only setting that differs
  from the last good boot (`ps3log_bump_run1.txt`) is `hotblocks=1`; the
  `[sys]` and RAM probes start at the first vblank, after the fault. The
  profiler was last run on 2026-10-04, before 5.25, 5.26, 5.24, 5.3, 5.27,
  5.31, 5.36 and 5.2. Fault not found yet in the code. Debug boots: profiler
  OFF (a race, for the probes), profiler ON again, profiler ON with Debug
  Loop (the `[link]` LIVE/DEAD trace).
- HW run 1 2026-10-08 ~20:40 (JIT HOTBLOCKS OFF, a full race; same log
  after byte 225880655, cut as `run1_off.txt`; `probesplit.py`): normal
  boot, clean XMB exit. 127 race windows (vt >= 10K, RPS < 40): SPEED
  100.0%, RPS 26.5, `[stall]` sys 80 ms/s, sleep 63 ms/s.
  - `[sys]` parts, ms/s (ns per call): arm 30.1 (1077, ~28K/s: the SPE
    ARM7 command per medium update, with ~0.8% of lag waits), pvr 18.1
    (163 per slice), plan 13.0 (458 per gate run), tmu 12.5 (112 per
    slice), gate 4.3, aicadma 3.8, gd 3.0, intc 2.3, dma 1.6, vslow 1.1,
    spin 1.0, vbl 0.3 (4.9 us per vblank); sum 91 (the stamps add some).
  - RAM SQ flushes: ~240-260 ns each (in ~100, write ~125, out ~24); 87-116K
    per s, 1.8-2.3% of the SH4 thread in the race; in the 60 fps scenes
    (7 windows, 360K per s) 217 ns, 6.2%. TA SQ path 6.3-7.5% (race).
- HW boot 2 (JIT HOTBLOCKS ON): the same freeze, the same 1704 bytes at
  the clear: deterministic. Boot 3 (JIT HOTBLOCKS ON + Debug Loop ON): no
  freeze, the game runs (Debug Loop adds code at each block entry, so the
  layout moves). Its `[link]` trace: #1, the exit of the block that wrote
  CCR (the clear ran inside it), site 0x46414 DEAD: the first new block
  (80000080, at 0x4625c) now covers it and has its own link `bl` to the
  same stub at 0x46414, 8 bytes before slot 2.
- Diagnosis (not proven on HW yet): `ngen_LinkBlock_Static` checks only
  slot 2 (the `bl` to the stub). After a cache clear inside a block, a new
  block can put its own link site at the old site's address; the check
  then passes and `b rv` rewires the new block's exit to the old target.
  With the profiler's layout this lines up in the BIOS (80000080 runs
  again, R3 +0x10). Proposed fix: also compare slots 0 and 1 (`addis` and
  `ori` of the target pc); a match then means the site wants the same pc,
  so the patch is right. A DYN_IC fill is safe as is: its guard compares
  the pc at run time.
- Fix built (Matt: "Yes"), package of 20:57, no toggle: `ps3/ps3_jit_link.h`
  (new) gives the site's `addis`/`ori` words; DoStatic emits them and
  `ngen_LinkBlock_Static` patches only when slots 0-2 all match (the
  compiled DoStatic checked in the disassembly). PC (WSL):
  `misc/link_check/run.sh` PASS, 600031 checks (the words against the
  emitter for 200K pcs, the boot 3 HW words, cleared sites); `mutate.sh` 7
  of 7 planted bugs fail. Link: `.got` 0xCE20, 0 `r2off`, the old warnings
  only. HW: the profile run of the steps above (JIT HOTBLOCKS ON, Debug
  Loop OFF); the boot passing the BIOS proves the fix.
- HW profile run 2026-10-08 ~21:03 (package of 20:57; `run_prof.txt`, cut
  after byte 423185683 of `ps3log_lt_run1.txt`; `hotlt.py`, `hotlt_deep.py`,
  report in `hotlt/`, scratchpad of session 898d8ac1): the boot passed the
  BIOS with the layout that froze before (1704 bytes at the clear): link
  fix confirmed on HW. One race, clean XMB exit. 254 dense windows (239 of
  them race), SPEED 92.4% with the profiler, RPS 24.8; table 43118 blocks
  (no overflow; 32768 would have been short), 25 censuses, 128 dumps =
  58.6% of the weight; Python re-classification: 8 blocks differ by one
  stub/bc word (DYN_IC sites), the rest equal.
  - JIT code: 10.9M block entries/s x 51.6 hot PPC instructions = 565M/s.
    Per entry: 6.7 loads, 4.2 stores (4.2 on the context), 0.57
    load-hit-stores (real ~2.7M/s in the dumped 59%: sr_T 0.98M/s after
    fcmp, FPSCR 0.90M/s at fschg, pr 0.29M/s), 0.35 bctr, 3.98 cold
    branches.
  - Hottest code: the vertex loop 8C08738C / 8C0873A6 / 8C0873B4 /
    8C0873CA (ftrv, fdiv, fipr, then 7 stores to the store queue and a
    pref), ~10.7% of the weight, ~0.5M turns/s. 8C0873B4 sends all 7 of
    its stores (`fmov.s FRm,@-R6`, `mov.l Rm,@-R6`) through the cold path:
    3.74M of the 3.9M cold entries/s. Its PREF is in the successor
    8C0873CA after `mov.l R7,@-R6`, and the 5.27 look-ahead stops at any
    op that writes R6. Both 8C0873A6 and 8C0873B4 end at an `fschg` (one
    block end and one FPSCR load-hit-store each, ~0.65M/s).
  - Then FP blocks of 27-92 SH4 ops at 8C0688xx-8C069xxx and 8C089xxx
    (transforms; 8C06997E runs 4 ftrv with 64 XMTRX loads from the
    context).
- NEXT STEPS (Step 2 candidates; all four built 2026-10-10 as 5.42, 5.43,
  5.44 and 5.37, see the top block). Estimates are % of
  the SH4 thread in the race; together ~7-8%. With the ~6% the race now
  sleeps, that is room for an SH4 Clock of ~215-225 MHz (Step 3). The plan
  rule holds: own toggle each, default OFF until HW validated, SH4 Clock
  200 for the HW runs, PC check first.
  1. Gate slice batching, ~3%, every game. Now `UpdateSystem_gate`
     (`dc/sh4/sh4_interpreter.cpp`) runs its ~3.9 skipped slices one by
     one through `sys_slice()`; `[sys]` in the race: tmu 11.5 (112 ns per
     slice), pvr 17.1 (166 ns), aicadma 3.5, gate 3.9, plan 12.0 (456 ns per
     run) ms/s. Change: one `UpdateTMU`, `UpdatePvr` and `UpdateAicaDma`
     call for all k slices (the gate already proves no event inside them;
     the medium update keeps its own slice); `libPvr_UpdatePvr`
     (`plugs/drkPvr/SPG.cpp`) then needs a loop for more than one scanline
     per call (VGA lines are ~6.4K cycles); `tmu_slices_to_event`
     (`dc/sh4/tmu.cpp`) steps slice by slice, so a closed form for it.
     Check: `misc/sysgate_check` (gate on/off traces must stay equal; it
     links again since this session, also with STALL=1 and RACEFIX=1),
     then HW: `[sys]` parts and `[stall]` sys in a race, Crazy Taxi
     same-load VPS.
  2. JIT SQ look-ahead through pointer steps, ~1.4%. The vertex loop's
     block 8C0873B4 has 7 stores per vertex (`fmov.s FRm,@-R6`, `mov.l
     Rm,@-R6`) to the store queue; its PREF is in the successor 8C0873CA
     after `mov.l R7,@-R6`, and `sqla_class` (`ps3/ps3_jit_sqla.h`) stops
     at any op that writes Rn, so all 7 take the cold path (3.74M of the
     3.9M cold entries/s). Change: an op that only steps Rn by a constant
     (`mov.x Rm,@-Rn`, `fmov FRm,@-Rn`, `add #imm,Rn`) passes, as
     `sqla_guess` already allows inside the block. The guess only picks
     the shape (`emit_sq_store` still tests the address), so a wrong guess
     costs speed, not correctness. Check: `misc/sqla_check`, then HW
     `[HOT]` cold entries/s (profiler) or `sqcold`, race `[stall]` sleep.
  3. RAM SQ direct, ~1.6-2% in the race, ~6% in the 60 fps scenes. A flush
     to area 3 now runs `do_sqw_flat` -> `sqw_chain` -> `do_sqw_target` ->
     `WriteMemBlock_nommu_ptr` (`sh4_cached_invalidate_range`, a no-op
     under the JIT; `_vmem_GetBlockPtr`; `memmove`): ~240-265 ns per flush
     (`[ta-split]` ram ns), 77-116K flushes/s in the race, 360K/s in the 60
     fps scenes. Change: in `do_sqw_flat` (`ps3/ps3_rend.cpp`), a RAM
     target copies its 32 bytes straight into the RAM array. Check: the
     cached interpreter must still invalidate (keep the old path when it
     runs); HW: `ram ns` gone, `[stall]` sleep up.
  4. 5.37 `fschg` without a block end, ~1%. The vertex loop ends 2 blocks
     per vertex at `fschg` (8C0873A6, 8C0873B4), each with an FPSCR
     load-hit-store (`stw` then `lwz` of fpscr, 0.90M/s). Section 5 has
     5.37 (P17); it touches 5.29's `emit_frchg_test`.
  - Later or uncertain: SPE ARM7 commands batched (`arm_spe_update`,
    `ps3/ps3_arm_spe.cpp`: arm 25-30 ms/s, ~1 us per call, likely cache
    misses on SPU-shared lines; sound timing risk); T kept in r3 after
    `fcmp` (sr_T load-hit-store 0.98M/s; 5.26 found no gain for the same
    kind); a return stack for `rts` (DYN_IC ret misses 1.2-1.9M/s); XMTRX
    kept in FPRs across the ftrv of one block (8C06997E loads it 4 times).
  - Step 3 (after Step 2): let SH4 Clock go above 200 MHz (`ps3_menu.cpp`
    range 150-200 today). Check first whether TMU time scales with the
    clock (the TMU counts SH4 cycles, so game timers may run fast); HW:
    race RPS and SPEED at 210-230 MHz. Built 2026-10-10 as 5.45, HW
    tested and closed the same day (the top of section 0).
- From the old logs (no new run; `racesplit.py`, scratchpad of session
  898d8ac1), race windows at vt >= 10K: `lt_spepix.txt` (61 windows, cut
  from `ps3log_spepix_run1.txt`) and `lt_on.txt` (262 windows). SPEED
  97.7-99.7%, RPS 26-27. SH4 thread per second: sleep 65-74 ms, sys 72-77
  ms, dma 5-7, maple 5.5, rend 2, log 2, gd dma 1; the rest (JIT code, the
  TA path, C calls without a bracket) 830-841 ms. TA flushes 655-698K/s
  (sq path 5.7-6.1%); C calls ~1.07M/s (sqw 657-698K, sync_fpscr 171-182K,
  writemem 74-79K, getcode 61-65K); DYN_IC ret misses 1.2-1.9M/s. No
  interpreter fallbacks; idle skips 27/s at most.
- 1. JIT HOTBLOCKS table 32768 -> 131072 blocks (~13 MB, only with the
  option ON). Looney Tunes clears its code cache only at boot and compiles
  ~32K blocks before a race and ~40K by its end, so the race blocks would
  get no slot.
- 2. `[sys]` line, with JIT C-Calls and JIT SYS GATE (after `[gate]`): 1
  gate run in 16 stamps each part of its slices; times leave out the nested
  `[stall]` sections (gd dma, rend, log, sleep), as `[stall]` sys does
  (`g_stall_nested`, new). Parts: gate (run entry and loop), arm (ARM7
  tick), dma, gd, spin, vslow (RTC, maple), tmu, pvr, vbl (a PVR update
  with a vblank start), aicadma, sched, plan (`sys_plan`), intc (pending
  check). `ms/s` per part (the samples scaled to all runs, one mftb per
  stamp taken out), `irq` (`UpdateSystem_handle_event`) and `sync`
  (`sys_gate_sync_slow`) timed in full, then ns per call.
- 3. `[ta-split]` ends in `| ram ns: in write out = n= long= path %`: the
  sampled store-queue flushes to main RAM (area 3), split at
  `WriteMemBlock_nommu_ptr` (in = the chain to it, write = the call, out =
  to the exit); "path" = % of the wall for all RAM flushes, 3 mftb out.
- PC (WSL): `misc/sysgate_check/run.sh` PASS (120 and 300 seeds), also
  with STALL=1 (the probe's stamps run; traces unchanged) and RACEFIX=1.
  The check had not linked since the Looney Tunes fixes and bundle 1: 5
  stubs added (`SB_ADSUSP`, `get_aica_race_fix_preset`, `aica_fix_dump`,
  `g_fb_dirty`, `fb_watch_set`).
- Link: `.got` 0xCE20, 0 `r2off`, the old warnings only (`verify`,
  `g_jit_blocks_entered`, the unused `fm_*`).
- HW steps (package of 19:22):
  1. Install. The install keeps `nullDC.cfg`. SH4 Clock 200. On the main
     menu, before the boot: VIDEO / DEBUG -> JIT HOTBLOCKS ON (the in-game
     menu acts only at the next boot), JIT C-Calls ON. The rest as now.
  2. UDP listener on port 18194, only when Matt says so. Matt: "Let's
     replace it for a cleaner run." At 20:23 the Bump Maps session's
     listener (PID 7520) was stopped; this session's runs as PID 24700 into
     `ps3log_lt_run1.txt` (scratchpad of session 898d8ac1).
  3. Looney Tunes: START past the attract demo if it begins. One full
     race, 3 minutes or more. The profiler costs ~6% of the SH4 thread, so
     a lower SPEED is expected. Then exit to the XMB.
  4. Log: `[jitcfg] ... hotblocks=1`, `[HOT] table of 131072 blocks`, no
     `blocks did not fit` line, `[HOTC]` censuses, `[HOT] --- dump` blocks,
     `[sys]` lines, `ram ns` with n above 0.
  5. Analysis: the last census and the dumps (`hot518.py`, scratchpad of
     session 29bdd1c1), the `[sys]` split against `[stall]` sys, the RAM
     flush cost. Then a ranking; Matt picks.

**Done: 3j Bump Maps, the Rayman 2 untextured level surfaces (Matt
2026-10-08: "Ok then, can we work on the Rayman 2 untextured surfaces?",
then design A: "A"). Package of 18:31. HW SUCCESS 2026-10-08 (Matt: "It
looks good now!", then "SUCCESS"); default ON in source since 2026-10-08
(Matt: "keep them on by default"), not packaged.** Section 3 has the
evidence and the DC rule (3j).
- HW run 2026-10-08 (package of 18:31, Bump Maps ON from boot, no OFF boot;
  `ps3log_bump_run1.txt`, scratchpad of session d0cd44fe):
  - Rayman 2: the level surfaces textured again (Matt). In the first level
    31-71 bump strips per render, every render replayed on the PPU
    (`ppu=` = renders); intro and menus 0 (SPU path). Level: SPEED 100.0%
    (lowest second 99.7%) over 83 s, RPS 50 (PAL 50 Hz), drop 0; render
    thread busy 4-15% (2-6% on the SPU path before the level), job_max
    2-9 ms. Clean XMB exit; largest free block at boot 144203 KB (-1 MB).
  - Crazy Taxi: 0 bump strips, 0 PPU replays; SPEED 99.8% over 344 s
    (99.8% in the 239 s at vt >= 6K), drop 0, no SPU fail or mismatch.
  - Sonic Adventure: 0 bump strips, 0 PPU replays; SPEED 99.8% over 218 s
    (100.0% in play, RPS 28.7), drop 0, clean XMB exit.
- Design A (Matt's pick of two): a scene with a bump polygon is decoded and
  built on the PPU, as after a late SPU fence; the SPU code and the 24-byte
  vertex do not change. Design B (offset colour in DcVertex/SpeVtx, both
  decoders, SPE Build) was not taken: every game would pay, and the SPE
  Build fast path assumes 24-byte vertices. 3b (offset colour in every game)
  would still need B's SPU work.
- VIDEO / DEBUG -> "Bump Maps" (`BumpMap`), default ON (OFF until the HW
  run), live.
- Texture (`ps3/ps3_tex.cpp`): a PixelFmt 4 texel decodes as raw 4444 (S in
  A:R, R in G:B), with no IgnoreTexA (Flycast `texBMP_TW`); the SPE Tex
  Decode job gets fmt 2. The toggle is in the cache key of bump textures, so
  a switch decodes them again.
- Offset colour (`ps3/ps3_rend.cpp`): the PPU decoder keeps it per vertex in
  `TaContext.offs` (1 MB of .bss in all), for bump polygons only: packed
  (types 3, 4, 11A, 12A), float (5B, 6B), intensity times the face offset
  colour of PolyParam 2B, alpha unscaled (7, 8, 13A, 14A; Flycast
  `vert_face_offs_color`), and sprites. `rth_job_decode` replays a scene on
  the PPU when the SPU lists hold a bump param (`has_bump`).
- Draw: a bump run writes each vertex's offset colour as RGBA bytes into
  u1 (byte 24 of ps3gl_vertex_t) on both PPU paths (vertex arrays and
  glBegin); 0 outside bump runs, the old value. ps3gl binds texcoord1 as 4
  normalised bytes for the bump programs only, and `ps3gl_tex_bump()` picks
  them (REPLACE, MODULATE, DECAL keys 7-9).
- Shaders (`ps3/ps3gl/shaders/`): `bump_vp.vcg` (texcoord1 as float4, no
  clip plane) and `bump_fp_replace/modulate/decal.fcg` with
  `bump.cgh` (the Flycast/Reicast `pp_BumpMap` formula). `compile_bump.sh`
  builds them (cgc.exe, then cgcomp in Docker) into `ps3gl_shader_bump.h`.
  `ps3gl_shader_data.h` is unchanged: `compile_shaders.sh` would rebuild
  `q3_vp.vpo`, whose committed binary is an older build (no clip plane).
  The decoded ucode: TEX, COS, SIN, COS, MAD_SAT, then the texenv op; the
  bump VP writes TEX1.xyzw (q3_vp: .xy).
- Log: `[tex] bump maps ON|OFF`; the `[rend] renders=` line ends in
  `| bump strips=N ppu=M`: bump strips drawn per render, and scenes the PPU
  decoded for bump in the window.
- PC (WSL): `misc/bump_check/run.sh` PASS: the decoder region of
  ps3_rend.cpp through the real FIFO splitter, 20000 streams (every textured
  vertex type, type-2 face offsets, sprites, records split across calls),
  91540 bump vertices right, 563124 others not written.
  `misc/spevb_check/run.sh` PASS (3000 scenes): the SPU compare as before,
  plus three passes per scene: Bump Maps off over lists with offs (word for
  word as before), on (u1 of every vertex in a bump run = its source's
  offset colour, every other word unchanged), and the glBegin path: 19.3M
  and 15.7M vertices in bump runs.
  `sim.sh`: the real SPU image, same as the PPU merge.
  `misc/bump_check/mutate.py`: 31 of 31 planted bugs fail (21 in the
  decoder, 10 in the draw). `misc/spepix_check/run.sh` PASS (code unchanged,
  as the fmt 2 job is the 4444 one).
- Link: `.got` 0xCD38, 0 `r2off`, no warning; .bss 35.4 MB (+1 MB).
- HW steps:
  1. Install. The install keeps `nullDC.cfg`. SH4 Clock 200; VIDEO / DEBUG
     -> Bump Maps OFF.
  2. UDP listener on port 18194, only when Matt says so.
  3. Rayman 2, the first level, OFF: the untextured surfaces as before.
  4. SELECT -> VIDEO / DEBUG -> Bump Maps ON -> SELECT. Look at the same
     surfaces: the level texture with the bump shading over it. Switch OFF
     and ON once more.
  5. Crazy Taxi and Sonic Adventure with ON: no change (no bump textures).
  6. Log: `[tex] bump maps ON`, `bump strips=` above 0 and `ppu=` near the
     renders in the Rayman 2 level, `[texfmt] bump=`.
  7. On a fault: Bump Maps OFF (live).

**CLOSED 2026-10-13: MMUCR Bits FIXED (bundle 13, the first block of this
section) lets Rez, Ikaruga, Sonic Shuffle and Border Down boot and play
on HW. The text below is the state of 2026-10-08.**

**Open, separate (Matt 2026-10-08: "Also write down the Ikaruga and Rez
issues. We may be able to get back to them later on."): Rez and Ikaruga
stop right after the BIOS logo; so does Sonic Shuffle. Not bundle 1: the
cause looks like P58 (`OPTIMIZATION_POTENTIAL.md`, MMUCR bitfields on big
endian), as in Border Down.** Bundle 1 HW run, package of 09:28, all four
rows ON (`ps3log_b1_run1.txt`, scratchpad of session ed40e651; Sonic
Shuffle cut as `shuffle_b1.txt`).
- Discs: Rez v1.003 (PAL, GDI), 2 boots (the second with LMMODE OFF);
  Ikaruga v1.002 (JP, GDI), 1 boot; Sonic Shuffle (CDI), 1 boot. Each
  loads 25 UTLB entries that map the store-queue area (50 `SQ remap`
  lines): E0000000-E0FFFFFF to 0C000000-0CFFFFFF (RAM), E1000000-E18FFFFF
  to 10000000-108FFFFF (TA FIFO and YUV converter). No `CCN: MMU address
  translation ENABLED` line in any boot. That is the Border Down pattern
  of P58: the emulator reads MMUCR.AT from the wrong bit, so the SQ
  flushes take QACR, not the TLB.
- After the logo: no TA, DMA or SQ activity for the rest of the boot
  (`[ta-split]` all 0; SPEED ~100%, the SH4 waits). Rez and Ikaruga set
  SB_LMMODE1 = 1 at that point. Sonic Shuffle's SQ flushes land in boot
  ROM (~100 blocks/s, ~50K `Write to Boot ROM` word lines at 0x1122B4..),
  as Border Down's did.
- Bundle 1 did nothing there: no SQ write to VRAM, no YUV; Ikaruga made one
  16 KB VRAM DMA with LMMODE0 = 0 (the old path) and one FB Write Watch
  present. LMMODE OFF changed nothing on Rez (Matt).
- Rez's stop after the logo is older: `plugs/vbaARM/arm7.cpp:22` notes a
  "post-logo stall (2026-07-21)", the same with both ARM7 engines.
- Way back: fix P58 (the 2 bitfield lines, the JIT cache clear on an AT
  change, `UTLB_Sync` skips V = 0), then boot Border Down, Rez, Ikaruga,
  Sonic Shuffle. These games are also the tests that bundle 1 lacks: Rez
  for LMMODE (P7: SQ textures to 0x11), Ikaruga for FB Write Watch (P26:
  its loading screens).

**Done: bundle 1, VRAM and TA-area writes (section 7; Matt 2026-10-08:
"Let's go with the first Bundle", then all four items, the probe and the
items in one package). Package of 09:28; HW run 2026-10-08 with all four
ON. Verdict (Matt: "Yes, thanks."): YUV Conv Fast (P43) SUCCESS, default
ON in source (not packaged); VRAM Direct (P55), LMMODE (P7) and FB Write
Watch (P26) stay OFF (never exercised).** Every item has its own row, live
(SELECT menu works).
- HW run (Matt: one boot with the four rows ON, no OFF baseline;
  `ps3log_b1_run1.txt`, scratchpad of session ed40e651; Crazy Taxi cut as
  `ct_b1.txt`; parser `fmvsec.py`). Baseline: the 5.12 #4 run
  (`ps3log_spepix_run1.txt`, session d24c41ee; the same `[jitcfg]` and
  `[sndcfg]`). JIT C-Calls ON in both.
  - P43: intro FMV ch2 DMA time on the SH4 thread (`[stall] dma`), seconds
    aligned from the FMV start: Sonic Adventure 32.2 -> 19.1 ms/s (-41%, 39
    s), Looney Tunes 47.0 -> 26.1 ms/s (-45%, 11 s): 1.3-2.1% of the SH4
    thread during FMVs (the estimate was 1-3%). Every macroblock converted
    in place (`yuv blk cp=0`; the FMVs feed the converter by DMA only).
    Sonic FMV SPEED 97.2 -> 98.0% (noise level). No image fault (Matt).
  - P55, P7: no game sent an SQ block to VRAM (`v64=0 v32=0` in every
    window). VRAM DMA only in Crazy Taxi (<= 32 KB/s) and Ikaruga (16 KB),
    both with LMMODE0 = 0, which is the old 64-bit path. So neither item
    changed a single write. Crazy Taxi keeps LMMODE 0/0.
  - P26: Crazy Taxi `fbw=0` (no 2D frame over 3D play); Ikaruga 1 watch
    present before its stop (above). The fix itself is unproven.
  - Crazy Taxi same-load VPS -0.4% (vs the 5.12 #4 run) and -0.1% (vs
    `ct_on.txt`, 5.12 #3): noise, as expected with nothing acting.
  - Rez, Ikaruga, Sonic Shuffle: they stop on P58 (the open item above).
- Probe finding, a new lead (not built, not measured): the non-TA SQ
  flushes are RAM writes, not VRAM. Crazy Taxi dense ~161K/s (max 264K;
  the "~127K/s to targets other than the TA" of 5.17 Step 1), Sonic FMV
  ~200K/s, Looney Tunes race 260-440K/s (next to ~1M/s TA flushes). Each
  takes `sqw_chain` -> `do_sqw_target` -> `WriteMemBlock_nommu_ptr` (out of
  line, `sh4_cached_invalidate_range`, a block lookup, a 32-byte memmove).
  A direct RAM path in the flush, as the TA flat path of 5.17, is the
  candidate; the Looney Tunes race is SH4-bound (open item below). Measure
  the cost per flush first.
- VIDEO / DEBUG: "YUV Conv Fast" (`YuvConvFast`, P43), "VRAM Direct"
  (`VramDirect`, P55), "LMMODE" (`Lmmode`, P7), "FB Write Watch" (`FbWatch`,
  P26). Shared code: `dc/pvr/pvr_if.cpp`, new `dc/pvr/pvr_vram.h`.
- Probe (always on, log only): the `[ta-split]` line now ends in `sq/s ta=
  yuv= v64= v32= mem= | dma KB/s ta= yuv= v64= v32= lm1stub= | yuv blk in=
  cp= | lm=LMMODE0/LMMODE1 chg= | p43= p55= p7= p26=`. `mem` = SQ flushes
  outside area 4 (RAM and the rest); `lm1stub` = ch2 DMA to 0x13 with P7 OFF
  (moves nothing). `[vram] LMMODE0=X LMMODE1=Y (was ...)`: the first 16
  changes, read at each SQ or DMA write to 0x11/0x13. `[rend] ... fb=N
  fbw=N`: fbw = presents only P26 caused.
- P43: the converter loads the planes as 32-bit words (the host word is the
  guest LE word, so plane byte k is bits 8k..8k+7) and builds each output
  word with masks and shifts; a block that arrives whole (DMA) converts
  where it lies (Flycast); a block split across calls (every SQ block)
  still collects in `YUV_tempdata`; TA_YUV_TEX_CTRL's format is read in
  `YUV_init` only (every CTRL write calls it); the region is noted once per
  frame, not per macroblock.
- P55: SQ blocks and DMA runs on the 32-bit bus map the address once per
  stretch inside a 4 MB bank, then store every other word
  (`vram_write32_run`). OFF: one `pvr_write_area1_32` call per word.
- P7: SQ and ch2 DMA to 0x11 (SB_LMMODE0) and 0x13 (SB_LMMODE1) take the
  64-bit bus (a linear copy) when the register is 0, else the 32-bit bus
  (Flycast `TAWriteSQ`, `DMAC_Ch2St`). OFF: SQ always 32-bit (Rez), DMA to
  0x11 always 64-bit, DMA to 0x13 the old stub. Not taken: Flycast's
  SB_C2DSTAT = end address after a VRAM DMA.
- P26: a 32-bit-bus write (the 0x05 handler, the SQ and DMA 32-bit paths)
  inside the shown field (FB_R_SOF2 when interlaced, else FB_R_SOF1; (y+1) x
  (x+modulus) x 4 bytes) marks the frame dirty. The range is set at each
  vblank and on an FB_R_SIZE change, which also clears the mark (Flycast).
  VBlank presents when the mark word is gone (as before), or when the frame
  is dirty and no StartRender came since the last vblank (Flycast);
  StartRender also clears the mark (the P26 text). The 64-bit bus is not
  watched (Flycast).
- PC (WSL): `misc/vram_check/run.sh` PASS: the file after its `vram_check:
  begin` marker, built twice (A: P43/P55 OFF; B: ON, flipped live), against
  the frozen old file (958964c, `ref_pvr_if.inc`) and a model of the LMMODE
  rule and the watch. 400 YUV cases (99K ops: BASE, CTRL and STARTRENDER
  writes, SQ and DMA feeds, 4:2:2, small mode, resets; 4522 blocks in
  place, 2382 copied); 400 VRAM cases (67K ops: SQ, DMA across the 4 MB
  bank and the 8 MB edge, unaligned, 16- and 32-bit handler writes at the
  watch edges, LMMODE 0-3). A with all OFF is the old file in VRAM and in
  every converter field. `mutate.py`: 41 of 41 planted bugs fail. Not under
  the check: the P7 branch in `dc/sh4/dmac.cpp` (RAM runs as the TA path),
  `fb_watch_update()` in `plugs/drkPvr/regs.cpp`, the VBlank rule in
  `ps3/ps3_rend.cpp`.
- The package of 09:28 also carries 5.12 #4/#5 default ON (below).
- HW steps as planned (Matt ran step 4 only, with Rez, Ikaruga and Sonic
  Shuffle added; results above):
  1. Install. The install keeps `nullDC.cfg`. SH4 Clock 200; VIDEO / DEBUG
     -> JIT C-Calls ON (for `[stall] dma`); the 4 new rows OFF.
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1 (probe only): Sonic Adventure intro FMV, Looney Tunes intro
     FMV, Crazy Taxi a dense drive; Rez if at hand (LMMODE). Log: the
     `[ta-split]` tail, `[vram] LMMODE` lines, `[stall] dma`, SPEED.
  4. Boot 2 (the 4 rows ON): the same. Look at the FMVs (P43), textures
     (P7, P55) and 2D screens. Log: `yuv blk in= cp=`, `[stall] dma` and
     SPEED against boot 1, Crazy Taxi same-load VPS, `fbw=` with no 2D
     frame over 3D play.
  5. On a fault: that row OFF in the SELECT menu (all are live); note when.

**Before bundle 1: 5.12 #4 and #5 (Matt 2026-10-08: "Let's go with 5.12 4 and 5"),
built and PC tested, package of 01:57, HW run 1 done: no faults. Both
default ON in source since 2026-10-08 (Matt), packaged with bundle 1 (09:28).** Matt's picks: the
render thread waits for each decode (not an RSX label wait), an own SPU
(the sixth and last free one) runs both jobs, and #5 writes straight into
RSX textures (not into `s_fb_pix`).
- HW run 1 2026-10-08 (package of 01:57, both ON from boot, no CHECK boot:
  Matt asked, ON first; `ps3log_spepix_run1.txt`, scratchpad of session
  d24c41ee): every boot `RSX DMA test: ok`, every window `fail=0`.
  - Looney Tunes: intro FMV decode 0.93 ms per frame on the SPU (P42 on
    the PPU 5.4 ms); race start render job 19 ms (was 364-426 ms; 85 VQ
    textures, render-thread wait 6 ms in total), a 34 ms CD-DA read at the
    start (49 ms SH4 frame). Matt: the race stutter is the same. It is the
    SH4-bound item below: 50 race seconds at SPEED 99.4%, the game draws
    25.2 frames/s (2- and 3-refresh frames), SH4 thread idle ~7%. Matt:
    "The faster JIT in one of the plans sounds fine."
  - Capcom vs. SNK 2 (JP), menus "basically unplayable", not this job: SPU
    idle (0-4 decodes per 5 s, no framebuffer presents), SPEED 100% (PAL
    50 Hz), SH4 thread asleep 55%, the game draws 5-6 frames/s. It spins
    in a loop at 8C068808 (3 calls, then `mov.l @rN` + `tst`) with ~505K
    area 7 reads/s (on-chip registers): it waits for an event that comes
    late. Separate open goal (Matt 2026-10-08: "Look the CvS2 slowness
    later on as a separated goal. We could use either Minicast or Flycast
    as reference for that."), log cut as `cvs2.txt` in the same scratchpad.
  - Sonic Adventure intro FMV: decode 0.46 ms per frame (P42 2.5 ms),
    decode 1.1-1.4% of the render thread; SPEED 91-101% as with P42 (the
    SH4 thread never sleeps there).
  - Crazy Taxi (Matt: "No issues whatsoever."): 11 dense windows, decodes
    7-10 us each on the SPU (max 150 us), decode 0.40% of the render thread
    (`ct_on.txt` 0.69%, `run6_ct.txt` 0.81%), SPEED 100%.
  - #5 only ran on boot screens (2-3 presents per boot, 0.2-0.3 ms each):
    no tested game draws into the framebuffer in play.
  - Border Down (JP), to look for framebuffer presents: no picture after
    the BIOS animation for 60 s, not this job (SPU idle). Its store-queue
    flushes land in boot ROM (area 0), not in the TA: 56 `Write to Boot
    ROM` at 0x1FF3E0+ (a polygon list: PCW 828C0002, then vertices), then
    0xF000-0xF07C with zeros (`[CC] sqw` 100/s = area 0 writes from:other
    800/s). RPS 0. The game then loops at 8C035350-8C035460 with ~1.9M SR
    writes/s (`[CC] sync_sr` + `interrupt`), SPEED 100% (PAL 50 Hz). Open,
    not started; log cut as `borderdown.txt` in the same scratchpad.
  - Defaults: both ON in source (Matt 2026-10-08: "Yes, place them as ON by
    default."), in the package of 09:28 (bundle 1). Matt's nullDC.cfg already has both ON.
- VIDEO / DEBUG -> "SPE Tex Decode" (`SpeTexDecode`) and "SPE FB Convert"
  (`SpeFbConvert`): OFF / ON / CHECK each, default ON, set before boot.
  The PPU paths as now when the SPU is down, late (50 ms: `[spepix] SPU job
  ... late`, the group stops, both PPU paths from then on) or a job does not
  fit.
- One SPU thread (`ps3/spu/spe_pix.c`, `spe_pix_job.h`, `spe_pix_simd.h`,
  `spe_pix.h`; `ps3/ps3_spepix.cpp`). Its bell line has two halves: the
  render thread's texture job and the SH4 thread's framebuffer job, each
  with its own seq; the SPU serves the framebuffer first. Boot: `[spepix]
  RSX DMA test: ok`, then `[spepix] SPU thread up, tex X, fb Y`.
- #4: at a bind that decodes (a new texture or a changed hash), the render
  thread allocates level 0 as glTexImage2D does (`ps3gl_tex_direct_level0`,
  new: either layout) and sends describe()'s fields; the SPU writes the
  final A8R8G8B8 words into the texture by DMA, in its RSX layout (swizzled,
  or linear rows for YUV and mirrored textures), and returns decode()'s
  black and alpha-0 checks; the render thread spins until then. Every
  format: twiddled, planar, stride, VQ, 4- and 8-bit palette, YUV (also
  VQ), bump, IgnoreTexA, mip offsets. A source that would cross the end of
  VRAM stays on the PPU (`skip`). P42's YUV path is only the fallback.
  - The SPU works in tiles of up to 32x32 texels (one get, or a DMA list of
    rows for planar sources; one put, or a list of rows for linear output),
    double-buffered. Swizzled 16-bit, VQ and 8-bit palette textures go
    straight to the RSX order; the rest through a second pass in LS.
- #5: at a vblank present, the SH4 thread sends the frame (FB_R_SOF1,
  depth, size, line stride) and waits; the SPU reads each line's words from
  the two VRAM banks (a line that crosses the 4 MB edge in two runs) and
  writes A8R8G8B8 rows into one of 3 linear RSX textures (768x576, pitch
  3072, allocated at boot with `ps3gl_tex_fixed`, new). The render thread
  only binds it, sets the size the RSX samples, and draws. 3 buffers: the
  RSX may still draw the last 2 frames (ps3gl's vring fence covers frame
  N-2). The `[rend]` stats now also print after presents (2D titles).
- CHECK: the SPU writes into a copy in main memory (4 MB for textures, one
  frame for the framebuffer); the PPU decodes as before (decode() and the
  ps3gl conversion into the copy's layout, `ps3gl_tex_convert_check`) and
  compares every word and the black/alpha-0 result; the first 8 mismatches
  print `[spepix] tex|fb check MISMATCH ...`. The PPU result is what shows.
- Every 5 s: `[spepix] tex= fb= up= | tex jobs N (/s) spu avg max us, KB/s,
  wait total ms max us, skip | fb jobs spu avg max us, wait total ms max |
  spu busy % wakes/s | fail check tex ok bad fb ok bad`; `[texfmt]` ends in
  `spudec=N`.
- PC (WSL): `misc/spepix_check/run.sh` PASS: the real `spe_pix_job.h` on SPU
  shims (big-endian lanes; an MFC model with lists, alignment rules, junk
  until the tag wait, random completion) against the PPU code extracted
  from `ps3_tex.cpp` (describe, decode, the job builder), `ps3gl_textures.c`
  (convert_pixels and the swizzle tables) and `ps3_rend.cpp` (fb_geom,
  fb_convert_ppu, fb_job): 7013 textures (80M texels: every kind, layout
  and alignment at 8-64 texels, then random ones up to 1024, FMV-like YUV
  strides, pure-red and black sources), 447 framebuffers (94M pixels, 146
  with a line across the bank edge). `mutate.py`: 96 of 96 planted bugs
  fail.
- misc/spu_sim, the real image (`build_sim.sh`, `sim.sh`): every word the
  same as the PPU code. SPU time at 3.2 GHz (the RSX put time at the 3.3
  GB/s of the 5.12 #2 bench in brackets): VQ 256x256 0.04 ms (0.08), 565
  twiddled 256x256 0.12 ms (0.08), 8-bit palette 256x256 0.11 ms (0.08),
  Looney Tunes FMV 1024x512 YUV 0.78 ms (0.64), Sonic Adventure FMV 512x512
  0.39 ms (0.32), framebuffer 640x480 565 0.59 ms (0.37), 0888 0.34 ms.
  Before (PPU, HW): Looney Tunes race load, 75 VQ textures in one 364 ms
  render job (~5 ms each); FMV decode 5.4 ms (P42) / 21.6 ms (old) per
  frame; framebuffer convert not measured.
- SPU: text 23.4 KB, data 0.8 KB, bss 50.7 KB, built with -funroll-loops.
  Link: `.got` 0xCBC8, 0 `r2off`, no warning.
- HW steps (package of 01:57):
  1. Install. The install keeps `nullDC.cfg`. VIDEO / DEBUG -> SPE Tex
     Decode CHECK, SPE FB Convert CHECK; the rest as now, SH4 Clock 200.
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1 (CHECK): Looney Tunes intro FMV, menus, one race (the track
     load); Sonic Adventure intro FMV; Crazy Taxi a short drive; any 2D
     title that draws into the framebuffer (`fb=` in `[rend]`). Log:
     `[spepix] RSX DMA test: ok`, `check tex ok=N bad=0 fb ok=N bad=0`,
     `fail=0`, no MISMATCH line.
  4. Boot 2 (ON, both): the same. Look at textures, FMVs and 2D screens.
     Log: `job_max` and `render job stalled` at race start and end (was
     364-426 ms), `[rend] tex split` decode %, FMV SPEED, `[spepix]` spu
     avg and wait totals.
  5. On a fault: that toggle OFF for the next boot.

**Next goal (Matt 2026-10-07: "Can you set the Vblank pacing as next goal
before resuming the 5.13 ones?"): 5.41 VBlank pacing.** Step 1 built
2026-10-07 (Matt: "Let's go with the vblank pacing"), package of 22:56; HW
run done 2026-10-07 (cause confirmed for Sonic Adventure, results below).
Step 2: Matt picked (b) ("Let's go with b"); Frame Hold built + PC tested
2026-10-07, package of 23:42, default OFF; HW run 2026-10-07: Sonic's
judder gone in the log (results below). Matt 2026-10-08: "It seemed the
same, actually. But It's fine." Done; Frame Hold stays OFF by default. Next
goal: Matt's pick (the 5.13 items, or 5.12 #4/#5).
- Why it felt the same: in the Sonic windows that feel choppy, the game
  itself draws 20-25 fps (3-vblank frames: 3s with RPS under 30), while the
  SH4 thread still sleeps 25-32% of each second (`[pace] slept=60`, `[stall]`
  sleep 6.6-9.5 ms in the worst frame). The host has spare time there; the
  emulated SH4's cycles per frame run out first.
- Fault: Sonic Adventure plays "choppy, not really 'true' 100% speed" (Matt)
  while the log reads SPEED 99.99% (play seconds at vt >= 6K, `sonic_on.txt`,
  5.12 #3 below): 30 game frames per second (RPS median 29.9; 150 presents
  per 5 s, 0 skipped), and the SH4 thread still sleeps ~9-10 ms in the
  slowest frame of each second. The same in `run5_sonic.txt` (no SPE Tex
  Hash). The Looney Tunes race judder (the SH4-bound item below, "frame
  pacing") is mostly another kind (step 1 HW below).
- Cause (from the code, not measured): the DC shows a frame at its own
  vblank, so a 30 fps game holds each frame 2 refreshes. Here
  `ps3_video_end_frame()` queues the flip (GCM_FLIP_VSYNC, 3 buffers) as soon
  as the render thread finishes, and `spg_PaceFrame()` (`plugs/drkPvr/SPG.cpp`)
  paces the emulated vblanks with `usleep`, not with the PS3 vsync. The finish
  time moves a few ms from frame to frame (the SH4 work before STARTRENDER,
  2-5 ms of render); near a vsync edge one frame stays 1 refresh and the next
  3. The emulated 59.95 Hz (NTSC) and the PS3's 720p refresh drift against
  each other, so the judder comes and goes.
- Step 1, log only: `[vid]` counts, per 5 s, how many refreshes each
  presented frame stayed on screen (1, 2, 3, 4+), from the vblank count at
  each completed flip. A 30 fps game shows only 2s; judder shows as 1s and
  3s.
  - Built 2026-10-07, package of 22:56, no toggle. The flip handler
    (`ps3/ps3_video.c`) reads the timebase and `gcmGetVBlankCount()` at each
    completed flip. Refreshes held = time since the last flip / the PS3
    refresh period, rounded; this does not depend on the order of the vblank
    and flip events. The period is a run average from the first flip (after
    600 vblanks; 59.94 Hz before). `bad=` counts flips where the vblank count
    gives another number (0 expected).
  - The `[vid]` line (every 5 s) now ends `| refresh 59.940 Hz, held 1=N 2=N
    3=N 4+=N bad=N`. The held counts are for the 5 s window; the other
    counts stay totals.
  - Emulated vblank rate with the SH4 at 200 MHz (NTSC 480i and VGA): 200 MHz
    / 3336375 cycles = 59.945 Hz. Against a 59.940 Hz PS3, the phase moves
    one refresh in ~190 s, so a judder can come and go in a 3-minute cycle.
  - HW: Sonic Adventure play for 5 minutes or more, a Looney Tunes race,
    Crazy Taxi; no in-game menu during the play (its flips count too).
  - HW 2026-10-07, package of 22:56 (`ps3log_vblank_run1.txt`, cut with
    `vidheld.py` into `vidheld_run1.txt`, scratchpad of session fdcdfbab):
    the three games, a clean XMB exit. `bad=0` in all 127 windows, refresh
    59.940 Hz. Each game boots in PAL 50 Hz (the BIOS) and plays in NTSC
    59.95 Hz.
    - Sonic Adventure play (RPS ~30, 35 windows, ~3 minutes): in one phase of
      ~85 s, 1s and 3s came in pairs (16 of 17 windows; 1=96 2=2309 3=114).
      Before (25 s) and after (80 s) it, almost only 2s (1s in 2 of 18
      windows, 6 in total); the 3s there come with RPS under 30 (the game's
      own slow frames). This is the drift cycle of the cause above: the
      finish time crosses the vsync edge for ~85 s of each ~190 s.
    - Looney Tunes race (32 windows): 1=55 2=2974 3=1043 4+=45, RPS 19-30.
      The game itself takes 2 or 3 vblanks per frame; few 1s, so a pacing
      fix changes little there.
    - Crazy Taxi play (60 fps, 11 windows): 1=3301, no 2s or 3s. The queued
      flip of a 60 fps game absorbs the finish-time jitter.
    - PAL 50 Hz (BIOS boot): ~4 1s to one 2, the 50-on-59.94 Hz cadence.
- Step 2, the fix (options for Matt after step 1), behind a toggle, default
  OFF: (a) the emulated vblank waits for the PS3 vsync instead of `usleep`;
  (b) `usleep` pacing stays, and each finished frame is held and flipped on
  the PS3 vsync that matches its emulated vblank. HW: Sonic Adventure play,
  a Looney Tunes race, Crazy Taxi.
  - From the step 1 data: (a) stops the drift but fixes the phase where it
    falls. The finish time then sits at "vsync + SH4 work to STARTRENDER +
    render", so a game whose finish time falls near the edge would judder all
    the time. (a) only fits NTSC (59.95 -> 59.94 Hz, -0.009% speed); PAL 50 Hz
    games keep `usleep`. (b) follows the game's vblanks whatever the finish
    time, for up to ~1 refresh more latency; the drift still moves one frame
    to 1 or 3 refreshes once per ~190 s (none with (a)'s rate lock added).
  - (b) built 2026-10-07 (Matt: "Let's go with b"), package of 23:42. VIDEO /
    DEBUG -> "Frame Hold" (`FrameHold`), OFF / ON, default OFF, live (the
    in-game menu switches it).
    - When: 4 frames in a row 2+ emulated vblanks apart (the game runs under
      60 fps). A 60 fps game keeps the flip queue, also with dropped frames.
    - StartRender (SH4 thread) takes the pacer's due time of the next emulated
      vblank, where the DC shows the frame (`spg_NextVblankTime()`, 0 with the
      Speed Limiter off), as a timebase tick. The render thread draws and
      flushes, then `ps3_video_end_frame_at()` picks the PS3 vsync for "that
      tick + half a refresh" and waits until the vsync before it, so the flip
      lands on it. The wait is idle time: not in `busy=` or `job_max=`.
    - `framehold_vsync()` (`ps3/ps3_framehold.h`): the first vsync at or
      after the mark, or the last held frame's vsync plus the same number of
      steps as the marks, while that vsync stays within T/8 of [mark, mark +
      T). The drift then moves one frame per cycle (~190 s), and noise near a
      vsync edge cannot move frames to and fro. A vsync 6+ ahead is not held.
    - The vsync grid: a new vblank handler keeps each vblank's tick; the
      reference is the earliest of the last 8 within half a refresh below
      their median (a late handler run only adds; a missed run leaves an
      entry 8 refreshes old).
    - `[vid]` line: `| hold=N late=N early=N wait=X ms` per 5 s: held frames,
      flips after or before their vsync, average wait.
    - PC (WSL): `misc/framehold_check/run.sh` ALL PASS: the real
      `framehold_vsync()` in a model of the pacer, the vblank handler (5-30%
      runs late by 1-6 ms, 1% missed), the wait and the flips. Sonic 30 fps,
      10 minutes: 1/3 = 1527/1524 -> 3/1 (the drift slips), late 0; Looney
      2/3 vblanks: 1s 769 -> 0; Crazy Taxi 60 fps unchanged; 60 fps with 10%
      dropped frames: 1 hold in 5 minutes. Cost: the frame reaches the screen
      on average 18.8 ms after its emulated vblank (5.1 ms now).
      `mutate.sh`: 6 of 6 planted bugs fail.
    - HW: Frame Hold ON. Sonic Adventure play 5+ minutes (ON/OFF in the
      in-game menu to compare the feel), a Looney Tunes race, Crazy Taxi
      (must not change: `hold=0`). Pass: Sonic play windows with only 2s
      (and RPS-30 3s), `late` near 0, no input lag Matt can feel.
    - HW 2026-10-07, package of 23:42, ON (`ps3log_vblank_run2.txt`, cut
      with `vidheld.py` into `vidheld_run2.txt`, scratchpad of session
      fdcdfbab): the three games, a clean XMB exit after each. `bad=0` and
      `early=0` in every window. Matt's verdict on the feel is open.
      - Sonic Adventure play (32 windows, ~160 s): 1=6 2=4330 3=274; 5 of the
        1s and all 13 late flips in the first window (60 -> 30 fps), 1 at
        the drift slip. Every window at RPS 30 shows 2=150 and nothing else;
        the 3s come with RPS under 30. Step 1 (OFF): 96 1s in an 85 s phase.
        Hold wait 17.9 ms per frame (8-26 ms with the drift); the SH4 waited
        for the render thread once (8 ms). Sonic's frames finish ~15 ms
        before their vblank, so ON shows a frame ~23 ms later than OFF (the
        model's +13.7 ms assumed later finishes).
      - Looney Tunes race (20 windows): 1=21 2=1892 3=636 (OFF: 55 1s in 32
        windows); 814 of 2550 held frames late (SH4-bound frames finish after
        their vsync and flip at once, as OFF).
      - Crazy Taxi play (12 windows): `hold=0`, 1=3598, unchanged.

**Done: 5.12 #3 SPE Tex Hash, HW SUCCESS 2026-10-07 (Matt) (Matt: "Work on
5.12 #3", then "Own SPU", not the TA decoder SPU at each FENCE).** Package
of 20:42; default ON since the package of 22:35 (Matt: "Yes, I want it by
default."). A `nullDC.cfg` with a `SpeTexHash` key keeps its value. VIDEO /
DEBUG -> "SPE Tex Hash" (`SpeTexHash`): OFF / ON / CHECK, set before boot.
- Sonic Adventure, ON (third boot of the same log, `sonic_on.txt`; Matt: "Sonic
  ran worse?"), against `run5_sonic.txt` (same `[jitcfg]`, SPE Tex Hash off).
  Play seconds at vt >= 6K: SPEED 99.99% (100.00%), RPS median 29.9 (29.9),
  RPS p10 24.8 (26.0); SH4 worst frame median 23.5 ms (23.2), over 25 ms in
  18% of seconds (14%), worst 66.4 ms (67.1), a JIT compile burst at an area
  change in both; no SH4 wait on the render thread or an SPU. Render busy
  5.5% (7.2%), hash 0.06% (1.62%). Menus and title screens: few params,
  textures of up to ~700 KB, the first bind waits for the SPU (up to 47 ms
  per 5 s, ~1% of the render thread; the SPU hashes ~1.9 GB/s, the PPU hash
  ran ~2.3 GB/s). Not the same spot as run 5, so the small play differences
  can be the scenes.
- HW 2026-10-07 (package of 20:42, ON from boot, no CHECK boot (Matt: ON
  first); `ps3log_spetex_run1.txt`, cut as `lt_on.txt` and `ct_on.txt`,
  scratchpad of session 606210f7; `racewin.py`, `speedsum.py` there).
  Against the SPE Build runs `run4_on.txt` and `run6_ct.txt` (the same
  settings without SPE Tex Hash). Both boots: every first bind from the SPU
  (`skip 0 none 0`), `fail=0`, clean XMB exit, flash saved.
  - Looney Tunes, a whole race (Matt: "No texture errors"); 55 windows at vt
    >= 10K: tex split hash 0.01% (1.95%), PPU hash 0 KB per render, render
    busy 8.5% (9.3%); the SPU 967 KB per request in 0.57 ms average;
    render-thread waits 0.23 ms per 5 s; texture decodes per render 0.12
    (0.15; `lt_fix.txt` 0.10). SPEED at vt >= 10K 99.7%, 86% of seconds at
    full speed (97.6%, 75%), not the same race.
  - Crazy Taxi, a short run (11 windows at vt >= 6K, 48 in `run6_ct.txt`):
    hash 0.15% (3.03%), render busy 17.3% (21.4%); vt >= 10K hash 0.20%
    (3.29%), busy 21.5% (25.2%). The SPU 469 KB per request in 0.38 ms; no
    waits in dense windows; decodes per render 0.24 (0.28). SPEED at vt >=
    10K 100.0% over 18 s (99.7%).
- Cost before (HW logs of the SPE Build runs, `[rend] tex split` hash):
  dense Crazy Taxi 3.0% of the render thread (144 textures, 523 KB hashed per
  render at 60 renders/s), Looney Tunes races 2.1% (54 textures, 967 KB at
  25/s), 2.4% at 25K+ vertices; Sonic Adventure 1.7% (84, 761 KB).
- ON: a fifth SPU thread (`ps3/spu/spe_tex.c`, `spe_tex_hash.h`;
  `ps3/ps3_spetex.cpp`). At the start of each draw (`ps3tex_frame_begin`) the
  render thread rings its bell with the scene's param list, the background
  param, TEXT_CONTROL and PAL_RAM_CTRL. The SPU hashes the background's
  texture, then each param's in list order: `describe()` and `hash_texture()`
  of `ps3_tex.cpp`, bit for bit (the PPU's four u64 lanes as two vectors of
  two lanes; 64-bit adds and shifts from 32-bit ops). A texture already hashed
  in the request (same TCW and size) takes the same hash. Results go to a slot
  per param (`SpeTexRes`: hash and request tag in one doubleword), in fenced
  128-byte line puts: after each fresh hash and at each full line.
- `ps3tex_bind` gets the param's slot (`spetex_slot()` in `ps3_rend.cpp`).
  At its first bind of a texture in a frame (hash check or cache fill) it
  reads the slot, spinning until the tag is this request's; no other change.
  The SPU leaves YUV (PixelFmt 3) and untextured params to the PPU (YUV keeps
  the converter's frame counter). The PPU hashes as before with no slot, a
  skipped slot, or the SPU down. 50 ms after the request with no result:
  `[spetex] SPU hash late`, the SPU group stops, PPU hash from then on.
- CHECK: the PPU hashes at each first bind as before and compares with the
  SPU's slot; the first 8 mismatches print `[spetex] check MISMATCH req slot
  tsp tcw spu/ppu`. A texture the game rewrites between the request and the
  bind can differ for one request; a bug differs in every request.
- Timing change with ON: the hash moves from the bind to the start of the
  draw (a few ms earlier at most). A texture rewritten in between is decoded
  at the bind as now and found changed again next frame (one more decode), or
  shows its old texels for one frame.
- Every 5 s: `[spetex] mode= up= reqs/s | spu busy % req avg max us | per
  req: params hashed dups skips KB wraps | binds: spu skip none | wait N avg
  max us, total ms | fail check ok bad`. `[rend] tex split` hash now holds the
  slot reads and waits (and the PPU's own hashes).
- PC (WSL): `misc/spetex_check/run.sh` PASS. The real `spe_tex_hash.h` on SPU
  shims (128-bit quadword shifts, cg, shufb byte model) and an MFC model (size
  and alignment rules, LS and EA regions, random or newest-first completion,
  fences; puts read LS when they complete), against `ps3_tex.cpp`'s code
  between two markers (its u64 VRAM load read big-endian): 12000 hash_range
  spans (every start alignment, chunk edges, the masked word loop past the
  end of VRAM), then 600 requests (294 of them next frames with re-uploaded
  textures), 824K params, every slot checked at the end and each of 242K
  result puts as it lands (a slot with this request's tag must be final).
  `mutate.py`: 68 of 68 planted bugs fail. Left out: unfenced line puts, and
  no wait for chunk c - 2's puts (the model's puts read LS as they complete,
  and its fenced 16-entry queue lands old puts first; the MFC promises
  neither).
- misc/spu_sim, the real image (`build_sim.sh`, `sim.sh`): every slot the
  same as `hash_texture()`. Crazy Taxi-like request (2000 params, 150
  textures, 354 KB): 0.46 ms; Looney Tunes-like (1500, 54, 578 KB): 0.41 ms;
  large textures 1.70 cycles per byte in the hash loop. Per-param lookups
  ~210 cycles; an 8-bit palette's FNV ~25 cycles per entry.
- SPU: text 9.9 KB, data 0.8 KB, bss 66.6 KB. Link: `.got` 0xC838 (was
  0xC638), 0 `r2off`, no warning.
- Expected (estimate, not measured): render-thread hash 3.0% -> ~0.2-0.3% in
  dense Crazy Taxi (~150 slot reads per frame), ~2% -> ~0.2% in Looney Tunes
  races. SH4 thread: only through SMT (5.16: busy 56 -> 32%, same load
  +0.5%), plus 0.5-1 MB of VRAM per frame no longer read through the PPE's
  shared L2.
- HW steps (package of 20:42):
  1. Install. The install keeps `nullDC.cfg`. VIDEO / DEBUG -> SPE Tex Hash
     CHECK; SPE Draw ON, SPE Build ON, Strip Merge ON, Vertex Arrays ON (the
     defaults); SPE Wait Yield OFF; SH4 Clock 200.
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1 (CHECK): Crazy Taxi ~2 min of dense traffic, then Looney Tunes,
     one race. Log: `[spetex] SPU thread up, mode CHECK`, `check ok=N
     bad=0`, `fail=0`; MISMATCH lines only if some (note their req numbers).
  4. Boot 2 (ON): Crazy Taxi dense, then a Looney Tunes race. Look for
     textures that do not update (menus, the arrow and the map in Crazy Taxi,
     FMVs). Log: `[rend] tex split %` hash against 3.0% (Crazy Taxi) and 2.1%
     (Looney Tunes), `[spetex]` wait total, `[rend] busy`, SPEED.
  5. On a fault: SPE Tex Hash OFF for the next boot.

**Done: 5.12 #2 SPE Build, the SPU rewrite (Matt 2026-10-07: "SPU rewrite
now", after HW runs 1 and 2 below), HW tested: correct and fast.** Package
of 17:41; default ON since the package of 18:36 (Matt: "Yes, I want it ON by
default."). A `nullDC.cfg` with a `SpeBuild` key keeps its value.
- HW run 3 2026-10-07 (package of 17:41; `ps3log_spevb_run1.txt` from line
  26728, cut per boot as `run3_check.txt`, `run4_on.txt`,
  `run5_sonic.txt`, `run6_ct.txt`, scratchpad of session 9d577958):
  - Boot 1, CHECK, a Looney Tunes race: 6117 scenes checked in 50 windows,
    `bad=0 late=0 fail=0`, no MISMATCH. Heavy race builds (28-33K vertices
    in, 37-43K out) 1.66-1.92 ms each, 2.19 ms at most (run 2: 7-10 ms).
  - Boot 2, ON, Looney Tunes (Matt: "very smooth", some micro-stutters he
    does not put on this): vt >= 10K SPEED 98.3% (run 2 95.9%, PPU path
    `lt_fix.txt` 99.2%), render busy 9.4% (22.1%), vtx+other 1.5% (13.2%);
    vt >= 25K 97.7% (98.7%), busy 10.5% (26.9%). Builds 1.9-2.5 ms average,
    3.1 ms max; render-thread waits ~1% of the wall; `fail=0 amis=0`. Frames
    over 25 ms in ~28% of the seconds in this run, run 2 and `lt_fix.txt`
    alike (the `[stall]` lines put them in "other", the SH4 side).
  - Boot 3, ON, Sonic Adventure (Matt: "the same"): SPEED 99.8-100% (already
    full speed), render busy 6-7%, vtx+other 0.6-0.8%, builds up to 1.9 ms.
  - Boot 4, ON, Crazy Taxi dense (48 windows at vt >= 6K): SPEED 99.9%
    (`ct_fix.txt` 99.8%), RPS 59.8 (59.8), render busy 21.4% (35.7%),
    vtx+other 1.8% (13.9%); builds 1.2-1.4 ms average, 1.7 ms max; `fail=0
    amis=0`; frames over 25 ms in 3.3% of the seconds (7.4%). Clean exits.
  - So: correct in all four boots, and the render thread loses its vertex
    work (13-16 points). The three games already ran at or near full speed
    in these scenes, so the frame rate does not change; the time is headroom.
  - Left open: publish waits of 0.26-0.34 ms per build in Sonic and Crazy
    Taxi (the echo put waits for the vertex puts; a fenced put on one tag
    could drop it); translucent-heavy scenes cost more SPU time (simulator:
    4000 TR strips -> 4.1 ms per build).
- Rewrite of `spe_vb_build.h` (same output, bit for bit, as the PPU merge):
  - Output: each vertex is a 48-byte slot (the 36 bytes of ps3gl_vertex_t,
    then 12 zero bytes), written with 3 whole quadword stores (no
    read-modify-write). The RSX reads stride 48: `ps3gl_draw_vb_stride`
    (new in ps3gl; `ps3gl_draw_vb` calls it with 36). The SPU block is
    cap x 48 + 128 bytes from `ps3gl_vring_alloc_raw`; if that fails, the
    scene goes the PPU path. Each put sends only the bytes since the last.
  - Input: the in-order passes (opaque, punch-through, the key pass) read
    strip records, vertices and params through two LS windows each (4080
    bytes, 8160 bytes with a 64-vertex overlap, 2 KB): the next window loads
    while one is in use. One scan per build finds each list's blocks of
    strip records, so a pass skips the other lists. The sorted TR pass keeps
    the per-strip DMA ring. EAs in 32 bits (64-bit math called `__muldi3`).
  - Vertices: two per vector (x and y of both, W of each twice):
    vertex_inf and the fast product range tested in one go; x, y, W turned
    into doubles with integer steps; one dfm and one frds per vertex. The
    next pair's tests run beside this pair's DP chain. Join copies and the
    first vertex's copies (dupe, parity) are plain stores. The old exact
    code takes inf vertices, zero/denormal/out-of-range products, the cap
    and strips over 64 vertices.
  - Keys: two exact 1/W per double vector without branches (the midpoints
    are q and q - 1 as doubles plus bit 28); fadds and its rounding with
    integer steps (round to nearest even; the overflow gives the Inf bits).
  - draw_strip is inlined into each pass (it saved 45 registers per strip);
    the join test and run records without branches or scalar stores.
  - TR_PPU: the PPU's 36-byte vertices go to a new vring block, with the cap
    the SPU left. CHECK: the SPU copy is 160K x 48 bytes; the compare checks
    the 9 words and the 3 zero words of each slot.
- misc/spu_sim (the SPU image with its timing; it gave 11.49 ms where HW
  measured 11.7 ms before), a Looney Tunes-like scene (38.5K vertices in,
  49.2K out; 4620 OP, 25 PT, 889 TR strips): 11.49 -> 2.21 ms per build
  (OP/PT ~8.7 -> 1.33 ms, key pass 1.23 -> 0.42 ms, sort 0.11 ms, TR pass
  1.49 -> 0.35 ms). Output the same as the PPU merge in the simulator too.
- PC: `run.sh 3000` PASS (106M vertices, 889K runs, 492 TR_PPU, 371 at the
  cap). `mutate.py` (96 planted bugs, 300 scenes each): 93 fail run.sh. One
  ("window one element long") hangs the check (the strip window never moves;
  on HW the 20 ms deadline turns SPE Build off). Missed, so test gaps, not
  bugs: the pair test without its W range (needs W = +Inf/NaN with x and y
  near 0 in the same vertex; the decoder never writes such a W), the fast
  path's cap test one slot short (needs the cap to fall on a first vertex
  with 3 copies), and the slide's wait on the wrong window (the MFC model
  completes old puts too soon to show the race).
- SPU: text 29.6 KB, data + bss 197 KB (~30 KB of LS left for the stack).
  `.got` 0xC638, no warning. The package also holds the menu footer icons
  of another session (`assets/`, `ps3_ui.*`, from this morning, already in
  the packages of 13:03 and 14:20).
- HW steps (package of 17:41):
  1. Install. The install keeps `nullDC.cfg`. VIDEO / DEBUG -> SPE Build
     CHECK; SPE Draw, Strip Merge, Vertex Arrays ON (defaults); SH4 Clock
     200; SPE Wait Yield OFF (as in runs 1-2: "wait spins").
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1 (CHECK, short): one Looney Tunes race. Log: `check ok=N bad=0
     late=0`, no MISMATCH line.
  4. Boot 2 (ON): a Looney Tunes race, then Crazy Taxi ~2 min dense. Look
     for missing or broken geometry. Log: `[spevb]` build avg/max and wait
     total, `split/build`, `[rend]` SPEED and vtx+other against run 2 and
     `lt_fix.txt`.
  5. On a fault: SPE Build OFF for the next boot.

History of #2 before the rewrite (Matt 2026-10-07: "We now work on 5.12",
then job "#2 vertex build" and "SPU build directly", not the XDR test
first). Package of 13:03, then the diagnostic build of 14:20.
- HW run 1 2026-10-07 (package of 13:03, ON from boot, Looney Tunes;
  `ps3log_spevb_run1.txt`, scratchpad of session 9d577958; Matt went to ON
  without CHECK). Boot: `RSX DMA test: ok` (the SPU can write RSX local
  memory). Every window `fail=0 amis=0 trppu=0`; clean XMB exit, flash
  saved.
  - Too slow: the SPU needs 8.1-9.8 ms per race frame (37-40K vertices out,
    ~28K in; estimate was ~1.7 ms), ~0.3 us per input vertex in light scenes
    too. The render thread spins 750-890 ms per 5 s waiting for runs.
  - Race windows against `lt_fix.txt` (the PPU path): vt >= 10K: SPEED
    91.3% (99.2%), RPS 24.9 (28.1), busy 24.2% (22.1%), vtx+other 16.1%
    (13.2%); vt >= 25K: SPEED 88.3% (98.7%), vtx+other 19.7% (16.1%).
  - So the spin costs the SH4 thread far more than the PPU path's vertex
    stores did: those stores mostly stall on cache-inhibited writes and
    leave the issue slots to the SH4 thread; a spin takes them.
- Diagnostic build (Matt: "Sure"), package of 14:20: `[spevb] bench:` at
  boot (SPU put bandwidth to RSX local and main memory, put and get round
  trips) and `[spevb] split/build:` every 5 s (waits for the strip DMAs,
  the publish and window puts, input loads and a full MFC queue; key pass,
  sort and TR pass; strips, vertices, puts, commands, KB out, slow
  products per build). Same behaviour otherwise; spevb_check PASS.
  `.got` 0xC638, 0 `r2off`, no warning.
- HW run 2 2026-10-07 (package of 14:20, ON, Looney Tunes; same log from
  line 10881, cut as `run2_lt.txt`): correct again (`fail=0 amis=0`,
  clean exit).
  - Bench: 1 MB of SPU puts to RSX local memory in 317 us (3.3 GB/s), to
    main memory in 48 us; one 128-byte put and wait 0.11 us (RSX) and 0.12
    us (main); one 192-byte get and wait 0.24 us. DMA is not the limit.
  - Split of a heavy build (36.6K vertices in, 48.6K out, 5536 strips; OP
    4620, PT 25, TR 889 strips): 11.7 ms = OP/PT passes ~8.7 ms, key pass
    1.3 ms, sort 0.1 ms, TR pass 1.6 ms. All waits together 0.41 ms (slot
    0.21, publish 0.16, input 0.04, full queue 0.002). So it is SPU compute:
    ~900 cycles per input vertex in the draw passes, ~700 in the key pass.
  - Why (from the code): it is scalar code on a SIMD core. Each 4-byte LS
    store is a quadword load, shuffle and store (9 per vertex out, chained
    through the same quadwords); each field load is a quadword load and a
    rotate; emit is a call; mul_xy is a chain of 4 DP ops (each stalls
    issue 6 cycles).
  - Race windows: vt >= 10K (34): SPEED 95.9% (run 1 91.3%, PPU path 99.2%),
    RPS 25.2; vt >= 25K (14): 93.7% (88.3%, 98.7%), RPS 23.5.
- VIDEO / DEBUG -> "SPE Build" (`SpeBuild`): OFF / ON / CHECK, set before
  boot. Needs SPE Draw (the SPU lists; default ON), Strip Merge and Vertex
  Arrays (defaults ON); else the PPU path, as now.
- ON: a fourth SPU thread (`ps3/spu/spe_vb.c`, `spe_vb_build.h`) builds
  draw_scene's merged vertex buffer and runs from the S3 lists, bit for bit:
  draw_list (opaque, punch-through), then draw_tr_sorted (keys, stable sort)
  or the translucent list in order, with draw_strip's merge, inf-vertex,
  parity and cull rules. The render thread asks after the background quad
  (`ps3spevb_request`), with the scene's vring block moved to a 128-byte
  line. The SPU writes the vertices by DMA straight into that block (RSX
  local memory) and publishes runs in batches: the first run at once, then
  every 256 vertices and at each list end. The render thread walks the runs:
  apply_param + one `ps3gl_draw_vb` each, as run_close does. No PPU vertex
  stores.
- TR_PPU: with more than 4096 translucent strips (autosort), or a W outside
  {0} and [2^-126, 1e9] in that list, the SPU stops after punch-through; the
  PPU sorts and draws the translucent list as now, in the same block.
- A missed deadline (20 ms per wait) logs `[spevb] SPU build late`, stops
  the SPU group and turns SPE Build off. The PPU draws the rest of that
  scene in a fresh vring block (a list the SPU had begun is lost for that
  frame).
- CHECK: the SPU builds into a copy in main memory. The render thread draws
  as now, then runs the same merge again as a dry pass (no GL call) into a
  second copy, and compares runs, list ends, strip counts and vertex words,
  bit for bit. The first 8 mismatches print `[spevb] check MISMATCH what[idx]
  spu/ppu: ...`. A copy holds 160K vertices; a larger scene is checked up to
  there, with the same cap on both sides.
- Boot: `[spevb] RSX DMA test: ok` (the SPU writes 128 bytes into RSX local
  memory, the PPU reads them back), then `[spevb] SPU thread up, mode ON|
  CHECK`. If the test fails, SPE Build stays off.
- Every 5 s: `[spevb] mode= up= builds/s runs/s vtx/s | spu busy % build
  avg max us | wait first avg max us, total ms | trppu fail amis | check ok
  bad late`. `amis`: apply_param's alpha force against the SPU's rule
  (expected 0: a texture always binds).
- Float steps (SPU single math truncates): x*W and y*W are the exact double
  product rounded once (frds), with an integer fmuls for denormal results.
  The sort keys use exact fdivs (frest + 2 Newton steps + midpoint residuals,
  as S2's 1/W) and fadds through double; +Inf and the overflow by hand. A
  radix sort (stable) on the key bits orders as std::stable_sort does.
- PPU code: run_close, draw_strip, draw_list and draw_tr_sorted are now
  templates on DRY (CHECK's dry pass). Their default instantiation compiles
  to the same instructions as before (checked in the disassembly; only TOC
  offsets differ).
- PC (WSL): `misc/spevb_check/run.sh` PASS: 3000 scenes (107M vertices,
  913K runs; 470 with TR_PPU, 381 at the vertex cap). The real
  `spe_vb_build.h` runs on SPU shims: frds/fesd range checks, a frest model
  with 2^-12 to 2^-7 error, and an MFC model (alignment and size rules, junk
  in a get's buffer until its tag wait, random completion order, puts read LS
  when they complete). The reference is ps3_rend.cpp's own merge code,
  extracted between two markers, in both the DRY and the GL instantiation.
  Every echo is checked as it lands (its runs and vertices are already
  final). 13.6M checks of the float steps against x86 IEEE single math.
  `mutate.py`: 60 of 60 planted bugs fail. Left out as equivalent: the
  list test in the merge (each list is its own pass), the window wait in
  `os_slide`, and the q - 1 steps of the divisions (two Newton steps always
  land below the true quotient, so they never fire).
- SPU: text 14.3 KB, bss 167 KB. Link: `.got` 0xC608, 0 `r2off`, no
  warning. The package also holds the menu footer icons of another session
  (`assets/`, `ps3_ui.*`, `ps3_menu.cpp`, Makefile).
- Estimate (not measured): the SPU needs ~180 cycles per input vertex
  (~1.7 ms for a 30K-vertex Looney Tunes scene, ~0.6 ms for dense Crazy
  Taxi), the render thread's run walk ~1.5 us per run. In Looney Tunes the
  render thread may wait for the SPU (`wait total` in the `[spevb]` line).
  Render-thread time reaches the SH4 thread only through SMT (5.16).
- HW steps (package of 13:03):
  1. Install. The install keeps `nullDC.cfg`. VIDEO / DEBUG -> SPE Build
     CHECK; SPE Draw ON, Strip Merge ON, Vertex Arrays ON (defaults); SH4
     Clock 200; the rest as in the last runs.
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1 (CHECK): Crazy Taxi, ~2 min of dense traffic, then exit; Looney
     Tunes, one race. Log: `[spevb] RSX DMA test: ok`, `check ok=N bad=0
     late=0`, no MISMATCH line, `trppu`.
  4. Boot 2: SPE Build ON (set in the menu before boot). Crazy Taxi ~2 min
     dense: look for missing or wrong geometry. Log: `[rend] draw split`
     vtx+other and `busy` against `ct_fix.txt`, `[spevb] wait`, same-load
     VPS.
  5. Boot 3 (ON): a Looney Tunes race, the 30-38K-vertex scenes: vtx+other,
     `[spevb] wait`, SPEED.
  6. On a fault (missing or broken geometry): SPE Build OFF for the next
     boot.

**Active: 5.12 (Matt 2026-10-07: "Let's go with 5.12", then "start
5.12").** Matt picks the job before any build. S0-S3 (job #1, TA decode)
are done; SPE Draw is ON by default. Matt picked #2 (block above). Cost of the open jobs in the latest
HW logs (`ct_fix.txt`: 22 windows at vt >= 6K; `lt_fix.txt`: 51 windows
at vt >= 10K; both from `ps3log_ltfix_run1.txt`, scratchpad of session
7b7c1c88; `rsplit.py` in the scratchpad of session b309523f):
- #2 strip merge + vertex build (`draw_strip`, `vb_emit`): `vtx+other`
  13.9% of the render thread's wall in dense Crazy Taxi (busy 35.7%), 13.2%
  in the Looney Tunes races (busy 22.1%), 16.1% at 25K+ vertices. The
  largest render-thread item left. `vb_emit` writes 9 words per vertex
  into the vring in RSX local memory (cache-inhibited). ps3gl already has
  a host ring in XDR mapped for the RSX (`ps3gl_hring_*`, 2 x 1 MB) and a
  draw path from XDR (`GCM_LOCATION_CELL`).
- #3 texture hash: 3.1% (Crazy Taxi), 2.1-2.6% (Looney Tunes races). Done
  as "SPE Tex Hash", HW SUCCESS 2026-10-07 (top of this section).
- #4 texture decode: 0.8% in play; Sonic Adventure FMV 7% with P42 ON.
  Built as "SPE Tex Decode" (top of this section), HW run 1 no faults,
  default ON.
- #5 `present_fb_convert`: no cost in the test games (1-2 presents per
  boot). Built as "SPE FB Convert" (top of this section), HW run 1 no
  faults (boot screens only), default ON.
- P43 YUV converter (SH4 thread): Sonic Adventure FMV (`ps3log_p42_run1.txt`,
  P42 ON): `[stall] dma` 34-45 ms/s (3.4-4.5% of the SH4 thread), SPEED
  92-102%; the SH4 thread is the limit there.
- The SH4 thread is the limit in the open cases. Render-thread work
  reaches it only through SMT (5.16: busy 56 -> 32%, same load +0.5%, 10K+
  bins +2.4% to +5.0%).

**Built, HW tested on Looney Tunes (Matt: "Perfect.") and Sonic Adventure
(Matt: "Video looked fine."): P42 YUV Block Decode
(Matt 2026-10-07: "Let's work on the YUV Decode.", then option (a), the
block decoder).** Built and PC tested in the package of 10:01. Default OFF.
5.12 SPE list #4 stays open (this is the PPU form).
- HW 2026-10-07 (package of 10:01, ON from boot, our listener at Matt's
  request; `ps3log_p42_run1.txt`, scratchpad of session 18a23bec). Matt ran
  the ON boots only; OFF is the Looney Tunes FMV in `ps3log_ltfix_run1.txt`
  and `ps3log_p18_run2.txt`.
  - Looney Tunes intro FMV (t=26-37 s): every YUV decode on the block path
    (`yuvblk` 65 and 68 = the decodes); the texture is 1024x512 planar,
    pitch 640 (so 384 of 1024 columns are the zero tail). Decode 5.4 ms per
    frame (OFF 21.6 ms, 4.0x), render busy 7-8% (OFF 28%). SH4 thread:
    slept in 17.5 frames per s (OFF 5.5-5.8), SPEED mean 100.6% and lowest
    second 96.6% (OFF 98.3-99.0% and 86.5-90.5%). `blacktex` 0.
  - A Sonic Adventure boot before it (ON, 56 s) reached no FMV. Clean XMB
    exit, flash saved.
  - Sonic Adventure intro FMV (third boot), the first 27 s against the
    same span of the P16 run (OFF): Matt: "Video looked fine." All YUV
    decodes on the block path; the texture is 512x512 planar, pitch 320.
    Decode 2.5 ms per frame (OFF 10.7 ms, 4.3x), render busy 5-7% (OFF
    11-16%). SPEED mean 94.8% (OFF 88.8%), lowest second 90.0% (OFF 85.1%),
    FMV frame rate median 27.8 (OFF 26.0). The SH4 thread is still the
    limit (it slept in no FMV second, in both runs). Not a same-build A/B:
    the P16 build lacked Depth Float and the Looney Tunes AICA fixes.
  - Space Channel 5: not run (Matt: "Space Channel already ran fine from
    previous runs."). Soul Calibur opening: not run.
- VIDEO / DEBUG -> "YUV Block Decode" (`YuvBlockDecode`), default OFF, live.
  A change acts at the next YUV decode and logs `[tex] yuv block decode
  ON|OFF` (also once at boot). Both settings give the same texels, so a
  switch does not clear the texture cache.
- ON (`ps3/ps3_tex_yuv.h`): a YUV texture (planar, stride, twiddled, VQ,
  mip) decodes by tables straight into its RSX texture. One word load per
  texel pair (planar) or two per 2x2 block (twiddled, VQ); three chroma
  lookups per pair (the green term is 11 * (u + 2v) / 32, one table by
  u + 2v); three clamp-table lookups per texel; no multiply, no branch.
  `volatile` word stores (RSX memory is cache-inhibited). No `s_out` pass,
  no ps3gl `convert_pixels()` pass. New ps3gl entry point
  `ps3gl_tex_direct_begin()` / `_end()` (`ps3gl_textures.c`; the level-0
  allocation is now one function shared with `glTexImage2D`). A source that
  crosses the end of VRAM takes the old path.
- OFF: `decode()` + `glTexImage2D`, unchanged (the decode now runs after the
  bind; same result).
- `[texfmt]` ends in `| yuvblk=N yuvlast=WxH{p|t|v}P`: block-path decodes in
  the window, and the size, kind (planar, twiddled, VQ) and pitch of the
  last YUV texture (the FMV texture sizes were never logged).
- PPU code (`ps3_tex.o`): the planar loop is 81 instructions per 4 texels
  (~20 per texel, no branch). The old planar `yuv_pair` loop is 103 per
  texel pair (~52 per texel, 12 conditional branches for the clamps), plus
  ~10 per texel in `convert_pixels`. Expected: decode time per YUV texture
  ~3-5x lower (estimate, not measured).
- Before (HW logs, OFF): Looney Tunes FMV decode 27.6% of the render thread
  (21.6 ms per decode, ~12.8 per s), Sonic Adventure intro 10-15% (10.7 ms),
  Space Channel 5 ~12% (5 ms, ~24 per s). The Soul Calibur opening is light
  since P16 (80 decodes per 5 s, 2.8%, `ps3log_p18_run1.txt`). In these
  FMVs the render thread shares the PPE core with the SH4 thread.
- PC (WSL): `misc/yuv_check/run.sh` PASS. The new header against a copy of
  the old path (`decode()` for fmt 3 + `convert_pixels()` with the PPE byte
  order), bit-exact on the final RSX words, row padding included, and the
  same black-texture count: every U, V, Y0 and Y1 (16.7M pairs); 240 fixed
  textures (black, white, chroma extremes, odd texels lit); 144 sources at
  the end of VRAM (96 fast, 48 fall back); 3000 random textures (planar and
  stride, twiddled, VQ, mip; 145M texels). `mutate.py`: 32 of 32 planted
  bugs fail.
- Link: `.got` 0xC358, 0 `r2off`, no warning. No wide store and no memset
  on the RSX pointer in the disassembly.
- HW steps (package of 10:01):
  1. Install. The install keeps `nullDC.cfg`. VIDEO / DEBUG -> YUV Block
     Decode OFF; the rest as now, SH4 Clock 200.
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1: Looney Tunes, the intro FMV: ~20 s with OFF, then SELECT ->
     VIDEO / DEBUG -> YUV Block Decode ON -> SELECT for the rest of it.
     Look at the video: colours, no stripes, no black or green blocks.
     Log: `[tex] yuv block decode ON` at the switch, then `[texfmt]
     yuvblk` equal to the YUV decodes, `[rend] tex split` decode % and
     `busy` lower than with OFF.
  4. Boot 2: Sonic Adventure intro FMV with ON: SPEED against the 86-97%
     of the P16 run.
  5. Boot 3: Space Channel 5, a stage with the FMV backdrop, with ON.
  6. Optional: the Soul Calibur opening with ON (twiddled tiles).
  7. On a fault (wrong colours, garbage): SELECT -> YUV Block Decode OFF
     (live).

**Open (Matt 2026-10-07: "mark the busy SH4 for Looney Tunes. It may be
worth coming back later on."): Looney Tunes Space Race is SH4-bound in its
races.** With every fix on (below), dense race windows run at SPEED 99.0%,
66% at full speed, the SH4 thread sleeping 49 ms/s; the heavy scenes (30-38K
vertices per frame) fall to SPEED 92-95% with worst frames of 20-40 ms of SH4
work, and the game draws 22-27 frames/s there (26-30 elsewhere). The same in
the DSP OFF boots before the fixes (57-59% at full speed). Not the disc (no
reads or waits in the race), not the render thread, not the sound SPUs.
`ps3log_ltfix_run1.txt` (cut as `lt_fix.txt`), scratchpad of session 7b7c1c88.
- 2026-10-08, with 5.12 #4 ON: the race start freeze is gone (render job 19
  ms), the in-race stutter stays (Matt: "Stutter during the race."): 25.2
  frames/s, 2- and 3-refresh frames, SH4 thread idle ~7%. Matt: "The faster
  JIT in one of the plans sounds fine." (JIT work on the race code, then an
  SH4 Clock above 200 MHz; the menu stops at 200.)
- 2026-10-10: both done (Steps 2 and 3, the top of section 0). The race is
  at SPEED 100% at 200 MHz and the stutter stays; a higher SH4 Clock is
  not the answer (Matt). The item stays open, cause not known.
- 2026-10-08, later: active. Step 1 (profile + 2 probes) built, package of
  19:22; see the top of section 0.
- Stutters (Matt 2026-10-07, with SPE Build ON: "very smooth. But there are
  some micro-stutters sometimes"; "add the stutter ... with a possible fix
  later on"). Race seconds (vt >= 10K) whose worst frame is over 25 ms, by
  the `[stall]` split (`stalls2.py`, scratchpad of session 9d577958): SPE
  Build ON (`run4_on.txt`) 13 of 87 (15%), OFF (`lt_fix.txt`) 43 of 248
  (17%):
  - SPU wait, 300-430 ms (2 in each run): at race start and race end. One
    render job decodes the new track's textures (VQ, mipmaps) in 364-426 ms
    (`job_max`); the TA-decode SPU waits for the render thread's release
    (S3), and the SH4 thread waits for that SPU (`[stall] spe`).
  - Frame pacing (7 ON, 20 OFF): the slow frame holds a pacer sleep of 9-14
    ms. The game draws 24-27 frames/s on a 60 Hz screen, so a frame stays 2
    or 3 refreshes: judder even at full speed.
  - Render thread (1 ON, 7 OFF): SPE Build took most of these away.
  - SH4 work, 23-28 ms in one frame (3 ON, 14 OFF): the SH4-bound item above.
  - Possible fixes, for later (Matt): texture decode off the render thread
    for the freezes (5.12 job #4, an SPU decodes; or the decode spread over
    frames); JIT work on the race's hot code for the SH4 frames; a look at
    the pacer's sleep for the judder.

**Done: the Looney Tunes fixes (Matt 2026-10-07: "Can we apply the Looney
Tunes fixes?", then picked AICA Race Fix, ARM7 Reset Fix and CA Clamp; not
the DSP ring in ARAM), HW SUCCESS (Matt: "This session is a success"),
default ON since the package of 03:41, with SPE AICA DSP and SPE DSP JIT ON
(Matt: "mark this on by default"; 5.13 A3 and P20 G1 done).** Built and PC
tested in the package of 03:24 (`.got` 0xC310, 0 `r2off`, only the old
`.text` attribute warning); the package of 03:41 only changes the five
defaults (same `.got`, no warning). AUDIO page, set before boot. A default
only reaches a cfg without the key; Matt's has all five ON already. While
one is on, `[aicafix]` every ~5 s: G2 DMA starts and guard hits, SH4
interrupts raised and ARM7 slices extended, ARM7 resets and stale FIQ
latches, CA reads and clamps.
- AICA Race Fix (`AicaRaceFix`, 6.5 + Flycast 51089c5e0, the Test Drive Le
  Mans freeze fix). G2 DMA: no start while one runs; SB_ADST reads 1 once
  while the DMA end IRQ pends (`Read_SB_ADST` live); STAR/STAG move at the
  end; ADEN after a bit-31 transfer is 0 (Flycast a44619931); ADSUSP bit 4
  set while idle. AICA: an SCIEB/MCIEB write updates the lines at once; an
  ARM7 store that leaves an SH4 interrupt pending (MCIPD SCPU or MCIEB,
  raised now or before) keeps 50 ticks of its slice after it, one extension
  per slice (so a raising loop still ends its slice). In the SPU core, the
  cached interpreter and the ARM7 JIT (`e_race_check`; an STM to registers
  counts its 50 from its end there).
- ARM7 Reset Fix (`Arm7ResetFix`, P44 + 6.8): a reset enters SVC (0x13),
  not System (0x1F), and clears aica_interr, e68k_out, e68k L and M.
- CA Clamp (`AicaCaClamp`, P31): a CA read stops at LEA-3 (LEA > LSA, LEA >
  3) in the twin and SPE ARM7 paths too, with the slot's LSA/LEA registers.
- PC (WSL): `misc/arm7spu_check` (B0) ALL PASS, 6 x 400 programs with ~4.5K
  extensions each; the 45 conformance tests pass in both reset modes on both
  cores; the 8192-entry map has the same 3 eviction mismatches with the
  fixes off (`nofix`); new `mutate.sh` 10 of 10. `misc/aicactl_check` (B1) 32
  seeds ALL PASS, `mutate.sh` 26 of 26. `misc/arm7spe_check` (B2) 22 seeds x
  6000 batches ALL PASS, `mutate.sh` 19 of 19 (harness fix: a random SH4
  write to 0x2C00 is ARMRST on the SPU, the reference now applies it too).
  New `misc/g2dma_check`: the real `aica_if.cpp` against a model of both
  rule sets, 8 configurations (fix x DmaFix x SCHED), 3600 runs PASS,
  `mutate.sh` 12 of 12.
- HW steps (package of 03:24): install (keeps `nullDC.cfg`); AUDIO: the
  three fixes ON, SPE AICA DSP ON, SPE DSP JIT ON (as the last Looney Tunes
  run), SPE ARM7 ON; SH4 Clock 200. UDP listener only when Matt says so.
  Boot Looney Tunes: menus (voice lines), a load (the echo/noise), one race
  (effects and voices, frame rate). Log: `[sndcfg]` with the three keys at
  1, `[aicafix]`, `[arm7spe] fiq` after the game's ARM7 reset (0 before),
  race RPS against `lt_part.txt` (16-22; DSP OFF boots 26-30). On a fault:
  the next boot with one fix OFF at a time.
- HW 2026-10-07 (package of 03:24, Looney Tunes, the three fixes + SPE AICA
  DSP and SPE DSP JIT ON; `ps3log_ltfix_run1.txt`, scratchpad of session
  7b7c1c88; the log starts at t=71 s). Matt: "The sound cues ALL played
  correctly. But the performance wasn't good." Clean XMB exit, flash saved.
  - Race frame rate back to the DSP OFF boots: RPS median 26-30 at 10-35K
    vertices (`lt_part.txt` 15-22, P16 boots 26-30). Dense windows: SPEED
    99.0%, 66% at full speed, SH4 sleep 49 ms/s (P16 boots 98.3-98.6%,
    57-59%, 46-49 ms/s). So the fixes and the DSP cost no SH4 time; the
    race was below full speed before them too (the SH4 thread is the limit).
  - FIQs still stop after the game's ARM7 reset (542/s, then 0 from t=76 s)
    with no stale latch at either reset, yet every cue plays: the driver
    runs on its monitor loop (now ~2500 reads/s, ARAM puts ~210K/s in the
    race; `lt_part.txt` ~1174, ~88K).
  - [aicafix] in the race: 30-600 SH4 interrupts raised by the ARM7 per 5 s,
    0-13 slices extended; G2 DMA busy refusals 0, the SB_ADST guard once;
    ~1000 CA reads/s, 0-3 clamped per 5 s. Which fix gives the cues is not
    known (no A/B yet). DSP jit ON: bad=0, slow=0, drops=0.
  - Settings that cost SH4 time in Matt's cfg: only the JIT C-Calls probe
    (VIDEO / DEBUG; the `[CC]` and `[stall]` lines are in the log).
  - Matt's theory (disc seeks in the slowdowns): no. In the race the only
    disc traffic is the CD-DA read-ahead (~74 sectors/s on the worker, ring
    misses ~0.02/s); no game data reads and no SH4 disc wait in any race
    window, slow or not (`gdrrace.py`). The slow windows are the 30-38K
    vertex scenes (SH4 work); the 335/401 ms stalls are SPU waits at scene
    changes, also in the DSP OFF boots.
- HW 2026-10-07, Crazy Taxi, same settings (same log, cut as `ct_fix.txt`):
  clean, FIQs ~1993/s throughout; DSP: program 1434 words, 79 live steps,
  slow=0, bad=0, drops=0, DSP 5.3% of the AICA SPU (busy 5-20%); at the
  game's ARM7 reset ARM7 Reset Fix cleared a stale latch; no G2 DMA, no SH4
  interrupt from the ARM7. Speed against the P18 Crazy Taxi run (no fixes,
  DSP OFF): same-load VPS -0.0%, spare time within ~5-10 ms/s per load bin;
  91.3% of dense windows at full speed (96.4%), this run had 3x the 10K+
  windows. ChuChu Rocket was booted briefly before it (no verdict).

**Done: the open HW checks (Matt 2026-10-07: "Start the listener. I'll do
the HW checks."); the Looney Tunes one led to the fixes above.** P18's Crazy Taxi check: done (P18 below). 5.36 TA Peak
Frame and 5.20 (the Soul Calibur check, the known-w pass): BLOCKED (Matt:
"You can block this goal and the others"). Now: 5.13 A3 + SPE ARM7 (P20
G1) on Looney Tunes (Matt: "I'll go with 4th aka 5.13 on Looney Tunes").
Log `ps3log_p18_run2.txt`; the Looney Tunes boot starts after byte 428607
(cut as `lt_part.txt`, with `rpsbins.py`, `monrps.py` and `ccrace.py`,
scratchpad of session 7b7c1c88). That listener stopped with the session;
port 18194 is free.
- HW 2026-10-07, Looney Tunes with SPE AICA DSP ON, SPE DSP JIT ON, SPE
  ARM7 ON. Matt: "Looney Tunes has the voice lines in! Perfect!" (they go
  through the DSP only: G1 confirmed). Then: "There's an echo going on
  during loading. A noise.", "This would kill the effects/voices
  in-game", "It also ran much worse." Clean XMB exit.
- DSP: program 113 words, 2 live steps, recompiled at each load (46 ->
  109 compiles); slow=0, fails=0, and bad=0, drops=0 in every window; AICA
  SPU busy ~5%.
- Race windows (10K+ vertices): game frame rate (RPS) median 16-22, in
  both P16 Looney Tunes boots (DSP OFF) 26-30, at the same SPEED
  (98.5-100%). The SH4 thread sleeps 218 ms/s (P16: 44), so the guest
  makes fewer frames per emulated second; the host is not slower. Against
  the P16 boots only `SpeAicaDsp` 0 -> 1 and `SpeDspJit` CHECK -> ON
  changed; this build also has Depth Float ON.
- FIQs stop at the game's ARM7 reset (t=201: 988/s -> 161 -> 0), as in
  the P16 log with the DSP OFF: F3's lead (section 6), not new.
- Next (Matt picks): the same race with SPE AICA DSP OFF; if the frame
  rate stays low there, Depth Float OFF (live) in that race.

**Done: P18 Depth Float (Matt 2026-10-07: "We now work on P18."), HW SUCCESS
(Matt: "It was"), default ON since the package of 01:01 (Matt: "Yes,
absolutely!").** Built and PC tested in the package of 00:42. The second of
the parked Soul Calibur leads (below), option (a). The UDP listener of
session 783a0c22 was stopped at Matt's request; the one of session 7b7c1c88
(01:12, Matt: "Start the listener") stopped with that session.
- VIDEO / DEBUG -> "Depth Float" (`DepthFloat`), live. A change acts at the
  next frame and logs `[rend] depth float ON|OFF` (also once at boot). A
  default only reaches a cfg without the key; Matt's has ON already.
- ON (`ps3/ps3_depth.h`): the RSX depth format is FLOAT (`rsxSetDepthFormat`,
  CONTROL0 bit 12; the Z24 surface then holds f32 bits >> 7, RPCS3's
  `d24f`). z = n/W (m[10] = 0, m[14] = n), the clear is 0 (1/W = 0), the DC
  tests without the flip (punch-through and autosort GEQUAL, modifier
  volumes GREATER), and `glDepthRange(-1, 1)`, so ps3gl's viewport z has
  scale 1 and offset 0. The end of `draw_scene` sets FIXED and the range
  0..1 back for the UI. OFF: the mapping before P18, unchanged.
- Change against the P18 sketch: m[10] = -1, m[14] = 2n through GL's depth
  range 0..1 computes (z/w + 1) / 2, which rounds n/W to the same 2^-24 step
  as Z24 (NVIDIA "Depth Precision Visualized": reversed Z needs a 0..1 clip
  z). On the PC model it gets 68% of the Soul Calibur pairs wrong.
- Why (`ps3log_p16_run1.txt`, scratchpad of session f65e2239; `zstep.py`
  in that of session 783a0c22): in the Soul Calibur boot (32 windows) near is
  0.000473 (median; min 2.7e-6) and the opaque list runs to W 2083 (median)
  and 10000. The Z24 step in 1/W is then 26% of the value at W 2083 and
  126% at W 10000, and 62748 of the 63062 opaque strips in the `[rendz]`
  frames use DepthMode 4 (greater): far polygons tie and fail. Crazy Taxi:
  about 1% at W 8000; Sonic Adventure 0.01% at W 2093.
- PC: `misc/depth_check/run.sh` PASS. The real header on a model of the RSX
  depth path (dp4, divide or rcp with up to 2 ulp error, fused or separate
  viewport z, rounded or truncated store; 8 variants): FLOAT gives 0 wrong
  DC compares at 1/W gaps from 2^-14 over every range the renderer allows.
  Soul Calibur stage (gaps 0.1-10%): FIXED 77.5% of pairs wrong, FLOAT 0;
  far Crazy Taxi: FIXED 17.5%, FLOAT 0. FIXED stays right where its step is
  fine; no FLOAT vertex leaves the depth range; the clear is raw 0 in both
  encodings. `mutate.py`: 19 of 19 planted bugs fail.
- Link: `.got` 0xC1E0, 0 `r2off`. The package also holds P16's default ON
  (`TexSlotHash` = 1, not committed).
- Not built: leaving modifier-volume z out of n (P18's extra step). With
  FLOAT, n only scales z; the precision does not depend on it.
- HW steps (package of 00:42):
  1. Install. The install keeps `nullDC.cfg`. On the main menu: VIDEO /
     DEBUG -> Depth Float OFF, Tex Slot Hash ON, Strip Merge ON, Vertex
     Arrays ON; SH4 Clock 200 and the rest as in the P16 runs.
  2. UDP listener on port 18194, only when Matt says so.
  3. Boot 1: Soul Calibur, one fight on a stage with a far background, ~60
     s with Depth Float OFF, then SELECT -> VIDEO / DEBUG -> Depth Float ON
     -> SELECT, ~60 s more. Look at the background (flicker, malformed
     polygons), the fighters and the HUD. Log: `[rend] depth float OFF` at
     boot, `[rend] depth float ON` at the switch, no fault, render `busy`
     and RPS as before.
  4. Boot 2: Crazy Taxi with Depth Float ON, ~2 min: far buildings, the
     road, the car shadows (modifier volumes), the HUD.
  5. Boot 3: Sonic Adventure with ON: a level and the menus.
  6. On a fault (black screen, missing geometry): SELECT -> Depth Float
     OFF. The SELECT menu draws with no depth test, so it still shows.
- HW 2026-10-07 (package of 00:42, our listener at Matt's request;
  `ps3log_p18_run1.txt` in the scratchpad of session 783a0c22). Depth Float
  ON in every boot. The log starts at t=56 s of the Soul Calibur boot, so
  that boot's `[rend] depth float` line is missing; the other boots log ON
  at the first frame. All exits to the XMB clean.
  - Soul Calibur fight: Matt: "The flicker is gone! And also some other
    graphical glitches." VPS and RPS 50 of 50 at 15K vertices, render busy
    42% (the P16 fight: 42-43%), near down to 5.9e-6.
  - Sonic Adventure: Matt: "Some text now displays properly." 68 windows,
    SPEED 99.4%, at most 3K vertices (light scenes only). Its wrong
    textures (open since P16) are fixed too (Matt).
  - Rayman 2: the untextured level surfaces stay (Matt), so they are not a
    depth fault. 205 windows at SPEED 100%, render busy at most 13%.
  - Power Stone 2 (second boot): Matt: "An issue in Power Stone 2 is also
    fixed." 50 windows at SPEED 100%.
  - Crazy Taxi, run later (`ps3log_p18_run2.txt`, scratchpad of session
    7b7c1c88), Depth Float ON from the cfg: Matt: "Ran perfect." The log
    starts at t=86 s, so the boot's `[rend] depth float` line is missing.
    56 dense windows (vt >= 6K): SPEED 100.0%, 96.4% at full speed (5.36:
    94.5%). Car shadows drawn: 246-452 modifier-volume tris per render, W
    up to 9567. Render busy 26-44%, one dropped render (first scene), SPE
    fences all ok. Clean XMB exit, flash saved.

**Done: P16 Tex Slot Hash (Matt 2026-10-06: "Ok then, do P16."), HW SUCCESS
(Matt: "It's a success"), default ON since the package of 23:46 (Matt: "Tex
Slot Hash should become the default ON").** Built and PC tested in the
package of 23:16. The first of the parked Soul Calibur leads (below).
- VIDEO / DEBUG -> "Tex Slot Hash" (`TexSlotHash`), live. A change clears
  the texture cache at the next frame and logs `[tex] slot hash ON|OFF,
  cache cleared` (also once at boot). The refill after a clear made one
  render job of 290-367 ms in the Soul Calibur fight.
- ON: the slot is the top 10 bits of `(key0 ^ key1 * 0x9E3779B1) *
  0x85EBCA6B` (`ps3/ps3_tex_slot.h`). OFF: the slot before P16, unchanged.
- `[texfmt]` ends in `evict=` (misses that freed a live texture) and `hot=`
  (of those, textures drawn in the last 4 frames). With OFF, `hot` near the
  decode count confirms the collisions.
- PC: `misc/texslot_check/run.sh` PASS, on the cache's probe, victim and
  aging rules. 80 FMV tiles 8 KB apart: OLD misses 91% of binds, HASH 0.
  Stride sets up to 88 textures, palette and TSP variant sets: HASH 0.
  Random sets: HASH no worse than an ideal random slot. `mutate.py`: 8 of 8
  planted bugs fail. Link: `.got` 0xC1B8, 0 `r2off`.
- HW 2026-10-06 (`ps3log_p16_run1.txt`, scratchpad of session f65e2239),
  per 5 s window:
  - Soul Calibur opening FMV, OFF: 11178 decodes, 11149 of them `hot`
    evictions, 3596 black tiles, render busy 67% (decode 61.5% of its
    wall). The FMV with ON was not measured: it had ended before the switch.
  - Soul Calibur fight, ON (~85 s): SPEED 100%, VPS and RPS 50.0 of 50, no
    dropped renders, 0-8 decodes, 0 evictions, 40-56 textures live. Render
    busy 42-43%: decode 0.2-1.0% of its wall (was 82-85%), hash 16-17% (~5
    MB per render). Before P16: ~1054 decodes, VPS ~26, half the renders
    dropped. A short OFF stretch: 414 `hot` evictions. Matt: "Soul Calibur
    runs much faster". The black textures that flicker stay (Matt).
  - Crazy Taxi, ON: the same as before (Matt); 0 evictions, 19 of 20
    windows at full speed, up to 257 textures live.
  - Space Channel 5, ON: full speed (Matt), 0 evictions. Its FMV is one
    texture, decoded once per frame; no OFF compare.
  - Sonic Adventure, ON: FMV SPEED 86-97% (one texture, so not P16; the 5.2
    run had 86-87%); play at full speed in 44 of 44 windows, up to 323
    textures live, 0 evictions. Its wrong textures are the same as before
    P16 (open item below).

**Done: 5.36 SYS GATE (Matt 2026-10-06: "5.36"; then "Gate + Resume
levels" and, for the TA peak, "Own toggle, as in the plan"), HW SUCCESS
(Matt: "SYSGATE is a success"), default + RESUME since the package of
22:52 (Matt: "Yes, make +RESUME the default").** Built and PC tested in
the package of 22:27. HW 2026-10-06 (5.36 "HW"): three boots clean (Crazy
Taxi GATE, Crazy Taxi + RESUME, Sonic Adventure + RESUME); dense Crazy
Taxi at full speed in 94.5% of windows with + RESUME (86.3% in the 5.38
run), ~2-3% more spare time; Matt: "It felt much smoother now". TA Peak
Frame: built, default OFF, not run on HW (expected gain none); BLOCKED
2026-10-07 (Matt: "You can block this goal").
- Found first: the 5.38 logs (JIT C-Calls ON) give the per-timeslice
  update 61-73 ms/s of the SH4 thread (`[stall]` sys), 6-7%, at 112K
  calls/s: 550-650 ns per call. HW then showed this is work per slice:
  the gate leaves it as it is and saves only the calls and lookups.
- JIT SPEED -> "JIT SYS GATE" (`SysGate`): OFF / GATE / + RESUME, default
  OFF, set before boot. GATE: the JIT counts slice ends in `Sh4cntx` and
  runs the update only at the slice end of the next event; the skipped
  slices then run, unchanged. At FAST the medium tick (every 4th slice)
  bounds it: ~28K updates/s, not 112K. + RESUME: with nothing due, a
  block's cycle exit enters the block again without bm_GetCode.
- VIDEO / DEBUG -> "TA Peak Frame" (`TaPeakFrame`), default OFF, set
  before boot: no peak store per TA record. Its fast path in the binary is
  one instruction longer (GCC saves 3 more registers): expect no gain.
- PC: `misc/sysgate_check/run.sh` PASS (the real TMU, INTC, SPG, PVR
  register, AICA DMA and update code: 300 random timelines give the same
  trace with the gate off and on; 1.42M checks of the JIT words against
  ppu-as and a PPC64 model); `mutate.py` 30 of 30 planted bugs fail.
- Start a UDP listener only when Matt says so.

**Closed (Matt 2026-10-10: "Rayman 2 is closed."), fixed by 3j Bump Maps
(HW SUCCESS 2026-10-08). Was open, separate (Matt 2026-10-06: "let's keep
this as an open, separated issue"): Rayman 2 draws level surfaces
untextured.** US GDI, first level:
large level polygons show only smooth vertex-colour gradients; Rayman and
the far tree backdrop are textured. The same with JIT Div32 OLD and FIXED
(5.38 HW). Log: `blacktex=0`, no YUV, palette or stride textures, mip and
VQ textures in use, modifier volumes (52 tris), W up to 1e9, 50 Hz mode.
Not tried yet: Strip Merge OFF and Vertex Arrays OFF (both live).
2026-10-08: the logs point to its bump maps (PixelFmt 4, TR list), which we
draw as opaque white. Fix item: 3j in section 3 (Matt). HW SUCCESS
2026-10-08, default ON (the 3j block above).

**Done: 5.38 JIT DIV32 (OPTIMIZATION_POTENTIAL P19; Matt 2026-10-06: "do
5.38"), HW SUCCESS, default FIXED since the package of 21:02 (Matt: "Yes,
let's put it as default").** Crazy Taxi, Sonic Adventure and Rayman 2
clean (5.38 "HW"). Built in the package of 19:40. EMULATION
-> "JIT Div32" (`JitDiv32`): OLD / FIXED, set before boot.
FIXED makes a matched division (`div0u`/`div0s` + 32 x `rotcl`; `div1`)
Flycast's 64/32 form in one's complement (`dc/sh4/rec_v2/div32_match.h`,
`ps3/ps3_jit_div32.h`), with one fix Flycast lacks: r1's top bit comes
from div0s's T (Flycast's `sar` is wrong for INT_MIN / 1). The matcher also
rejects `div1 Rn,Rn`, and the `[DIV]` probe skips sites below 0x8C010000.
PC: `misc/div32_check/run.sh` PASS, 2.48M checks; `mutate.py` 48 of 48
planted bugs fail; `constprop_check` PASS. The OLD forms on the same rows:
unsigned 64/32 93% wrong, the signed idiom 50%. HW steps in section 5
after 5.36-5.40. Start a UDP listener only when Matt says so.

**Merged 2026-10-07: the OPTIMIZATION_POTENTIAL bundles (Matt: "Merge the
bundles then on OPTIMIZATION_PLAN.md").** Nothing is built or scheduled.
Section 7 groups the open items into 12 bundles: items that share code and
checks, so one session can build them together. Each item keeps its own
toggle and its own HW run. Matt picks a bundle, then the items.
1. VRAM and TA-area writes (P43, P55, P7, the P26 watch): the only
   SH4-thread speed left (P43 ~1-3% in Sofdec FMVs, P55 0.5% or less;
   estimates).
2. GD-ROM and disc images (3h, the open 5.34 parts and its CD-DA misses,
   P22, P48, P9, the P23 GDI parser, the P57 GDI paths, P34): the most
   games fixed.
3. Texture path (P45, P52, P25, P1 steps 2-3): the most visible change
   (mipmaps).
4. TA decode and draw rules (P8, P27, P25, P10, P9).
5. AICA RAM coherency (6.6, P20 G2-G4, P31): the Crazy Taxi crackle.
6. G2 DMA (P24, P9).
7. Time base (6.4, P11, the P57 TMU test, P28).
8. SH4 interpreter, FPU and small JIT items (P46, P32, 5.3 `JitMac` and
   `bcache`, 5.37, 5.39, 5.40, the P57 RM verify).
9. ARM7 engines (P49, P40, 6.8).
10. Maple (P29, P9, P13, P37).
11. Output stage (P10, P53, P36, the P26 reads, P54).
12. HW test tools (P50, P51).

Not bundled: P1 steps 0-1 and P2 (no target left), P14 (3a, 3b, 3j), P42
(b)-(d) (5.12), P35 and P39 (section 4), P15, P38, P12, P30, P33, and
P31's nullAICA-only fixes (section 7, "Not bundled").

**Merged 2026-10-06: the OPTIMIZATION_POTENTIAL items that have a plan
goal (Matt: "Can you do the merge, then?").** Nothing is built. Each item
now sits at its goal with a short entry. The full text (evidence, file and
line numbers) stays in `OPTIMIZATION_POTENTIAL.md` under the same P number.
- Section 5: P41 is 5.2 (done); P3 = 5.34 and P6 = 5.35 from before. P17
  BCACHE and P46 + P32 (mac.w) in 5.3; P42 and P43 in 5.12; P20 in 5.13
  A3; P55 + P7 in 5.17; P15 in 5.19; P22 (DMA rules) and P9 (GD-ROM DMA)
  in 5.34. New rows: 5.36 (P5), 5.37 (P17 fschg), 5.38 (P19), 5.39 (P56),
  5.40 (P4).
- Section 6: P11 and P57 (TMU test) in 6.4; P24 and P31 (CA clamp) in
  6.5; P44, P40 and P49 in 6.8.
- Section 3: P14 in 3a/3b, P27 in 3c, P38 and P57 (RM verify) in 3d, P47,
  P21 and P23 (TOC fields) in 3h, P54 in the RTT note.
- Parked Soul Calibur faults (this section, "Parked by Matt 2026-10-03"):
  P16, P18, P1, P2, P51.
- Not merged (no plan goal): P8, P10, P12, P13, P25, P26, P28-P30,
  P33-P37, P39, P45, P48, P50, P52, P53, and the other parts of P9, P22,
  P23, P31, P32 and P57. 2026-10-07: now in the section 7 bundles, except
  P12, P30, P33, P35, P39 and P31's nullAICA-only fixes.

**Done: 5.34 GD-ROM Async (OPTIMIZATION_POTENTIAL P3) and 5.35 JIT EXIT
(P6), HW SUCCESS (Matt 2026-10-06: "SUCCESS for both then").** Default ON
since the package of 18:19 (Matt: "Keep toggles on"): GD-ROM Async at +
CD-DA, JIT Exit ON. Known, parked (Matt: "place the
loop bug in the known list ... we will catch up with it waaay later"):
with + CD-DA, a ring miss (song start or loop) still stalls the SH4 thread
24-85 ms; options: silence for the missed sector, or start the read-ahead
at the play command. Matt 2026-10-06: "Can we test P6 with another item of
my choice, then?", then "Go with P3". Built and PC tested, package of
17:35 (`build/notdc_ps3.pkg`); steps in section 5 after 5.2. HW
2026-10-06: Crazy Taxi gd read on the SH4 thread 50 -> 0 ms/s, 95% of
dense windows at full speed (70-77%); Matt: "very, very smooth"; Sonic
"ran better"; three clean XMB exits with the flash saved. ChuChu's CD-DA
ring missed after loops: one-line fix (Matt), package of 18:10. ChuChu
re-test: 99.4% hits (was 87%), but each of the 11 misses still stalls the
SH4 thread 24-85 ms (the known item above).
EMULATION -> "GD-ROM Async" (`GdAsync`: OFF / DATA / + CD-DA) and "JIT
Exit" (`JitExit`: OFF / ON), both default OFF, set before boot. 5.34: a
GD-ROM DMA read goes to a worker thread and the DMA waits for it in
emulated time, so the SH4 thread stops waiting on the disk (dense Crazy
Taxi: ~50 ms/s and the 330 ms dip, 5.32/5.33); + CD-DA also reads CD-DA
ahead. A `[gdr]` line gives the reads per caller at every level. 5.35: an
XMB exit returns from the JIT loop (no 3 s forced exit) and saves
`dc_flash_wb.bin` (Matt: "Save the flash only"). P6 was wrong on one point:
nothing calls `Term_DC()`, so `EMU_TERM` stays unrun. PC: `gdasync_check`
ALL PASS, 17 of 17 planted bugs fail (after the fix); `exit_check` PASS, 10
of 10. UDP: this session's listener (`ps3log_534_run1.txt`) was stopped
at Matt's request; port 18194 is free. Start one only when Matt says so.

**Done: 5.2 JIT Carry (Matt 2026-10-06: "I'd say to work on 5.2 since It's
a known issue."), default ON in the source since 2026-10-06 (Matt: "Sure"),
in the packages since 17:35.** The carry ops (addc, subc, negc, addv, subv, div1)
took CA and OV from 64 bits. The new forms in `ps3/ps3_jit_carry.h` use no
XER: 64-bit arithmetic on extended operands, T from bit 32 or from the
overflow test. EMULATION -> "JIT Carry" (`JitCarry`), set before boot. PC:
`misc/carry_check` PASS (1.6M checks), 39 of 39 planted bugs fail. HW
2026-10-06 (package of 09:16): both boots clean, one interpreter fallback
per boot. Crazy Taxi same load -0.4% (noise); Matt: "It looked the same".
Sonic Adventure FMV after boot (P41) +23% VPS (SPEED 69-71% -> 86-87%),
play equal. Matt: "Sonic felt choppier"; the log shows no cause in 5.2
(see 5.2 "HW"). An install keeps the settings now (`nullDC.cfg` is in
`/dev_hdd0/data/NotDC/`), so HW steps must list every option to check.
Found, not fixed: JitMac (5.3) has the same 64-bit carry defect.

**Active: Crazy Taxi at full speed (Matt 2026-10-05: "I'd like to get Crazy
Taxi in full speed").** With 5.34 DATA (2026-10-06): dense SPEED 100.0%,
95% of dense windows at full speed; Matt: "Crazy Taxi felt like full
speed". Before it, dense Crazy Taxi (`run528b.txt`): VPS 59.2, SPEED
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
scheduler (P5, now 5.36). Not useful now: a lower SH4 Clock (JIT IDLE SKIP makes the wait time
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

Other open items: the XMB exit no longer takes the "SH4 did not stop in
3 s" fallback (5.35 JIT Exit, HW SUCCESS, default ON since 18:19).
Known, parked (Matt 2026-10-06): 5.34's CD-DA ring misses at song starts
and loops stall the SH4 thread 24-85 ms each (section 0, 5.34).
JitCarry was broken on PS3; fixed in 5.2 (HW 2026-10-06).
Why the ARM7 JIT time saving did not show as SPEED: likely SMT sharing with
the render thread (5.6).

Parked by Matt 2026-10-03 (speed first, return later): Soul Calibur faults,
not caused by S3 (5.12 S3 runs 2-3).
- Background geometry flickers and is malformed, with S3 ON and OFF. It
  flickers more often when the render thread is slower. Not seen on
  2026-09-23 (before 5.8 Vertex Arrays and 5.10). First check: in the same
  fight, turn Vertex Arrays OFF, then Strip Merge OFF (both live). FIXED
  2026-10-07 by P18 Depth Float (Matt: "The flicker is gone!").
- One texture went black for about 1 s. With P16 ON (2026-10-06, Matt):
  "some black textures that often flickered". CLOSED 2026-10-07, after P18
  (Matt: "Soul Calibur's black flickering textures are closed.").
- Open (Matt 2026-10-06): Sonic Adventure draws wrong textures, the same
  before and after P16. Matt: "It's most likely tied to whatever Soul
  Calibur's issue is." FIXED 2026-10-07 by P18 Depth Float (Matt: "Sonic's
  wrong textures were tied to Soul Calibur's issue! So IT IS fixed for it
  too!").
- Fight load before P16: texture decode 82-85% of wall (~15 re-decodes and
  4.7 MB hashed per render, mostly pal4/pal8), VPS ~26 of 50, half the
  renders dropped. With P16 ON: full speed, decode 0.2-1.0% of wall.
- S2 compare: the SPU writes min_w = 0 where the PPU keeps a denormal
  (`wden`); stats only, but it stops the compare of those fences.
- Leads merged 2026-10-06 from `OPTIMIZATION_POTENTIAL.md`, in its order.
  One change per HW run, each behind its own toggle, default OFF:
  1. P16, texture cache slots collide (~3 lines): the slot depends only on
     the low 10 bits of the TCW, so textures 8 KB apart with the same size
     and flags share one 8-slot window. In the fight every decode is a
     cache miss with only 88 textures live (fits eviction; measure). The
     opening has the same misses. Measure the fight again after it: it can
     change the facts of P1 and P2. Done 2026-10-06 as "Tex Slot Hash", HW
     SUCCESS, default ON since the package of 23:46 (top of this section).
     New facts for P1 and P2: in the fight, decode is 0.2-1.0% of the render
     thread (was 82-85%), hashing 16-17%, no renders dropped.
  2. P18, depth precision, option (a): RSX float depth with z = n/W (~15
     lines, no shader). Today the 1/W step is 6-75% of the value at the
     stage's W (500-2500), so the background z-fights: the probable cause
     of the flicker. Done 2026-10-07 as "Depth Float", HW SUCCESS (the
     flicker is gone), default ON since the package of 01:01 (top of this
     section).
  3. P2 step 1 (no code): Frame Drop DROP against the default in one
     fight. P1 step 0 (split the re-decodes into palette and texel
     changes), if still needed after P16.
  4. P1 steps 1-3: the palette snapshot at STARTRENDER (the render thread
     reads the live `PALETTE_RAM` today; this race and 6.7 are the
     black-texture suspects), block decoders, 16-bit uploads. Then P2
     step 2 (one pending slot), if step 1 shows the trade.
  - P51, screenshot to PNG (~60-80 lines): pixel evidence for the flicker.
  - 2026-10-07 (section 7): P1 steps 0-1 and P2 have no target left. P1
    steps 2-3 are in bundle 3, P51 is in bundle 12.
  - 2026-10-10: Matt dropped P1 steps 2-3 (bundle 3, top of this section).

| Item | Status |
|---|---|
| 2.1 YUV reset on STARTRENDER | `[x]` HW, no visible change |
| 2.2 AICA EG rate + loop fixes | `[x]` HW, echo unchanged, no regression |
| 2.3 DSP | `[x]` done as 5.13 A1b + A3 (SPU DSP recompiler), HW SUCCESS 2026-10-07, default ON; P20 G2 (DSP Ring RAM) ran on HW 2026-10-11 with no fault (PSO), default ON since the package of 15:02; G3, G4 built, rows OFF, HW not run (section 0) |
| 2.4 Strip merge + inf drop | `[x]` HW, default ON, ~3x fewer draws |
| 2.5 ARM7 clock (`arm_sh4_bias` 20) | moved to section 6 (6.1-6.3) on 2026-10-04; now 6.3, `[ ]`, section 6 is parked |
| 3c modifier volumes (Flycast GL) | `[x]` HW; P27 (TR-list volumes) built 2026-10-11 as "TR Volumes", default OFF, no HW test yet (bundle 4, section 0) |
| 3d background depth bias | `[x]` HW, no regression; the WinCE games need P38 (merged, open); the P57 RM verify is in bundle 8 (section 7), built 2026-10-11 as the row JIT RM 2/3, default VERIFY, no HW run |
| 3e background vertex order | dropped, our code has no such fault |
| 3f NaN colour = 255 | `[x]` HW, no regression |
| 3g GD-ROM CD-DA 2352 + zero-fill | `[x]` HW, no regression |
| 3i region / cable from IP.BIN | `[x]` HW, no regression, switch path unexercised |
| 3a fog, 3b offset colour, 3j bump maps | 3a built 2026-10-12 as the "Fog" row, HW SUCCESS 2026-10-12 (Ecco the Dolphin), default TABLE in source since 2026-10-12 (Matt), not packaged (section 0); 3b OPEN: needs the offset colour in every vertex; reference P14 (merged); 3j added 2026-10-08 (Rayman 2), built as design A (bump programs, PPU path), `[x]` HW SUCCESS 2026-10-08, default ON (section 0) |
| 3h GD-ROM CDDA lead-out / restart rules | pause rule `[x]` HW 2026-10-03; lead-out / restart: P47, P21, P23 (TOC fields), P61, P62 built 2026-10-10 in bundle 2 (section 0), HW SUCCESS 2026-10-10, default ON in source |

Section 3 correctness (3a, 3b, 3h) is SHELVED by Matt on 2026-09-23. Active
work is section 5, SH4-side speed. Update 2026-10-12: 3a (fog) is built
(Matt asked for it after bundle 9; section 0), HW SUCCESS 2026-10-12.

All changes are uncommitted in the working tree. Matt commits.

---

## 1. Work order

| # | Item | Size | Gain |
|---|---|---|---|
| 1 | YUV converter reset on STARTRENDER | 1-2 lines | FMV correctness |
| 2 | AICA `EG_EffRate` and loop fixes | small | sound correctness, maybe the ChuChu echo |
| 4 | Strip merging + inf-vertex drop | large | draw calls in dense scenes |
| 3 | DSP - done as 5.13 A1b + A3 (SPU DSP recompiler), HW SUCCESS 2026-10-07 | large | DSP effects (reverb, echo) |
| P | ARM7 clock (`arm_sh4_bias`) - moved to 6.3 on 2026-10-04 (section 6, parked) | small | ChuChu echo, slow SFX |

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

### 2.3 DSP  `[x]`  done as 5.13 A1b + A3 (SPU DSP recompiler), HW SUCCESS 2026-10-07

- 2026-10-07: done. 5.13 A3 (the DSP in the SPU sound) is HW SUCCESS and
  default ON with SPE DSP JIT ON since the package of 03:41. Left: P20
  G2-G4: built 2026-10-11 in bundle 5. G2 (DSP Ring RAM) ran on HW with no
  fault, default ON since the package of 15:02; G3 and G4: rows OFF, HW not
  run (section 0).
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

### 2.5 ARM7 clock  moved to 6.3 on 2026-10-04 (`[ ]`, section 6 is parked)

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
| 3k | Translucent sort per triangle | An autosorted translucent list draws in the right order inside a model and across large surfaces (Sonic Adventure models, Rayman 2 water) with no depth write. | The PPU cuts the strips into triangles, sorts them (radix) and draws them in runs; the SPU builder skips the list. Medium. | `core/hw/pvr/ta_util.cpp` `sortTriangles`, `gldraw.cpp` `drawSorted` |
| 3j | Bump maps | Rayman 2 level surfaces show their textures again. Today its bump-map overlay covers them with plain vertex colour. No other test game uses bump maps. | Bump texels decode as raw 4444 (S, R) on the PPU and the SPU; the decoders keep the offset colour (K1, K2, K3, Q) per vertex (3b's work); a bump Cg fragment program. Shares the shader work of 3a and 3b. Medium. | `core/rend/dx9/d3d_shaders.cpp` `pp_BumpMap`, `core/rend/texconv.cpp` `texBMP_TW` |

Status 2026-09-23 (all big-endian checked):
- 3d `[x]` built. `draw_scene()` subtracts 1e-6 from the background 1/W, floor
  1e-11, as Flycast `FillBGP`. We keep nullDC-v1's 28-bit mask; Flycast dropped
  it, the difference is below the bias. The register is a native u32 on BE, so
  the `memcpy` to float is exact.
  CORRECTION to the gain above: the Windows CE games need full SH4 MMU
  emulation, and ours has none (`dc/mem/mmu.cpp` only remaps the store queue).
  They cannot boot here yet, so their FMV fix does not apply. What is left:
  Xtreme Sports skybox, Blue Stinger (JP) intro.
  Merged 2026-10-06 (open): the SH4 MMU for these games is
  `OPTIMIZATION_POTENTIAL.md` P38 (an interpreter form first, ~1.3-1.6K
  lines with longjmp; a JIT form is very high risk). P57:
  `verify(fpu_cfg.RM<2)` in `dc/sh4/rec_v2/decoder.cpp` hangs the JIT when
  a WinCE game sets RM = 3; Flycast removed it (1 line). 2026-10-07: P57's
  RM verify is in bundle 8 (section 7); P38 stays here. 2026-10-11: built
  as the row JIT RM 2/3 (VERIFY / ACCEPT, default VERIFY; section 0), no
  HW run (no game that boots here sets RM 2 or 3).
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
  - Merged 2026-10-06 (open): `OPTIMIZATION_POTENTIAL.md` P27, Flycast dev
    `f2013c652`. When a pass shares the opaque and translucent volume
    pointers (region-array words +8 and +16) and the TR list has more
    volumes, Flycast draws the TR list's volumes (Xtreme Sports shadows).
    Ours drops list 3 at decode, on the PPU and on the SPU. ~30 lines in
    both decoders. 2026-10-07: in bundle 4 (section 7). 2026-10-11: built
    as the "TR Volumes" row, default OFF, no HW test yet (section 0).

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
  - Broadcast is not touched, as in Flycast. Corrected 2026-10-08 (P59):
    Flycast writes the broadcast byte at each boot. Since 2026-10-13 the
    row Broadcast does it (bundle 15, section 0), default CONSOLE.
  - Log: `[flash] region X fits` / `[flash] region X -> Y for disc area '...'`.
- HW test 3g/3i: boot Crazy Taxi and Soul Calibur. Read the `[flash]` line.
  Both must still boot. NBA Hoopz only if Matt has it.
- HW 2026-09-23: "still boots correctly" (Matt). Log: Crazy Taxi read
  `[flash] region 1 fits disc area 'JUE     '`. So Matt's flash region is USA
  (1), not EU as assumed earlier. No switch happened on that disc, so the
  switch path itself is still unexercised on hardware. NBA Hoopz not run.
- 3h, merged 2026-10-06 from `OPTIMIZATION_POTENTIAL.md` (open):
  - P47: REQ_STAT position bytes. Ours copies `cdda.CurrAddr.B0`-`B2` from
    a union, so on our BE host the guest reads FAD >> 8 (FAD 45150 reads
    as 176). Flycast `e5344d37d` (one of the two 3h commits) sends
    `curFad >> 16`, `curFad >> 8`, `curFad`, and the track, control/ADR
    and index 1 from the TOC. ~8 lines with shifts, own toggle. Change
    GET_SCD only after a HW test: it is tuned for the BIOS CD player and
    the ChuChu pause fix.
  - P21: step 1, SPI_CD_READ stops CD-DA (1 line, plus PLAY -> PAUSE in
    the status that GET_SCD reads; Hydro Thunder, NFL Blitz 2001). Step 2,
    Flycast dev status timing (PLAY until 67 ms after a data read, SEEK for
    20 ms, BUSY for 5 or 20 ms): ~60 lines, medium risk. Its
    resume-only-when-paused rule needs a 3-state CD-DA flag (overlaps 3h).
  - P23, TOC fields: ADR=1 in every entry, the lead-out with ADR=1 and
    CTRL=4, the single-density lead-out at the end of track 2 + 1 (ours,
    GDI: fixed at 13085). V-Rally 2 CD-DA tracks, Eldorado Gate. Flycast's
    other 3h commit, `9ea0563a7`, changes the same files
    (`core/imgread/common.cpp`, `common.h`).
  - HW check: ChuChu Rocket and Looney Tunes CD-DA, the BIOS CD player,
    Crazy Taxi loading.
  - 2026-10-10: built in bundle 2 (section 0): "GD Status" (P21), "GD
    Position" (P47), "TOC Fields" (P23), with "CD-DA Rules" (P61) and
    "GET_SCD Q Data" (P62). HW SUCCESS 2026-10-10 (ChuChu Rocket, Crazy
    Taxi), all default ON in source (Matt).
  - 2026-10-07: 3h is part of bundle 2 (section 7), with the other GD-ROM
    items and the parked 5.34 CD-DA misses.
- 3j, added 2026-10-08 (Matt: "Sure, add the bump mapping to one of
  them"), open. Full references: `OPTIMIZATION_POTENTIAL.md` P14.
  - Evidence, logs only (no HW test yet): the 5.38 Rayman 2 runs
    (`run538_rayman_fixed.txt`, `run538_rayman_old.txt`, scratchpad of
    session 2b073cf1). Bump-map polygons (PixelFmt 4) start with the first
    level; the intro and menus have none. 14-167 per frame, all in the TR
    list, SRC_ALPHA / INV_SRC_ALPHA, modulate alpha, vertex alpha often
    177-255; `[texfmt]` shows 2 bump textures. In the 152 saved HW logs no
    other game loads a bump texture (Crazy Taxi, Sonic Adventure, Soul
    Calibur, ChuChu Rocket, Looney Tunes, Power Stone 2, Space Channel 5,
    CvS2, Border Down, Quake3).
  - Today: `cvt16()` in `ps3/ps3_tex.cpp` and `planes16()` in
    `ps3/spu/spe_pix_job.h` turn every bump texel into opaque white, and
    the decoders drop the offset colour (`AppendPolyVertex3/4` keep only
    `BaseCol`). So the TR overlay draws its vertex colour at near-full
    alpha over the textured surface: the open Rayman 2 fault (section 0).
  - DC rule (Reicast and Flycast agree): the texel holds S (high byte) and
    R (low byte); texel alpha = clamp(offs.a + offs.r * sin(S) + offs.g *
    cos(S) * cos(R - 2 pi * offs.b)), texel RGB = 1. A bump polygon does not
    add its offset colour.
  - Parts: (1) fmt 4 as raw 4444 in `cvt16()` and `planes16()` (Flycast
    `texBMP_TW` = `tex4444_TW`). (2) The offset colour per vertex:
    `DcVertex`, `SpeVtx` (`ps3/spu/spe_bell.h`, 24 -> 28 bytes), both
    decoders, the SPE Build vertex words (`vtx_words()` in
    `ps3/spu/spe_vb_build.h`), a second colour attribute in ps3gl. (3) A
    bump Cg fragment program. (2) and (3) are 3b's work.
  - Own toggle, default OFF until HW validated. Checks: PC first; the S2
    compare stays bit-exact; `misc/spepix_check` for the fmt 4 decode. HW:
    the Rayman 2 first level.
  - 2026-10-08: built as design A (Matt's pick, section 0): bump scenes go
    to the PPU decoder and draw, so parts (1)-(3) need no SPU change; the
    offset colour lives in `TaContext.offs`, not in DcVertex. HW SUCCESS
    2026-10-08 (Matt), default ON in source.

- 3k, added 2026-10-11 (Matt, at the bundle 4 verdict: "Build it next"):
  built as the "TR Triangle Sort" row. HW run 2: ON alone does not fix
  Sonic Adventure; two more levels built (+ DEPTH WRITE, + OPAQUE DEPTH).
  `[x]` HW SUCCESS 2026-10-11 at + OPAQUE DEPTH (Sonic Adventure models,
  Rayman 2 water and elements), default + OPAQUE DEPTH in source (Matt),
  not packaged (section 0).

- 3a, built 2026-10-12 (Matt: "Work on those, in case") as the "Fog" row
  (OFF / TABLE / + VERTEX; built with default OFF): table fog per pixel on the PPU
  and SPU paths, vertex fog on the PPU path. Not as the table above says:
  the table is a 512 x 4 texture in log2(z), so the program needs no
  floor and no pow. HW SUCCESS 2026-10-12 (Ecco the Dolphin, Matt:
  "Everything looked right!"); default TABLE in source since 2026-10-12
  (Matt: "Let's set it to TABLE."), not packaged (section 0).

Already covered, drop from the list: "do not clip negative-depth vertices"
(`4b0d377e6`). Our `vertex_inf()` never treats a negative 1/W as inf.

None of section 3 is a speed gain. 3a-3c share one piece of work: a real PVR
shader for `ps3gl`, which replaces the fixed-function texture modes.
Merged 2026-10-06: the shader reference for 3a and 3b is
`OPTIMIZATION_POTENTIAL.md` P14: Reicast's GLES shader, and Flycast's dx9
shaders (the closest to Cg; the fog table is a 128x2 A8 texture). ps3gl
already draws with precompiled Cg fragment programs, one per texture mode,
so fog and offset colour mean more programs and a second colour attribute.
3j (bump maps, 2026-10-08) needs the same: 3b's offset colour and one more
program.

RTT test discs (for `rtt=0/0`): Flycast `core/emulator.cpp:64-157` lists games
that need RTT copy to VRAM - Tony Hawk 1/2, Shenmue, Jet Set Radio, Skies of
Arcadia, Grandia II, Worms World Party, Silent Scope.
Merged 2026-10-06 (open): `OPTIMIZATION_POTENTIAL.md` P54, the RTT pixel
packing for those games: the 1555 alpha threshold from FB_W_CTRL (ours: a
fixed 128), rounding (ours truncates), the clip origin, alpha in pack mode
6, writers for modes 4 and 5. ~15 lines plus the two modes, endian-neutral.
2026-10-07: P54 is in bundle 11 (section 7).

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
| 5.7 | Draw split (`[rend] draw split %:` line) | `[x]` HW 2026-09-23 with 5.8: dense draw ~42% of wall (vtx+other ~22, tex bind ~12); the line is always on |
| 5.8 | Vertex arrays for merged strips (VIDEO / DEBUG, `VertexArrays`) | `[x]` HW, dense VPS +6%, busy 61 -> 57%, default ON |
| 5.9 | `collect_diag()` once per stats window | `[x]` HW, busy 57 -> 47%, VPS unchanged |
| 5.10 | `ps3tex_bind` cost (~12% of wall) | `[x]` HW, faster hash: hash 8.2 -> 1.9%, busy 47 -> 40%, no stale textures |
| 5.11 | TA decode writes the final vertex format | REVERTED 2026-10-03 (Matt): no net gain, felt worse |
| 5.12 | SPE offload: TA decode at submit time, then vertex build, texture hash | SHELVED 2026-10-03 (Matt) after S0 `[x]` HW; resume at S1 after 5.9-5.11; merged 2026-10-06: P42 (list #4, YUV decode), P43 (converter, PPU form); P43 in bundle 1 (section 7); #2 SPE Build (vertex buffer + runs on an SPU, Matt 2026-10-07), VIDEO / DEBUG "SPE Build" OFF / ON / CHECK, HW runs 1-2 correct but too slow (8-12 ms per build); SPU rewrite (Matt) package of 17:41, HW tested: correct, builds 1.2-2.5 ms, render thread -13 to -15 points; default ON since package 18:36 (Matt) (section 0); #3 SPE Tex Hash (texture hashes on a fifth SPU, Matt 2026-10-07: "Own SPU"), VIDEO / DEBUG "SPE Tex Hash" OFF / ON / CHECK; built + PC tested, package of 20:42; HW SUCCESS 2026-10-07 (Matt): render-thread hash 3.0 -> 0.15% (Crazy Taxi dense), 1.95 -> 0.01% (Looney Tunes races), no texture errors; default ON since the package of 22:35 (Matt) (section 0) |
| 5.13 | AICA on an SPE: control pass on the PPU, sample work + DSP on the SPE (caique model) | A0 `[x]` PC; A1 HW tested (bad=0, SPU DSP too slow); A1b DSP recompiler `[x]` HW (0 mismatches, DSP 90% -> 4-6% of the SPU); A1c cheaper PPU control pass `[x]` HW (ppu ctl 11.2 -> 5.8% in game, dense VPS 45.5 -> 48.8); A2 SPU sound `[~]` HW tested: Crazy Taxi OK; ChuChu SPU overload at 22-25 voices; Looney Tunes no sound (fixed 2026-10-07: A3 with 6.5, P44, P31); A2 high voices `[x]` HW 2026-10-04 (ChuChu stage SPU 95-101% -> 29%, ppu ctl 21% -> 8.9%, SPEED ~99%; Crazy Taxi dense VPS 49.3 -> 52.5); open: small crackle at full speed with SPE AICA Sound (cause found 2026-10-04, fix is 6.6); A3 DSP in the SPU sound `[x]` HW SUCCESS 2026-10-07 (Matt), default ON with SPE DSP JIT ON since the package of 03:41 (P20 G1; Looney Tunes' voices go through the DSP only); merged 2026-10-06: P20 (gaps G2-G4 open); G2-G4 in bundle 5 (section 7) |

| 5.14 | Sound thread: ARM7 + AICA on a second PPU thread (AUDIO "Sound Thread", `SoundThread`) | `[!]` HW 2026-10-04: works, but dense VPS 52.9 -> 51.6 (SMT slows the SH4 ~10%); Matt chose 5.15 |
| 5.15 | SPE ARM7: ARM7 + nullAICA registers + caique control twin on an SPU (AUDIO "SPE ARM7", `SpeArm7`) | `[x]` HW, default ON since the package of 23:22 on 2026-10-05 (Matt); HW 2026-10-04: Crazy Taxi dense VPS 52.9 -> 54.7, ChuChu stage 100%, Sonic Adventure fine; the Crazy Taxi popping is 6.6 (old path pops too); clean-up package 19:03 |
| 5.16 | SPE Draw check with SPE ARM7 ON (existing option, one boot) | `[x]` HW 2026-10-05: same load +0.5% (10K+ bins +2.4% to +5.0%), render busy 56 -> 32%, 80% of dense windows at full speed; SPE Draw default ON since the package of 20:13 (Matt); SPE Wait Yield (db16cyc in the fence spin) HW: no gain, default OFF |
| 5.17 | TA store off the SH4 thread (`ta` 6.0% of wall in dense Crazy Taxi) | Steps 1-3 `[x]` HW, TA SQ Direct and JIT SQ STORES default ON since the package of 23:22 on 2026-10-05 (Matt); Step 1 probe HW 2026-10-04: SQ path ~252 ns per TA flush, 9.5% of wall (dense, SH4 200), call chain 47%; Step 2 option 1 ("TA SQ Direct", call-chain fix) HW 2026-10-04: dense VPS 48.4 -> 50.5 (+3.2% same load), no faults, default OFF; Step 3 (Minicast M2, "JIT SQ STORES") HW 2026-10-04: dense VPS 51.5, +4.2% same load vs Step 2 (+6.7% vs Step 1), log clean, default OFF; merged 2026-10-06, open: P55 + P7 (VRAM writes from SQ and DMA); bundle 1 (section 7) |
| 5.18 | JIT hot-block profile in dense Crazy Taxi, then fix the hottest generated code | `[x]` measured: Step 1 built + PC tested 2026-10-04 (VIDEO / DEBUG "JIT HOTBLOCKS", default OFF), package of 23:13; Step 2 HW 2026-10-04: ranking in 5.18 "HW" (wait loop 12.4% of JIT instructions, real load-hit-stores ~6M/s, dynamic ends 2.6M/s, `mfcr` 3.5M/s, missed SQ stores 0.88M/s); fixes are their own items (5.25 first) |
| 5.19 | SH4 Clock below 150 MHz (speed trade, last resort) | `[ ]`; merged 2026-10-06: P15 (freerunning, a dynamic form) |
| 5.20 | Minicast M1: ftrv with known w + interleaved MAC chains | `[!]` blocked 2026-10-07 (Matt): the Soul Calibur check and the known-w pass; HW 2026-10-04 ("JIT FTRV", bit-exact form, no known-w pass): Crazy Taxi dense VPS 53.5, +3.8% same load vs 5.17 Step 3, log clean |
| 5.21 | Minicast M3 + M4: constant propagation (no memory baking) + dead value elimination | `[x]` HW, correct, no gain, default OFF; built + PC tested 2026-10-04 ("JIT CONSTPROP", "JIT DEAD VALUES", default OFF; fuzzer PASS 1.6 M blocks, 31/31 mutants fail), package of 22:06; HW 2026-10-04: Crazy Taxi log clean, SHIL ops -2.9%, same-load VPS +0.2% (noise); geometry and sound right (Matt) |
| 5.22 | Minicast M5: cycle check out of line, then only on backward branches | `[x]` HW, LOOPS ONLY default since 2026-10-04 (Matt); built 2026-10-04 ("JIT CYCLE CHECK": OLD / COLD EXIT / LOOPS ONLY, default OLD), package of 22:27; HW 2026-10-04: logs clean, same-load VPS +0.8% (COLD EXIT), +1.9% (LOOPS ONLY) against 5.21 |
| 5.23 | Minicast M6: `fmov.s @Rn+` pair fusion | `[ ]` not scheduled (low) |
| 5.24 | PPE codegen seen in the review (not Minicast): `mfcr` -> `mfocrf`, int/float moves through memory | `[x]` HW, default ON since the package of 13:49 (Matt); built + PC tested 2026-10-05 (Matt: "Go with 5.24"): JIT SPEED "JIT MFOCRF" and "JIT FPUL MOVES"; ppe_check PASS, 22/22 mutants fail; package of 13:01; HW 2026-10-05 (JIT SQ MISS + LOOK-AHEAD ON too): Crazy Taxi log clean, same-load VPS +1.2% vs `run527b` (same settings without 5.24, about the noise); Sonic Adventure clean, speed equal within noise (95% of dense windows at full speed) |
| 5.25 | Wait-loop idle skip (Crazy Taxi 0C15A658-0C15A67E, from 5.18) | `[x]` option a + safety (Matt 2026-10-05), JIT SPEED "JIT IDLE SKIP", default OFF; HW 2026-10-05 (package 00:45): Crazy Taxi +3.0% same-load VPS, Sonic Adventure 30 -> 20 frames/s (work loop skipped); fix with a register-hash check, package of 01:13: HW Crazy Taxi +3.0% same-load VPS (dense 57.1), work loops no longer skip; Sonic not rechecked; default ON since the package of 01:27 (Matt) |
| 5.26 | T for `bt.s`/`bf.s` kept in a register (the jcond load-hit-store, ~1.9M/s, from 5.18) | `[x]` HW, no gain, default ON since the package of 10:13 (Matt); option a (Matt 2026-10-05), JIT SPEED "JIT T COPY", default OFF; built + PC tested 2026-10-05 (tcopy_check PASS, 18/18 mutants fail), package of 09:28; HW boot 1 2026-10-05 (Crazy Taxi): clean, miss=0, same-load VPS -0.1% (no measurable gain); Sonic Adventure clean (30 fps outside heavy scenes); Soul Calibur clean (render-bound as before) |
| 5.27 | Store-queue stores JIT SQ STORES misses (0.88M slow-path entries/s, from 5.18) | `[x]` HW, default + LA STEPS since the package of 15:49 on 2026-10-10 (5.43, Matt); before: default OFF (Matt 2026-10-05: "It's fine for now"); both options (Matt 2026-10-05): JIT SPEED "JIT SQ MISS" OFF / SLOW PATH / + LOOK-AHEAD, default OFF; built + PC tested 2026-10-05 (sqstore_check and sqla_check PASS; 12/12 and 26/26 mutants fail), package of 11:19; HW 2026-10-05 (Crazy Taxi): logs clean, same-load VPS +1.7% (SLOW PATH), +0.9% (+ LOOK-AHEAD, best in heavy scenes; Matt: "much better") against `run526a` |
| 5.28 | Larger SH4 code cache (6 MB was a Wii size; Sonic Adventure refills it every 44-55 s) | `[x]` HW, default 24 MB since the package of 19:08 (Matt); built 2026-10-05 (Matt picked it): EMULATION "JIT Code Cache" 6 MB / 12 MB / 24 MB, array 24 MB, `bl` reach 27549 KB of 32768; package of 17:08; HW 2026-10-05 at 24 MB: Sonic Adventure no full clear in 157 s of play (6 MB: 3), cache 7.8 MB, dense SPEED 99.7% (99.6%); Crazy Taxi same-load VPS -0.2% (noise) |
| 5.29 | JIT FRCHG: an FPSCR sync that only flips FR swaps the pinned bank inline (~140K full syncs/s in dense Crazy Taxi) | `[x]` HW, no gain, default OFF; built + PC tested 2026-10-05 (frpref_check PASS, 23/23 mutants fail), VIDEO / DEBUG "JIT FRCHG"; package of 20:40; HW 2026-10-05 (one boot with 5.30): clean, sync_fpscr 187K/s -> 2.4K/s, same load -0.6% against `run516` (noise) |
| 5.30 | JIT PREFETCH: a `pref` outside the store queue prefetches its RAM line with `dcbt` | `[x]` HW, no gain, default OFF; built + PC tested 2026-10-05 (same check), VIDEO / DEBUG "JIT PREFETCH"; package of 20:40; HW with 5.29 (one boot): clean, 174 sites, no gain; whether it costs a little is open |
| 5.31 | JIT GUARD REUSE: an access skips the RAM or SQ check its base register passed earlier in the block (a CR flag) | `[x]` HW, no gain, default OFF; built + PC tested 2026-10-05 (greuse_check PASS, 25/25 mutants fail), VIDEO / DEBUG "JIT GUARD REUSE"; package of 21:50; HW 2026-10-05: clean, 20994 reuse sites, same load +0.1% against `run516` (noise) |
| 5.32 | Stall probe: the SH4 thread's slowest frame each second, split by cause (rides on JIT C-Calls) | `[x]` HW 2026-10-05 (package of 22:22, `runstall_ct.txt`): the Crazy Taxi dip is one 330 ms disc read; dense play ~61 ms/s in disc reads |
| 5.33 | CDI batched reads: one seek and one read per run of up to 32 sectors (was one per sector) | `[x]` HW, small gain, kept (Matt: "It's okay"), follow-ups parked; built + PC tested 2026-10-05 (cdi_check PASS, 14/14 mutants fail), package of 22:38; HW 2026-10-05: Crazy Taxi and Sonic Adventure clean; Crazy Taxi read time per request -7%, dense 60.9 -> 49.9 ms/s, the 330 ms read stays; same load +0.7% (noise) |
| 5.34 | GD-ROM Async (OPTIMIZATION_POTENTIAL P3): DMA reads on a worker thread, + CD-DA read-ahead | `[x]` HW SUCCESS 2026-10-06 (Matt), default + CD-DA since the package of 18:19 (Matt); CD-DA miss stalls known, parked; built + PC tested 2026-10-06 (gdasync_check ALL PASS, 17/17 mutants fail), EMULATION "GD-ROM Async" OFF / DATA / + CD-DA, default OFF; package of 17:35; HW 2026-10-06: DATA Crazy Taxi gd read on the SH4 thread 50 -> 0 ms/s, full-speed dense windows 70-77% -> 95% (Matt: "very, very smooth"), Sonic "ran better"; + CD-DA missed after loops, fixed in the package of 18:10: ChuChu 99.4% hits, 11 misses of 24-85 ms each; merged 2026-10-06, open: P22 (DMA rules), P9 (GD-ROM DMA clamp); with the CD-DA misses in bundle 2 (section 7) |
| 5.35 | JIT EXIT (P6): an XMB exit returns from the JIT loop, the flash is saved | `[x]` HW SUCCESS 2026-10-06 (Matt), default ON since the package of 18:19 (Matt); built + PC tested 2026-10-06 (exit_check PASS, 10/10 mutants fail), EMULATION "JIT Exit", default OFF; package of 17:35; HW 2026-10-06: four clean XMB exits, the flash saved and loaded on the next boot |
| 5.36 | TA per-record counters + deadline scheduler (OPTIMIZATION_POTENTIAL P5; section 0 "left from the list") | SYS GATE `[x]` HW SUCCESS (Matt), default + RESUME since the package of 22:52 (Matt); TA Peak Frame `[!]` blocked 2026-10-07 (Matt), not run on HW, default OFF; built + PC tested 2026-10-06 (Matt: "5.36", then "Gate + Resume levels" and a TA peak toggle): JIT SPEED "JIT SYS GATE" OFF / GATE / + RESUME, VIDEO / DEBUG "TA Peak Frame", both default OFF; sysgate_check PASS, 30/30 mutants fail; package of 22:27; HW 2026-10-06: 3 boots clean, updates 112K -> 28.4K/s, lookups 116K -> 32.6K/s (+ RESUME), dense Crazy Taxi full-speed windows 86.3% -> 94.5%, ~2-3% more spare time (Matt: "much smoother"); the per-slice work itself (`[stall]` sys 63-79 ms/s) did not fall |
| 5.37 | `fschg` without a sync or a block end (P17; the "FPSCR 0.22M/s" load-hit-store of 5.18) | `[x]` HW SUCCESS 2026-10-10; built + PC tested 2026-10-10 (Matt: "Resume with the next steps."; Looney Tunes race Step 2, candidate 4): JIT SPEED "JIT FSCHG" has a 3rd value "+ NO BLOCK END" (`JitFschg=2`), default was ON (level 1); fschg_check PASS (11/11 mutants fail), frpref_check PASS (29/29); package of 14:26; HW run 2026-10-10 with the four Step 2 settings ON together: logs clean, sleep +36 ms/s in the Looney Tunes race and +63 ms/s in Crazy Taxi at 6K+ (not split per setting); HW SUCCESS (Matt: "everything looked like before"); default ON since the package of 15:49 on 2026-10-10 (Matt) (section 0); merged 2026-10-06; bundle 8 (section 7) |
| 5.38 | div32 match: 64/32 and signed divides (P19, bug), next to 5.2 | `[x]` HW SUCCESS (Matt), default FIXED since the package of 21:02; built + PC tested 2026-10-06 (div32_check PASS, 48/48 mutants fail), EMULATION "JIT Div32" OLD / FIXED; package of 19:40; HW 2026-10-06: Crazy Taxi, Sonic Adventure and Rayman 2 clean, same load -0.0% / -0.1%; Rayman 2's untextured level is the same with OLD (separate issue); merged 2026-10-06; the BIOS boot animation, Sonic Adventure and Rayman 2 run matched divisions (Crazy Taxi's game code none) |
| 5.39 | fmov, flds, fsts and fneg in the JIT when PR=1 (P56) | `[~]` built + PC tested 2026-10-11 (bundle 8, section 0): JIT SPEED "JIT PR MOVES" (`JitPrMoves`), default OFF; fmov, flds and fsts, fneg left out (P32 not done); package of 22:43; HW run 15, 2026-10-12 (Quake3.cdi): 20 sites compiled, no fault in the log, no word from Matt on the image; the row stays OFF; HW run 5 (Looney Tunes) compiled no PR=1 move, that game has none; the PR=1 games in the old logs are Space Channel 5, Quake3.cdi and Rayman 2; merged 2026-10-06 |
| 5.40 | FPSCR.DN -> PPC NI bit (P4) | `[x]` closed 2026-10-11, no effect (Matt: "You can close 5.40."): the `[ni]` probe on HW shows that NI = 1 changes no value and no time on the PPE (section 0); built 2026-10-11 (bundle 8): JIT SPEED "FPU DN" (`FpuDn`), default OFF, never run ON; the row removed from source 2026-10-12 (Matt: "Since it doesn't matter anymore, remove FPU DN row."), the `[ni]` probe still there; the package of 22:43 has the row; merged 2026-10-06 |
| 5.41 | VBlank pacing: a 30 fps game's frames reach the screen 1 or 3 refreshes apart (Sonic Adventure "choppy" at SPEED 100%) | `[x]` done 2026-10-08, no felt change (Matt: "It seemed the same, actually. But It's fine."), Frame Hold default OFF; step 1 a `[vid]` refresh count, built 2026-10-07, package of 22:56; HW 2026-10-07: Sonic judder confirmed (1/3 pairs for ~85 s of each ~190 s drift cycle), Looney Tunes mostly the game's own 2/3 cadence, Crazy Taxi clean; step 2 (b) Frame Hold (Matt's pick) built + PC tested 2026-10-07, package of 23:42; HW 2026-10-07: Sonic only 2s at RPS 30 (1 drift slip), Crazy Taxi unchanged, ~23 ms more delay (section 0) |
| 5.42 | SYS GATE BATCH: the slices the gate skipped run as one step per module, the TMU plan in closed form (Looney Tunes race Step 2, candidate 1) | `[x]` HW SUCCESS 2026-10-10; built + PC tested 2026-10-10 (Matt: "Resume with the next steps."): JIT SPEED "SYS GATE BATCH" (`SysGateBatch`); sysgate_check PASS in 3 modes (63/63 mutants fail); package of 14:26; HW run 2026-10-10 with the four Step 2 settings ON together: logs clean, sleep +36 ms/s in the Looney Tunes race and +63 ms/s in Crazy Taxi at 6K+ (not split per setting); HW SUCCESS (Matt: "everything looked like before"); default ON since the package of 15:49 on 2026-10-10 (Matt) (section 0) |
| 5.43 | JIT SQ MISS "+ LA STEPS": the look-ahead passes `@-Rn` stores and `add #imm,Rn` on the store's base register (candidate 2) | `[x]` HW SUCCESS 2026-10-10; built + PC tested 2026-10-10: 4th value of JIT SQ MISS (`JitSqMiss=3`); sqla_check PASS (35/35 mutants fail); package of 14:26; HW run 2026-10-10 with the four Step 2 settings ON together: logs clean, sleep +36 ms/s in the Looney Tunes race and +63 ms/s in Crazy Taxi at 6K+ (not split per setting); HW SUCCESS (Matt: "everything looked like before"); default ON since the package of 15:49 on 2026-10-10 (Matt) (section 0) |
| 5.44 | RAM SQ Direct: a store-queue flush to main RAM is a 32-byte copy inside `do_sqw_nommu` (candidate 3) | `[x]` HW SUCCESS 2026-10-10; built + PC tested 2026-10-10: VIDEO / DEBUG "RAM SQ Direct" (`RamSqDirect`), live; ramsq_check PASS (18/18 mutants fail); package of 14:26; HW run 2026-10-10 with the four Step 2 settings ON together: logs clean, sleep +36 ms/s in the Looney Tunes race and +63 ms/s in Crazy Taxi at 6K+ (not split per setting); HW SUCCESS (Matt: "everything looked like before"); default ON since the package of 15:49 on 2026-10-10 (Matt) (section 0) |
| 5.45 | SH4 Clock above 200 MHz: the SH4 gets clock/200 times the cycles per slice, the modules keep 200 MHz time (Looney Tunes race Step 3) | `[x]` closed 2026-10-10 (Matt: "Keep at 200 MHz. Clearly not the right approach on this issue."), default 200; built + PC tested 2026-10-10 (Matt: "CPU cycles per slice", limit 250 MHz): EMULATION "SH4 Clock" 150-250, default 200, set before boot; clock_check PASS (22/22 mutants fail); package of 16:30; HW Looney Tunes race: logs clean, 28.0 frames per emulated second at 220 and 28.2 at 250 (26.4 at 200), SPEED 97.5% and 86.6% (100.0%); Matt: 220 "basically the same", "250 feels worse" (section 0) |
| 5.46 | JIT SQ COPY: store-queue shape for stores whose block PREFs through a copy of their base register (the Sonic Adventure 2 vertex loop 8C141BBA: stores through r6, `pref @r3; mov r6,r3`; 5 out-of-line stores per vertex, HW run 23) | `[~]` built + PC tested 2026-10-12 (Matt: "Work on the Sonic Adventure 2 fix."): JIT SPEED "JIT SQ COPY" (`JitSqCopy`), default OFF, set before boot; `sqcp_guess` in `ps3/ps3_jit_sqla.h`; `[ta-split] sqcp=` sites, `[jitcfg] sqcopy=`; sqla_check PASS (17/17 new mutants fail); package of 02:30 on 2026-10-12; HW run 25 (Sonic Adventure 2, row ON): clean log, 14K+ SPEED 94.8% (90.6% and 92.5% in the OFF runs), about 1.5 to 3 points at the same load; no verdict from Matt (section 0) |

FSCHG, FSQRT, NewOps, FPU PIN, TFWD and CR0 are default ON since 2026-09-23 (Matt).
Dynarec, JIT Fastmem, TA SQ Direct, JIT SQ STORES, JIT FTRV, JIT CONSTPROP, SPE AICA Sound and SPE ARM7 are
default ON since the package of 23:22 on 2026-10-05 (Matt).
GD-ROM Async (+ CD-DA) and JIT Exit are default ON since the package of 18:19 on 2026-10-06 (Matt: "Keep toggles on").
JIT Div32 defaults to FIXED since the package of 21:02 on 2026-10-06 (Matt: "Yes, let's put it as default").
JIT SYS GATE defaults to + RESUME since the package of 22:52 on 2026-10-06 (Matt: "Yes, make +RESUME the default").
Tex Slot Hash (P16) defaults to ON since the package of 23:46 on 2026-10-06 (Matt: "Tex Slot Hash should become the default ON").
Depth Float (P18) defaults to ON since the package of 01:01 on 2026-10-07 (Matt: "Yes, absolutely!").
AICA Race Fix, ARM7 Reset Fix, CA Clamp, SPE AICA DSP and SPE DSP JIT ON default to ON since the package of 03:41 on 2026-10-07 (Matt: "mark this on by default").
SPE Build (5.12 #2) defaults to ON since the package of 18:36 on 2026-10-07; SPE Tex Hash (5.12 #3) since the package of 22:35 on 2026-10-07 (Matt: "Yes, I want it by default.").
YUV Conv Fast (P43, bundle 1) defaults to ON in source since 2026-10-08, in the packages since 14:26 on 2026-10-10 (Matt: "Yes, thanks."); VRAM Direct, LMMODE and FB Write Watch stay OFF.
Bump Maps (3j) defaults to ON in source since 2026-10-08, in the packages since 14:26 on 2026-10-10 (Matt: "keep them on by default").
SYS GATE BATCH (5.42), JIT SQ MISS + LA STEPS (5.43), RAM SQ Direct (5.44) and JIT FSCHG + NO BLOCK END (5.37) are default ON since the package of 15:49 on 2026-10-10 (Matt: "keep the settings default on since they seem to be mostly massive wins.").
ARM7 JIT level 5 is the default since 2026-09-23 (Matt).
Vertex Arrays are default ON since 2026-09-23 (Matt: geometry correct).
Matt, 2026-09-23: speed over accuracy. The Flycast cycle model was dropped for
the existing SH4 Clock option. SPEED% reads against 200 MHz, so judge a lower
clock by VPS.
| 5.2 | `JitCarry`: fix, then test | `[x]` HW 2026-10-06, default ON (Matt), in the packages since 17:35; fixed + PC tested 2026-10-06 (`ps3/ps3_jit_carry.h`, no XER; carry_check PASS, 39/39 mutants fail), package of 09:16; HW: Crazy Taxi and Sonic Adventure clean, one fallback per boot, Crazy Taxi same load -0.4% (noise), Sonic FMV after boot +23% VPS, play equal; Matt: Sonic felt choppier, no cause in the log; = OPTIMIZATION_POTENTIAL P41 (the FMV fallbacks) |
| 5.3 | `JitMac`, `jit_cr0`, `jit_tfwd`, `dyn_ic`, `bcache` | `dyn_ic` `[x]` HW, done (Matt 2026-10-05), default JSR/JMP since the package of 16:42 (Matt); built + PC tested 2026-10-05 (Matt: "Go with DYN_IC"): JIT SPEED "JIT DYN IC" OFF / JSR/JMP / + RTS, default OFF; dynic_check PASS, 25/25 mutants fail; package of 14:19; HW 2026-10-05: clean, Crazy Taxi same-load VPS +1.0% at both levels (noise), Sonic equal, code +3-4%. `jit_cr0` + `jit_tfwd` done in 5.1e. `bcache` `[x]` HW SUCCESS 2026-10-11 (bundle 8, section 0): JIT SPEED "JIT BCACHE" (`JitBcache`); Crazy Taxi correct, no gain to see; Looney Tunes race SPEED 97.62% -> 99.95%, sleep 24 -> 59 ms/s (Matt: "It's much better"); default ON in source since 2026-10-11 (Matt), not packaged. `JitMac` `[~]` built + PC tested 2026-10-11: the `JitMac` 64-bit carry fixed (`ps3/ps3_jit_mac.h`), JIT MAC stays OFF, no game in the old logs runs mac.l or mac.w; `misc/jit8_check` PASS; package of 22:43; no HW run yet; merged 2026-10-06: `bcache` = P17 BCACHE, `JitMac` check = P46 + P32 (mac.w) |
| 5.4 | ARM7 JIT port (`arm7_jit.cpp` was Wii-gated) | `[x]` HW 2026-09-23, same item as the 5.4 row above |

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
    | 4 | Texture decode on a miss (twiddle, VQ, palette) | FMV / loading spikes | good; could write the swizzled layout directly. Built 2026-10-08 as "SPE Tex Decode" (section 0), HW no faults, default ON |
    | 5 | `present_fb_convert` | SH4 thread, each vblank, direct-FB games only | good. Built 2026-10-08 as "SPE FB Convert" (section 0), HW no faults, default ON |
    | 6 | AICA DSP | not emulated (2.3 parked) | moved to 5.13 |
    | 7 | AICA channel mix (`GenerateAllFast`) | 4-4.5%, SH4 thread | moved to 5.13 (control/render split solves the readback) |

    Left out by Matt: the YUV FMV converter (latency-bound, less important).
    Stay on the PPE: SH4 JIT, ARM7 JIT, MMU, memory handlers, the TA store
    FSM, maple, GD-ROM, RSX/ps3gl calls.
    Merged 2026-10-06 from `OPTIMIZATION_POTENTIAL.md` (open):
    - #4 for YUV FMVs is P42: (a) a block decoder that writes straight to
      the RSX buffer (estimate ~10-12 instructions per texel against ~60
      now, both passes), (b) the RSX 4:2:2 formats with a Cg program, (c)
      this SPE item, (d) decode only the rows and columns the converter
      wrote. Do it after P16 (section 0, Soul Calibur) shows how many
      decodes are left. 2026-10-07: (a) built on the PPU, "YUV Block
      Decode" (section 0), ~20 instructions per texel; HW tested
      2026-10-07 (Looney Tunes, Sonic Adventure; decode ~4x faster),
      default OFF.
    - The converter stays on the PPU: P43 loads the planes as 32-bit words
      (`rlwimi`) and skips the copy when a block starts at offset 0 (~40
      lines; estimate ~1-3% of the SH4 thread in a Sofdec FMV).
    - 2026-10-07: #2 built as "SPE Build" (section 0): an SPU thread builds
      the vertex buffer by DMA into the vring block, from the S3 lists; the
      render thread draws its runs. HW runs 1-2: correct, too slow; SPU rewrite
      (Matt: "SPU rewrite now") built, package of 17:41, HW pending.
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
      - Merged 2026-10-06 (`OPTIMIZATION_POTENTIAL.md` P20, open):
        - G1: the default (SPE AICA Sound ON, SPE AICA DSP OFF) runs A2,
          with no DSP: no reverb, and a voice that only feeds the DSP
          (DISDL = 0) is silent. Fix: Matt's A3 verdict, then SPE AICA DSP
          ON and SPE DSP JIT ON by default (CHECK costs ~2.5x the SPU
          time). No logged run has A3 and SPE ARM7 both ON (SPE ARM7 is
          default ON since 2026-10-05), so one HW run first: today's
          defaults plus both DSP options; Crazy Taxi, Sonic Adventure,
          ChuChu Rocket, Looney Tunes. Read the `dsp jit` line (`slow=`,
          `bad=`), SPU busy, SPEED/VPS; listen for reverb, missing voices,
          old echo after a scene change. Looney Tunes also tests F3
          (section 6; a link is not shown). HW 2026-10-07, Looney Tunes:
          voice lines now play; noise at loads, in-game frame rate lower
          (section 0). With the Looney Tunes fixes all cues play at the old
          frame rate: SUCCESS (Matt), SPE AICA DSP and SPE DSP JIT ON by
          default since the package of 03:41. G1 done.
        - G2: the ring is in LS, not in wave RAM (the limits above). Fix
          idea: a "ring dirty" record on guest writes inside the ring, and
          the SPU writes its ring output back by DMA after each block.
        - G3: a 64K-word ring (RBL 3) and TABLE writes outside the LS ring
          are lost at each CHECK. Fix idea: write the dirty lines back by
          DMA before the invalidate.
        - G4: guest reads of TEMP, MEMS, MIXS and EFREG return nullAICA's
          copy of the CPU writes, not DSP values (rarely read, unverified).
        - Do not port Flycast's dry mix (Matt 2026-10-06: "the DSP fix is
          wrong").
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
  - Merged 2026-10-06 (`OPTIMIZATION_POTENTIAL.md` P55 + P7, open; one
    branch, do the two together):
    - P55: `TAWriteSQ()` and the DMA path `TAWrite()` (`dc/pvr/pvr_if.cpp`)
      call `pvr_write_area1_32` per word, each through the out-of-line
      `vramlock_ConvOffset32toOffset64()` (no LTO). Map the address once
      per 32-byte block (a block never crosses the bank bit 22); the DMA
      path needs a fallback when a run crosses a bank. ~10 lines, own
      toggle, the same stores. First count the VRAM-direct blocks in
      `[ta-split]`: dense Crazy Taxi sends ~127K SQ flushes/s to targets
      other than the TA (Step 1); if all were VRAM, ~0.5% of the SH4
      thread (estimate).
    - P7 (bug): `SB_LMMODE0/1` are never read. SQ writes to 0x11/0x13
      always take the 32-bit path; ch2 DMA to 0x11 is always 64-bit, and
      to 0x13 moves no data. A texture sent through the wrong path lands
      scrambled. Log LMMODE0/1 once per change first (Rez must stay right),
      then Flycast's rule in both paths. ~40 lines, no speed cost.
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
- 5.19 SH4 Clock below 150. The option allows 150-250 MHz (`ps3_menu.cpp`;
  200-250 since 5.45).
  A lower floor (e.g. 120-140) buys VPS, but the guest gets fewer cycles per
  frame, so heavy scenes drop more of the game's own frames (already at 150:
  dense 46.6 fps at 54.8 VPS). Last resort, Matt's call.
  - Merged 2026-10-06 (`OPTIMIZATION_POTENTIAL.md` P15): Minicast
    freerunning, a dynamic form of this option. The scheduler advances by
    wall-clock time (the PPE timebase), at most 1/60 s per call. When the
    SH4 thread is short, the guest gets fewer cycles and drops its own
    frames, so it is not a speedup. Costs: when ahead, the SH4 spins in
    idle loops and takes SMT slots from the render thread (it needs a
    sleep), VPS stops measuring speed, runs are not deterministic. Matt's
    call, as a pacing option.
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
- 5.3 `bcache` and `JitMac`, merged 2026-10-06 from
  `OPTIMIZATION_POTENTIAL.md` (open, not built):
  - `bcache` (P17 BCACHE): the dynamic-end lookup (BET_Dynamic* in
    `ps3/ps3_driver.cpp`) is ~16 instructions: `ldx cache[idx]`, dependent
    loads of addr and code, and a `lookups++` load and store. Every rts
    takes it at the default DYN IC level, and `loop_no_update` calls
    `bm_GetCode` in C after every timeslice (getcode 115K/s, 5.18). The
    BCACHE path exists but cannot run on the PS3: `BlockCacheFlatEntry`
    (`dc/sh4/rec_v2/blockmanager.h`) holds a u32 addr and a
    `DynarecCodeEntry*` code, 16 bytes on PPC64, and the emitted code
    expects 8 (it reads code with `lwz` at +4). Declare `code` as a u32
    (the code cache is in `.text` below 32 MB): ~9 instructions, two
    independent loads, no store. ~5 lines. Expected gain: noise level
    (5.26).
  - `JitMac`: it builds MACL/MACH with `addc`/`adde` in 64-bit mode, the
    defect that 5.2 fixed for the carry ops ("Not in 5.2"). Its check is
    P46: Flycast's `tests/src/sh4_ops.h` (300 asserts, mac.l and mac.w
    among them) as `misc/interp_check` (WSL, ~400 lines, little-endian
    branches only) and as a boot self-test of the interpreter (the real BE
    `GetDR`/`SetDR`, the XOR byte lanes); its PrepareOp/RunOp split allows
    a JIT runner later. Trap: its `mach()` helper returns `mac.l`, so
    compare `(mach << 32) | macl`. P32: with JitMac OFF, the interpreter's
    mac.w with S=1 prints a message and skips the op (no pointer
    increments).
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
- 5.2 JIT Carry fix (Matt 2026-10-06: "I'd say to work on 5.2 since It's a
  known issue."). Built and PC tested, package of 09:16, HW pending.
  - `ps3/ps3_jit_carry.h`, no XER: addc, subc and negc zero-extend the
    operands (`clrldi`), add or subtract T in 64 bits and take T from bit 32
    (`rldicl t,d,32,63`). addv and subv sign-extend (`extsw`) and set T when
    the exact 64-bit result is not the sign extension of its low word
    (`extsw`, `xor`, bit 63). div1 zero-extends Rm or ~Rm and takes its
    carry from bit 32. cmp/str and swap.b moved to the header with the same
    words, so the check covers every op the toggle turns on; xtrct and
    clrmac use the base shift, or and mov32 forms.
  - Option: EMULATION -> "JIT Carry" (`JitCarry`), as before: default OFF,
    read at compile time (set before boot); `[jitcfg]` prints `Carry=`.
  - PC (WSL): `misc/carry_check/run.sh` checks the header against ppu-as
    (98 words, `gen.py`), then each form on a PPC64 model in 64-bit mode
    against the SH4 interpreter's code, with junk above the low word of
    pinned registers and each register choice of the driver (Rn and Rm
    pinned, the same, or r11): PASS, 1.6M checks. `mutate.py`: 39 of 39
    planted bugs fail, 37 on the model alone (2 give the same results
    through another bit). On the same model the pre-5.2 forms (`run.sh N
    old`) are wrong in addc 43-46%, subc 0-30%, negc 0-2%, addv 21-28%, subv
    8-11%, div1 21-37% of the cases (clean / junk upper halves).
  - Build: the 7 old warnings, `.got` 0xBED8, 0 `r2off`.
  - Expected: correctness only. ~40K interpreter fallbacks/s leave dense
    Crazy Taxi (5.0 census: addc 18.8K, subc 15.3K, div1 5.0K), below the
    noise of one run.
  - Not in 5.2: JitMac (5.3) builds MACL/MACH with `addc`/`adde` in 64-bit
    mode, the same defect.
  - HW test steps (package of 09:16):
    1. Install. An install resets the settings to the defaults. On the main
       menu: EMULATION -> JIT Carry ON, VIDEO / DEBUG -> IFB Probe ON.
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: Crazy Taxi at 60 Hz, dense traffic, ~150 s, exit to the XMB.
       Log: `[jitcfg] NewOps=1 Carry=1`, the `[IFB]` lines without addc,
       subc and div1, no fault. The game plays as before.
    4. Boot 2: Sonic Adventure, a level load and a few minutes of play.
    5. On a fault: the same boot with JIT Carry OFF.
  - HW 2026-10-06 (package of 09:16, our listener at Matt's request; log
    `ps3log_52_run1.txt`, cut into `run52_ct.txt` and `run52_sonic.txt` in
    the scratchpad of session 48d69156). Reference: `run533_ct.txt` and
    `run533_sonic.txt` (5.33, JIT Carry OFF).
    - The install kept the earlier settings: `nullDC.cfg` is in
      `/dev_hdd0/data/NotDC/`, outside the installed game. Crazy Taxi ran
      with JIT FRCHG and JIT PREFETCH ON (noise in 5.29/5.30); Sonic ran
      with both OFF, so its JIT settings match 5.33 except JIT Carry.
    - Crazy Taxi, 60 Hz, 137 s: `[jitcfg] NewOps=1 Carry=1`, log clean.
      Interpreter fallbacks: one `or.b` in the whole boot (5.33: 34K/s on
      average, 92K/s at most; subc, div1, addc, swap.b, cmp/str). Same load
      -0.4% (noise). 6K+ windows: SPEED 99.2-99.7%, 77% at full speed
      (5.33: 70%). The deepest dip is the known 328 ms disc read (5.33).
      Matt: "It looked the same".
    - Sonic Adventure, 60 Hz, 210 s: log clean, one `or.b` fallback (5.33:
      1.4-3.5M/s in the FMV, ~8.4K/s in play). The FMV after boot (P41),
      in its two seconds with no disc read: SPEED 69.3/71.2% -> 87.2/86.1%,
      VPS 41.6/42.7 -> 52.3/51.6 (+23%). The SH4 still never sleeps there.
      An older build ran the same FMV at 73-87% (P41), so one run each does
      not give a precise size. Play: dense SPEED 99.8% in both runs, 96%
      (95%) of dense windows at full speed, same load -0.1%.
    - Matt: "Sonic felt choppier". The log shows no cause in 5.2:
      - Game frame pacing is the same: `[ps3gl] frame time` mostly 30 fps
        with 29-38 ms frames in both runs; play seconds below 29 RPS 20%
        (5.33: 23%); worst frame per second median 22.8 ms (22.3), p90
        25.9 ms (24.7).
      - This run had one 330 ms disc read in play (one second at SPEED
        74.6%, 48 silent audio blocks). The 5.33 Sonic play had none (its
        longest: 190 ms). It is the known first read after seconds with no
        reads (5.33), not 5.2.
      - Emulated time does not change: each op bills CPU_RATIO with or
        without the fallback (`decoder.cpp` decode loop), and
        `dec_fallback` sets no flag that the x30 idle rule reads. A loop
        with addc, subc, negc, addv, subv, div1 or xtrct reads and then
        writes a register, so JIT IDLE SKIP never takes it as an idle turn
        (`idle_turn_is_pure`); idle blocks in play: 229 in both runs.
    - Matt: "Sure" to default ON: `JitCarry` defaults to 1 in
      `ps3/ps3_presets.cpp` since 2026-10-06, not in a package yet. Matt's
      own `nullDC.cfg` already has it ON.
- `jit_fmov` does not apply: it needs the Wii DSI fastmem, which is off.
- Dropped: `SmcCheckLevel` (the guard is 2 halfword loads), `regalloc`, SSA.
- 5.34 GD-ROM Async (OPTIMIZATION_POTENTIAL P3; Matt 2026-10-06: "Go with
  P3", to test with 5.35). Built and PC tested, package of 17:35, HW
  pending.
  - Why: dense Crazy Taxi spends ~50 ms/s (~5%) of the SH4 thread in disc
    reads, and the deepest dip of every run is one 330 ms read, the first
    after seconds of no reads (5.32, 5.33). The cost is per request, so
    batching cannot fix it (5.33).
  - `ps3/ps3_gdasync.cpp`: one worker thread (prio 999, above the SH4 1001
    and render 1000 threads; it waits on the disk), a semaphore and
    `__atomic` flags as in `ps3_sndthr.cpp`, and an lv2 mutex around every
    drive call (`libGDR_ReadSectorLocked`, TOC, session info): the CDI
    reader's FILE and buffers are shared.
  - DATA: `FillReadBuffer` kicks the worker. `UpdateGDRom` returns while
    the fill is in flight; a refill mid-DMA ends the transfer early (only
    after a full 32-sector batch, so the length stays 32-aligned), and
    `SB_GDLEND` counts only the bytes that moved. `SPI_CD_READ`, the ATA
    soft reset and the GD-ROM reset wait for a fill in flight (an aborted
    read). PIO reads stay on the SH4 thread.
  - + CD-DA: a 64-sector ring keyed by FAD (slot = FAD & 63). The SH4
    thread copies a sector when its tag matches and checks the tag again
    after the copy; else it reads the sector itself. It asks for more below
    32 sectors ahead or after a miss. The worker reads 16 sectors a batch,
    data fills first; a seek or a loop starts a new run.
  - `[gdr]` line each second with reads, per window: `sh4: dma, pio, cdda
    n/sectors ms(max)`, `wait`, `worker: fill, ra`, `ra hit/miss`.
  - `gda_stop()` runs before `main` returns (the 5.35 path), so no read is
    in flight while `exit()` closes the image file.
  - PC (WSL): `misc/gdasync_check/run.sh` builds the real `gdromv3.cpp` and
    `ps3_gdasync.cpp` with pthreads for lv2 and a fake drive (random delays
    up to 3 ms, an error if two threads are in it). Random DMA reads in
    random DMA pieces, aborted reads (NOP or soft reset), CD-DA play, seek
    and loops pulled during the DMAs; bytes, interrupts and BSY against a
    plain model. ALL PASS at levels 0, 1 and 2 (ThreadSanitizer and -O2, 5
    seeds each; steady play 2 misses in 3000 sectors, ~2 misses a seek).
    `mutate.py`: 16 of 16 planted bugs fail (17 of 17 after the loop fix
    below).
  - Build: the 7 old warnings, `.got` 0xC068, 0 `r2off`; `.bss` +188 KB
    (the ring).
  - Expected: `[stall] gd read` in dense Crazy Taxi ~50 -> ~0 ms/s and the
    330 ms stall off the SH4 thread (the game waits for that read in
    emulated time instead). The SPEED gain is not known.
  - Merged 2026-10-06 (`OPTIMIZATION_POTENTIAL.md` P22 DMA part and P9
    GD-ROM part, open), all in `UpdateGDRom()`, which 5.34 changed:
    - SB_GDLEN = 0 means 32 MB (Flycast `d0d971254`, Pro Pinball Trilogy
      intro hang). Ours raises the end IRQ with no data and leaves the
      drive mid-read.
    - DMA pacing: Flycast moves 10 KB chunks at 1.8 MB/s and small
      transfers at 25 MB/s (World Series Baseball 2K2 freezes, Sakura
      Taisen 3 music). Ours, before 5.34: 32,000 bytes inside the SB_GDST
      write, then more every SlowUpdate (~446 MB/s). Form: a "GD-ROM Speed"
      FAST / REAL option, counted down in SlowUpdate; REAL loads as slowly
      as a Dreamcast.
    - A DMA longer than the data read hangs in
      `verify(read_params.remaining_sectors>0)` or `verify(!(SB_GDST&1))`
      (`verify` ends in an endless loop). Reicast clamps the length.
    - No byte-order issue, low risk. P22's command hangs and P48 stay in
      `OPTIMIZATION_POTENTIAL.md`.
- 5.35 JIT EXIT (OPTIMIZATION_POTENTIAL P6; Matt 2026-10-06: test it with
  5.34; at exit "Save the flash only"). Built and PC tested, package of
  17:35, HW pending.
  - P6 was wrong on one point: nothing calls `Term_DC()`, so `EMU_TERM` and
    `mem_Term()`'s flash save do not run even when the loop returns.
  - `UpdateSystem_no_event()` also returns nonzero on a stop request (a
    flag set for the JIT run). The main loop's cold path then reads
    `sh4_int_bCpuRun`; when it is clear, it jumps to an epilogue
    (`ps3/ps3_jit_exit.h`): the FPU PIN bank to `Sh4cntx`, then f14-f31,
    cr2-cr4 and r13-r31 from the frame, then `blr`. Only with JIT Exit ON
    does the prologue save f14-f31 and the CR (frame 272 -> 416 bytes); OFF
    emits the old loop.
  - Back in C: the FPSCR to round-to-nearest and `[jit] main loop
    returned`. `dc.cpp` then saves `dc_flash_wb.bin` (`mem_SaveFlash()`,
    `mem_Term()`'s code), and `EmuMain` returns to `main`. The PS3 no longer
    frees the DC memory there: the SPUs write AICA RAM until the process
    ends. The 3 s fallback stays.
  - Also: `ps3_menu.cpp` `MAX_INGAME_OPTS` 64 -> 96. The pages had 67 rows,
    so the in-game menu's next-boot handling missed the last JIT SPEED
    rows; now 69 rows.
  - PC (WSL): `misc/exit_check/run.sh` checks the words against ppu-as (63),
    then runs the prologue, a clobber and the epilogue on a PPC64 model:
    PASS, 210064 checks. `mutate.py`: 10 of 10 planted bugs fail. Found on
    the way: `ppc_bclrx(BO_ALWAYS,BI_CR0_EQ,0)` writes 0x4E820020 (BI is
    unused); the epilogue emits ppu-as's `blr`, 0x4E800020.
  - HW test steps for 5.34 + 5.35 (package of 17:35):
    1. Install. The install keeps nullDC.cfg, so check: SH4 Clock 200,
       Dynarec ON, JIT Carry ON; EMULATION -> GD-ROM Async DATA, JIT Exit
       ON; VIDEO / DEBUG -> JIT C-Calls ON (the `[stall]` line).
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: Crazy Taxi at 60 Hz, ~150 s through the dip, then PS button
       -> Quit Game. Log: `[gdr] GD-ROM Async DATA`, `[jitcfg] ... jexit=1`;
       in play `[gdr]` with worker fills and `[stall]` gd read near 0; no
       fault. At the exit: `[jit] main loop returned`,
       `SaveSh4FlashromToFile: saved flash`, `[gdr] worker stopped`, `[ps3]
       EmuMain returned 0`, and no `SH4 did not stop in 3 s`.
    4. Boot 2: ChuChu Rocket (CD-DA music) with GD-ROM Async + CD-DA, menu
       and one stage, ~60 s: the music plays right; `[gdr] ... ra hit N
       miss M` with few misses. The boot log shows `Loaded dc_flash_wb.bin
       (writeback)` from boot 1. Exit by the XMB again.
    5. Optional, boot 3: Sonic Adventure (DATA), a level load and some
       play: the loads and the graphics are right.
    6. On a fault: the same boot with GD-ROM Async OFF; for an exit fault,
       JIT Exit OFF.
  - HW 2026-10-06 (package of 17:35, our listener at Matt's request; log
    `ps3log_534_run1.txt`, cut into `run534_ct.txt`, `run534_chuchu.txt`
    and `run534_sonic.txt` in the scratchpad of session 3744e9dd). Matt's
    cfg: Carry=1, dyn_ic=2, dve=1, sqmiss=2, greuse=1, frchg=0, pref=0,
    jexit=1, JIT C-Calls ON.
    - Boot 1, Crazy Taxi (DATA), 195 s. Matt: "Crazy Taxi felt like full
      speed, very, very smooth!" Dense windows (6K+ vertices, 55): `[stall]`
      gd read 52.4 ms/s (5.2 run) and 49.9 (5.33 run) -> 0.0; SPEED 99.5%
      -> 100.0%; windows at full speed 77% / 70% -> 95%. Worst frame 67.7
      ms in game code (was a 328-700 ms disc read); lowest VPS 55.3 (20.4
      / 43.3). The worker: 729 fills, 16876 sectors, 7.1 s, the longest
      350, 337 and 321 ms; no read and no wait on the SH4 thread. `sys`
      rose 65 -> 75 ms/s (not explained, no speed cost).
    - The XMB exit after each boot: `[jit] main loop returned`, the flash
      saved, `[gdr] worker stopped`, `EmuMain returned 0`, `[aud] shut
      down`; no "did not stop in 3 s". The next boots loaded
      `dc_flash_wb.bin (writeback)`.
    - Boot 2, ChuChu Rocket (+ CD-DA): the echo is unchanged (section 6).
      Data: 177 fills, the longest 49 ms, no waits. CD-DA ring: 5195 hits,
      759 misses (87%), in runs (8 s at 75 misses/s). Cause: a loop or a
      replay back inside the worker's run kept the run, but the 64-slot
      ring no longer held those sectors, so the worker read nothing until
      play passed its end. Each miss is a read on the SH4 thread: ~0.2 ms,
      some 28-51 ms (it waited for a worker read).
    - Boot 3, Sonic Adventure (DATA): 964 fills (6.1 s, the longest 329
      ms), 4 PIO reads on the SH4 thread, no waits. Matt: "It ran better."
  - Fix (Matt: "implement the one line fix"): `ra_batch()` also starts a
    new run when play is more than RA_SLOTS behind the run's end. PC: a new
    loop phase (a replay, a long loop, a short loop): 1985 misses in 3100
    sectors before the fix, 27-33 after; `run.sh` ALL PASS. Package of
    18:10. Next: ChuChu Rocket again with + CD-DA (`ra hit/miss`).
  - HW 2026-10-06, package of 18:10 (`run534_chuchu2.txt`, 53 s): ChuChu
    Rocket (+ CD-DA), Matt: "Still same." The ring: 1684 hits, 11 misses
    (99.4%; was 87%), no run of misses. Each miss still costs the SH4
    thread 24-85 ms: the read waits for the worker's read in flight (a
    data fill of 13-53 ms or a read-ahead batch) and seeks. The misses are
    1-2 in each of 8 seconds (likely song starts and loops; the log does
    not show the play commands). Clean XMB exit, the flash saved.
  - `mutate.py` after the fix: 17 of 17 planted bugs fail (one new: the
    run kept after a loop back inside it, the HW bug).
  - Matt 2026-10-06: "SUCCESS for both then." Then "Keep toggles on":
    GD-ROM Async defaults to + CD-DA and JIT Exit to ON since the package of
    18:19 (`ps3/ps3_presets.cpp`). The CD-DA miss stalls go to the known
    list, parked: "we will catch up with it waaay later". Options when it comes back: silence for the missed
    sector (no stall, a 13 ms gap), or start the read-ahead at the play
    command (fewer misses, a miss can still stall).
- 5.36-5.40, merged 2026-10-06 from `OPTIMIZATION_POTENTIAL.md` (Matt:
  "Can you do the merge, then?"). Not built; the full text is under the
  same P number there.
  - 5.36 (P5): the deadline scheduler (Reicast: one decrement per
    timeslice). Ours runs the whole peripheral update every slice (TMU,
    PVR counters, AICA DMA, the INTC check): estimate 1% or less, medium
    refactor. And `raw_note_peak()` (`ps3/ps3_rend.cpp`) stores to a
    global on every 32-byte TA record: update the peak at reset and at
    STARTRENDER instead (estimate 0.1-0.5% of the SH4 thread). Built
    2026-10-06 as "JIT SYS GATE" and "TA Peak Frame": see 5.36 below.
  - 5.37 (P17 fschg): Flycast's fschg is an xor on FPSCR plus a decoder SZ
    flip, and it does not end the block. Ours (`dc/sh4/rec_v2/decoder.cpp`)
    emits a sync and ends the block outside delay slots; the SZ-only sync
    reloads FPSCR right after the xor stored it, a load-hit-store on every
    fschg (hot block 0C0820C2 in `run518b.txt`). Change: drop the
    `dec_End` and the SZ-only sync; `emit_frchg_test`
    (`ps3/ps3_jit_frchg.h`, 5.29) must then mask bit 20. ~10 lines.
    Expected gain: noise level (5.26 removed 1.9M load-hit-stores/s with
    no measurable gain).
  - 5.38 (P19, bug): `MatchDiv32` (`dc/sh4/rec_v2/decoder.cpp`) turns
    div0u/div0s + 32 x (rotcl; div1) into one divide, but rs3 is empty:
    the high dividend word is dropped, and the ops do a 32/32 divide with
    an unsigned-only fixup. Simulated (200K cases per row, our
    interpreter's div1 chain as the reference): unsigned 64/32 wrong in
    100%; the signed SH manual idiom wrong in 32% of final quotients. That
    idiom matches in Crazy Taxi, Sonic Adventure and Soul Calibur. ON by
    default; only JitSafe turns it off. Fix: Flycast's 64/32 form, with
    `divdu`/`divd` on a dividend built by `clrldi` + `rldimi` and an
    `extsw` divisor (poisoned upper halves); T back to 0/1 (JIT T COPY, T
    forwarding, CR0); quo = 0 and rem = dividend on a divide by zero. ~60
    lines. PC check first: Flycast's `div32_test.cpp` plus random cases in
    a `misc/*_check` against the interpreter. The `[DIV]` probe must skip
    pc < 0x8C010000 (its 16 slots fill with boot sites).
  - 5.39 (P56): with PR=1 every FPU op is an interpreter fallback with a
    full spill; fmov, flds, fsts and fneg do not depend on PR (Flycast
    `62085539a`). ~5 lines. Build it only if a PR=1 game shows in `[IFB]`
    (none in the Crazy Taxi census); fneg only after the P32 fix.
  - 5.40 (P4): `SetFloatStatusReg()` sets only RM. SH4 DN=1 (the reset
    FPSCR has it) means denormals are zero; the PPC form is
    `mtfsfi 7,(DN?4:0)|RM` (NI). A gain only if the PPE stalls on
    denormals: check what NI does on the PPE first. 1 line, own option.
    The render thread keeps its own FPSCR.
- 5.38 JIT DIV32 (P19; Matt 2026-10-06: "do 5.38"). Built and PC tested,
  package of 19:40, HW pending.
  - `dc/sh4/rec_v2/div32_match.h` (`div32_emit`, called by DM_DIV0) holds
    the SHIL of both forms. FIXED, unsigned: `div32u` with rs3 = r3 (the
    dividend's high word), then T = quo & 1, r1 = quo >> 1, `div32p2`.
    FIXED, signed: T = (r3 ^ r2) & 0x80000000 (div0s's T), `div32s` (one's
    complement: +1 before the divide when negative, then quo - 1 when the
    signs differ, else rem - 1 when r3 < 0), r1 = (quo >> 1) | T, T |=
    quo & 1, `div32p2` (with the sign: rem - (b > 0) + (bit 0 ? b : 0)),
    T &= 1. OLD: the ops before 5.38 (rs3 empty).
  - Not Flycast's: Flycast shifts r1 with `sar`, which copies the one's
    complement quotient's bit 31. The chain's top bit is always div0s's T;
    the two differ only for a quotient of -2^31 (INT_MIN / 1 in the SH
    manual idiom), found by the PC check. One more op (`shr` + `or`).
  - `ps3/ps3_jit_div32.h`: `divdu`/`divd` on `clrldi` + `rldimi r3,32`,
    `extsw` divisor, remainder by `mulld`/`mullw` and `subf`; divide by 0
    masked to quotient 0 (`neg`, `sradi 63`), remainder = the dividend's low
    word (Flycast). `div32p2` branch-free, 14 instructions. The driver
    picks the form by rs3; `div32p2` always takes the new form (the same
    results for T = 0/1, checked).
  - `shil_canonical.h`: div32u, div32s and div32p2 are Flycast's forms
    without signed overflow. The matcher rejects `div1 Rn,Rn` (a changing
    divisor); the `[DIV]` probe skips pc < 0x8C010000. `[jitcfg]` prints
    `Div32=`.
  - PC (WSL): `misc/div32_check/run.sh`: the SHIL of `div32_emit` run with
    the real canonical ops (`canon.cpp`) and with the PPU forms on a PPC64
    model (junk above pinned values, the driver's register choices) against
    the interpreter's `div0` + 32 x (`rotcl`; `div1`) chain: 0 wrong in
    200000 cases per row (unsigned 32/32, unsigned 64/32 with r3 < r2, the
    SH manual signed idiom, signed with dividend >= 0, signed 64/32 whose
    quotient fits s32), Flycast's `div32_test.cpp` cases, 15 signed
    boundaries; PPU forms = canonical ops for every input; 88 words =
    ppu-as (`gen.py`). PASS, 2480214 checks. `mutate.py`: 48 of 48 planted
    bugs fail, 46 on the model alone (2 change only the encoding).
    `misc/constprop_check` now builds its division ops with `div32_emit`:
    PASS, 200000 blocks.
  - OLD forms on the same rows (report): unsigned 32/32 0%, unsigned 64/32
    93.4%, signed idiom 49.8%, signed with dividend >= 0 0%, signed 64/32
    74.7% wrong.
  - Not changed: outside those rows (a quotient that overflows, a divisor
    of 0) both forms differ from the chain, as Flycast's; a divide by 0
    gives quotient 0 (the chain gives other values). Neither form sets
    SR.Q and SR.M, which the chain leaves set; only a `div1` after the 32
    steps would read them.
  - Build: the old warnings, `.got` 0xC070, 0 `r2off`.
  - Expected: correctness only, no measurable speed change.
  - HW test steps (package of 19:40):
    1. Install. The install keeps `nullDC.cfg`. On the main menu:
       EMULATION -> JIT Div32 FIXED, VIDEO / DEBUG -> IFB Probe ON; SH4
       Clock 200 and the other options at their defaults.
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1: Crazy Taxi at 60 Hz, ~2 min of play, exit to the XMB. Log:
       `[jitcfg] ... Div32=1`, `[DIV] div0s pc=8C... MATCHED 65/65`
       lines, no fault. The game plays as before.
    4. Boot 2: Sonic Adventure, a level load and a few minutes of play.
    5. Boot 3 (optional): Soul Calibur, one fight (it has a matched signed
       division too; correctness only).
    6. On a fault: the same boot with JIT Div32 OLD.
  - HW 2026-10-06 (package of 19:40, listener PID 17680 at Matt's request;
    log `ps3log_538_run1.txt`, Crazy Taxi part `run538_ct.txt`, scratchpad
    of session 2b073cf1). Reference: `run534_ct.txt` (5.34, the same JIT
    settings but Div32 OLD and GD-ROM Async DATA; no CD-DA in either run).
    - Every boot first runs the BIOS boot animation, whose SDK division
      routine matches: div0s 8C09108C and 8C0911EC, div0u 8C091138 and
      8C0912AC (before `boot address AC008300`). These are the sites P19
      attributed to Crazy Taxi, Sonic Adventure and Soul Calibur.
    - Crazy Taxi, 164 s (60 Hz after the boot): `Div32=1`, log clean, one
      `or.b` fallback. After `boot address AC010000` the game decodes no
      div0u/div0s, so only the BIOS animation ran FIXED. Clean XMB exit
      (JIT EXIT), flash saved. Same load VPS -0.0%; dense SPEED 99.8%
      (100.0%), 87% of dense windows at full speed (95%): the 10 slower
      seconds are heavy frames of 21-24 ms with no disc stall, at equal
      VPS per load bin. Matt: "it looked and played as before".
    - Sonic Adventure (`run538_sonic.txt`, reference `run534_sonic.txt`):
      game code matches the same SDK routine at div0s 8C10E68C and
      8C10E7EC, div0u 8C10E738 and 8C10E8AC. Log clean, one `or.b`
      fallback, clean XMB exit. Same load VPS -0.1%; dense SPEED 100.0%,
      97% of dense windows at full speed (98%). Matt: "Sonic looked
      right".
    - Rayman 2 (US GDI, on Flycast's old list of games with this bug;
      `run538_rayman_fixed.txt`, 349 s): game code matches div0s 8C067950
      and 8C067AB0, div0u 8C0679FC and 8C09C28C. Log clean, clean XMB
      exit, 50 Hz mode at full speed (97% of dense windows). Level
      surfaces draw untextured (Matt's screenshot); the same with JIT
      Div32 OLD (`run538_rayman_old.txt`, 207 s, `Div32=0`), so not 5.38:
      a separate open issue (section 0).
    - Matt, asked for SUCCESS and the default: "Yes, let's put it as
      default". `JitDiv32` defaults to 1 (FIXED) since the package of
      21:02 (`.got` 0xC070, 0 `r2off`). A default only reaches a cfg
      without the key (`cfgLoadInt` writes it back), and Matt's ran OLD
      last: set FIXED once in the menu.
- 5.36 SYS GATE and TA Peak Frame (Matt 2026-10-06: "5.36"; asked for
  the scope after the cost below, he picked "Gate + Resume levels"; TA
  peak: "Own toggle, as in the plan"). Built and PC tested, package of
  22:27, HW pending.
  - Cost found first. The 5.38 logs have JIT C-Calls ON, so `[stall]`
    splits the SH4 thread. Its sys section (UpdateSystem_no_event and the
    interrupt dispatch, nested sections out) in `run538_ct.txt`: 61.7 ms/s
    at 3-6K vertices, 73.0 at 6K+, 63.3 in light windows; Sonic Adventure
    64.8 / 60.6 ms/s. That is 112K calls/s, 550-650 ns per call. Of it,
    `arm_spe_update` (every 4th slice) is ~14 ms/s (`aica:` 1.4%). The
    probe adds a part; the cost with C-Calls OFF is not measured.
  - Probable cause, not measured: L1 misses. The update calls ~8
    functions on both sides of the 24 MB code cache in .text (UpdateTMU
    near 0x3C800, libPvr_UpdatePvr near 0x1A91000) and reads globals in
    many lines.
  - JIT SPEED -> "JIT SYS GATE" (`SysGate`): OFF / GATE / + RESUME,
    default OFF, set before boot; `[jitcfg] sysgate=`.
    - GATE: `loop_do_update_write` adds 1 to `Sh4cntx.sys_elapsed` and
      compares it with `sys_due` (`ps3/ps3_jit_gate.h`, 5 words). Below
      it: `blt loop_no_update`, no C call. Else `UpdateSystem_gate()` runs
      the skipped slices with the old per-slice code (`sys_slice()`), then
      `sys_plan()` sets the next due count: the next medium tick (every 4
      slices at FAST; ARM7/AICA, DMA, GD-ROM, maple, RTC), the first TMU
      underflow, the next SPG line with an interrupt or a vblank edge
      (SCANINT1/2, vbstart, vbend; every line with Exit Fix 3), a
      render-done or list-end countdown, an AICA DMA end; with SCHED ON
      every slice. Each module dry-runs its own per-slice code
      (`tmu_slices_to_event`, `spg_slices_to_event`,
      `aica_dma_slices_to_event`), so the plan follows that code.
    - Kicks set `sys_due` to 0, so the next slice end runs the update as
      every slice end did before: an interrupt that becomes acceptable
      (`SetInterruptPend`, `SetInterruptMask`, `SRdecode`, the SIID
      rebuild) and a stop request.
    - Hooks (`dc/sh4/sys_gate.h`): TCNT0-2 and SPG_STATUS reads add the
      skipped slices to a copy (the plan puts no event among them). Writes
      that move a deadline run the skipped slices first, then kick: TSTR,
      TCNT, a TCR prescaler change (TMU handlers swapped in by
      `tmu_gate_install`), FB_R_CTRL, SPG_VBLANK_INT, SPG_CONTROL,
      SPG_LOAD, SPG_VBLANK, SCALER_CTL, STARTRENDER's countdowns, a list-end
      IRQ with Render Delay, an AICA DMA start. SLEEP (an interpreter
      fallback) counts each of its UpdateSystem() calls as a slice end.
    - + RESUME: a block's cycle exit is `li r3,pc; bl loop_gate; b
      <block entry>`. `loop_gate` adds the slice's cycles and counts; with
      nothing due it returns (`bltlr`) and the block starts again as after
      bm_GetCode (SMC check and cycle check again), else it stores next_pc
      and runs the update. A block with a cycle exit grows by 1 word.
    - `[gate]` each second: slices/s, runs/s (updates), syncs/s, kicks/s
      and why each run came (med, tmu, spg, dma, sched, kick). `[CC]
      timeslice` and the `[stall]` sys count now count runs.
  - VIDEO / DEBUG -> "TA Peak Frame" (`TaPeakFrame`): OFF / ON, default
    OFF, set before boot; `[jitcfg] tapeak=`. ON: PREF calls
    `do_sqw_nommu_frame`, which has no peak store per TA record; the DMA
    and old SQ paths skip it too. Both settings note the peak at list
    init, STARTRENDER and the `[rend]` stats line, so `peak=` reads the
    same. In the binary the new fast path is 49 instructions, the old one
    48 (GCC saves 3 more registers in it): expect no gain.
  - PC (WSL), `misc/sysgate_check/run.sh`:
    - `sysgate_check`: the real `tmu.cpp`, `intc.cpp`, `SPG.cpp`,
      `regs.cpp`, `sh4_interpreter.cpp`, `aica_if.cpp` and `sh4_sched.cpp`,
      run with the gate off and on in forked children over the same random
      guest timeline (TCNT and SPG_STATUS reads; TMU, SPG and STARTRENDER
      writes; AICA DMA starts; list-end IRQs; IMASK changes; SLEEP loops;
      bare syncs; an interrupt handler with rte). The traces (reads, Holly
      raises, dispatches, medium ticks, vblanks, end state) must be equal.
      PASS: 300 timelines x 4000 slices, all three Accuracy presets, Render
      Delay, SCHED and Exit Fix 3 (1.13M trace lines).
    - `gate_emit_check`: the GATE count, `loop_gate` and a cycle exit, 24
      words = ppu-as (`gen.py`), then on a PPC64 model over the signed
      edges: count, path, r29, next_pc, LR, no other register changed.
      PASS, 1.42M checks.
    - `mutate.py`: 30 of 30 planted bugs fail. `tr4_check` and
      `div32_check` still PASS.
  - Build: the 7 old warnings, `.got` 0xC180, 0 `r2off`.
  - Expected: at FAST, ~28K updates/s in play, not 112K. With C-Calls ON
    that could save ~30-50 ms/s (3-5%); + RESUME also skips ~84K
    bm_GetCode calls/s. Less with C-Calls OFF (not measured).
  - HW test steps (package of 22:27). Reference: `run538_ct.txt` and
    `run538_sonic.txt` (scratchpad of session 2b073cf1; C-Calls ON).
    1. Install. The install keeps `nullDC.cfg`. On the main menu: JIT
       SPEED -> JIT SYS GATE GATE; VIDEO / DEBUG -> TA Peak Frame OFF and
       JIT C-Calls ON; the rest as in the 5.38 runs (SH4 Clock 200).
    2. UDP listener on port 18194, only when Matt says so.
    3. Boot 1 (GATE): Crazy Taxi at 60 Hz, ~2 min of play, exit to the
       XMB. Log: `[jitcfg] ... sysgate=1 tapeak=0`, `[gate]` lines, no
       fault. The game, the sound and the FMVs play as before.
    4. Boot 2: the same with JIT SYS GATE + RESUME (`sysgate=2`).
    5. Boot 3 (+ RESUME): Sonic Adventure, a level load and a few
       minutes of play.
    6. Boot 4 (optional): Crazy Taxi, + RESUME and TA Peak Frame ON.
    7. On a fault: the same boot with JIT SYS GATE OFF.
    Read: same-load VPS (`vtbins.py`) against the reference, `[gate]`
    runs/s, `[stall]` sys ms/s, `[CC] getcode` with + RESUME.
  - HW 2026-10-06 (package of 22:27, our listener at Matt's request;
    log `ps3log_536_run1.txt`, cut into `run536_gate_ct.txt`,
    `run536_resume_ct.txt`, `run536_sonic.txt` in the scratchpad of
    session f245bc41, with `dense.py` and `sleepbins.py`). Matt's cfg had
    JIT Div32 OLD (`Div32=0`), the references FIXED; Crazy Taxi's game code
    has no matched division.
    - All three boots clean, clean XMB exits. `[gate]`: 111.6K slices/s,
      28.4K updates/s (med 27.9K, spg ~225, kick ~250, tmu and dma ~0),
      syncs ~35/s. `[CC] timeslice` 112K -> 28.4K/s; with + RESUME `[CC]
      getcode` ~116K -> 32.6K/s.
    - Crazy Taxi at 60 Hz, dense windows (6K+ vertices), against
      `run538_ct.txt` (86.3% at full speed, worst frame mean 21.2 ms, p90
      22.7, sleep 135.9 ms/s): GATE 89.1%, 21.4 / 23.5 ms, sleep 158.8;
      + RESUME 94.5%, 20.8 / 22.1 ms, sleep 163.5 ms/s. Matt on + RESUME:
      "It felt much smoother now". VPS is capped at 60 in both, so the
      gain shows as sleep: +23 and +28 ms/s (55 windows each, noisy).
    - Sonic Adventure, + RESUME, 155 dense windows: 99.4% at full speed
      (`run538_sonic.txt` 97.7%, `run534_sonic.txt` 98.4%), worst frame
      mean 22.9 ms (24.0, 23.4).
    - The `[stall]` sys time did not fall: 63-79 ms/s with the gate, as
      before. So it is work per slice, not cost per call; my "3-5%" took
      it for call cost and was wrong. The gain is the C calls and lookups
      the JIT no longer makes: ~2-3% spare time in dense Crazy Taxi.
    - Matt: "SYSGATE is a success", then "Yes, make +RESUME the default":
      `SysGate` defaults to 2 since the package of 22:52 (`.got` 0xC180, 0
      `r2off`). A default only reaches a cfg without the key; Matt's has
      2 already. TA Peak Frame was not run (default OFF).

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
| 6.2 ARM7 Interleave SAMPLE | F1 ChuChu, F3 Looney Tunes | `[ ]` no target left: F3 fixed by 6.5, F1 fixed 2026-10-12 by "ARM7 STR Align" (bundle 9) |
| 6.3 ARM7 Rate (capped) | F1 ChuChu | `[ ]` no target left: F1 fixed 2026-10-12 by "ARM7 STR Align" (bundle 9) |
| 6.4 TMU Clock EFF | every game at SH4 150 | `[~]` built 2026-10-11 as "TMU Clock" SH4 / EFF, bundle 7 (section 0), default SH4, no HW run (Matt keeps SH4 Clock 200, where EFF changes nothing); merged 2026-10-06: P11 (built as "TMU Fine TCNT", HW run 2026-10-11, default ON in source), P57 (TMU test, in `misc/timebase_check`) |
| 6.5 AICA Race Fix | F3 Looney Tunes | `[x]` HW SUCCESS 2026-10-07 with P44, P31 and A3 (section 0), default ON since the package of 03:41; F3 fixed; merged 2026-10-06: P24 (built 2026-10-11 as "G2 DMA Small", bundle 6, default OFF; HW run 1: no fix for F1 in ChuChu Rocket), P31 (CA clamp, done) |
| 6.6 SPE AICA Sync | F2 Crazy Taxi crackle | `[x]` HW SUCCESS 2026-10-11 (Crazy Taxi crackle fixed, Matt: menu at ON, drive at + SAMPLE SYNC: "Fully gone."), default + SAMPLE SYNC since the package of 18:46 (section 0); the test moved to the twin's page map, and + SAMPLE SYNC syncs with no test |
| 6.7 JIT FMOV.D ABI fix | every game | `[ ]` |
| 6.8 Verified, not scheduled | various; merged 2026-10-06: P44 (F1, F3 unverified), P40, P49; P40 and P49 in bundle 9 (section 7) | **F1 FIXED on HW 2026-10-12 by 6.8's unaligned ARM7 word store: row "ARM7 STR Align" ON (bundle 9, section 0; Matt: "THE SOUND IS FIXED")**; default ON in source since 2026-10-12, not packaged |

### 6.0 Facts set on 2026-10-04

- F1 ChuChu Rocket (FIXED 2026-10-12 by "ARM7 STR Align", bundle 9,
  section 0: the cause was an unaligned ARM7 word store): Matt compared it with Flycast on a PC. Our sound effects
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
- F3, HW log 2026-10-07 (`ps3log_p16_run1.txt`, scratchpad of session
  f65e2239, SPE ARM7 ON): the ARM7 gets FIQs at ~700-990/s until the
  game's ARM7 reset (~t=18 s), then 69-156, then 0 for the rest of the boot
  (the other games: FIQs in every window, ~950-1880/s). Music plays; voices
  and effects stay silent with JIT Div32 OLD and FIXED alike. On return: a
  probe of the interrupt latch at each ARM7 reset and of the driver's timer
  and interrupt register writes (6.5, 6.8, P44). Parked (Matt: "Park it for
  now").
- F3 FIXED 2026-10-07 (Matt: "The sound cues ALL played correctly"): 5.13
  A3 (the voices go through the DSP only) with 6.5, P44 and P31, all
  default ON (section 0). The FIQs still stop after the game's ARM7 reset;
  the driver runs on its monitor loop and needs none.
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

### 6.4 TMU Clock EFF  `[~]` built 2026-10-11, bundle 7, default SH4, no HW run: Matt keeps SH4 Clock 200 (section 0)

- Built 2026-10-11: the row TMU Clock SH4 / EFF on the TIMING page, live.
  EFF scales a slice's count once (x 200 / clock, 12 fraction bits), with
  no carried remainder. The text below is the plan of 2026-10-04.
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
- 2026-10-10: the defect is for 150-195 MHz only. Above 200 MHz (5.45)
  the modules keep 200 MHz time, so the TMU is exact there. An EFF form
  must also keep the SYS GATE closed forms (5.36, 5.42): they need the
  same TMU cycles in every slice, so no carried remainder.
- Merged 2026-10-06 (`OPTIMIZATION_POTENTIAL.md` P11 + P57, open):
  - P11: TCNT is updated once per timeslice (`UpdateTMU_chan()`), so a read
    inside the slice sees an old count. Flycast `b31f92f8e` adds the cycles
    spent inside the slice (issue #2093: no randomness in Soul Calibur).
    Our JIT keeps the cycle count in a register (`ppc_cycles`), so it must
    store it before a cold memory call. Medium, minor gain.
  - P57: Flycast's `tests/src/hw/sh4/modules/TimerTest.cpp` (4 tests, 27
    asserts) as P11's PC check. Its "precision" and "underflow" cases
    encode P11's rule, so they fail on ours today. ~100 lines of harness.

### 6.5 AICA Race Fix (Looney Tunes)  `[x]` HW SUCCESS 2026-10-07, default ON, see section 0

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
- Merged 2026-10-06 (`OPTIMIZATION_POTENTIAL.md`, open):
  - P24, the same G2 DMA code: Flycast raises the end IRQ inside the start
    call when a transfer takes 512 SH4 cycles or less (128 bytes or less);
    longer ones stay scheduled at len x 4 cycles (Bangai-O startup, Nomad
    Soul, the Sports Jam announcer). Ours (DmaFix=1, the default) defers
    every transfer by at least 64 cycles, so it ends on a later timeslice.
    3 lines, behind DmaFix for an A/B, apart from the AicaRaceFix option.
    Keep the Read_SB_ADST guard.
  - P31, CA clamp (built 2026-10-07 as "CA Clamp", section 0): the LEA-3 clamp of 2.2 runs only in nullAICA mode; the
    twin and SPE ARM7 paths return the raw CA. Near the end of Looney
    Tunes' ring (LEA 8192) the two modes read a different CA (8189 with the
    clamp, 8191 without). Clamp after `ps3aica_read_mon` and `ac_twin_mon`
    with the slot's LSA/LEA (the halfword at A+2 on BE). caique: the
    hardware reads LEA at a one-shot end, so the clamp is Flycast's
    workaround, not hardware behaviour.

### 6.6 SPE AICA Sync (Crazy Taxi crackle)  `[x]` HW SUCCESS 2026-10-11, default + SAMPLE SYNC since the package of 18:46 (section 0)

- Built on 2026-10-11 with one change (Matt's pick): with SPE ARM7 ON the PPU has no twin, so the twin makes a
  page map of the wave RAM near each play point and the SH4 thread tests its writes on it; the exact test below
  runs on the twin. Section 0 has the build. The text below is the plan of 2026-10-04.
- The clipping step (the last line of the HW test below) is built on 2026-10-11 as the row SPE AICA Gain, after
  HW run 2 (Matt: "My bet's on the output clipping."). HW run 3: the log does not support clipping as the cause.
  Then two levels without the test, + BLOCK SYNC and + SAMPLE SYNC, as a proof of the missed tests (Matt: "Yes, go
  with b"). HW run 4: + SAMPLE SYNC removes the crackle of the drive (Matt: "Fully gone."), default since the
  package of 18:46. Section 0 has both.

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
  ARM7 and Flycast write the aligned word. Row "ARM7 STR Align" built
  2026-10-12 (bundle 9, section 0): HW SUCCESS, it fixes F1; default ON.
- AICA internal DMA (0x288C DEXE) exists in neither nullAICA nor caique.
- Sample-rate surplus: floor(150e6/44100) = 3401 gives 44104.7 Hz
  (+106 ppm); with AudioBuffers 0 the ring fill rises about 5 frames/s.
- nullAICA idle-slot EG readback is 0x3FF; caique and Flycast give 0x1FFF.
- ARM7 self-test: no STR/STRB, unaligned STR or register-window cases.
  STR, STRB and unaligned STR cases built 2026-10-12 (`arm7_optest.h`,
  bundle 9); still no register-window case.
- `_vmem` handler ids: only ids 0-7 fit (id*4 under HANDLER_MAX 0x1F,
  `dc/mem/_vmem.cpp:16-17`); 8 are registered, so a 9th would break.
- Merged 2026-10-06 from `OPTIMIZATION_POTENTIAL.md` (not scheduled):
  - P44 (built 2026-10-07 as "ARM7 Reset Fix", with the latch clear above; section 0): `arm_Reset()` sets `armMode = 0x1F` (System) for the cached
    engine and the JIT (`plugs/vbaARM/arm7.cpp`), and the SPE ARM7 does the
    same (`ps3/spu/arm7_spu.h`). A reset enters SVC (0x13): the AICA's
    ARM7DI is ARMv3, with no System mode. Reicast `4caa2b47` and Flycast
    `882e14f13` (2020) changed it to 0x13. A driver that sets SP right
    after reset (for SVC) and later selects SVC with a constant runs with
    SP at ARAM 0x7FE0 (the VBA R13_SVC), so its stack can overwrite its
    code or data. Link to F1 and F3 unverified. Steps: a log probe in one
    ChuChu Rocket boot (the first 8 mode changes after a reset; armMode
    and R13 per mode every 5 s; an SP of 0x03007Fxx in use confirms it),
    then 0x13 in both engines (2 lines, own toggle), the 45 self-tests,
    `misc/arm7spe_check` and `misc/arm7spu_check`. HW: ChuChu Rocket,
    Crazy Taxi, Sonic Adventure, Looney Tunes.
  - P40: Flycast `tests/src/AicaArmTest.cpp` (13 groups, ~312 checks) as
    self-tests on all three engines (cached, JIT, SPU). A pass rules out an
    engine bug in F1's note-off logic. Test only, no risk. It does not
    catch P49. Built 2026-10-12 (bundle 9, section 0): passes on the cached
    interpreter and the SPU engine (PC); the JIT runs it at boot, no HW run.
  - P49 (census built 2026-10-12, bundle 9, section 0; no HW run): with a register shift, Rn = PC reads insn+8 where the ARM7 reads
    insn+12 (the Rd = PC forms too), and STR/STRB of PC store a stale
    reg[15], in all three engines (Rm = PC is right). Not in the
    `[ARMHOT]` dumps (ChuChu is in none); a link to F1 is unlikely. Steps:
    a log census first; fix only if it fires (2-3 lines per engine,
    regenerate `arm7_cached_gen.inc`, two self-tests). Do not paste
    Reicast's `arm-new.h` diff.

---

## 7. Bundles of open OPTIMIZATION_POTENTIAL items (2026-10-07)

Matt 2026-10-07: "Merge the bundles then on OPTIMIZATION_PLAN.md". A bundle
is a set of open items that share code and checks, so one session can build
them together. The plan rule stays: each item keeps its own toggle and its
own HW run, default OFF until HW validated, SH4 Clock 200. Nothing is built
or scheduled: Matt picks a bundle, then the items. The full text (evidence,
file and line numbers) stays in `OPTIMIZATION_POTENTIAL.md` under the same
P numbers. Its line numbers are from 2026-10-04 and 2026-10-06, so search
for the function.

Update 2026-10-11 (Matt: "Yes, I want these in the OPTIMIZATION PLAN."):
the items of the sixth review (`OPTIMIZATION_POTENTIAL.md` P71-P85, line
numbers from 2026-10-11) have a place here. Those that share the code of a
bundle went into bundles 2, 4, 5, 8, 10 and 11 ("added 2026-10-11" in the
table and in each bundle). Bundles 13, 14 and 15 are new: they hold the
items that had no bundle, with P58 and P59 of the fifth review. P83 stays
with GOALS.md G11. Nothing of this is built.

Re-cut 2026-10-12 (Matt: "take the bundles, the goals, the unbundled
parts, and make them more fitting and more balanced. Keeping in mind which
parts I've done already.", then "Yes, do it."): the open work is cut again
into bundles 10 to 21.
- Closed: bundles 1, 2, 3, 4, 8 and 9 are done. Bundles 5, 6 and 7 are
  closed as bundles: their main parts are done. The table says where each
  tail went.
- Bundles 10 to 15 keep their numbers and their core. Old bundle 10
  (Maple) is now 10 (controllers) and 16 (the bus and the VMU). Old bundle
  11 (Output stage) is now 11 (render to texture and present) and 12,
  which also keeps the test tools. Bundle 13 takes the memory fixes and
  the three rows of bundle 1 that never ran. Bundle 15 takes the clock.
- Bundles 17 to 20 hold the tails of the closed bundles, one per code
  area. Bundle 21 holds what Matt parked for the end.
- Goals (GOALS.md): G12 is inside bundle 10. G10's remap and dead zone are
  in bundle 10, its flash save in bundle 15. G11 comes after bundle 15.
  G13 is last, with the rest of G10. G9 is closed.
- Order recommended: 11, 13, 14, 10, 15, 19, 17, 12, 16, 18, 20, then 21,
  then G11, then G10 with G13. Matt picks.
- A bundle number in a text of before 2026-10-12 (section 0, the item
  table of `OPTIMIZATION_POTENTIAL.md` where it says so) has its old
  meaning.
Nothing of the open bundles is built by this re-cut.

| # | Bundle | Items (plan home) | Main gain | Status |
|---|---|---|---|---|
| 1 | VRAM and TA-area writes | P43 (5.12), P55 + P7 (5.17), P26 write watch | the only SH4-thread speed left: P43 ~1-3% in Sofdec FMVs, P55 0.5% or less (estimates); P7 and P26 image fixes | `[x]` HW 2026-10-08 (section 0): P43 SUCCESS (FMV DMA -41/-45%), default ON in source; P55 no target (SQ non-TA flushes are RAM); P7 and P26 never exercised (Rez, Ikaruga stop on P58), OFF; re-cut 2026-10-12: P7, the P26 write watch and P55 wait in bundle 13 |
| 2 | GD-ROM and disc images | 3h (P21, P47, P23 TOC fields), 5.34 (P22 DMA rules, P9 DMA clamp, the parked CD-DA misses), P22 commands, P48, P9 2340-byte sectors, P23 GDI parser, P57 GDI paths, P34; P61 and P62 added 2026-10-10 (Matt); P77 and P82's lead-out stop added 2026-10-11, open | hangs and freezes in 10 named games; GDI sets; multi-disc games; one fix point for the CD-DA miss stalls; P77: no endless loop at a disc swap | `[x]` HW SUCCESS 2026-10-10 (ChuChu Rocket, Crazy Taxi; section 0): 14 rows on the GD-ROM page, all default ON in source (Matt), GD-ROM Speed REAL; packaged since the package of 22:06 (bundle 3); Disc Swap and the GDI rows not tested on HW yet; re-cut 2026-10-12: tail in bundle 19 (P77, the Disc Swap and GDI HW tests, the lead-out stop) |
| 3 | Texture path | P45, P52, P25 (trilinear, VQ formats), P1 steps 2-3 (section 0) | mipmaps on about half of the texture fills; palette VQ and planar VQ textures right | `[x]` HW SUCCESS 2026-10-10 (Crazy Taxi, Sonic Adventure; section 0): P45, P52 and the P25 VQ formats, 4 rows on the TEXTURES page; default in source: Mipmaps ON, Anisotropic 16x, VQ Formats ON (Matt); not packaged (the package of 22:06 has them OFF); VQ Formats not tested by a game that needs it; the P25 trilinear weights left out and P1 steps 2-3 dropped (Matt); re-cut 2026-10-12: tail in bundle 17 (the trilinear weights, a game for VQ Formats) |
| 4 | TA decode and draw rules | P8, P27 (3c), P25 (depth write, sprite cull, flat shading), P10 (lists, registers, tile clip, empty frame), P9 TA replay; P63 and three P66 items added 2026-10-10 (Matt); P79 and P80 added 2026-10-11, open | image fixes in 8 named games and KOS homebrew; no render-thread hang; P79: punch-through edges, P80: trapezoid sprites | `[x]` HW SUCCESS 2026-10-11 (Crazy Taxi, Sonic Adventure, Jet Grind Radio, Rayman 2; section 0): 16 rows on the RENDERER page; default ON in source (Matt) but TR Depth Write (GAME) and Flat Shading (OFF); Empty Frame left out (Matt); Sprite Cull, Tile Clip, List Passes, the TR volume draw and the TA Data Gate not used by a game yet; re-cut 2026-10-12: tail in bundle 17 (P79, P80, the rows no game used); the cost of 3k is bundle 14 |
| 5 | AICA RAM coherency | 6.6, P20 G2-G4 (5.13), P31 Mono and LFORE; P81 and P82's EXTS read-back added 2026-10-11, open | the Crazy Taxi crackle (F2); the DSP ring in wave RAM; P81: a probe for the Looney Tunes balance and the SPU clipping | `[~]` HW runs 1-4 2026-10-11 (section 0): SPE AICA Sync HW SUCCESS (F2 fixed: menu at ON, drive at + SAMPLE SYNC, the default since the package of 18:46, Matt), DSP Ring RAM no fault in PSO, default ON since the package of 15:02 (Matt); SPE AICA Gain added, stays at 100% (clipping was not the cause); the other 4 rows OFF: Mono Mix Fix ran with no word, DSP Ring 64K, DSP Reg Reads and LFORE Keep not on HW; re-cut 2026-10-12: closed as a bundle, tail in bundle 18 (the four OFF rows, P81, P82) |
| 6 | G2 DMA | P24 (6.5), P9 SB_ADDIR | Bangai-O and Nomad Soul freezes | `[~]` built 2026-10-11 (section 0): the rows G2 DMA Small and G2 DMA Read on the AUDIO page, default OFF, live; PC checks PASS; package of 19:22; HW run 1 2026-10-11 (ChuChu Rocket): the rows work, no fault, no fix for F1 (Matt: "Neither fixes the issue."); PARKED by Matt 2026-10-11, he tests Bangai-O and Nomad Soul later; re-cut 2026-10-12: closed as a bundle, its HW tests are in bundle 18 |
| 7 | Time base | 6.4 (with P11 and P57's TMU test), P28 | accuracy only | `[~]` HW runs 1-2 2026-10-11 (Crazy Taxi, ChuChu Rocket; section 0): 5 rows on a new TIMING page; TMU Fine TCNT, SPG Field Bit, HBlank IRQ and VBLANK INT Reset ran with no fault and are default ON (Matt: "Keep them as default, they seem harmless."), in the packages since 22:16 (the package of 21:15 has them OFF); TMU Clock stays SH4, no HW run: EFF needs SH4 Clock 150 to act and Matt keeps 200; **TMU Fine TCNT default OFF again since 2026-10-12 (Matt)**: about 95 ns per TCNT read, Quake3.cdi -10 to -17 SPEED points with it (bundle 8 runs 15 and 17), not packaged; PC checks PASS (`misc/timebase_check`); re-cut 2026-10-12: closed as a bundle, the TMU rows are in bundle 21 |
| 8 | SH4 interpreter, FPU and small JIT items | P46, P32, `JitMac` and `bcache` (5.3), 5.37, 5.39, 5.40, P57 RM verify (3d); P71 and P78's RFCR added 2026-10-11 | correctness; speed at noise level; P71: the frame rate of the Looney Tunes race in emulated time (it can cost SPEED) | `[x]` HW SUCCESS 2026-10-12 (Matt: "still mark this as a success"): JIT BCACHE default ON, JIT IDLE RULE default X30 with X3 BACKWARD as a level, FPU DN row removed (5.40 no effect), not packaged; open list "more friendly for other users" under Bundle 8; history: the JIT items built 2026-10-11 (Matt: "Ignore the Interpreter parts. I want JIT/Dynarec stuff."; section 0): rows JIT BCACHE, JIT PR MOVES (no fneg), JIT RM 2/3, JIT IDLE RULE and FPU DN on the JIT SPEED page, default OFF, and the JIT MAC fix; a `[ni]` probe for 5.40; PC checks PASS (`misc/jit8_check`, 78 of 78 mutants); package of 22:43; HW run 1 2026-10-11 (Crazy Taxi, rows OFF): clean, and 5.40 CLOSED by Matt (NI does nothing on the PPE); HW run 2 (Looney Tunes, JIT IDLE RULE X3 BACKWARD): the race draws 29.95 frames per emulated second (26.4) but SPEED is 60.9%, 18.2 frames per wall second (Matt: "It runs much worse"); HW run 3 (OFF): the race at SPEED 56.4%, the boot after the BIOS logo at 12.8% (Matt: "OFF is much worse."); X30 stays the default; HW run 9 (Sonic Adventure, X3 BACKWARD, JIT BCACHE ON): "VERY smooth" (Matt), 29.69 frames/s (28.65 at X30), 14K+ scenes 29.6 (26.3), SPEED 100%; HW run 10 (Phantasy Star Online, X3 BACKWARD): the 20 fps scene at 30.0 frames/s, SPEED 100%: the level is a per-game matter, open; HW runs 7-8 (Sonic Adventure, Phantasy Star Online, JIT BCACHE ON, X30): clean, frame rates as before; HW run 4 (Crazy Taxi, JIT BCACHE ON): correct (Matt: "Look and sounded fine"), 9255 sites, no gain to see there; HW run 6 (Looney Tunes, JIT BCACHE ON): HW SUCCESS, the race at SPEED 99.95% (97.62% OFF), sleep 58.6 ms/s (24.1), Matt: "It's much better", default ON in source since 2026-10-11 (not packaged); HW run 5 (Looney Tunes, JIT PR MOVES ON) did not hit the row (`prmoves=0`: the wrong game, it has no PR=1 FPU op); JIT PR MOVES, JIT MAC and JIT RM 2/3 have no HW run yet; P46, P32 and P78's RFCR not built; 5.37 done 2026-10-10, HW SUCCESS; re-cut 2026-10-12: the interpreter parts are in bundle 20, the open list and the heavy scenes in bundle 21 |
| 9 | ARM7 engines | P49 and P40 (6.8), 6.8's STR cases and unaligned store | rare encodings right; an engine check for F1 | `[x]` HW SUCCESS 2026-10-12; built 2026-10-12 (section 0): the P49 census (log only, three decoders), P40's tests and the STR cases (`arm7_optest.h`), row "ARM7 STR Align" on the AUDIO page, default OFF; PC checks PASS; package of 09:34; **HW runs 1-3 2026-10-12: the tests pass on all three engines (the JIT at boot); census A 0, B 0 in ChuChu Rocket and Crazy Taxi (the P49 fix has no target there, not built); "ARM7 STR Align" ON fixes F1, the ChuChu Rocket slow sound (Matt: "THE SOUND IS FIXED")**; HW runs 4-5 (Sonic Adventure, Looney Tunes, row ON): no unaligned store, no P49 form, no fault; **CLOSED 2026-10-12 (Matt), the row default ON in source, not packaged**; re-cut 2026-10-12: nothing left |
| 10 | Controllers (re-cut 2026-10-12; was Maple) | P84's stick dead zone, GOALS.md G12 (the second stick), P13, the DS3 remap of G10 | a smooth analog stick; the right stick; DS3 rumble; button maps | `[x]` HW SUCCESS 2026-10-12 (Matt: "We can consider this a success."; section 0): Controller FULL works in Quake III Arena (Matt: "Quake 3 double sticks works"; G12 done); built 2026-10-12 with bundle 11 (Matt: "Let's go with Bundle 10 and 11 together."): the CONTROLS page has Controller (STANDARD PAD / FULL (2 STICKS)), Stick Dead Zone (AXIS / RADIAL), Rumble Pack (OFF / ON) and 15 button-map rows; every default is the old path (Matt named none); PC checks PASS (`misc/pad_check`, 37 of 37 mutants); package of 17:40; no HW run: Stick Dead Zone RADIAL, Rumble Pack, a button-map change |
| 11 | Render to texture and present (re-cut 2026-10-12; was Output stage) | P75 (first), P66's RTT scaler, P76, P54, P60, P26 reads | the Crazy Taxi pause freeze; RTT games; no flashing screens or mirrors; interlaced and 24-bit 2D screens | `[x]` HW SUCCESS 2026-10-12 (Matt: "We can consider this a success."; section 0): RTT Read-back FIXED ends the Crazy Taxi pause freeze (Matt: "works as intended"); built 2026-10-12 with bundle 10: 10 rows on a new OUTPUT page (RTT Read-back, RTT Scaler, RTT Size, RTT Pack, Draw Retry, FB Line Double, Blank Video, Present Rule, Region Scissor, FB Reads), every default is the old path (Matt named none), so the Crazy Taxi pause still freezes until RTT Read-back is FIXED; PC checks PASS (`misc/rtt_check`, 88 of 88 mutants); package of 17:40; no HW run: Present Rule, RTT Size TILE CLIP; left: the Looney Tunes boot videos (not on the TV, cause not known); **the fix for the Looney Tunes race loading screen (black but the bar with Region Scissor ON), asked for by Matt after the verdict ("Fix it."): row "Keep Last Frame" (OFF / ON) built 2026-10-12, package of 19:52, PC checks PASS, HW SUCCESS 2026-10-12 (Looney Tunes, Matt: "Success! The image stays."; Crazy Taxi), default ON in source since 2026-10-12 (Matt: "Keep it up as default."), not packaged; it acts only with Region Scissor, which is still default OFF**; not built: the fb_concat bits of P26, P54's writers for pack modes 4 and 5 |
| 12 | Screen format and test tools (re-cut 2026-10-12; was HW test tools) | P50, P51, P53, P69's stretch, P10 (clip, scaler), P36 | SPEED on the TV without the UDP listener; PNG evidence; 16:9 output | `[x]` HW SUCCESS 2026-10-12 (Matt: "Success!"; section 0): Sonic Adventure with FPS Overlay ON, Screen Format 16:9 WIDE and Screen Scaler ON, SPEED median 99.99%, no fault; Matt named no default, every row stays at its old path; no HW run: the screenshot, 16:9 STRETCH, Cheats; built 2026-10-12 (Matt: "Work on Bundle 12"): a new SCREEN page with FPS Overlay (OFF / ON), Screen Format (4:3 / 16:9 STRETCH / 16:9 WIDE), Screen Scaler (OFF / ON) and Cheats (OFF / ON), every default is the old path; the screenshot is SQUARE in the in-game menu (no row); PC checks PASS (`misc/screen_check`, 178 of 178 mutants); package of 21:37; P10's FB clip was already Region Scissor (bundle 11); not built: Flycast's per-game widescreen table |
| 13 | MMU and memory (re-cut 2026-10-12; was MMU store queues) | P58, P78 (MMUCR mask, TLB data array 2), P74, P30, P65, bundle 1's P7, P26 write watch and P55 | Border Down, Rez, Ikaruga and Sonic Shuffle get past the BIOS logo; bundle 1's rows get their first run; NHL 2K2 | `[x]` **HW SUCCESS 2026-10-13 (Matt: "Mark it as a success, and we will check the Ikaruga and Rez issues later on."; section 0): with MMUCR Bits FIXED, Border Down, Sonic Shuffle, Ikaruga and Rez get past the BIOS logo and play (HW run 1 2026-10-12; Matt: "The game ran fine!", "Ikaruga runs!", "Rez ran fine!")**; left for later: the SH4-bound heavy scenes of Ikaruga and Rez (bundle 21, item 7); MMUCR Bits default FIXED in source since 2026-10-12 (Matt), not packaged (the package of 23:15 has OLD); HW run 2: Crazy Taxi, Looney Tunes and Sonic Adventure unchanged with FIXED (none turns the MMU on); not run: FB Write Watch ON (Ikaruga), the other three rows; LMMODE has no test game; P74's condition is met (Ikaruga and Rez are SH4-bound in heavy scenes, the old chain takes 15.2% and 11.2% of the SH4 thread); built 2026-10-12 (Matt: "We will now work on Bundle 13."): a new MEMORY page with MMUCR Bits (OLD / FIXED), P4 Mirrors (OFF / ON), Cache RAM Banks (OLD / FLYCAST) and Ch2 DMA Rules (OLD / FLYCAST), every default is the old path, so the four games still stop until MMUCR Bits is FIXED; the PS3 compiler confirms the wrong bits (`misc/mmu_check/bitfield_ppu.sh`); PC checks PASS (`misc/mmu_check`, 75 of 75 mutants); package of 23:15; bundle 1's LMMODE, FB Write Watch and VRAM Direct have no code change and wait for the HW run; P74 not built (the plan: only if a game is SH4-bound after the fix) |
| 14 | Render-thread draw cost | P72 (5.10), P73 | the cost of TR Triangle Sort (Crazy Taxi render busy 45%, SPEED 97.9% at 11-13K vertices); every draw | `[x]` **HW SUCCESS 2026-10-13 (Matt: "Yes, bundle 14 is a success. Keep it ON.")** after HW runs 1-3 with the three rows ON (Crazy Taxi: render busy 47.2% -> 29.2% at 9K+ vertices, a bind 1.47 -> 0.53 us, 1033 -> 615 TR draws per render; Sonic Adventure 2; Rayman 2; no fault); **the three rows default ON in source since 2026-10-13 (TR First Pass at + GAME ORDER), not packaged (the package of 01:15 has them OFF)**; built 2026-10-13 (Matt: "Let's work on bundle 14!"; section 0): a new DRAW COST page with Tex Bind Table (OFF / ON), GL State Compare (OFF / ON) and TR First Pass (OLD / SKIP / + GAME ORDER), all live, every default the old path at the build; counters for the merge key and the texture lifetime (`[cost]`, `[texlife]`); the index list not built; PC checks PASS (`misc/glpath_check`, 73 of 73 mutants; `misc/rend4_check`, 88 of 88 mutants); package of 01:15 on 2026-10-13 |
| 15 | Flash and clock (re-cut 2026-10-12; was Flash defaults) | P59 (corrects 3i), P85, P37's BIOS language row, P12 | Capcom vs. SNK 2, Border Down and Rayman 2 at 60 Hz; a new flash in English; no BIOS settings lost on a forced exit; the block code that G11 needs | `[~]` built 2026-10-13 (Matt: "We will work on Bundle 15 now."; section 0), no HW run yet: a new FLASH / CLOCK page with Broadcast (CONSOLE / NTSC / PAL / DISC), BIOS Language (CONSOLE and the six languages), Flash Defaults (OLD / FLYCAST), Console ID (OLD / FLYCAST), Exit Flash Save (OFF / ON, live) and RTC Local Time (OFF / ON), every default is the old path; no row's value reaches `dc_flash_wb.bin` but the console ID; a `[flash]` line at each boot shows the bytes; the old logs: 10 of 23 games stay at 50 Hz in every boot, and the PS3 clock is UTC with no time zone, so P12's line never runs there and the row RTC Local Time (the XMB's zone and summer time) is an addition; PC checks PASS (`misc/flash_check`, 94 of 94 mutants); package of 09:56 on 2026-10-13 (= the G14 package + this bundle); not built: Flycast's block reuse in `alloc_block` (G11 needs it on a flash as full as Matt's) |
| 16 | Maple bus and VMU (new at the re-cut of 2026-10-12) | P29, P9 SB_MDST, P68's maple parts, P37 (VMU per game, VMU LCD, keyboard and mouse) | Geist Force and Silent Scope; no maple hang or crash; one VMU file per game | `[ ]` |
| 17 | Draw rules tail (new at the re-cut of 2026-10-12) | P79, P80, 3b (offset colour, colour clamp), P25's trilinear weights, the open verdicts of bundles 3 and 4 | punch-through edges (Jet Grind Radio); trapezoid sprites; specular highlights; trilinear (Shenmue) | `[ ]` |
| 18 | Sound wrap-up (new at the re-cut of 2026-10-12) | bundle 5's four OFF rows, P81, P82, bundle 6's HW tests, P69's VMU beep, P31's nullAICA-only fixes | the Looney Tunes balance (a probe); the Bangai-O and Nomad Soul freezes; a verdict for every sound row | `[ ]` |
| 19 | Disc tail (new at the re-cut of 2026-10-12) | P77 with the first Disc Swap HW test (P34), the GDI HW tests (P23, P57), P82's lead-out stop, the parked CD-DA miss stall (5.34) | multi-disc games (Shenmue); GDI sets; no stall at a CD-DA miss | `[ ]` |
| 20 | SH4 correctness tail (new at the re-cut of 2026-10-12) | P46, P32, P64, P67, P78's RFCR, P68's system parts, P33, the verdicts for JIT PR MOVES, JIT MAC and JIT RM 2/3 | correctness (Resident Evil Code Veronica X); homebrew .elf | `[ ]` |
| 21 | Frame rate in heavy scenes (new at the re-cut of 2026-10-12) | bundle 8's list "more friendly for other users", the SH4-bound heavy scenes (section 0), 5.46's verdict, bundle 7's TMU rows, the Capcom vs. SNK 2 menus, Ecco the Dolphin, settings per game | frames at SPEED 100% without a choice by the user (Looney Tunes, Sonic Adventure 2) | `[ ]` parked for the end (Matt 2026-10-12) |

Main gains:
- Speed: little is left. Only bundle 1 gives SH4-thread time, and it is
  small (estimates). Bundle 2 removes stalls, not average load. Bundle 8 is
  at noise level. No open item targets the Looney Tunes race (SH4-bound,
  section 0); the 5.18 profile measured only Crazy Taxi.
- Most games fixed: bundle 2. Most visible: bundle 3 (mipmaps). Audible:
  bundle 5 (the crackle).
- Update 2026-10-11: P71 (bundle 8) is the first item that targets the
  Looney Tunes race. It acts on emulated time, not on host speed, and it
  can cost SPEED. Bundle 14 gives render-thread time (measured: the bind
  is 11 points of busy in Crazy Taxi at + OPAQUE DEPTH). Bundle 13 lets
  four games boot. P75 (bundle 11) ends the Crazy Taxi pause freeze.
- Update 2026-10-12 (re-cut): the largest gains left, in the recommended
  order: bundle 11 ends the Crazy Taxi pause freeze, bundle 13 lets four
  games boot, bundle 14 gives render-thread time, bundle 10 fixes the
  stick, bundle 15 gives three games 60 Hz.

### Bundle 1: VRAM and TA-area writes  `[x]` HW 2026-10-08, P43 default ON, the rest OFF (section 0)

- Re-cut 2026-10-12: the rows that no game ran (P7, the P26 FB Write
  Watch, P55) are in bundle 13. They need Rez and Ikaruga, which boot
  after P58.
- Items: P43 (5.12: the YUV converter, PPU form), P55 and P7 (5.17), P26
  (the framebuffer write watch only; its reads are in bundle 11).
- Shared code: `dc/pvr/pvr_if.cpp` (`TAWrite()`, `TAWriteSQ()`,
  `pvr_write_area1_16/32`, `YUV_data()`) and the ch2 DMA in
  `dc/sh4/dmac.cpp`.
- Shared first step, one probe build (log only): split the SQ flushes and
  the ch2 DMA bytes by target (TA FIFO, YUV converter, VRAM 64-bit, VRAM
  32-bit) on the `[ta-split]` line, and log SB_LMMODE0/1 at each change.
  It is the first step of P7 (LMMODE in a few games; Rez must stay right),
  P55 (count the VRAM-direct blocks) and P43 (the split is not logged).
- Merge point: P55 replaces the per-word `pvr_write_area1_32` calls with
  direct stores. P26 puts its watch in those calls, so the direct path
  must keep the watch.
- Gain:
  - P43: ~1-3% of the SH4 thread in Sofdec FMVs (estimate). In the Sonic
    Adventure intro FMV with YUV Block Decode ON, `[stall] dma` is 34-45
    ms/s (3.4-4.5% of the SH4 thread), and the SH4 thread is the limit
    (section 0, the 5.12 block). P43 is also on that 5.12 list.
  - P55: 0.5% of the SH4 thread or less in dense Crazy Taxi (estimate).
  - P7: a texture sent through the 64-bit LMMODE path no longer lands
    scrambled (games unknown). P26: a 2D screen that redraws only part of
    the frame shows again (Flycast: Ikaruga loading screens).
- Byte order: only P43 needs care (it loads the planes as 32-bit words).
- Checks: PC first, bit-exact (P43 against the current converter: random
  planes, the four quadrants, blocks split across calls). HW: the Sonic
  Adventure intro FMV (`[stall] dma`, SPEED), dense Crazy Taxi (same-load
  VPS), the Looney Tunes intro FMV.

### Bundle 2: GD-ROM and disc images  `[x]` HW SUCCESS 2026-10-10, every row default ON in source (section 0)

- Re-cut 2026-10-12: the tail is bundle 19 (P77, the Disc Swap and GDI
  HW tests, P82's lead-out stop, the CD-DA miss stall).
- Items: 3h (P21, P47, P23 TOC fields), 5.34 (P22 DMA rules, P9 DMA clamp,
  the parked CD-DA misses), P22 commands, P48, P9 2340-byte sectors, P23
  GDI parser, P57 GDI paths, P34 disc swap.
- Shared code: `dc/gdrom/gdromv3.cpp`, `plugs/ImgReader/` (`iso9660.cpp`
  for GDI, `common.cpp` for the TOC and the sector forms),
  `ps3/ps3_gdasync.cpp`.
- Shared checks: `misc/gdasync_check` already builds the real
  `gdromv3.cpp` and `ps3_gdasync.cpp`. Flycast's `tests/files/test_gdis`
  fixtures and `GdiTest.cpp` can become a check in the `misc/cdi_check`
  style (P57, ~60 lines).
- Merge points:
  - P21 step 2 starts CD-DA 5 ms after CD_PLAY. The parked 5.34 fix
    ("silence for the missed sector, or start the read-ahead at the play
    command") goes in the same CD_PLAY handler. The read-ahead is
    `gda_cdda_next()` in `ps3/ps3_gdasync.cpp`.
  - P34 must wait for the 5.34 worker (`gda_wait()`, as the GD-ROM reset
    does) and drop the CD-DA ring before it opens the next image.
  - P22's DMA pacing and 5.34's wait both set when a DMA ends: build the
    pacing on top of 5.34.
- Gain: hangs and freezes in named games: Pro Pinball Trilogy (SB_GDLEN =
  0), World Series Baseball 2K2 and Sakura Taisen 3 music (DMA pacing),
  Hydro Thunder and NFL Blitz 2001 (P21 step 1), Cool Boarders Burrrn, Ms.
  Pac-Man and Bust-a-Move 4 (P21 step 2), V-Rally 2 CD-DA and Eldorado
  Gate (P23 TOC fields). GDI sets with quoted names or `\` paths boot.
  Multi-disc games get past disc 1 (Shenmue I and II, Skies of Arcadia,
  Grandia II, D2, RE Code Veronica). Each CD-DA miss stalls the SH4 thread
  24-85 ms (ChuChu: 11 misses in the 5.34 re-test). No average speed.
- Section 3 (3h) was shelved by Matt on 2026-09-23.
- HW: ChuChu Rocket and Looney Tunes CD-DA, the BIOS CD player, Crazy Taxi
  loading; a GDI set with spaces in its file names; a two-disc game for
  P34.
- Added 2026-10-11 (sixth review; open, nothing built):
  - P77, before the first HW test of Disc Swap. `gdrom_disc_swap()`
    clears the command in flight but not the state, so a swap during a
    command ends in an endless `verify`. Flycast stopped these clears in
    `cbd01fba3`. Drop them (about 8 lines less). Then the open-lid phase
    before the BUSY second (about 12 lines), if a game ignores the swap.
  - P82's stop of CD-DA at the lead-out (about 8 lines; "not taken" in
    section 0, low).

### Bundle 3: Texture path  `[x]` HW SUCCESS 2026-10-10, default in source: Mipmaps ON, Anisotropic 16x, VQ Formats ON (section 0)

- Re-cut 2026-10-12: the tail is in bundle 17 (the trilinear weights, a
  game for VQ Formats).
- Built (Matt's picks, 2026-10-10): P45, P52 and the P25 VQ formats, as
  the rows Mipmaps, Anisotropic, Texture Filter and VQ Formats on a new
  TEXTURES page. Left out: the P25 trilinear weights. Dropped: P1 steps
  2-3 (step 2 is done by SPE Tex Decode; step 3 has no image or speed
  gain). The hash covers the top level only, not the whole chain. The
  text below is the plan of 2026-10-07.
- Items: P45 (the whole mip chain), P52 (anisotropic filtering, a filter
  override), P25 (the two-pass trilinear weights, palette VQ and planar
  VQ), P1 steps 2-3 (block decoders, 16-bit RSX formats).
- Shared code: `ps3/ps3_tex.cpp` (`describe()`, `decode()`, the hash) and
  `ps3/ps3gl/ps3gl_textures.c` (the mip path that never ran,
  `g_mipmap_enabled` = 0, `GCM_TEXTURE_MAX_ANISO_1`).
- Merge point: P42's `ps3gl_tex_direct_begin()` returns NULL for a
  swizzled texture and writes level 0 only. (YUV textures are linear:
  `ps3_tex.cpp` sets the no-swizzle hint for fmt 3.) P1 step 2 needs the
  swizzled order and P45 needs the mip levels: extend it once for both.
- P52 and the P25 trilinear weights come after P45: they need mip levels.
- Gain:
  - Image: mipmaps on about half of the texture fills: Crazy Taxi 879 of
    1856, Sonic Adventure 810 of 1252 (`[ps3gl] mip uploads` reads 0
    today). Distant roads and floors stop aliasing. Palette VQ and planar
    VQ textures stop decoding as garbage. Trilinear: NFL 2K2 field,
    Shenmue snowflakes.
  - Speed: small. Decode runs on misses only, 0.8% of the render thread in
    play (section 0, the 5.12 block). P45 adds ~33% decode on mipmapped
    misses and ~1% of the render thread for the longer hash (estimate). P1
    step 3 halves the RSX memory of 16-bit textures.
- Risks: the ps3gl mip path never ran on HW. RSX linear textures froze the
  console twice in G7, so the first build puts no mips on mirrored
  textures. Check the 16-bit byte order on HW with a non-uniform image.
- Checks: PC first, bit-exact on the final RSX bytes, as `misc/yuv_check`
  (every format, mip and VQ case, random VRAM). HW: Crazy Taxi roads and
  Sonic Adventure floors at a distance (`[ps3gl] mip uploads` above 0,
  render `busy`); Rayman 2 (mip and VQ textures in use; its untextured
  surfaces are closed, 3j).

### Bundle 4: TA decode and draw rules  `[x]` HW SUCCESS 2026-10-11, default ON in source but TR Depth Write and Flat Shading (section 0)

- Re-cut 2026-10-12: the tail is in bundle 17 (P79, P80, Flat Shading,
  the rows that no game used). The cost of 3k is bundle 14.
- Built (Matt's picks, 2026-10-10): every item below but P10's Empty
  Frame; added P63 and, from P66, the invalid TA types, the volume cap and
  "no data before list init". 16 rows on a new RENDERER page. The tile
  clip rect is packed into reserved param bits, so DcParam and the SPE
  layout did not change. The text below is the plan of 2026-10-07.
- Items: P8 (intensity colour clamp order), P27 (3c: TR-list modifier
  volumes), P25 (depth write, sprite cull, flat shading), P10 (region
  array, LIST_CONT passes, user tile clip, TA_ISP_CURRENT, SOFTRESET bit 1,
  empty frame), P9 (the TA replay `die()` on Object_List_Set and types 3
  and 6).
- Shared code: the PPU decoder and draw (`ps3/ps3_rend.cpp`) and the SPU
  decoder (`ps3/spu/spe_dec.c`). SPE Draw is ON by default, so the RSX
  draws the SPU lists.
- Merge points: P8, P27 and the P10 tile clip change both decoders the
  same way (the tile clip also changes DcParam and the SPE layout). The P25
  sprite cull changes only `apply_param_body` and the merge parity check.
  For P9, also check what the SPU decoder does with those records.
- Gain: image fixes: Red Dog (P8), Xtreme Sports shadows (P27), Worms World
  Party, Bust-a-Move 4 and Re-Volt (P25 PT depth write), South Park Rally
  and Wacky Races menus (P25 sprite cull), Cosmic Smash (P10 LIST_CONT),
  KOS homebrew backgrounds (P10 TA_ISP_CURRENT). No render-thread hang on
  reserved TA records (P9). The integer form of P8 also removes 3
  double-precision multiplies per intensity vertex on the SPU.
- Checks: the S2 compare (SPE Draw OFF, SPE Doorbell ON) stays bit-exact,
  then SPE Draw ON for the image. HW: Crazy Taxi, Sonic Adventure, Soul
  Calibur; the named games only if Matt has them.
- Added 2026-10-11 (sixth review; open, nothing built):
  - P79, a third level of the PT Blend row: a punch-through texel that
    passes the alpha test draws with alpha 1, as Flycast GL (its DX9
    disagrees). Jet Grind Radio then draws its PT list with no blend.
    About 6 lines.
  - P80: the sprite's fourth corner from the plane of the other three, in
    both decoders (`misc/ta4_check`). About 15 lines PPU, 30 SPU.
  - The speed follow-ups of 3k are in bundle 14.

### Bundle 5: AICA RAM coherency  `[~]` HW runs 1-4 2026-10-11: SPE AICA Sync SUCCESS (default + SAMPLE SYNC), DSP Ring RAM no fault (default ON); 4 rows OFF (section 0)

- Re-cut 2026-10-12: closed as a bundle. The tail is bundle 18 (the four
  OFF rows, P81, P82's EXTS read-back).
- Built (Matt's picks, 2026-10-11): every item below, as the rows SPE AICA Sync, DSP Ring RAM, DSP Ring 64K, DSP
  Reg Reads, Mono Mix Fix and LFORE Keep at the end of the AUDIO page. The hazard test of 6.6 moved to a page
  map that the twin makes (the ARM7 SPU, or the PPU twin with SPE ARM7 OFF): with SPE ARM7 ON the PPU knows no
  CA. The text below is the plan of 2026-10-07.
- Items: 6.6 (SPE AICA Sync), P20 G2-G4 (5.13: the DSP ring in wave RAM,
  64K-word rings, live DSP register reads), P31 (Mono halving, LFORE).
- Shared part: one hook on guest wave RAM writes. A write inside a live
  slot's unrendered span syncs the SPU (6.6). A write inside the DSP ring
  sends a ring-dirty record (G2). The SPU writes its ring output and its
  dirty cache lines back to wave RAM by DMA (G2, G3).
- Trap: 6.6 and G2 assume that the PPU sees every wave RAM write. With SPE
  ARM7 ON (default since 2026-10-05), ARM7 stores reach wave RAM by DMA
  from that SPU (`ps3/spu/spe_arm7.cpp` puts lines at `aram_ea`). The hook
  must cover those puts too.
- Gain: the Crazy Taxi crackle with SPE AICA Sound, also at full speed
  (F2). The DSP ring in wave RAM: no old echo after a DSP program change
  (Looney Tunes changes its DSP program at each load; a link to its
  loading echo is not shown). 64K-word rings work (`slow=` on the `dsp
  jit` line finds such games; Crazy Taxi, Sonic Adventure and ChuChu
  Rocket show 0). Mono: Sega Bass Fishing Challenge saturation.
- Cost: 6.6's sync waits have a cap: 3.9% of wall or less with A2, 15% or
  less with A3 and the DSP JIT ON (now the default). 6.6 expects
  0.01-0.3% with A2.
- Checks: `misc/spu_sim`, `misc/a2_check` and `misc/a3_check` stay
  bit-exact (6.6). HW: the 6.6 steps (Crazy Taxi main menu music at full
  speed), Looney Tunes loads.
- Added 2026-10-11 (sixth review; open, nothing built):
  - P81, for the Looney Tunes balance and the SPU clipping (both open in
    section 0). Two tests with no code first: a Crazy Taxi drive with SPE
    AICA DSP OFF (read `peak=`), and Looney Tunes against Flycast with its
    DSP ON. Then a probe on the `[aica-spe] sound` line (about 45 lines).
    No fix is known. The boot of the complaint played no CD audio, so the
    lead "the music is CD audio (EXTS)" does not hold for it.
  - P82's EXTS read-back, inside the DSP Reg Reads row (about 10 lines).

### Bundle 6: G2 DMA  `[~]` PARKED 2026-10-11 (Matt); built, default OFF; HW run 1 (ChuChu Rocket): works, no fix for F1; Matt tests Bangai-O and Nomad Soul later (section 0)

- Re-cut 2026-10-12: closed as a bundle. Its HW tests (Bangai-O, Nomad
  Soul) are in bundle 18.
- Built (2026-10-11): both items below, as the rows G2 DMA Small and G2
  DMA Read at the end of the AUDIO page, live. A small start over a
  running transfer (AICA Race Fix OFF only) stays scheduled. The text
  below is the plan of 2026-10-07.
- Items: P24 (6.5: small transfers complete at once), P9 (SB_ADDIR = 1:
  wave RAM to system RAM).
- Shared code: the G2 DMA start in `dc/aica/aica_if.cpp`, next to the AICA
  Race Fix (6.5). `misc/g2dma_check` builds the real `aica_if.cpp` (8
  configurations).
- P24: a transfer of 512 SH4 cycles or less (128 bytes or less) raises its
  end IRQ in the start call, behind DmaFix for an A/B. Longer ones stay
  scheduled (the Sports Jam announcer needs that). Keep the Read_SB_ADST
  guard. P9: swap the source and the destination (both RAMs hold native
  u32 words), then raise the end IRQ.
- Gain: the Bangai-O startup freeze, the Nomad Soul freeze and crash
  (P24). No endless wait on a DMA from wave RAM (P9; no game known, the
  Looney Tunes logs have no such message). About 5 lines.
- HW: ChuChu Rocket (its stage sends G2 DMAs of 30-75 bytes; Looney Tunes
  sends 512-byte ones, above the limit; Crazy Taxi sends none; section 0,
  corrected 2026-10-11); Bangai-O and Nomad Soul only if Matt has them.

### Bundle 7: Time base  `[~]` HW runs 1-2 2026-10-11, the 3 SPG rows default ON since the package of 22:16 (Matt); TMU Fine TCNT default OFF again since 2026-10-12 (Matt; it slowed Quake3.cdi); TMU Clock SH4, no HW run (section 0)

- Re-cut 2026-10-12: closed as a bundle. TMU Clock and TMU Fine TCNT are
  in bundle 21.
- Built (Matt's picks, 2026-10-11): every item below, as the rows TMU
  Clock, TMU Fine TCNT, SPG Field Bit, HBlank IRQ and VBLANK INT Reset on a
  new TIMING page. The TimerTest is in `misc/timebase_check`. Not as the
  text below says: 6.4 has no carried remainder (a fixed-point slice count);
  the "underflow" case passes, "smallTCOR" does not (ours takes a modulo
  where Flycast clamps; left as it is). The text below is the plan of
  2026-10-07.
- Items: 6.4 (TMU Clock EFF, with P11 and P57's TMU test), P28 (the SPG
  field bit, HBlank IRQ modes, the SPG_VBLANK_INT reset value).
- Shared code: the 5.36 SYS GATE. TCNT reads add the skipped slices
  (`tmu_tcnt_now()` in `dc/sh4/tmu.cpp`), SPG_STATUS reads work out their
  value, and the planner asks `tmu_slices_to_event()` and
  `spg_slices_to_event()` (`dc/sh4/sys_gate.h`).
- Merge points: P11 adds the cycles inside the current slice to
  `tmu_tcnt_now()` (the JIT must store `ppc_cycles` before a cold memory
  call). 6.4 changes the TMU rate. P28 changes the SPG events, so
  `spg_slices_to_event()` and the SPG_STATUS read must follow it.
- Checks: `misc/sysgate_check` builds the real `tmu.cpp`, `SPG.cpp`,
  `regs.cpp` and `sh4_interpreter.cpp`. Add Flycast's TimerTest: its
  "precision" and "underflow" cases fail on ours today (P11's rule).
- Gain: accuracy only. TMU periods right at SH4 150 (at 200, the HW test
  clock, they are already exact). TCNT inside a slice (Soul Calibur
  randomness, Flycast issue #2093). The field bit once per frame. HBlank
  IRQ modes 0 and 2. Cost: one multiply per slice (6.4); HBlank mode 2
  adds one event per line, only in games that set it.
- HW: the ChuChu Rocket menu at SH4 150 (6.4), Soul Calibur, Crazy Taxi.

### Bundle 8: SH4 interpreter, FPU and small JIT items  `[x]` HW SUCCESS 2026-10-12 (Matt), the JIT items: JIT BCACHE default ON; JIT IDLE RULE default X30 with X3 BACKWARD as a level; FPU DN row removed (5.40 no effect); JIT PR MOVES, JIT MAC, JIT RM 2/3 OFF; the interpreter parts not built; open list "more friendly for other users" below (section 0)

- Re-cut 2026-10-12: the interpreter parts, P78's RFCR and the verdicts
  for the OFF rows are in bundle 20. The list below and the SH4-bound
  heavy scenes are in bundle 21.
- **What to do to make bundle 8 more friendly for other users** (Matt
  2026-10-12; nothing of it is built or scheduled). Today a user must know
  which games gain from JIT IDLE RULE X3 BACKWARD:

  | Game | X30 (default) | X3 BACKWARD |
  |---|---|---|
  | Phantasy Star Online Ver. 2, heavy scenes | 20 frames/s | 30, SPEED 100% |
  | Sonic Adventure, heavy scenes | 26 frames/s | 30, SPEED 100% |
  | Jet Grind Radio (US), 12K+ vertices | 20-24 frames/s | 30, SPEED 100% (run 18) |
  | Sonic Adventure 2 | 50-52 frames/s | 60, but SPEED 92.5% at 14K+ vertices |
  | Crazy Taxi | 60 frames/s | 60, SPEED -2.5 points at 12K+ |
  | Looney Tunes Space Race, race | 26 frames/s, SPEED 100% | SPEED 61%, 18 per wall second |
  | Quake3.cdi (homebrew), play | SPEED 78% | SPEED 42% |

  1. Take the choice from the user. Three ways, none chosen: a setting
     per game (Matt's parked idea of a per-game ID checker); a rule that
     uses X3 only while the host has spare time (not designed; the bill
     is in the compiled block today); or X3 for all games after the wait
     loops below cost nothing (points 2 to 4: way (A), way (B) as
     Reicast, the timer poll).
  2. Way (A), the wider JIT IDLE SKIP: Looney Tunes waits on a memory
     counter in a 3-block loop (8C01453E, 8C0713D0, 8C014544; 46.6% of
     the JIT weight at X3, run 12). The candidates would take a closing
     block with FPU reads and a few more opcodes; the run-time proof
     stays (no store, the same register hash, plus the block's FPU
     inputs). About 50 lines and `misc/idle_check`. Risk: a work loop
     taken for a wait loop loses frame rate in that place.
  3. Way (B), Reicast's approach (Matt 2026-10-12: "can you also mention
     the Reicast 'approach' we thought of? It may be worth a check."): a
     list of known wait loops. Reicast and Minicast
     (`libswirl/hw/sh4/dyna/decoder.cpp:30-63`, `:1147-1154`) keep
     `idle_hash`, 12 SHA-1 hashes of block code (4 BIOS, 3 "SC", 2 "HH", 3
     more); a block whose hash is in the list bills `max_cycles`, so its
     turn ends the timeslice. Its x3 rule is our X3 BACKWARD already.
     - For us: the same list with our own entries, and a listed block
       ends the slice through JIT IDLE SKIP's exit. First entries from
       this bundle's logs: Looney Tunes 8C014544 (the block that closes
       the 3-block wait, run 12), Quake3.cdi 8C08467C and 8C08468A (the
       two JIT IDLE SKIP candidates of its timer poll, run 16; not yet
       proven by a profile to be the whole loop).
     - What it gives over way (A): no proof at run time, so it also
       fits a timer poll, where the read value moves and way (A) cannot
       hold. A listed loop costs about one turn per slice.
     - What it costs: data per game. Each new game needs a JIT
       HOTBLOCKS run at X3 BACKWARD and a new entry, and a wrong entry
       (a work block) starves that game. It is a per-game table inside
       the emulator: check it against Matt's "no game presets" rule
       before a build.
     - The check that is worth it first (no build of the skip): port
       Reicast's `hash()` and log which compiled blocks match its 12
       entries in the BIOS, Soul Calibur and Ikaruga. If they match,
       Reicast's own list comes for free. Trap: `hash()` reads the code
       through a `u8*` (`u16 data=ptr[i]`), so it hashes the block's
       first `guest_opcodes` bytes, each as 2 bytes, and its `0xD`
       reloc mask never acts; on our big-endian host feed the bytes
       {b, 0}. About 80 lines with the SHA-1.
     - Ways (A) and (B) can live together: (A) for the loops it can
       prove, (B) for the others.
     - **Matt 2026-10-12: "In Bundle 8, let's go with Reicast's approach
       for the JIT. It's worth a shot." Built 2026-10-12 as the row JIT
       IDLE HASH, default OFF, package of 01:37. HW run 19 (section 0):
       Looney Tunes race at X3 BACKWARD, SPEED 60 -> 88%, 29.9 frames per
       emulated second; not full speed. No verdict from Matt yet.**
     - The check of Reicast's 12 hashes, done on the PC with the old HW
       logs (7713 dumped blocks, `[HOT]` and `[idle]` dumps of every
       session; `scan_logs.py`, scratchpad of session 1c8776dd). The
       reading of `hash()` is right: the BIOS wait 8C0BA526 gives
       Reicast's second BIOS entry. Six entries match our blocks:

       | Entry | Blocks in our logs | What it is |
       |---|---|---|
       | BIOS `04:CB0C9B99` | 8C0BA526 in every game; 8C06155A in Sonic Adventure | the BIOS wait; the Sonic block is a loop with calls and a store |
       | SC `04:DD9FDF9D` | Crazy Taxi 0C15A678, Soul Calibur 8C224818, Sonic Adventure 8C645DB8, Sonic Adventure 2 8C12CBB8, ChuChu 8C0FE5B8, Jet Grind Radio 8C226258, Phantasy Star Online 8C3C53F8 | the library wait loop that JIT IDLE SKIP skips already |
       | SC `04:BC1C1C9C`, `0A:5693F8B9` | Crazy Taxi 0C15A658, 0C15A660 | the head and the middle of the same loop |
       | IKA/HH `0D:DF0C1754` | Looney Tunes 8C1ADD60 | a function entry, 1.24M runs/s in the first 10 s of 3D, then none |
       | IKA/HH `04:DB35BCA0` | Looney Tunes 8C1ADD7A, 8C090918 | `add #8,r15; lds.l @r15+,PR; rts`: the return of any function with that end; 8C090918 runs 33-43K times/s |

       So Reicast's list gives us no new wait loop in these games, and
       its half-block hash makes two entries match work code (4 opcodes
       = the first 4 bytes = 2 opcodes). With the slice end, 8C090918
       alone would cost Looney Tunes up to a fifth of its SH4 cycles
       (estimate: 43K half slices per second). Reicast's own bill there
       is 224 cycles per run.
     - What is built, for that reason: our entries are in `hash()`'s full
       form, with the block address, so the same bytes in another game do
       not match. Levels: ON = our list acts, a block of Reicast's list
       is logged only (`[idlehash] ... Reicast's list: log only`); +
       REICAST LIST = both act. `dc/sh4/rec_v2/idle_hash.h` (SHA-1, the
       two lists, `idle_hash_listed`), `dec_idle_hash()` in the decoder
       (blocks up to 64 opcodes), `block.idle_hash`, and one `li
       cycles,0` after the block's bill in `ngen_Begin`: the next poll
       ends the slice, about one turn per slice. `[jit8]` prints `idle
       hash <level> listed=<blocks>`.
     - Our list has one entry: Looney Tunes 8C014544
       (`>:1:8C014544:06:82CA5453:...`). Quake3.cdi has none: the logs
       hold no code for 8C08467C and 8C08468A (the 64 `[idle]` dumps ran
       out before them). A new entry needs the block's words from a JIT
       HOTBLOCKS run: `/tmp/hash8_check <address> <words>` prints the
       line (after `misc/jit8_check/run.sh`).
  4. The timer-poll wait: Quake3.cdi reads TCNT 18-59K times per emulated
     vblank until its time is over (run 16). Way (A) does not catch it;
     way (B) can. Capcom vs. SNK 2's menus are the same class (area 7
     reads).
  5. A profile of Sonic Adventure 2 at X3 BACKWARD (JIT HOTBLOCKS, a
     heavy scene), to see which kind of wait costs its 7.5 points.
  6. The level OFF of JIT IDLE RULE is worse than X3 in every run (the
     boot at SPEED 12.8% after the BIOS logo): take it out of the row.
  7. Rows a user cannot judge: JIT PR MOVES (hit by Quake3.cdi and Space
     Channel 5 only; needs a look at the image, then a default), JIT MAC
     and JIT RM 2/3 (no game known). With GOALS.md G13 they are rows to
     hide.
  8. The `[ni]` boot probe has done its job (5.40 closed): take it out.
  9. TMU Fine TCNT (bundle 7) is OFF for speed. If it must come back ON,
     `tmu_tcnt_now()` needs a short path (95 ns per read today).
  10. A package: the one of 22:43 on 2026-10-11 has none of the verdicts.
      (2026-10-12: the package of 02:30 has them, with JIT IDLE HASH and
      JIT SQ COPY, both OFF.)
  11. **Open issue, parked by Matt 2026-10-12 for the end: the scenes
      where real SH4 work is the limit.** Sonic Adventure 2 above about
      18K vertices lacks about 10 SPEED points at X3 BACKWARD (50-52
      frames in place of 60 at X30). The Looney Tunes race lacks about
      12 (X30: 27 of 30 frames, the microstutter Matt sees; X3: SPEED
      88%). No wait loop is left in either (HW runs 22 and 23), and the
      Looney Tunes race costs the same JIT work per frame at both
      levels. The numbers and the reading are in section 0. Candidates,
      in the order recommended to Matt, none built:
      a. A number row for the idle rule's factor (3 to 30): at X30 the
         SH4 thread sleeps 55-108 ms/s in the Looney Tunes race while
         frames drop. Estimate, not measured: 28-30 frames per second
         near full speed at about 15.
      b. The store-queue call into the TA, one per vertex: 11.4% of the
         SH4 thread in Sonic Adventure 2 at 18K+ (1.24M calls/s, 170 ns
         each), 8% in the Looney Tunes race. Read the code first.
      c. 5.23, `fmov.s @Rm+` pair fusion: both vertex loops read with
         it. Estimate 1-2 points.
      Also open with it: the defaults of JIT IDLE HASH and JIT SQ COPY
      (5.46), and a Crazy Taxi and a Looney Tunes run with JIT SQ COPY
      ON for no change.
  12. **CLOSED 2026-10-12 by Matt, no fault: "Turns out, I was wrong all
      along. The latest build was acting as intended on x3 reverse.
      Everything was working fine. Basically Sonic Adventure felt so
      clunky to the point where I thought I hit a regression".** So there
      is no regression in the victory animation. Every "right" and "wrong"
      below was a judgement by eye with no reference, so the conclusions
      drawn from them are withdrawn: bundle 5, SPE ARM7, JIT BCACHE, the
      idle rule and the `[ni]` probe have no fault shown by this search,
      and "SPE ARM7 OFF" is not a work-around for anything. What stays
      true: the measured numbers (SH4 reads and stores of ARAM, commands
      in flight, RPS 30 and the even display cadence in every run). The
      probe row AUDIO "SPE ARM7 Read Sync" is removed from the source
      (Matt 2026-10-12: "Yes, remove it."): `ps3_arm_spe.cpp`/`.h` and
      `nullAICA_plug.cpp` are equal to HEAD `a7c19df` again, and
      `ps3_presets.cpp`/`.h` and `ps3_menu.cpp` hold only the fog
      session's lines. No build was made: the package of 14:14 still has
      the row (OFF); the next build drops it, and the key `SpeArm7RdSync`
      in `NotDC.cfg` is then not read. The text below is the history.
      **Issue as opened, Matt 2026-10-12: in Sonic Adventure "some animations
      looked off"** ("May worth placing that in the issues."). Seen in HW
      run 4 of bundle 9 (section 0; `b9_run1.txt` lines 22288-44177,
      scratchpad of session d13d658e). Not known: which animations, in
      which scene, and since when. Facts of that run: JIT IDLE RULE X3
      BACKWARD, and three rows that are OFF by default were ON in Matt's
      install: JIT IDLE HASH, JIT SQ COPY (never run with Sonic Adventure
      before) and JIT PR MOVES (`[jitcfg] idlerule=1 prmoves=1 idlehash=1
      sqcopy=1`). On 2026-10-11 (bundle 8 run 9: X3 BACKWARD, JIT BCACHE
      ON, before these rows) Matt called the same game "VERY smooth" with
      no word on animations. ARM7 STR Align is not a cause: it acts on
      ARM7 stores only, and Sonic Adventure made none (`unaligned str 0`).
      No fault line in the log. First steps when Matt takes it up: ask
      which animation and scene; then one boot at X30 with the three rows
      OFF, then one row at a time.
      **Update, Matt 2026-10-12 (later the same day, his own test, no
      log: the listener was stopped): "Sonic's animation is still off even
      with all those 3 set. We may need to look down further."** So it is
      Sonic's own animation, and the three rows (JIT IDLE HASH, JIT SQ
      COPY, JIT PR MOVES) are not the cause. Matt confirmed: the three
      rows were OFF, and the test was at JIT IDLE RULE X3 BACKWARD.
      **Symptom, Matt 2026-10-12: "It had a jerky movement between pose
      while also wrong. It was done in the victory screen."** So: in the
      victory screen at the end of a stage, Sonic's animation moves in
      jerks between poses, and the poses are wrong too. Still open: since
      which package, and if other animations have it. Not tested: JIT
      IDLE RULE X30 (every test so far was at X3 BACKWARD), the other JIT
      rows (a boot with Dynarec OFF would show if the JIT is in it at
      all), the renderer rows of bundles 3 and 4, and an older package.
      **Matt 2026-10-12: "It ran well until one of the recent changes,
      though."** So it is a regression: a bisect can find it. The last
      good day is not known yet (ask Matt). The changes of the last days,
      newest first, as bisect points (commits): `1533cc4` JIT IDLE HASH
      and JIT SQ COPY (10-12; the two rows are ruled out, their code
      outside the rows is not), `880e269` and `a306a38` JIT IDLE (10-11
      to 10-12: JIT IDLE RULE and its X3 BACKWARD level), `4f6b920` bundle
      7, the TIMING page (10-11: SPG Field Bit FRAME, HBlank IRQ MODES,
      VBLANK INT Reset FLYCAST are ON in Matt's install; TMU Fine TCNT is
      OFF again since 10-12), `ad7c1ab` bundle 6, `02444ac` bundle 5,
      `b76f710` bundle 4 (10-11), `ebc8897` bundles 2 and 3 (10-10). Bundle
      9 (not committed) acts on the ARM7 only. Rows first, no build: the
      TIMING rows at their old values and JIT IDLE RULE X30; then
      packages of older commits.
      **Matt 2026-10-12, his own test: "I can confirm that x3 or x30 is
      not tied to the issue."** The JIT IDLE RULE level is ruled out (the
      fault shows at both). Ruled out so far: JIT IDLE HASH, JIT SQ COPY,
      JIT PR MOVES, the idle rule level, ARM7 STR Align. Next rows to
      try, none tested: the TIMING rows of bundle 7 at their old values
      (SPG Field Bit, HBlank IRQ, VBLANK INT Reset, TMU Fine TCNT), then
      Dynarec OFF; then packages of older commits.
      **Matt 2026-10-12 ~12:06, two screenshots of the victory screen and:
      "With some of these on It's still the same."** So some TIMING rows
      (which ones: not said) change nothing. The screenshots are
      `sa_victory_1_wrong.png` and `sa_victory_2_score.png` in the
      scratchpad of session d13d658e (Emerald Coast clear, TIME 02:24:94,
      242 rings). Picture 1, before the score panels: Sonic's eyes are
      almost blank white (one small green spot where an iris should be)
      and the head looks deformed. Picture 2, with the score panels:
      irises and head look normal. The body, the shoes, the background
      and the HUD look right in both. Two stills do not say which part is
      wrong: the head's pose, the face shape (the game morphs it), or the
      draw order of the eye parts. So both sides stay open: what the SH4
      computes (JIT rows) and how it is drawn (RENDERER rows of bundle 4).
      **Logged test, 2026-10-12 ~12:10-12:14 (`sa_anim1.txt`, scratchpad
      of session d13d658e): one boot with JIT BCACHE OFF (`bcache=0`) and
      TMU Fine TCNT ON; Matt: "Nothing."** Picture 3
      (`sa_victory_3_bcache_off.png`, TIME 02:12:24) shows the same pose
      and the same blank eyes as picture 1: the wrong frame comes back the
      same way each time. Ruled out more: JIT BCACHE, TMU Fine TCNT (OFF in
      the runs before, ON here). TR Triangle Sort: Matt: "It's not the
      Triangle sort, most likely" (in the log it was 3 at boot and set to
      2, with Tile Clip 2, in the menu late in the run; OFF not logged).
      Unchanged in every logged run: SPG Field Bit FRAME, HBlank IRQ
      MODES, VBLANK INT Reset FLYCAST, JIT FSCHG 2, the bundle 3 texture
      rows. No fault line. Rows guessed so far found nothing: a bisect
      with packages of older commits is the sure way.
      **Bisect packages built 2026-10-12 12:20 (Matt: "Yes, give me the 3
      separate packages.")**, each from a clean `git archive` export in
      the scratchpad of session d13d658e (`bisect/src_<commit>`; the
      working tree was not touched): `build/bisect/A_10-08_93fca79.pkg`
      (1486960 bytes), `B_10-10_ebc8897.pkg` (1495152), `C_10-11_4f6b920.pkg`
      (1528432). All: 0 `r2off`, `.got` under 0x10000. A and B read the
      old settings file `nullDC.cfg`, C reads `NotDC.cfg` (Matt's rows of
      today). Order: C, then B, then A; the first good one bounds the
      change. Known bad: the package of 09:34 (HEAD `1533cc4` + bundle 9).
      **Matt 2026-10-12: "It's most likely a FPU issue. Maybe FSCA?"**
      Code read, no test: FSCA itself did not change in the window. Its
      JIT case (`ps3/ps3_driver.cpp`, `shop_fsca`) is from 09-21
      (`0fc3549`), the table load (`GenerateSinCos`) from 09-21 with one
      line of 10-06, the interpreter op and the canonical op are older.
      The FPU change that is in the window and ON in every logged run is
      **JIT FSCHG level 2, "+ NO BLOCK END" (5.37, 10-10; `fschg=2`)**:
      `fschg` no longer ends the block and the decoder follows SZ itself
      (`dec` `i1111_0011_1111_1101`). Read only: no fault seen in it
      (`UpdateFPSCR` does not mind the stale SZ in `old_fpscr`). It is
      the first row to test: JIT SPEED -> "JIT FSCHG" -> ON, set before
      boot, no install. In the bisect, A has no level 2; B and C have it.
      Other changes since 10-08 that are ON in his runs and not ruled
      out: JIT SQ MISS level 3 (5.43), RAM SQ Direct (5.44), SYS GATE
      BATCH (5.42), the GD-ROM rows of bundle 2 (wrong data would also
      give the same wrong frame each time), bundles 3 and 4. The `[ni]`
      boot probe restores the host FPSCR (read).
      **Logged test, 2026-10-12 ~12:28-12:32 (`sa_anim1.txt` from line
      31060): JIT FSCHG ON (`fschg=1`, no "+ NO BLOCK END"), JIT BCACHE
      OFF, TMU Fine TCNT ON, package of 09:34; Matt: "Nope, still
      broken."** Pictures `sa_victory_4_fschg1.png` (the same blank eyes
      in the nose-rub pose, score panels up) and
      `sa_victory_5_fschg1_before.png` (on the platform before the pose:
      the face is right). JIT FSCHG level 2 is ruled out. Ruled out so
      far: JIT IDLE RULE level, JIT IDLE HASH, JIT SQ COPY, JIT PR MOVES,
      JIT BCACHE, TMU Fine TCNT, JIT FSCHG level 2, ARM7 STR Align. Next:
      the bisect packages (not installed yet).
      **Bisect run 1, 2026-10-12 ~12:34-12:38 (`sa_anim1.txt` from line
      45555): package A (`93fca79`, 10-08 21:23); Matt: "A looked
      right".** The log confirms the old build (no `Arm7StrAlign` key,
      three `[jitcfg]` lines; `sqmiss=0 bcache=0 sysgate=2 idle=1`, old
      `nullDC.cfg`). So the change is after `93fca79`: in `ebc8897`
      (10-10), `b76f710`, `02444ac`, `ad7c1ab`, `4f6b920` (10-11),
      `a306a38`, `880e269`, `1533cc4` (10-12) or bundle 9. Next: package B
      (`ebc8897`); it reads the same settings file as A, so only the code
      differs.
      **Bisect run 2, ~12:40-12:44 (from line 52254): package B
      (`ebc8897`, 10-10 21:04); Matt: "B is right".** The log confirms B
      (`[gdcfg]` with all GD-ROM rows ON, `gbatch=1 ramsq=1 fschg=1
      sqmiss=0`). So bundle 2 (GD-ROM), 5.42 SYS GATE BATCH and 5.44 RAM
      SQ Direct are not the cause. The change is in `b76f710` (10-11
      02:13, 69 files: bundles 3 and 4), `02444ac` (bundle 5), `ad7c1ab`
      (bundle 6), `4f6b920` (bundle 7), `a306a38`, `880e269`, `1533cc4`
      (JIT IDLE ...) or bundle 9. Next: package C (`4f6b920`). The two
      middle points `02444ac` and `880e269` are being built for the step
      after it.
      **Bisect run 3, ~12:48-12:52 (from line 58805): package C
      (`4f6b920`, 10-11 22:17), with today's settings file; Matt: "C is
      right".** In the log: `TmuFineTcnt=1 SpgFieldBit=1 HblankIrq=1
      VblankIntReset=1`, `sqmiss=3 fschg=2 gbatch=1 ramsq=1`, no
      `idlerule` line (C is older than bundle 8's JIT rows). So bundles 3
      to 7 are not the cause, with the TIMING rows, JIT SQ MISS level 3
      and JIT FSCHG level 2 all ON. The change is in `a306a38` (10-11
      23:54, "JIT IDLE": bundle 8's JIT items, 21 files), `880e269`
      (10-12 01:16), `1533cc4` (10-12 02:56) or bundle 9. Packages D
      (`02444ac`) and E (`880e269`) are in `build/bisect/`; next is E.
      `a306a38` and `1533cc4` are built too: `F_10-11_2354_a306a38.pkg`
      and `G_10-12_0256_1533cc4.pkg` (G is the same code as the package
      of 02:30).
      **Matt 2026-10-12: "We took some JIT codes 'as correctness' from
      Reicast ... That may be the cause?"** Read of the whole source diff
      from C to HEAD (15 files, 410 lines), no test. What it holds: JIT
      IDLE RULE (Reicast's x3 rule; level 0 is the old code), JIT IDLE
      HASH (Reicast's way, a list of wait loops), JIT PR MOVES (Flycast),
      JIT RM 2/3, the JIT MAC fix, the JIT BCACHE rework, JIT SQ COPY,
      and the `[ni]` boot probe of 5.40 (`ps3_ni_probe()`, called once in
      `ngen_mainloop`). The Sonic Adventure logs of today: `[jit8]
      prmoves=0 mac=0 rm23=0`, idle hash `listed=0` (it acted on no
      block), idle rule 7343-8841 blocks billed at X3. So of the code
      taken from Reicast only the idle rule acts in this game, and Matt
      found its level not tied to the fault (not logged). The `[ni]` probe
      is the one part that no row turns off: it sets the host FPSCR
      (`mtfsfi 7,0`, `mtfsfi 7,4`) and puts the saved value back (`mtfsf
      0xFF`); read: the restore is complete. Not proven either way.
      **Bisect run 4, ~13:08-13:14 (from line 71009): Matt installed
      package D (`02444ac`, 10-11 18:51), not E: "This is the run with the
      fault!", "Bundle D broke something".** The log confirms D (texture
      and renderer rows, no TIMING rows, no `finetcnt`; old `nullDC.cfg`:
      `sqmiss=0 fschg=1`). So the results do NOT follow the commit order:
      A right, B right, **D wrong**, **C right** (C is newer than D),
      today's build wrong. What follows from it:
      - The fault comes in with `b76f710` (bundles 3 and 4, 10-11 02:13)
        or `02444ac` (bundle 5): B and D read the same settings file, so
        only the code and the rows that B does not have differ.
      - C has that code too, with the same texture and renderer row
        values as D in the log, and is right. C adds bundles 6 and 7; in
        its run the TIMING rows were ON (`SpgFieldBit=1 HblankIrq=1
        VblankIntReset=1 TmuFineTcnt=1`) and `sqmiss=3 fschg=2`. Something
        of that hides the fault in C, or the C result does not repeat.
      - **Correction: the sentence above that bundles 3 to 7 "are not the
        cause" is withdrawn.** The rows ruled out by Matt's tests on
        today's build stay ruled out only for today's build.
      - `sin_table` is at the same address in B, D, C, F, E and G
        (0x109E25D4): its address is not the difference.
      - `build/notdc_ps3.pkg` is no longer the package of 09:34: the fog
        session rebuilt it at 12:33 (1541872 bytes).
      Next: packages of `b76f710` (H) and `ad7c1ab` (I) are being built:
      H splits bundles 3-4 from bundle 5, I shows if bundle 6 or bundle 7
      hides the fault. And one repeat of C.
      **Matt 2026-10-12 ~13:20: "It's the animation that is broken, not
      the eyes."** Correction of this issue's text: the blank eyes in the
      pictures are not the fault, and my guess about the texture path is
      withdrawn. The fault is the motion: jerky between poses, and wrong.
      From the logs (`[vid]` lines of `sa_anim1.txt`): in D's victory
      screen the display cadence is even (`held` 0/150/0/0: each frame
      stays 2 vblanks), SPEED 99.6-100.5%, 29.5-30.5 frames per second.
      So the frames come out on time and the game itself computes the
      motion wrongly: SH4 side (maths, or what the game reads of time and
      interrupts), not frame pacing and not the draw.
      Packages built 13:20, in `build/bisect/`:
      `H_10-11_0213_b76f710.pkg` (bundles 3 and 4 only) and
      `I_10-11_2027_ad7c1ab.pkg` (D + bundle 6). Both read `nullDC.cfg`,
      as A, B and D: inside that group only the code differs.
      **Bisect run 5, ~13:33-13:40 (from line 78271): package H
      (`b76f710`, bundles 3 and 4); Matt: "H: Right".** H right and D
      wrong, same settings file: **the fault comes in with commit
      `02444ac` (10-11 18:51, "Crazy Taxi sound fix" = bundle 5, AICA RAM
      coherency)**. Its source changes are all on the sound side (22
      files: caique model, nullAICA, `ps3_aica_spe.cpp`, `ps3_arm_spe.cpp`,
      the two sound SPU programs, `arm_mem.cpp`, 3 lines in
      `dc/aica/aica_if.cpp`). Bundles 3 and 4 are clear. Two bundle 5 rows
      are ON in D (and in C and today): SPE AICA Sync 3 (+ SAMPLE SYNC)
      and DSP Ring RAM. In the logs both act in this game: it streams its
      music by G2 DMA, about 20 writes per second hit a marked page, each
      makes a sync, and the SH4 thread waits 0.3-1.6 ms per sync (1-5% of
      wall). The sync counts and waits of C (right) and D (wrong) are of
      the same size, so the logs do not show why C is right. How a sound
      change reaches the motion is not known (a guess, not tested: the
      game times this motion from the sound side). Next: on a wrong build
      (D), SPE AICA Sync OFF, then DSP Ring RAM OFF, one per boot.
      **Row test 1, ~13:41-13:47 (from line 84927): package D, SPE AICA
      Sync OFF, DSP Ring RAM ON; Matt: "Wrong".** The log confirms the
      row (`sync=0`, 0 syncs). SPE AICA Sync is not the trigger, so the
      Crazy Taxi fix is not in question. Seen in the same lines: `ring=1
      ... map pages 256 ... spu ring lines in 0-512 out ~62300` per
      window: with DSP Ring RAM ON the sound SPU writes its DSP ring, a
      64 KB area, back to wave RAM about 12000 lines per second in this
      game. Also in this run: the SH4 reads the play position about 250
      times per second (`ca reads`), 40 G2 DMA starts per second, no
      AICA interrupt to the SH4. Next: DSP Ring RAM OFF (SPE AICA Sync
      stays OFF, so both rows are off).
      **Row test 2, ~13:48-13:54 (from line 92076): package D, SPE AICA
      Sync OFF and DSP Ring RAM OFF; Matt: "Wrong".** No `[aram-hz]`
      line in that run (nothing of bundle 5 acted on the PPU side), no
      fault line. So neither row is the trigger: the fault comes with
      bundle 5 code that no row turns off. Code read of `02444ac`, SH4
      side (`aica_if.cpp`, `nullAICA_plug.cpp`, `aica_mem.cpp`,
      `ps3_arm_spe.cpp`, `aica_ctl.h`, the caique model's new functions,
      all `const`): with the rows off it changes nothing the game can
      read. What is left is the two SPU programs (`spe_arm7`, `spe_aica`:
      larger, other timing). **Lead, from the logs, not tested:** the SH4
      makes no AICA register read in this game (`sh4 reads 0`); the play
      position is read about 1000 times per second by the ARM7 driver
      (`mon`, at the FIQ rate), so the game gets its sound time from the
      driver's data in wave RAM. With SPE ARM7 the driver runs on an SPU
      and its stores reach the SH4's view of wave RAM with a lag that
      depends on wall time (up to `LAG_MAX` 256 commands, `max` 112-203
      in the logs), not on emulated time. A build whose SPU timing differs
      can then show uneven sound time to the game: this would fit a fault
      that comes and goes with builds (D wrong, C right). Test: package
      D with SPE ARM7 OFF (the ARM7 then runs on the PPU, in step with
      the SH4).
      **Row test 3, ~13:55-13:58 (from line 99059): package D, SPE ARM7
      OFF (`SpeArm7=0`, the ARM7 on the PPU JIT), both bundle 5 rows OFF;
      Matt: "This run it went right."** SPEED 99.80% over 198 windows,
      none under 97% after the boot; `arm:5.3% aica:2.8%` of the SH4
      thread; no fault line. **So the fault needs the ARM7 on its SPU:
      SPE ARM7 ON is part of the cause, and the lead above holds so far
      (the lag between the ARM7 SPU's stores and what the SH4 reads in
      wave RAM). The lag itself is not measured yet.** State of the
      search: A, B, H right; D wrong with SPE ARM7 ON (sync and ring rows
      ON or OFF), right with SPE ARM7 OFF; C right, today's build wrong
      (both with SPE ARM7 ON). Open: a measure of the lag at SH4 reads of
      wave RAM, and a fix that keeps SPE ARM7 (candidate, not built: an
      SH4 read of wave RAM waits for the ARM7 SPU's tail, as a register
      read does; own row, default OFF). Work-around today: SPE ARM7 OFF.
      **Built 2026-10-12 14:02 (Matt: "Yes, build the fix with the
      current build."), no HW run yet: row AUDIO -> "SPE ARM7 Read Sync"
      (`SpeArm7RdSync`), default OFF, live.** `libAICA_ReadMem_aica_ram`
      calls `arm_spe_aram_read()` (`ps3/ps3_arm_spe.cpp`) when SPE ARM7
      runs: it counts the read and the commands in flight (`s_head` minus
      the echo's tail), and with the row ON it waits for the tail
      (`wait_tail`, the same wait as a register read). The echo's tail
      moves only after the SPU's ARAM puts, so the SH4 then reads the
      driver's data as of the last MediumUpdate, as with the ARM7 on the
      PPU. New log line every ~5 s while the SH4 reads ARAM: `[arm7spe]
      sh4 ARAM reads <n>/s, <n>/s with commands in flight (1-4: .., 5-16:
      .., 17-64: .., 65+: .., max ..) | read sync <row>: waits <n>/s,
      <t> ms/s`; `[sndcfg]` has `SpeArm7RdSync=`. Package
      `build/notdc_ps3.pkg`, 1542320 bytes, from HEAD `a7c19df` (bundle 9,
      committed by Matt at 11:53) plus the fog session's work in the tree
      (row "Fog", default OFF) plus this row; 0 `r2off`, `.got` 0xD880; a
      copy is `notdc_rdsync_1402.pkg` in the scratchpad of session
      d13d658e. No PC check runs this PPU path. HW test: Sonic Adventure,
      SPE ARM7 ON, the victory screen once with the row ON (fix) and once
      with it OFF (control: does this build show the fault, and how many
      commands are in flight at the reads).
      **HW run, ~14:05-14:10 (`sa_anim1.txt` from line 109742): the
      package of 14:02, SPE ARM7 ON, the row set ON in the menu before
      the stage (`[sndcfg] menu ... SpeArm7RdSync=1`), Fog TABLE then +
      VERTEX (Matt tests the fog in the same run); Matt: "Still wrong."**
      The waits ran (`read sync 1: waits 35-50/s, 17-37 ms/s`). So a
      wait at the SH4's reads alone does not fix it. The measure, row OFF
      and ON: the SH4 reads ARAM 65-125 times per second, and at about
      half of the reads commands are in flight, most often 65 or more
      (`65+: 150-300` per window, `max` 168-256): the ARM7 SPU runs 2-9 ms
      of guest time behind the SH4 at those moments. The display cadence
      of that victory screen is even again (`held 0/150/0/0`, 0 skipped).
      **Next try, built 14:14 (package 1542512 bytes; copy
      `notdc_rdsync2.pkg`): the same row has a third level, "+ WRITES"
      (`SpeArm7RdSync=2`)**: an SH4 store to ARAM also waits for the tail
      (`arm_spe_aram_write()`, first in `libAICA_WriteMem_aica_ram`).
      Reason: the ARM7 SPU fetches wave RAM lines from live memory, so
      while it runs behind it can meet an SH4 store before its own time
      reaches it (a request seen early, or one request missed when two
      follow each other); with the ARM7 on the PPU this cannot happen.
      With both waits the ARAM traffic between the two CPUs has the order
      it has with SPE ARM7 OFF. The log line now has a `stores` twin. 0
      `r2off`, `.got` 0xD890. Not HW tested. If it fails too: stop the
      guesses, and trace what the SH4 reads from ARAM in a right and in a
      wrong run.
      **HW run, ~14:20-14:26 (`sa_anim1.txt` from line 118829): the
      package of 14:14, SPE ARM7 ON, the row at "+ WRITES" at boot
      (`SpeArm7RdSync=2`), `Fog=2`; Matt: "Still wrong".** Both waits
      ran: `read sync 2: waits 32-42/s, 18-29 ms/s` for 68-80 reads per
      second (32-42 with commands in flight, `65+` 155-196 per window,
      `max` 217-245). The stores: ~22,300 per second (all from the G2
      DMA), only 1-3 per second with a command in flight (`max` 1, once
      100). So an SH4 store almost never came before the ARM7 SPU's tail:
      the "+ WRITES" level had nothing to wait for, and the "request seen
      early" idea is dead. **Both ARAM order ideas are dead (the SH4 reads
      late driver data; the ARM7 meets SH4 stores early).** No more
      guessed patches for this fault. The row "SPE ARM7 Read Sync" stays
      in the tree as a probe, default OFF, not committed; its fate is
      Matt's.
      **Matt 2026-10-12, after that run: "My strong lead in this is that
      when we implemented the Reicast JIT we mixed with ours and it broke
      some stuff."** What the runs say about it:
      - H -> D: no SH4 or JIT file differs (`git diff b76f710 02444ac`: 22
        files, all sound), and both read `nullDC.cfg` with the same
        `[jitcfg]` lines (`sqmiss=0 fschg=1`). H right, D wrong, D with
        SPE ARM7 OFF right. So the JIT code is not what makes D wrong.
        The Reicast items of bundle 8 (`a306a38`, `880e269`, `1533cc4`)
        are not in D at all.
      - C -> today: the code added is bundle 8's JIT items and bundle 9.
        C right, today wrong, same settings file. This step fits Matt's
        lead.
      - **Gap: today's build never ran with SPE ARM7 OFF.** Only D did.
        If today's build is wrong with SPE ARM7 OFF, there is a second
        cause after C, and that is bundle 8's JIT work (Matt's lead).
      - All of A, B, C, H (right) and D, today (wrong) had the ARM7 SPU up
        (`[arm7spe]` lines, `arm:0.0%`). RPS is 29.9-30.0 in the victory
        screen of right and wrong runs alike.
      - A class that fits both leads, from reading only, not tested: the
        SH4 thread's C path at each MediumUpdate differs with SPE ARM7
        (`arm_spe_update` in place of `arm_aica_run`), so host state that
        the JIT wrongly keeps live across that call (a volatile register,
        a CR field, the host FPSCR) would break with one path and not
        with the other, and would move with each build.
      Next, no build (the package of 14:14 is installed): (1) today's
      build, SPE ARM7 OFF, the victory screen. Right: one fault that needs
      SPE ARM7, as in D. Wrong: bundle 8's JIT work broke it by itself;
      then packages F, E, G (`build/bisect/`) find the commit. (2) Only if
      (1) is right: SPE ARM7 ON and Dynarec OFF. Wrong: the JIT is clear.
      Right: the JIT is part of it, or the low speed hides it (the ARM7
      SPU then has no lag).
      **Test (1), HW run ~15:00 (`sa_anim1.txt` from line 134052; a short
      boot before it at 129965): the package of 14:14, SPE ARM7 OFF
      (`SpeArm7=0 SpeArm7RdSync=0`), `Fog=2`, `idlerule=1 bcache=1
      fschg=2 sqmiss=3 finetcnt=0`; Matt: "Broken".** The log confirms
      the ARM7 on the PPU (`arm: 4-5%`, no `[arm7spe]` line), SPEED
      99.9-100.0%, RPS 29.97, `held 0/150/0/0`. **So today's build is
      wrong without SPE ARM7 too, and D is right without it: a cause came
      in after D that does not need the ARM7 SPU. Matt's lead (the JIT
      work of bundle 8) fits; SPE ARM7 is not the whole story, and the
      "work-around: SPE ARM7 OFF" holds for D only.** Candidates, in
      commit order: `ad7c1ab` (bundle 6) and `4f6b920` (bundle 7; C, right
      with SPE ARM7 ON, not run with it OFF), `a306a38` (F), `880e269`
      (E), `1533cc4` (G), `a7c19df` (bundle 9), and the work not yet
      committed (fog, the read sync row). Next: bisect with SPE ARM7 OFF
      (the SPU timing is then out of the picture): package E first; E
      wrong -> F; E right -> G. All three read `NotDC.cfg`, so the rows
      stay as they are.
      **Bisect run with SPE ARM7 OFF, ~15:10-15:16 (from line 145898):
      package E (`880e269`); Matt: "Wrong".** The log confirms E
      (`[jitcfg] idlerule=1 prmoves=0 rmany=0`, no `idlehash`, bundle 6
      and 7 keys, no bundle 9 key), the ARM7 on the PPU (`arm: 4-5%`),
      SPEED 100%, `bcache=1`. So bundle 9 and the work not yet committed
      are clear. F (`a306a38`) differs from E only by the FPU DN row
      (default OFF) and the TMU Fine TCNT default: a run of F as it is
      tells little.
      **Found in the logs after that run (`b9_run1.txt`, `sa_anim1.txt`):
      every logged wrong run on a build with bundle 8's JIT rows has `JIT
      IDLE RULE` at X3 BACKWARD (`idlerule=1`): 8 Sonic Adventure runs,
      with `bcache` 0 and 1, `idlehash`/`sqcopy`/`prmoves` 0 and 1, SPE
      ARM7 ON and OFF. No logged run reaches the victory screen with X30
      on such a build (one 29 s boot at line 129965 had `idlerule=0`).
      The right packages A, B, C, H hold only the old x30 code. X3
      BACKWARD is Reicast's rule (P71), so this is Matt's lead in one
      row. Matt's word of the morning ("x3 or x30 is not tied to the
      issue") has no logged run behind it. D (x30 code, wrong with SPE
      ARM7 ON only) is not explained by it and may be a second, timing
      fault.** Also checked by reading, no fault found: every host FPSCR
      write (`sh4_registers.cpp`, the `[ni]` probe, the JIT EXIT path,
      the menu, `ps3_rend.cpp`) saves and restores or runs on its own
      thread; each run of the day was a fresh process (`[ps3] NotDC`
      banner before each launch). Next, no build: the bundle 8 package on
      the PS3 (stamp 10:53:18 = F or G), SPE ARM7 OFF, JIT IDLE RULE X30,
      the victory screen.
      **HW run ~15:25-15:31 (from line ~157476): Matt: "Wrong". The log
      shows package F (`fpudn=0` in `[jitcfg]`, the `[ni]` lines), SPE
      ARM7 OFF, but `idlerule=1` at boot and `[jit8] ... idle rule 1
      billed=6644`: the row was still at X3 BACKWARD.** So this run is "F
      as it is": wrong, as E. It is not the X30 test; that test is still
      open. (`[ni]`: NI 0 and 1 give the same values, as on 2026-10-11.)
      **X30 test, HW run ~15:35-15:41 (from line 171674): package F, SPE
      ARM7 OFF, `idlerule=0` (`[jit8] idle rule 0 billed=9869`),
      `bcache=1`; Matt: "Wrong".** So JIT IDLE RULE is clear, now with a
      logged run: Matt's word of the morning was right, and my reading of
      the logs (all wrong runs at X3) was a coincidence of his settings.
      State with SPE ARM7 OFF: D right (`bcache=0`, old settings file);
      today's build, E and F wrong (all `bcache=1 finetcnt=0`). The three
      wrong runs share JIT BCACHE ON; its code was reworked in `a306a38`
      (8-byte entries, `ps3_jit_bcache.h`) and no run before bundle 8 had
      it ON. By reading, the lookup is equal to the old path but for the
      `lookups++` (cache replacement only): no fault seen. Plan, one
      variable per run: (1) F, SPE ARM7 OFF, X30, JIT BCACHE OFF. Right:
      BCACHE is a trigger. Wrong: (2) package C with SPE ARM7 OFF and JIT
      BCACHE OFF (C's old BCACHE code stops at a `verify` when ON). C
      right: an un-gated part of `a306a38` (the `[ni]` probe is the only
      one found). C wrong: bundle 6 or 7 with the ARM7 on the PPU, then
      package I.
      **End, Matt 2026-10-12 ~15:50: no fault (his words at the top of
      this point). Run (1) was not made.** The PS3 holds package F and a
      settings file changed for the search (SPE ARM7 OFF, JIT IDLE RULE
      X30): the current package and those two rows go back before any
      other test.

- Built (Matt 2026-10-11: "Ignore the Interpreter parts. I want JIT/Dynarec
  stuff."): `bcache`, the `JitMac` fix, 5.39 (fmov, flds, fsts; no fneg),
  5.40 with its `[ni]` probe, the P57 RM verify and P71, as the rows JIT
  BCACHE, JIT PR MOVES, JIT RM 2/3, JIT IDLE RULE and FPU DN on the JIT
  SPEED page. The check is `misc/jit8_check`, not P46: it holds Flycast's
  mac cases. Not built: P46, P32, P78's RFCR. Not as the text below says:
  P46 was not first. The text below is the plan of 2026-10-07 and
  2026-10-11.
- Items: P46 (Flycast's SH4 op tests), P32 (fneg with PR=1, mac.w with
  S=1), `JitMac` and `bcache` (5.3), 5.37 (P17 fschg), 5.39 (P56), 5.40
  (P4), P57 RM verify (3d).
- Shared code: `dc/sh4/rec_v2/decoder.cpp` (the fschg block end, the PR=1
  fallback gate, `verify(fpu_cfg.RM<2)`), the interpreter's FPU ops,
  `SetFloatStatusReg()`, and the JIT FPSCR and lookup code in
  `ps3/ps3_driver.cpp`.
- Shared check: P46 first. `misc/interp_check` (WSL, ~400 lines) and a
  boot self-test (the real big-endian `GetDR`/`SetDR`) run every
  interpreter op. From reading, only P32's fneg fails them. They are also
  JitMac's check. 5.39 adds fneg only after the P32 fix.
- Gain: correctness: fneg on an odd register with PR=1, mac.w with S=1 in
  the interpreter, JitMac's 64-bit carry (the 5.2 defect), no JIT hang
  when a game sets RM = 2 or 3 (WinCE games, which also need P38). Speed:
  noise level (5.37, `bcache`), no user seen (5.39: no PR=1 game; JitMac:
  no mac.l or mac.w in the Crazy Taxi fallback census), unverified (5.40).
- HW: Crazy Taxi and Sonic Adventure (`[IFB]`, same-load VPS).
- Added 2026-10-11 (sixth review; open, nothing built):
  - P71. The decoder's idle rule (`dc/sh4/rec_v2/decoder.cpp`) bills each
    small block that reads memory and ends in a conditional branch x30
    (Reicast x3, Flycast no rule). It hits work blocks: 20% of the billed
    cycles in the Looney Tunes race. An option X30 / X3 BACKWARD / OFF,
    about 3 lines. It is the only item of this bundle with a visible
    effect, and it can cost SPEED (the host runs more real ops per
    emulated second), so it gets its own HW runs: the Looney Tunes race
    (RPS against 26.4), Phantasy Star Online, a dense Crazy Taxi drive.
  - P78's RFCR (read-only 17, as Flycast; 2 lines).

### Bundle 9: ARM7 engines  `[x]` HW SUCCESS 2026-10-12, CLOSED (Matt): "ARM7 STR Align" fixes F1 (ChuChu Rocket, "THE SOUND IS FIXED"), default ON in source; tests pass on all three engines; census A 0, B 0 in four games, so no P49 fix (section 0)

- State 2026-10-12 (detail in section 0): step 1 (the census) and step 2
  (P40's tests with the STR cases) are built, and the unaligned store has
  its row ("ARM7 STR Align", default OFF in source). HW runs 1-3: the
  tests pass on all three engines; the census found no P49 form in ChuChu
  Rocket and Crazy Taxi, so the P49 fix is not built ("fix only if it
  fires"); ChuChu Rocket makes ~1930 unaligned word stores per second and
  the row ON fixes its slow sound (F1). HW runs 4-5: Sonic Adventure and
  Looney Tunes with the row ON make no unaligned store and have no P49
  form. Closed by Matt 2026-10-12, the row default ON in source.
- Items: P49 (PC reads with a register shift, STR of PC), P40 (Flycast's
  AicaArmTest as self-tests), 6.8 (no STR, STRB or unaligned STR
  self-tests; an unaligned ARM word store scatters 4 bytes). P44 is done
  (ARM7 Reset Fix, section 0).
- Shared code: the three engines change together (cached:
  `gen_arm7_cached.py` -> `arm7_cached_gen.inc`; JIT: `arm7_jit.cpp`; SPU:
  `ps3/spu/arm7_spu.h`). Shared checks: `misc/arm7spu_check` (B0) and
  `misc/arm7spe_check` (B2) compare the engines; the 45 self-tests.
- P49's log census comes first; fix only if it fires. Then P40's tests,
  with the STR cases and P49's two cases (`orr r0,pc,r1,lsl r2`;
  `str pc,[r1]`).
- Gain: rare encodings right in all three engines. A full pass rules an
  engine bug in or out for F1 (ChuChu, section 6, parked). No speed.
- Trap: do not paste Reicast's `arm-new.h` diff (P49).
- HW: ChuChu Rocket, Crazy Taxi, Sonic Adventure, Looney Tunes.

### Bundle 10: Controllers  `[x]` HW SUCCESS 2026-10-12 (Matt): Controller FULL works in Quake III Arena; built 2026-10-12, every row at its old path by default; no HW run: Stick Dead Zone RADIAL, Rumble Pack, a button-map change (section 0); re-cut 2026-10-12 (was "Maple"; its bus and VMU items are bundle 16)

- Items: P84's radial stick dead zone; the second analog stick (GOALS.md
  G12; P84 has the exact edits); P13 (Puru Puru rumble pack); the DS3
  button remap of GOALS.md G10 step 3 (a table of `[ps3]` keys, no port).
- Shared code: `ps3/ps3_input.cpp`, `ps3/dc_pad_bits.h`,
  `dc/maple/maple_devs.cpp` (the controller device, a new vibration
  device), `dc/maple/maple_cfg.cpp`, the CONTROLS page of
  `ps3/ps3_menu.cpp`.
- Order: the dead zone first, so both sticks get it. Then the second
  stick, the rumble pack, the remap.
- Dead zone: today each axis is cut alone below 20, with no rescale;
  Flycast uses a radial dead zone with a rescale and a limit of 128. About
  15 lines.
- Second stick: FULL (2 STICKS) is its own controller type (value 1 is the
  light gun); STANDARD PAD stays the default and keeps its capability
  word. The device name is "Dreamcast PantherDC Controller" (P84).
- Byte order: the six axis bytes go through `w8` (XOR-3), never as one
  u32. The rumble AST byte needs XOR-3 access; VIBSET is a word read
  (P13).
- Gain: a smooth analog stick in every game; the right stick in the games
  that read it; DS3 rumble; user button maps. No speed.
- Trap: the rumble pack takes VMU slot 2, which holds a second VMU today.
- HW: Quake III Arena (the right stick, G12 step 5), Crazy Taxi (the stick
  feel; a standard-pad game plays as before), one rumble game.

### Bundle 11: Render to texture and present  `[x]` HW SUCCESS 2026-10-12 (Matt): RTT Read-back FIXED ends the Crazy Taxi pause freeze; built 2026-10-12, every row at its old path by default; no HW run: Present Rule, RTT Size TILE CLIP; left: the Looney Tunes boot videos; the race loading screen fix, row "Keep Last Frame" (package of 19:52): HW SUCCESS 2026-10-12 (Matt), default ON in source, not packaged (section 0); re-cut 2026-10-12 (was "Output stage"; its 16:9 items are bundle 12)

- Items: P75 (the RTT read-back that never returns, and `rsxFinish`;
  first, it is a freeze fix), P66's RTT scaler, P76 (RTT and 2D presents
  dropped silently, the RTT size from the tile clip, no RTT in pack modes
  4-7, the RTT clear alpha, `fb_line_double`, blank video), P54 (RTT pixel
  packing, the RTT note in section 3), P60 (present only the renders the
  game shows, no colour clear, the region-array scissor), P26 (interlaced
  and 24-bit framebuffer reads).
- Shared code: `rtt_render()` and `pack16()` in `ps3/ps3_rend.cpp`,
  `ps3_video_readback()` in `ps3/ps3_video.c`, the present path
  (`rth_job_draw()`, `present_fb_draw()`, `ps3_video_end_frame()` and its
  forms), the FB_R_SOF writes in `plugs/drkPvr/regs.cpp`.
- P75: the Crazy Taxi pause (START in a drive, section 0) is a render to
  texture, and `ps3_video_readback()` calls PSL1GHT's
  `rsxSetTransferData`, which never returns. 3 lines, and 2 for
  `rsxFinish` (it waits only on its first call).
- Merge points:
  - P60's held frame and an RTT share the back buffer: flip before an
    RTT, or give the RTT its own buffer. The vblank JOB_PRESENT and Frame
    Hold must respect a held frame.
  - P26's reads and P54's writes share the framebuffer pixel formats
    (565, 555, 888, 8888, the fb_concat bits).
- Gain: no freeze at the Crazy Taxi pause, and its background in place
  (the scaler). RTT games. No flashing screens and mirrors (South Park:
  Chef's Luv Shack, Metropolis Street Racer), Quake III split screen
  (P60). Interlaced 2D screens as one 480-line image, and 24-bit
  framebuffers shown. No speed.
- Byte order: VRAM words are native u32 with the left pixel in the low
  half. Read region entries with `vri()` and shifts (P60).
- Risk: medium for P60 (the present path, Frame Hold, SPE Draw).
- HW: Crazy Taxi, START in a drive, twice in one boot; Quake III split
  screen or a game of P60's list; a 2D screen. The old line "none of our
  test games, `rtt=0/0`" is out of date: Crazy Taxi is an RTT game.

### Bundle 12: Screen format and test tools  `[x]` HW SUCCESS 2026-10-12 (Matt): FPS Overlay, 16:9 WIDE and Screen Scaler ON in Sonic Adventure; built 2026-10-12, every row at its old path by default; no HW run: the screenshot, 16:9 STRETCH, Cheats (section 0); re-cut 2026-10-12 (was "HW test tools"; it takes the 16:9 items of old bundle 11)

- Built 2026-10-12 (section 0 has the rows, the code, the checks and the
  HW steps): a new SCREEN page with FPS Overlay, Screen Format (4:3 /
  16:9 STRETCH / 16:9 WIDE), Screen Scaler and Cheats; the screenshot is
  SQUARE in the in-game menu. P10's FB clip as a scissor was already
  built as Region Scissor (bundle 11). The text below is the plan of
  before the build.
- Items: P50 (on-screen FPS and SPEED), P51 (screenshot to PNG), P53
  (widescreen geometry), P69's stretch to fill, P10 (the FB clip as a
  scissor, the scaler), P36 (cheat files, 16:9 by cheat).
- Order: P50 and P51 first. They help every later HW run.
- Shared code: the viewport and scissor (`draw_frame()`, the canvas rect
  of `draw_scene()`), the framebuffer present, the end of `draw_frame()`
  for the overlay, the `ps3/ps3_ui.cpp` text and libpng (with the jmp_buf
  fix), SELECT menu entries.
- Merge points: P53's full-width scissor needs P10's clip rule (Flycast
  `isClipped()`), and the clip must scale with our X Scaler option. P36's
  stretch, P69's stretch and P53 share one 16:9 viewport option (different
  images). Call `reset_param_cache()` after the overlay, so no GL state
  leaks into the next frame (also the state cache of bundle 14, if built).
- Today: SPG already builds "FPS: x / SPEED: y%" (`rend_set_fps_text()`),
  and `SetFpsText()` in `ps3/ps3_rend.cpp` is empty.
- Gain: SPEED on the TV without the UDP listener. A PNG of the 4:3
  viewport for image faults. 16:9 output (drawn, or stretched). The FB
  clip as a scissor; the scaler (half-height screens, P10's game list). No
  speed; no cost when the tools are OFF.
- Traps: games cull at 4:3, so P53 can show missing geometry. The JIT does
  not see cheats that patch code (P36).
- Byte order: a big-endian A8R8G8B8 word is the bytes A, R, G, B: write
  bytes 1-3 (P51).
- HW: any game for the text in the left pillarbox bar and a PNG under
  `/dev_hdd0/data/NotDC/`; Crazy Taxi and Sonic Adventure at 16:9.

### Bundle 13: MMU and memory  `[x]` HW SUCCESS 2026-10-13 (Matt): MMUCR Bits FIXED lets Border Down, Sonic Shuffle, Ikaruga and Rez boot and play, default FIXED in source; built 2026-10-12, the other three rows at their old path with no HW run; left: FB Write Watch with Ikaruga, the heavy scenes of Ikaruga and Rez (bundle 21) (section 0); re-cut 2026-10-12 (was "MMU store queues"; it takes P30, P65 and the three rows of bundle 1 that never ran)

- Built 2026-10-12 (section 0 has the rows, the log lines, the checks and
  the HW steps): steps 1, 2 and 4 of the order below. Step 3 has no code:
  its rows are of bundle 1 and wait for the HW run. Step 5 (P74) is not
  built.
- Items: P58 (the MMUCR fields on big endian, with the JIT cache clear and
  the V = 0 skip), P78 (the MMUCR write mask, the TLB data array 2
  writes), P74 (a flat store-queue flush for AT = 1), P30 (P4 mirrors and
  the operand-cache RAM banks), P65 (ch2 DMA: length, the 0x12 mirror,
  DMAOR), and bundle 1's rows that no game ran: P7 (LMMODE), P26 (FB Write
  Watch), P55 (VRAM direct writes).
- Shared code: `dc/sh4/ccn.h` (the bitfield) and `dc/sh4/ccn.cpp`
  (`CCN_MMUCR_write()`), `dc/mem/mmu.cpp` (`UTLB_Sync()`, `sq_remap`),
  `dc/mem/sh4_internal_reg.cpp` (the F3 and F7 arrays, the on-chip RAM),
  `dc/sh4/sh4_sqw.h`, the JIT PREF in `ps3/ps3_driver.cpp`,
  `dc/sh4/dmac.cpp` and `dc/pvr/pvr_if.cpp` (bundle 1's code).
- Order:
  1. P58 with the write mask. The cache clear must run on both AT edges:
     the PREF target, JIT PREFETCH and JIT GUARD REUSE read AT at compile
     time. Add a log line per MMUCR write (the values and their order in
     the four games are not known).
  2. The data array 2 writes (2 lines each).
  3. Bundle 1's rows, now that their games boot: LMMODE with Rez, FB Write
     Watch with Ikaruga.
  4. P30 and P65 (about 15 and 6 lines).
  5. P74 last, and only if a game is SH4-bound after the fix.
- Check before HW: the devkitPPC `-S` test, or a boot self-test line that
  writes 0x00000105 and prints AT, TI and URC (P58).
- Gain: Border Down, Rez, Ikaruga and Sonic Shuffle get past the BIOS
  logo (they stop there today: section 0, "Open, separate"). Bundle 1's
  rows get their first run. NHL 2K2 (P30). No endless wait for a ch2 end
  IRQ (P65). P74: these games get TA SQ Direct and RAM SQ Direct (gain not
  measured). P30's bank bit is also a lead for the Capcom vs. SNK 2 menus
  (bundle 21).
- Risk: a game that turns AT on then sends its store-queue flushes
  through the TLB. WinCE games still need P38.
- HW: the four games; Crazy Taxi, Sonic Adventure and Looney Tunes
  unchanged.

### Bundle 14: Render-thread draw cost  `[x]` HW SUCCESS 2026-10-13 (Matt), the three rows default ON in source (not packaged); left: the texture lifetime, the merge key mask, the index list (section 0); added 2026-10-11

- Built 2026-10-13 (section 0 has the rows, the log lines and the HW
  steps): the bind table and the compares as two rows (Tex Bind Table, GL
  State Compare); the first-pass skips as row TR First Pass, with the
  first pass in game order as its second level (the parked "cheaper +
  OPAQUE DEPTH" of section 0); the two counters. Not built: the merge key
  mask and the texture lifetime rule (their counters come first), the
  index list (last in the order below; read `vtx+other` of the HW run
  first).
- Items: P72 (5.10: the ps3gl texture bind scan, compare before set), P73
  (the + OPAQUE DEPTH first-pass skips, the merge key bits that the draw
  never reads, the texture lifetime, an index list for the sorted
  triangles).
- Re-cut 2026-10-12: no change of items. It is the home of the parked
  "cost of TR Triangle Sort" (section 0); the settings per game of that
  block are in bundle 21.
- Shared code: `ps3/ps3gl/ps3gl_textures.c`, `ps3gl_states.c` and
  `ps3gl_vertices.c`; `apply_param()` and the 3k passes in
  `ps3/ps3_rend.cpp`; `ps3/ps3_tex.cpp` (the lifetime).
- Shared measure: one Crazy Taxi drive at TR Triangle Sort + OPAQUE DEPTH:
  render `busy`, its `gl` share, TR draws per render, SPEED at 11-13K
  vertices. Today (HW run 5 of 3k): busy 45.2%, of it the bind 11.1 and
  the sort 3.6; 4.17 us per draw, 1.37 us of it the bind.
- Order: P72's bind (about 10 lines), then its compares (about 40), then
  P73's first-pass skips (4 lines). A counter before the merge key and
  before the texture lifetime. The index list last (its ceiling is 4.6
  points of busy).
- Trap: each place that changes RSX state behind ps3gl (the UI, the
  framebuffer present, the RTT, the fog texture) must reset the new state
  cache.
- Gain: the parked cost of 3k (Crazy Taxi SPEED 97.9% in 11-13K vertex
  scenes). Every opaque draw binds too. The picture does not change.
- Checks: `misc/rend4_check` (the draw trace stays equal) and
  `misc/spevb_check` (the merge key). HW: Crazy Taxi at + OPAQUE DEPTH.

### Bundle 15: Flash and clock  `[~]` built 2026-10-13, every row at its old path by default, no HW run yet (section 0); re-cut 2026-10-12 (was "Flash defaults"; it takes P12 and the BIOS language row of P37)

- Built 2026-10-13: the page FLASH / CLOCK with the rows Broadcast, BIOS
  Language, Flash Defaults, Console ID, Exit Flash Save and RTC Local
  Time. The rows, the facts from the old logs, the choices and the HW
  steps are the bundle 15 block of section 0. Two lines below are
  corrected by those facts: P59's three games are ten, and P12's one
  line does not act on the PS3 (the row RTC Local Time does).

- Items: P59 (the broadcast byte: NTSC, as Flycast writes it; corrects
  3i), P85 (the language of a new syscfg block, a flash built when
  `dc_flash.bin` is missing, the console ID, the flash save on the forced
  exit, which is what is left of GOALS.md G10 step 4), P37's BIOS language
  row, P12 (the RTC under daylight saving time, 1 line).
- Shared code: `dc/flash_fix.cpp` (`dc_flash_set_time()`,
  `dc_flash_set_region()`, `dc_flash_restore_region()`, `lookup_block`,
  `alloc_block`), the exit path in `ps3/ps3_main.cpp` for the save, the
  RTC seed.
- Byte order: none. The flash is read and written as bytes (`wr16`).
- Gain: Capcom vs. SNK 2 (JP), Border Down (JP) and Rayman 2 (US) play at
  60 Hz, not 50 (P59). A new flash boots in English, not Japanese, and a
  row sets the language. The settings that the BIOS saved survive a forced
  exit. The clock is right under daylight saving time.
- Cost: a 60 Hz game has 20% more frames per second to run and draw
  (P59).
- For GOALS.md G11: the dial-up account blocks of P83 use this block code,
  and the console ID is used by network games. Build this bundle before
  G11.
- HW: the three games (VPS about 60, SPEED, the menus); one boot with a
  new flash; Crazy Taxi unchanged.

### Bundle 16: Maple bus and VMU  `[ ]` new at the re-cut of 2026-10-12 (from old bundle 10, with P68's maple parts)

- Items: P29 (reply timing and commands), P9 (SB_MDST before the vblank
  maple DMA), P68's maple parts (a reply outside RAM raises the overrun
  interrupt; SB_MMSEL and the recipient address; SB_MDSTAR outside RAM,
  which is a PPU crash today; the VMU LCD media-info byte), P37 (one VMU
  file per game, the VMU LCD, keyboard and mouse; its BIOS language row is
  in bundle 15).
- Shared code: `dc/maple/maple_if.cpp`, `maple_helper.cpp`,
  `maple_devs.cpp` (the VMU), `maple_cfg.cpp`.
- Byte order: keep a u32-aligned scratch buffer, so the XOR-3 byte writes
  stay valid (P29). For SB_MMSEL swap the native u32 values, never the
  XOR-3 byte view (P68).
- Gain: the Geist Force freeze and the Silent Scope VMU load at boot
  (P29). No hang in `maple_DoDma()` (P9) and no crash on a bad SB_MDSTAR
  (P68). One VMU file per game (in Flycast only slot A1 is per game),
  keyboard and mouse (P37). No speed.
- Trap: today the saves are one file per port (`vmu_save_A1.bin` in
  `/dev_hdd0/savedata/vmu`). P37 changes the file names.
- HW: Sonic Adventure VMU save and load; Geist Force and Silent Scope only
  if Matt has them.

### Bundle 17: Draw rules tail  `[ ]` new at the re-cut of 2026-10-12 (the tails of bundles 3 and 4, and 3b)

- Items: P79 (punch-through alpha is 1 after the alpha test, a third
  level of the PT Blend row), P80 (the sprite's fourth corner from the
  plane of the other three), 3b (offset colour, with the colour clamp;
  P14; 3a fog is done), P25's two-pass trilinear weights, and the
  verdicts that bundles 3 and 4 left open: Flat Shading (the RSX provoking
  vertex is not known), and the rows that no game used yet (Sprite Cull,
  Tile Clip, List Passes, the TR volume draw, the TA Data Gate, VQ
  Formats).
- Shared code: `apply_param()` and the draw in `ps3/ps3_rend.cpp`, both
  decoders (`ps3/spu/spe_dec.c`), the SPU vertex builder, the ps3gl
  shaders (`ps3/ps3gl/ps3gl_shaders.c`).
- Merge point: 3b and the trilinear weights both need a change of the SPU
  vertex builder: do it once. 3j and the fog left plumbing for 3b (vertex
  word 6 as 4 bytes, a 4-component TEXCOORD1, the fog programs; a scene
  with vertex fog already goes to the PPU replay because it needs offset
  colours). Bundle 14's merge key must get the Offset and ColorClamp bits
  back when 3b is in.
- Gain: hard punch-through edges, and Jet Grind Radio draws its PT list
  with no blend (P79). The right texture on trapezoid sprites (P80).
  Specular highlights (3b). Trilinear: the NFL 2K2 field, Shenmue
  snowflakes (P25).
- Checks: `misc/ta4_check` (both decoders), `misc/rend4_check`, the S2
  compare. HW: Jet Grind Radio; Crazy Taxi and Sonic Adventure unchanged;
  Shenmue only if Matt has it.

### Bundle 18: Sound wrap-up  `[ ]` new at the re-cut of 2026-10-12 (the tails of bundles 5 and 6)

- Items: a verdict for the four rows of bundle 5 that are OFF (Mono Mix
  Fix, DSP Ring 64K, DSP Reg Reads, LFORE Keep); P81 (the output level
  budget: two tests with no code, then a probe on the `[aica-spe] sound`
  line); P82 (the EXTS read-back inside the DSP Reg Reads row; the
  nullAICA ALFO depth; the ARMRST read-back); bundle 6's HW tests (G2 DMA
  Small and G2 DMA Read are built, default OFF); P69's VMU beep; P31's
  nullAICA-only fixes.
- Shared code: `ps3/ps3_aica_spe.cpp` and the SPU model
  (`ps3/spu/spe_aica_model.cpp`, `plugs/caiqueAICA`), `plugs/nullAICA`,
  `dc/aica/aica_if.cpp`, the AUDIO page.
- Done before the re-cut: the Crazy Taxi crackle (F2, SPE AICA Sync), the
  ChuChu Rocket slow sound (F1, ARM7 STR Align, bundle 9), DSP Ring RAM.
  SPE AICA Gain stays at 100%.
- P81: the Looney Tunes boot of the complaint played no CD audio, and the
  level laws differ from Flycast by 1.5 dB at most. No fix is known; the
  probe comes first.
- Gain: the cause of the Looney Tunes balance; the Bangai-O startup freeze
  and the Nomad Soul freeze (bundle 6's rows); every sound row has a
  verdict before GOALS.md G13 settles the defaults.
- HW: Looney Tunes (the probe; it also uses G2 DMA); Bangai-O and Nomad
  Soul only if Matt has them.

### Bundle 19: Disc tail  `[ ]` new at the re-cut of 2026-10-12 (the tail of bundle 2)

- Items: P77 (Disc Swap: drop the clears of the command in flight, then
  the open-lid phase), with the first HW test of the Disc Swap row (P34);
  the first HW test of GDI Parser and GDI Backslash (P23, P57); P82's
  stop of CD-DA at the lead-out; the parked CD-DA miss stall of 5.34.
- Shared code: `dc/gdrom/gdromv3.cpp`, `plugs/ImgReader/`,
  `ps3/ps3_gdasync.cpp`; `misc/gdasync_check` builds the real files.
- Order: P77 before the first Disc Swap run. A swap during a command ends
  in an endless `verify` today.
- Gain: multi-disc games get past disc 1 (Shenmue, Skies of Arcadia). GDI
  sets with quoted names or `\` paths boot. No SH4 stall of 24-85 ms at a
  CD-DA miss.
- Not here: CUE/BIN and CHD (P35; section 4 says "Do not take").
- HW: a two-disc game at its swap prompt; a GDI set with spaces in its
  file names; ChuChu Rocket CD-DA.

### Bundle 20: SH4 correctness tail  `[ ]` new at the re-cut of 2026-10-12 (the interpreter parts of bundle 8, with the small system fixes)

- Items: P46 (Flycast's SH4 op tests as `misc/interp_check` and a boot
  self-test), P32 (fneg with PR=1, mac.w with S=1), P64 (the FPSCR cause
  bits), P67 (small SH4 and memory fixes), P78's RFCR (read-only 17), P68's
  system parts (no BIOS and flash mirror at 0x02000000, the ATA soft reset
  values, the SB_ADSTAR mask), P33 (the ELF loader, with P68's AICA
  registers at an ELF boot), and a verdict for the JIT rows of bundle 8
  that are OFF (JIT PR MOVES, JIT MAC, JIT RM 2/3).
- Shared code: the interpreter's FPU ops and `SetFloatStatusReg()`,
  `dc/sh4/rec_v2/decoder.cpp` (the PR=1 fallback gate), `dc/mem/sb.cpp`,
  `dc/mem/sh4_area0.cpp`, `dc/sh4/bsc.cpp`, `plugs/ImgReader/elf_loader.cpp`.
- Shared check: P46 first. From reading, only P32's fneg fails it. JIT PR
  MOVES can take fneg only after the P32 fix.
- Gain: correctness. Resident Evil Code Veronica X (P67). Homebrew .elf
  files (P33). No speed.
- HW: pick games that reach the path. PR=1 FPU ops: Space Channel 5 and
  Quake3.cdi. No game is known for mac.l or mac.w.
- Not here: P38 (WinCE SH4 MMU) stays in 3d (large).

### Bundle 21: Frame rate in heavy scenes  `[ ]` new at the re-cut of 2026-10-12; parked for the end (Matt 2026-10-12)

- Items:
  1. Bundle 8's list "more friendly for other users": the idle rule
     without a choice by the user. Way (A), a wider JIT IDLE SKIP. Way
     (B), JIT IDLE HASH (built, default OFF, no verdict). A rule that
     uses X3 only while the host has spare time. A table per game.
  2. The three candidates of the SH4-bound heavy scenes (section 0): a
     number row for the idle rule factor (3 to 30), the per-vertex
     store-queue call into the TA, 5.23 (pair fusion). A verdict for 5.46
     JIT SQ COPY (built, default OFF).
  3. Bundle 7's tail: TMU Clock EFF (no HW run; it needs SH4 Clock 150)
     and TMU Fine TCNT (default OFF: about 95 ns per TCNT read).
  4. The Capcom vs. SNK 2 menus (5-6 fps; one menu frame is exactly 9 PAL
     vblanks). A test with no code: one boot at Accuracy ACCURATE.
     **HW 2026-10-13 (Matt's own idea: "I suppose I need to boot Capcom
     vs SNK 2 in x3 reverse?", then "I was right."): JIT IDLE RULE X3
     BACKWARD fixes the menus.** Same package, same rows, NTSC 60 Hz
     (Broadcast DISC, bundle 15): the menus draw 60 renders per second at
     SPEED 100% (X30: 10 per second, a steady `100% ... RPS:10`). So
     the cause of the slow menus is the x30 bill, as in Sonic Adventure
     and Phantasy Star Online, and the game joins the list for item 6.
     Log: `b15_hw.txt` lines 9731-18547 (X30: lines 5118-9218),
     scratchpad of session e511e0f6 (tools `fight.py`, `block.py`).
     **The fights at X3 are SH4-bound (Matt: "avg is 50 FPS. Game is
     supposed to hard run at 60."):** in the 53 windows with 2K+
     vertices per frame, SPEED is 87.9% on average (83.5% to 95%), the
     renders are the vblanks (51-52 per second, so the game drops no
     frame of its own), and the SH4 thread never sleeps (`[stall]` sleep
     0 ms in the median window, `[pace] slept=0`). The render load is
     small (5.5K vertices in a sample window). 12 points of SH4 time are
     missing at 60 Hz. JIT C-Calls was ON in this boot (my request, for
     the `[stall]` line). **A second boot with JIT C-Calls OFF (lines
     26360-30485) gives the same: a steady SPEED 88% and 53 renders per
     second at 5.56K vertices per frame (37 windows; 82-83% in the last
     three). So the probe was not the cost** (Matt after it: "Seems like
     2D games aren't really liked that much"; this is the only 2D game
     in the logs). No fight ran at X30 (that boot stayed in the menus).
     This is the class of item 2 (the SH4-bound heavy scenes); no
     profile of the fight exists (JIT HOTBLOCKS was OFF), so it is not
     known if the time is game work or a wait loop that X3 runs for
     real, as in Looney Tunes and Quake III Arena.
  5. Ecco the Dolphin: 18-34 fps at SPEED 100% with X30; X3 BACKWARD runs
     it at SPEED 50%.
  6. Settings per game (Matt's idea of a product-ID table: the idle rule
     level, the TR Triangle Sort level). It changes the rule "no per-game
     lists": ask Matt for the scope before a design.
  7. Added 2026-10-13, from bundle 13's HW runs (Matt: "we will check the
     Ikaruga and Rez issues later on"): Ikaruga and Rez are SH4-bound in
     their heavy scenes (SPEED 75-91% at 11.7-20.1K vertices, 87-95% at
     10.8-12.3K). With MMUCR.AT = 1 their store-queue flushes take the old
     chain: 952K/s at 336 ns (15.2% of the SH4 thread) and 849K/s at 174
     ns (11.2%). The candidate is P74, the flat flush for AT = 1 (about 20
     lines, own row; not built). Sonic Shuffle: 19.9 renders/s above 4K
     vertices at X30 and SPEED 100%; X3 BACKWARD runs it at SPEED 56%
     (Matt: "It runs better at x30. But It's too heavy for now.").
- Shared code: the idle rule in `dc/sh4/rec_v2/decoder.cpp`,
  `ps3/ps3_jit_idle.h`, the SYS GATE and `dc/sh4/tmu.cpp`, the JIT
  store-queue paths.
- Shared measure: frames per emulated second and SPEED by vertices per
  render, in the same scene, at X30 and at X3 BACKWARD.
- Known cases: the Looney Tunes race (27 of 30 frames at X30; SPEED 88%
  at X3 with JIT IDLE HASH), Sonic Adventure 2 above 18K vertices (89-91%
  at X3), Phantasy Star Online, Sonic Adventure and Jet Grind Radio (30
  frames at X3, SPEED 100%).
- Rule: do not start it unasked. Matt: "We'll get back to it at the end."

### Not bundled

Re-cut 2026-10-12: most of the old list has a bundle now (P12 in 15, P30
in 13, P33 in 20, the P25 trilinear weights and 3b in 17, the nullAICA
fixes in 18). What stays out:
- No target left: P1 steps 0-1 and P2. Soul Calibur runs at full speed
  since P16, and its black textures are closed (2026-10-07). P1 steps 2-3
  were in bundle 3; Matt dropped them on 2026-10-10 (section 0).
- Section 4 says "Do not take": P35's CUE/BIN and P39 (reios). CHD is
  GOALS.md G14 since 2026-10-13.
- P15 (5.19): a pacing option, not a speedup.
- P38 (WinCE SH4 MMU): stays in 3d (large; the JIT form is very high
  risk).
- P42 (b)-(d): stay in 5.12.
- P70 and P83 (the modem facts) belong to GOALS.md G11.
- The distance pop-in: 3a (fog) is done; its three other candidates are
  parked (section 0).
- Done or built, but still open in `OPTIMIZATION_POTENTIAL.md`: P44 (ARM7
  Reset Fix), P40 and P49 (closed with bundle 9), P43 (bundle 1), P14's
  3j and 3a, the P31 CA clamp and P20 G1 are done; P42 (a) is built and HW
  tested, default OFF (section 0).

---

## 4. Do not take

| Item | Reason |
|---|---|
| x64/ARM JIT backends, DSP JITs | no PPC |
| OIT / per-pixel sorting, Vulkan, GL4 | RSX cannot run them |
| VRAM lock, SMC page tracking | need mprotect |
| Naomi, network, savestates | out of scope |
| SSA optimizer (`dyna/ssa.cpp`) | JIT work measured at 0-3% (`Optimization.md` 4) |
| CUE/BIN, reios | Reicast and Minicast have them too (CHD left this row on 2026-10-13: GOALS.md G14) |
| Minicast fallthrough (M7) | our link sites are 3 words and cold code sits between blocks; almost no case to fall through |
| Minicast register allocator work, XMTRX kept in registers | we pin r0-r15 (not r11) and fr0-fr15; no free FPRs for the matrix |
| Minicast constprop level 2 (memory baking) | needs SMC page locks (mprotect) |
| Minicast perf map | needs Linux `perf` |
