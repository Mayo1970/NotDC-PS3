# Dynarec on PS3 — feasibility study

Investigation of the nullDC4Wii SH4 dynamic recompiler (`rec_v2`) and what a PS3
(PSL1GHT) backend would cost. Written 2026-09-20, revised the same day after
review. No code was changed.

Sources read:
- `E:\Users\Matteo\Desktop\Dreamcast\nullDC4Wii\wii\dc\sh4\rec_v2\wii_driver.cpp` (PPC backend)
- `E:\Users\Matteo\Desktop\Dreamcast\nullDC4Wii\emitter\PPCEmit\ppc_emit.h` (PPC emitter)
- `E:\Users\Matteo\Desktop\Dreamcast\nullDC4Wii\wii\wii_fastmem.h` (MMU fastmem)
- our tree: `dc/sh4/rec_v2/`, `ps3/ps3_nojit.cpp`, `Makefile`

## 1. What exists

| Part | File | Lines | Portable? |
|---|---|---|---|
| Decoder + SHIL IR | `dc/sh4/rec_v2/decoder.cpp`, `shil.cpp` | ~1900 | Yes — already in our tree, already compiled |
| Block manager | `dc/sh4/rec_v2/blockmanager.cpp` | 340 | Mostly |
| Driver / code cache | `dc/sh4/rec_v2/driver.cpp` | 717 | Needs a PS3 branch |
| PPC emitter | `emitter/PPCEmit/ppc_emit.h` (Wii tree) | 2251 | Yes, the PPC32 subset is valid PPC64 |
| Wii backend | `wii/dc/sh4/rec_v2/wii_driver.cpp` | 4846 | Partly — see section 3 |

Our tree already builds `rec_v2` with `HOST_NO_REC`; the decoder and SHIL tables
link because `sh4_opcode_list` uses them. Only the native backend is missing.
It is stubbed in `ps3/ps3_nojit.cpp`.

The Wii backend handles 66 SHIL opcodes and has optional presets
(`bcache`, `dyn_ic`, `fpu_pin`, `jit_align`, `ifb_flush`, `hotblocks`, `fastmem`).
A first PS3 port would keep them all off.

## 2. What ports cleanly

1. **No paired singles.** The backend uses plain PowerPC FP only. No `psq_*`,
   no Broadway-only opcode. Verified by grep.
2. **The register map is safe on the PS3.** SH4 `r0..r15` (minus `r11`) pin to
   PPC `r14..r28`; `r29`=cycles, `r30`=SH4 context, `r31`=dynamic jump target.
   It never allocates `r2` (TOC) or `r13` (TLS pointer on PS3). Both are
   callee-saved ranges on the PS3 ABI too.
3. **Branch range is fine.** `bl` reaches +/-32 MB. Our ELF is 4 MB and the code
   cache is 6 MB (`CODE_SIZE` in `ngen.h`).
4. **Data pointers fit in 32 bits.** PS3 user addresses stay below 2 GB, so the
   `ppc_li` / `ppc_lip` `addis`+`ori` pairs still build correct addresses.

## 3. Blockers, in order

### 3.1 Executable memory — SETTLED, the gate is open

**Probe result, run on hardware 2026-09-20.** See `ps3/ps3_execmem.cpp`.

| # | Buffer | Result |
|---|---|---|
| 0 | `.bss` static array (the Wii way) | fault |
| 1 | `malloc` | fault |
| 2 | `sysMemoryAllocate`, `PAGE_SIZE_1M` | fault |
| 3 | `sysMMapper` allocate + search_and_map | not reached, see below |
| 4 | **array forced into `.text`** | **returned 42** |

Test 4 wrote both instructions into the buffer, ran `dcbst` + `sync` + `icbi` +
`isync`, and called it through a hand-built ELFv1 descriptor. It returned 42.

Two facts follow:

1. **lv2 does not enforce W^X on the loaded image.** `readelf` marks that
   segment `R E`, with no write bit, yet the store succeeded and the page
   executed. Whatever protection the ELF header asks for, the loaded text pages
   are writable and executable at runtime.
