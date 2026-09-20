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

### 3.1 Executable memory — this is the gate

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

Nothing else in this document matters until this is settled. The project rule
so far is "no JIT on PS3", and `Makefile` records it.

**Proposed probe (~40 lines):** map a buffer with the three calls above, write a
tiny PPC function into it (`li r3,42; blr`), run `dcbst` + `sync` + `icbi` +
`isync` over the range, then call it through a function pointer. Log the result
over UDP. Repeat with the linker-section buffer. If both fault, the dynarec is
dead and the cached interpreter stays. If either returns 42, the port becomes a
real option.

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

1. Run the exec-memory probe from 3.1. Everything depends on its result.
2. Separate limit A from limit B. Read `rnd%` from the dense-driving log. Then
   run one dense-driving pass with `CPU_RATIO` at 4 and one at 2, and watch
   SPEED% and RPS. This is a two-line change, and it answers what the RPS
   number alone cannot.
3. Close the G8 freeze (the phase-marker build is already packaged) and look at
   renderer draw-call batching, because that is cheap and certain.
4. Decide on the dynarec with steps 1 and 2 in hand.
