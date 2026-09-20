# Dynarec on PS3 — feasibility study

Investigation of the nullDC4Wii SH4 dynamic recompiler (`rec_v2`) and what a PS3
(PSL1GHT) backend would cost. Written 2026-09-20. No code was changed.

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
execute permission. Options if it is: map a buffer through `sys_mmapper`, or
place the cache in an executable segment with a linker section attribute.

Nothing else in this document matters until this is settled. The project rule
so far is "no JIT on PS3", and `Makefile` records it.

**Proposed probe (~30 lines):** allocate/map a buffer, write a tiny PPC function
into it (`li r3,42; blr`), run `dcbst` + `sync` + `icbi` + `isync` over the
range, then call it through a function pointer. Log the result over UDP. If it
faults, the dynarec is dead and the cached interpreter stays. If it returns 42,
the port becomes a real option.

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

### 3.4 The FASTMEM preset cannot be ported

`wii_fastmem.h` claims the low 512 MB of PowerPC effective address space, builds
a hashed page table by hand, sets segment registers, and installs a DSI
exception handler that back-patches faulting load/store sites. GameOS homebrew
has none of that access. Drop it; keep the C-call memory path (`ReadMem*` /
`WriteMem*` trampolines).

This costs real speed on the Wii side, so the PS3 dynarec would land slower than
the Wii numbers suggest.

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
- Dense driving stayed at 10.8–14 RPS. The RSX needs 78.5 ms/frame for ~1306
  draw calls of ~8 vertices each (one `glBegin`/`glEnd` per PVR strip).

So a dynarec would help light scenes and would **not** fix the worst case.
Batching same-state strips would.

## 5. Recommendation

Do not start the backend port yet. In order:

1. Run the exec-memory probe from 3.1. Everything depends on its result.
2. Close the G8 freeze (the phase-marker build is already packaged) and look at
   renderer draw-call batching, because that is where the frames are.
3. Only then decide on the dynarec.