2. **Nothing in the RW segment is executable.** `.bss`, `malloc` and
   `sysMemoryAllocate` all faulted, with no exception.

So the code cache must live inside the ELF's executable segment, as a
`section(".text")` array. That is the linker-section option below, and it is the
only one that works.

**The failure mode is a dead thread, not a dead process.** lv2 killed only the
faulting thread; the heartbeat and audio threads kept running and the app
appeared to hang. Plan for that when debugging generated code: a bad block will
look like a freeze, not a crash.

Test 3 was never a test of execution. `sysMMapperAllocateAddress` returned
`0x80010010` (alignment) because the probe passed a 1 MB alignment argument and
lv2 wants a much larger power of two. That is a probe bug. It still needs
fixing, because 3.4's mirror-mapping idea depends on that call.

The original reasoning is kept below.

### 3.1b The original reasoning

Our binary is ELF64 / `EM_PPC64` (read from the header of `build/nulldc_ps3.elf`).

The Wii puts `CodeCache` in a plain `.bss` array and executes from it, because
libogc maps all RAM executable. On the PS3, `.bss` is very likely RW with no
execute permission.

**This needs lv2 API calls, not `malloc` and not `mprotect`.** PSL1GHT's newlib
has no working `mprotect`, so the Linux branch at
`dc/sh4/rec_v2/driver.cpp:593` cannot be reused. The allocation goes through the
memory mapper:

1. `sysMMapperAllocateMemory(size, SYS_MEMORY_PAGE_SIZE_1M, &mem_id)` - create
   the memory object.
2. `sysMMapperAllocateAddress(size, SYS_MEMORY_PAGE_SIZE_1M, 0, &addr)` -
   reserve the virtual range.
3. `sysMMapperSearchAndMap(addr, mem_id, SYS_MEMORY_PROT_READ_WRITE, &out)` -
   map the object into that range.

Note what is absent: that API has **no execute flag**. Protection is read-only
or read-write, nothing else. Execute permission is whatever lv2 already grants
the page. So the probe does not request an executable page. It finds out
whether lv2 gives one. Page granularity is 64 KB or 1 MB, never 4 KB.

The linker section attribute is the fallback: put the cache inside the ELF's
executable segment, which is mapped executable by definition. The cost is that
the 6 MB cache becomes part of the binary image.

That project rule, "no JIT on PS3", is now withdrawn.

**Probe as built (`ps3/ps3_execmem.cpp`):** map a buffer with the three calls above, write a
tiny PPC function into it (`li r3,42; blr`), run `dcbst` + `sync` + `icbi` +
`isync` over the range, then call it through a function pointer. Log the result
over UDP. A failing test kills the calling thread, so progress is journalled to
`/dev_hdd0/data/NullDC/execprobe.log` before every attempt and the next launch
resumes at the next test.

### 3.2 Calling convention (ELFv1, 64-bit)

- `ppc_call(&UpdateINTC)` emits `bl <symbol address>`. On PPC64 ELFv1 that
  address is the **OPD function descriptor**, not code — the emitted branch
  would land in data. Fix: dereference the descriptor at emit time and branch
  to the real entry point.
- **Stack frame is wrong.** The Wii prologue builds an 88-byte frame and saves
  LR at `sp+4`. PPC64 needs a 112-byte minimum frame, the back chain at `0(sp)`,
  and the callee stores our LR at `16(sp)` — so those slots must be reserved
  and kept free of live data.
- **`r2` must stay untouched.** We link with `--no-multi-toc`, so one constant
  TOC covers the whole program. The generated code can simply never write `r2`,
  which removes any per-call TOC reload. This makes 3.2 much easier than usual.

### 3.3 Cache coherency

`DCFlushRange` / `ICInvalidateRange` are libogc. Replace with `dcbst` + `sync`
+ `icbi` + `isync` loops. **Cell cache lines are 128 bytes, not the Wii's 32.**
`driver.cpp` aligns and rounds to 32; that must follow.

### 3.4 FASTMEM: the Wii design is dead, but the PS3 has another route

`wii_fastmem.h` claims the low 512 MB of PowerPC effective address space, builds
a hashed page table by hand, sets segment registers, and installs a DSI
exception handler that back-patches faulting load/store sites. GameOS homebrew
gets none of that. There is no user DSI hook that resumes at the faulting
instruction, so **that exact design cannot be ported.**

The PS3 does give virtual-memory primitives, and they support a different kind
of fastmem:

- `sysMMapperSearchAndMap` maps **one** memory object at **several** virtual
  addresses. The SH4 RAM mirrors (`0x0C000000`, `0x8C000000`, `0xAC000000`) can
  become real mappings of the same 16 MB instead of a masked C call.
- `sysMMapperAllocateAddress` reserves one contiguous window, so a SHIL load
  becomes `base + (addr & mask)` with `base` pinned in a register.
- Page size is 64 KB or 1 MB. That is coarse, but RAM and VRAM windows are
  megabyte-aligned anyway.

This is **fault-free** fastmem. No exception handler, no back-patching. It only
covers ranges that are plain RAM. Anything that must run a handler (PVR
registers, the TA FIFO, AICA) still needs the C call, so the emitter needs a
static range decision or a guard branch.

lv2 also has a page-fault notification syscall, but it signals a handler
*thread* through an event queue, for on-demand paging. It is not a synchronous,
resumable trap, so it cannot stand in for the Wii DSI handler. PSL1GHT does not
expose it either. Treat it as unavailable.

Keep all of this out of the first port. Ship the C-call path first (`ReadMem*` /
`WriteMem*` trampolines), measure, then decide whether the mirror mapping is
worth it. Until then the PS3 dynarec lands slower than the Wii numbers suggest.

**Built, 2026-09-21 — see 6.8.** The guard branch is unavoidable either way, so
the mapping half was not built: `sysMMapper` would replace one `rlwinm` and add
an lv2 dependency. The window is arithmetic instead.

### 3.5 Small 64-bit fixes

- `bm_bcache` entries are 8 bytes on the Wii (`u32 addr` + 32-bit pointer). On
  the PS3 a pointer is 8 bytes, so the entry is 16. The emitted dispatch
  hardcodes `idx*8` (see the invariant comment in `blockmanager.h`).
- `driver.cpp` has `#if HOST_OS` branches for Windows / Linux / Wii only. A PS3
  branch is needed for allocation, protection, and flushing.

## 4. Effort and payoff

**Effort:** roughly 400–700 lines touched. The bulk of `wii_driver.cpp` is
opcode emission that ports unchanged; the work concentrates in the ABI layer,
the code-cache allocation, and cache flushing. The expensive half is debugging
generated code on hardware with UDP logs only and no debugger.

**Payoff, from the G8 measurements already on record:**

- The SH4 cached interpreter took Crazy Taxi from 45.3% to 67–73% emulation,
  VPS 26.4 to 40–43.7, RPS 12 to 43 in light scenes.
- Dense driving stayed at 10.8–14 RPS. Two separate limits land on that same
  number. The earlier "the RSX is the only wall" reading was not proven.

**Limit A — the cycle ratio.** `SH4_CPU_RATIO_*` is `8` in
`dc/sh4/sh4_interpreter.h`, for all three accuracy presets. Every opcode is
charged 8 SH4 cycles, in both `Sh4_int_Run()` and `Sh4_int_Run_Cached()`. A real
SH4 averages about 1–2 cycles per instruction, so the emulated CPU is roughly
4–8x slower than real hardware even at "100% SPEED".

SPEED% in `plugs/drkPvr/SPG.cpp:256` is
`vblanks/s * spg_FrameSh4Cycles / 200 MHz`. It measures the **emulated clock**,
not instruction throughput. So a 60 fps game that needs the whole SH4 caps near
15 RPS at a 4x derate. 10.8–14 RPS sits right on that cap.

**Limit B — the RSX.** 78.5 ms/frame for ~1306 draw calls of ~8 vertices each
(one `glBegin`/`glEnd` per PVR strip) is measured wall time. That alone is a
12.7 fps wall.

The two ceilings are close, so RPS alone cannot separate them. `rnd%` in the
per-second line can. If `rnd%` is near 100, B binds. If `rnd%` is well below it
while SPEED% is also below 100, A binds. Both can bind at once.

Note the dynarec already assumes the honest number: `rec_config.h:3` sets
`CPU_RATIO (1)`, one SH4 cycle per opcode. So the Wii JIT runs an emulated SH4
about 8x faster than our interpreter does, before any host-speed gain is
counted.

**Why this matters for the dynarec.** The interpreter's ratio is not a physical
constant.
It is 8 because the interpreter cannot execute enough instructions per second to
hold 60 VPS at a realistic ratio. Lowering it to 2 needs about 4x more host work
per frame, and SPEED% would collapse. A dynarec is what buys the right to lower
it. So the payoff is not only more host speed. It is a higher RPS ceiling. The
old conclusion that a dynarec would not help the worst case is withdrawn.

Batching same-state strips is still worth doing, and is still cheaper.

## 5. Recommendation

Do not start the backend port yet. In order:

1. ~~Run the exec-memory probe from 3.1.~~ Done. It passed through `.text`.
2. Separate limit A from limit B. Read `rnd%` from the dense-driving log. Then
   run one dense-driving pass with `CPU_RATIO` at 4 and one at 2, and watch
   SPEED% and RPS. This is a two-line change, and it answers what the RPS
   number alone cannot.
3. Close the G8 freeze (the phase-marker build is already packaged) and look at
   renderer draw-call batching, because that is cheap and certain.
4. Port the backend. Step 1 is the ABI self-test in `ps3/ps3_jitmem.cpp`:
   emit a real ELFv1 function into the `.text` code cache, have it call out to C
   and return. That validates 3.2 on hardware before 4800 lines of opcode
   emission are ported.

---

# 6. Port status — handoff, 2026-09-21

The backend is ported, builds, links, and runs real game code on hardware.
It is **not** finished. This section is the state of play for the next session.

## 6.1 Where it stands

**Static linking works.** `Dynarec=1` with `JitDynEnds=0` and `JitSafe=0`
boots Crazy Taxi and plays it with 3D rendering, for minutes at a time.
Section 6.4 is closed; see 6.7 for the measurements.

New files: `ps3/ps3_driver.cpp` (the backend, from `wii_driver.cpp`),
`ps3/ps3_jitmem.cpp` (ABI self-test), `ps3/ps3_execmem.cpp` (the 3.1 probe).
Modified: `config.h`, `dc/sh4/rec_v2/driver.cpp`, `dc/sh4/rec_v2/decoder.cpp`,
`ps3/ps3_presets.*`, `ps3/ps3_menu.cpp`, `ps3/ps3_nojit.cpp`, `ps3/ps3_main.cpp`.

`HOST_NO_REC` is no longer set for Cell (`config.h`, the `USE_INTERP` test now
excludes `CPU_PPC_CELL`). The code cache is a 6 MB `.text` array in
`dc/sh4/rec_v2/driver.cpp`; `JIT_MANAGED_CACHE` covers Wii and PS3 together and
`jit_sync_icache()` dispatches to libogc or `ps3_jit_icache_sync`.

## 6.2 Bugs found and fixed, all ABI or 64-bit

| # | Bug | Symptom | Fix |
|---|---|---|---|
| 1 | `loop_code()` called as a C function pointer | never left `A0000000` | `ppc_enter_code()` builds an ELFv1 descriptor |
| 2 | Mainloop frame: 88 bytes, LR at `+4`, `stw` saves | would corrupt any C callee | 272-byte frame, `std`/`stdu`, LR at `16(sp)` |
| 3 | `cache[]` indexed `idx*4`, loaded with `lwzx` | died at the first block exit | `idx*8` + `ldx`; `code` read with `ld` |
| 4 | `ppc_li` sign-extends any constant with bit 31 set | died at `8C00B800` | `clrldi rD,rD,32` after the `addis`/`ori` pair |
| 5 | fast RAM read built a raw host address with `addis` from a pinned reg | SH4 thread killed at `8C00B87C` | `clrldi` after the `addis`; same before every cold `ReadMem*`/`WriteMem*` call |
| 6 | `_vmem_MemInfo_ptr[]` indexed `*4` and read with `lwzx` | ran fast, drew almost nothing | `*8` (`rlwinm 11,21,28`) and base biased `+4` for the BE low word |

Bug 4 deserves emphasis: every Dreamcast RAM address starts `0x8C`, so `addis`
made it `0xFFFFFFFF8C00xxxx`. Harmless for SH4 arithmetic, because the emitter
is PPC32-generated and only produces `w`-form instructions, but poison at a C
call — ELFv1 makes the CALLER extend a `u32` argument.

The emitter has no 64-bit forms, so `ppc_std`, `ppc_stdu`, `ppc_ld`, `ppc_ldx`
and `ppc_clrldi32` are hand-encoded near the top of `ps3_driver.cpp`.

## 6.3 Toggles, all in the front end under Emulation

| Menu row | `[ps3]` key | Meaning |
|---|---|---|
| Dynarec | `Dynarec` | 0 interpreter, 1 recompiler |
| JIT Safe Mode | `JitSafe` | interpreter fallback per opcode (also forces DynEnds) |
| JIT DynEnds | `JitDynEnds` | no static block linking, every exit via the dispatcher |
| JIT Trace | `DebugLoop` | logs every block, its guest opcodes, and block ends |
| JIT NewOps / Carry / MAC | `JitNewOps` etc. | extra native opcode groups, all default OFF |
| JIT PCrel IFB | `JitPcRelIfb` | `mova` and PC-relative loads via the interpreter |

`JitNewOps`, `JitCarry` and `JitMac` were hardcoded ON before this port and are
now OFF. They are untested on PS3; turn them on one at a time.

`DebugLoop` now also emits two bring-up instruments, both off unless it is on:
a `[jitcfg]` dump of every toggle in force (no run need ever be ambiguous
again), a `[link]` trace of the first 64 static links, and per-block code that
publishes the guest pc and bumps `g_jit_blocks_entered` at every block entry.
The heartbeat prints that counter as `blk=`: frozen means a dead SH4 thread,
climbing means a livelock. That distinction is what closed 6.4.

`JitPcRelIfb` was the bisect that found bug 4. It should no longer be needed —
leaving it off is the test that bug 4 is really fixed.

## 6.4 CLOSED — static block linking works

The livelock was never in the patch machinery. Two 64-bit defects were, and
both are bugs 5 and 6 above.

**What the evidence ruled out.** A `[link]` trace (in `ngen_LinkBlock_Static`,
behind `DebugLoop`) logged 33 links on hardware. Every one read back the exact
three words `DoStatic()` emitted, the `bl` displacement matched the stub, and
every target displacement was small and correct. The `-12`, the single-word
patch width and the branch encoding were right all along. Both hypotheses in
the old 6.4 — an arithmetic slip, and Cell cache coherency — were wrong.

**Bug 5, the freeze.** A block-entry counter (also behind `DebugLoop`) showed
`blk` frozen with `vbl`, so the SH4 thread was dead, not livelocked. It died
inside one block after 2.4 million block entries. The fast main-RAM read path
tested the window with `rlwinm` — 32-bit — then built the host address with
`addis`, which is 64-bit. A pinned SH4 register holds `w`-form values, and
`extsb`/`extsh`/`srawi` sign-extend to 64 bits on this CPU, so the guard passed
on a register whose upper half was `0xFFFFFFFF` and the raw `lwz` faulted.
This is bug 4's class, in the one place the poison reaches a real host access
instead of a C argument.

Static linking did not cause it. It only changed how far the guest got.

**Bug 6, the dead 3D.** With bug 5 fixed the game ran fast but drew nothing:
`vt:0.00K`, `ta:0k/s`. `_vmem_MemInfo_ptr` is `void*[0x100]`, 4 bytes per entry
on the Wii and 8 here, and three JIT sites still used the Wii stride with a
32-bit `lwzx`. On big-endian, an even page index then read the entry's high
word (0, taken for MMIO, so the correct C call ran) and an odd one read a
*neighbouring* entry's low word — a plausible pointer for the wrong region.
The scalar write path has no RAM shortcut, so every guest write went through
this table; VRAM's 64-bit window is `0xA5......`, page index 165, odd.

Same class as bug 3, fixed there and missed here.

## 6.5 Also open

- **Freeze after ~3 minutes of play, `phase=rtt`.** The last good run stopped
  at `vbl=7412` with the pc parked in a render-to-texture phase. New, and not
  the 6.4 defect. This is the next task.
- **Stall at `8C003FB4`, `vbl=60`,** with `JitDynEnds=1`. Recorded before bugs
  5 and 6 were fixed and very likely one of them. Re-test before chasing it.
- Frequent full cache clears well below capacity. SETTLED: not `JIT_SBP` and
  not `rdv_BlockCheckFail`. Every one follows a `CCN: I-cache invalidation
  requested` line — the guest writes `CCR.ICI` and `CCN_CCR_write` calls
  `sh4_cpu.ResetCache()` (`dc/sh4/ccn.cpp`), dropping all 6 MB. Three fired in
  two seconds of gameplay. `ngen_LinkBlock_Static` now guards against patching
  a site that a clear has killed, mirroring `ngen_LinkBlock_Dynamic_IC`.
- `bm_bcache` still hardcodes `idx*8` for an entry that is 16 bytes at 64-bit.
  Guarded by `verify(sizeof(BlockCacheFlatEntry)==8)`, so BCACHE will abort
  loudly rather than corrupt. Fix before enabling that preset.
- The Wii MMU FASTMEM is permanently off (3.4). `rec_fastmem_patch()` is kept
  behind `#if PS3_FASTMEM_DSI` for reference only. The PS3 replacement is 6.8.

## 6.6 Workflow

- Build: `./build.sh pkg` (runs make in the `ps3dev/ps3dev` Docker image).
  Output `build/nulldc_ps3.pkg`.
- Test only on Matt's PS3 over UDP, never RPCS3. Listener on port 18194.
- **Installing the package wipes `/dev_hdd0/data/NullDC/`**, so every install
  resets `nullDC.cfg` and every toggle. Set them again after each install.
- The startup banner comes from `ps3_main.cpp`'s `__TIME__`, so it does NOT
  change unless that file recompiles. Do not use it to identify a build; use a
  value that moves, such as the jitprobe displacement or the emitted sizes.
- A JIT fault kills only the SH4 thread. The app keeps heartbeating, so a bug
  looks like a freeze with no message. Log before the risky call, not after.

## 6.7 Measurements, 2026-09-21

Crazy Taxi, `Dynarec=1`, `JitDynEnds=0`, `JitSafe=0`, everything else off,
accuracy preset FAST. 141 one-second samples, 112 of them with real geometry
(`vt > 1K`).

| | min | avg | max |
|---|---|---|---|
| VPS | 20.0 | 33.3 | 93.1 |
| RPS | 12.8 | 30.7 | 68.4 |
| SPEED% | 33.4 | 55.5 | 155.3 |
| vertices/s | 1.2 K | 8.1 K | 17.0 K |

`RPS` tracks `VPS` almost exactly, so nothing is being skipped.

Against the baselines already on record: the plain interpreter managed
`RPS:12`, and the cached interpreter `RPS:43` in light scenes but 10.8–14 in
dense driving. The dynarec holds ~30 RPS on average across whole play sessions
including dense driving.

SPEED% is NOT comparable across the two. `rec_config.h` sets `CPU_RATIO 1`
while the interpreter charges 8 (section 4), so the dynarec is doing roughly
8x the emulated work per percent.

## 6.8 FASTMEM, PS3 flavour — built and HW validated 2026-09-21

Preset `JitFastmem` (`[ps3]` key, Emulation -> "JIT Fastmem"), default OFF.
`[jitcfg]` prints it; a `[fastmem]` line names the RAM window at start-up.

**What it does.** Main RAM is 16 MB of plain host memory and the SH4 reaches it
through area 3 of every P0-P3 mirror. So a runtime load or store needs no table
lookup at all:

    rlwinm r0,addr,3,29,2      ; r0 = (area<<29) | region
    xoris  r0,r0,0x6000        ; area 3 folds to 0
    cmplwi r0,7                ; RAM in P0-P3 -> 0..6, P4 -> 7
    bge    cold
    rlwinm rT,addr,0,8,31      ; ea = addr & 0x00FFFFFF
    addis  rT,rT,hi(mem_b)
    [xori  rT,rT,4-sz]         ; big-endian sub-word swizzle
    l/st   ...,0(rT)

The single rotate works because the region bits (31..29) and the area bits
(28..26) are adjacent, so one `rlwinm` brings both into one word with the area
on top. Excluding region 7 is what keeps P4 right: `0xEC000000` masks onto
area 3 but is not RAM, and the store queue at `0xE0000000` must keep reaching
`sq_both`. The page-by-page guard was checked against `mem_map_defualt()` for
all 256 top bytes — it accepts exactly the pages `_vmem` maps to `mem_b`.

**Why this and not `sysMMapperSearchAndMap`.** 3.4's mapping idea still needs a
guard branch, because unmapped pages inside the window would fault fatally and
GameOS has no resumable handler. The mapping would therefore buy one `rlwinm`
and cost an lv2 dependency plus moving `mem_b` out of `memalign`.

**What moved.** The generic `_vmem_MemInfo_ptr` tier is factored into
`emit_vmem_tier()` and, on a fastmem site, emitted in the COLD fragment ahead
of the `ReadMem*`/`WriteMem*` call. So VRAM, the TA FIFO, wave RAM and the
store queue still avoid the C call; they only leave the straight-line stream.
Nothing that is direct today becomes a C call.

Scalar reads gain the most. Before this they had no table tier at all: only the
top byte `0x8C` was inlined and everything else — including `0x0C`, which is
where Crazy Taxi runs — took a full C call.

**Cleanup still owed.** The inline copies of the tier in `shop_readm` /
`shop_writem` were left in place so a `JitFastmem=0` run stayed the untouched
baseline. That baseline has now served its purpose, so they can be deleted and
the preset defaulted ON.

**HW 2026-09-21: PASS.** Crazy Taxi, `Dynarec=1 JitDynEnds=0 JitSafe=0
JitFastmem=1`, RAM window at `0x51b10000`. 327 s of play, 236 one-second
samples with real geometry (`vt > 1K`), no freeze — past the ~3 min `phase=rtt`
stop in 6.5. Matt: "A bit faster!". Log clean: no unimplemented opcodes, no TA
overflow, no dropped audio, 12757 frames presented against 48 skipped.

| | min | avg | max | 6.7 baseline avg |
|---|---|---|---|---|
| VPS | 21.6 | 45.7 | 60.2 | 33.3 |
| RPS | 8.0 | 45.2 | 60.2 | 30.7 |
| SPEED% | 36.1 | 76.3 | 100.4 | 55.5 |

RPS +47%, SPEED% +37%. 49% of samples sit at the 59.95 NTSC display cap.
By geometry load, which is the reading that matters:

| scene | samples | RPS | SPEED% |
|---|---|---|---|
| light, 1-1.5K verts | 9 | 51.9 | 98.2 |
| medium, 1.5-8K | 131 | 56.2 | 94.6 |
| dense, >8K | 96 | 29.5 | 49.3 |

Everything but dense traffic now runs at or near full Dreamcast speed. Dense
driving is unchanged in character and is the renderer wall G6/G7 predicted:
508 strips per render, one `glBegin`/`glEnd` each. Fastmem cannot move that.
Batching same-state strips is the next lever, and `GPstate_cache` already
knows when the state is unchanged.

Open, not a fastmem regression: the XMB exit took the `ExitFix=1` fallback
(`[ps3] SH4 did not stop in 3 s, forcing process exit`) where earlier runs
exited cleanly. A much faster SH4 sits longer inside a timeslice before it
sees `Stop`. Worth one check.
