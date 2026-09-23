# Minicast research - techniques for the PS3 port

Investigation, 2026-09-23. Step 1 (PLFO fix) is applied and built. The other
steps have no code yet.

Rule: every item ends with a hardware test on Matt's PS3. Never RPCS3.
Status: `[ ]` open, `[~]` active, `[x]` done (HW validated), `[!]` blocked.

---

## 0. What Minicast is

- Path: `E:\Users\Matteo\Desktop\Dreamcast\minicast`
- skmp's reicast fork. It runs on the ARM CPU of the DE10-Nano and feeds an
  FPGA PowerVR2 core (polly2-rtl).
- Base: a reicast `libswirl` snapshot (commit `580d7b8`), then 90 commits
  (2025-01 to 2026-08).
- `libswirl/hw/aica/` is **byte-identical** to our local
  `E:\Users\Matteo\Desktop\Dreamcast\reicast-emulator\libswirl\hw\aica\`.
  Only `hw/arm7/arm7_jit_virt*.cpp` and `oslib/audiostream.cpp` differ in the
  sound path.
- Minicast's 2026 work is in the SH4 JIT, the ARM7 JIT, GD-ROM, the TA and the
  host audio. None of it touches the AICA itself.
- Minicast AICA code is little-endian only. It has no big-endian bitfield
  branches. Our `plugs/nullAICA` already has them (`#ifdef WII`, defined in
  `nullAICA.h`).

Our sound path today (for reference):

| Part | File | State |
|---|---|---|
| Channel mixer | `plugs/nullAICA/sgc_if.cpp` | nullDC-era, plus `aica_fast` active-channel mask |
| DSP | `plugs/nullAICA/dsp.cpp` | **stub**, `DSPEnabled = 0` in `nullAICA.cpp:66` |
| ARM7 | `plugs/vbaARM/arm7_cached.cpp` | cached interpreter; `arm7_jit.cpp` compiles only for `OS_WII` |
| Output | `ps3/ps3_audio.cpp` | 44.1 -> 48 kHz **linear** resampler |
| ARAM layout | `plugs/vbaARM/arm_mem.cpp` | native BE u32 words; u16 at `addr^2`, u8 at `addr^3` |

---

## 1. AICA findings

### 1.1 DSP interpreter  `[ ]`  - the main gain

- Our `dsp_step()` only zeroes EFREG. No game gets DSP effects (reverb, echo,
  chorus).
- Minicast/reicast has a portable interpreter:
  - `libswirl/hw/aica/dsp_interp.cpp` (336 lines, from Audio Overload / MAME)
  - `libswirl/hw/aica/dsp_helpers.cpp` (`PACK` / `UNPACK`, `DecodeInst`)
  - `libswirl/hw/aica/dsp_backend.h` (`dsp_context_t`, `_INST`)
- It is pure integer C. No x86 dependency.
- nullDC-v1 (`nulldc/plugins/nullAICA/dsp.cpp`) and nulldc-360
  (`plugins/nullAICA/dsp.cpp`) only have the x86 DSP dynarec, under
  `RECOMPILED_DSP`. Reicast is the only portable DSP source we have.
- Also port from reicast `sgc_if.cpp` `AICA_Sample()`: write CDDA into
  `DSPData->EXTS[0/1]` (or 0 when CDDA is muted), so the DSP can read it.
- `Recompile()` sets `Stopped = 1` when every MPRO word is 0. Keep that:
  it makes the DSP free for games that do not load a DSP program.

### 1.2 PLFO bug in our code  `[~]`  - one line

- `plugs/nullAICA/sgc_if.cpp:870`, in `CalcPlfo`:
  `ch->lfo.alfo = rv >> ch->lfo.plfo_shft;`
  must be `ch->lfo.plfo = ...`.
- 2026-09-23: fixed and built (`build/nulldc_ps3.elf`). HW test pending.
- Source: nullDC-v1 `sgc_if.cpp:933` and nulldc-360 `sgc_if.cpp:1068` have the
  same bug. reicast `sgc_if.cpp:965` fixed it.
- Effect: `plfo_calc` runs after `alfo_calc`, so it overwrites the amplitude
  LFO.
  - PLFOS = 0: `plfo_shft` = 8, so `alfo` is always 0. Tremolo is lost.
  - PLFOS != 0: `alfo` takes the pitch LFO value. Wrong tremolo.
- I could not link it to the ChuChu Rocket echo (`MutePcm16` preset). Test it
  anyway.

### 1.3 Catmull-Rom resampler  `[ ]`

- Minicast `oslib/audiostream.cpp`, `AudioStream_Mister::WriteSample`:
  push-side 4-point Catmull-Rom, 0.45 gain (headroom against overshoot),
  1 or 2 output frames per input frame at 44100/48000.
- Formula:
  `0.45f * ((2*x1) + t*((-x0+x2) + t*((2*x0 - 5*x1 + 4*x2 - x3) + t*(-x0 + 3*x1 - 3*x2 + x3))))`
- Replaces our linear resampler in `ps3/ps3_audio.cpp`. Quality gain, small cost.

### 1.4 Do NOT take

| Item | Reason |
|---|---|
| `AICA_Sample32` channel batching (32 samples per channel) | Disables the DSP. Delays ARM register writes up to 32 samples. Our `aica_fast` mask already skips inactive channels. |
| reicast `AEG_EffRate` | Ours skips the octave term when KRS = 0xF. That agrees with MAME, the SCSP/AICA reference our comment cites. Keep ours. |
| `SendLevel[0] = 255` + 1024-entry `tl_lut` | Same audible result as ours: `tl_lut[255]` is 0 as s32. |
| AICA G2 DMA (`Write_SB_ADST`) | Ours in `dc/aica/aica_if.cpp` already schedules the end interrupt. |

---

## 2. DSP port - endianness and 64-bit check

PS3 PPE is big-endian, 64-bit, `-m64` (8-byte pointers).

| Item | Status on PS3 |
|---|---|
| `DSPData` fields (MPRO, COEF, MADRS, EFREG, EXTS) read as `u32` | Safe. Our register file holds native u32 words (see `ReadMemArrRet` in `nullAICA.h`). SH4 16-bit writes land at `^2` = the low half of the u32. |
| Ring buffer `*(u16*)&aica_ram[ADDR & mask]` (read and write) | **Must change** to `*(u16*)&aica_ram[(ADDR ^ 2) & mask]`. ARAM is native BE words. ADDR is always even (`ADDR <<= 1`), so `^2` is exact. |
| EFREG read back as `*(s16*)&EFREG[i]` | **Must change** on BE. Use `(s16)DSPData->EFREG[i]`. Our current `+2` byte offset in `sgc_if.cpp:1216` is equivalent. |
| `CommonData` RBL / RBP bitfields | Already reversed under `WII` in our `aica.h`. |
| `DSPData_struct` in `aica.h` | Check that our copy matches reicast's layout (`+0x000` COEF, `+0x200` MADRS, `+0x400` MPRO, `+0x1000` TEMP, `+0x1400` MEMS, `+0x1500` MIXS, `+0x1580` EFREG, `+0x15C0` EXTS). |
| `x <<= 8; x >>= 8;` sign extension on `s32` | Safe. GCC uses arithmetic right shift. |
| `(s64)X * (s64)Y` | Safe. Uses `mulld` on the PPE. |
| `PACK` / `UNPACK` | Pure integer arithmetic. Endian-neutral. |
| `dsp_context_t::DynCode[4096*8]` | Drop it. JIT only. |
| `DECL_ALIGN(4096)` on `dsp` | Keep or drop, no correctness effect. |
| `verify(!NOFL)`, `verify(IRA < 0x38)` | Make them no-ops in the PS3 build (reicast does this under `RELEASE`). |

### 2.1 Cost warning

- 128 steps x 44,100 samples = 5.6 M steps/s.
- Each step: ~20 bit-field extracts, several branches, one 64-bit multiply.
- On the in-order PPE this can take a large part of one hardware thread.
- Cheaper options:
  1. **Predecode.** Decode MPRO into an `_INST[128]` array only when
     `dyndirty` is set (`DecodeInst` already exists). The per-sample loop then
     reads plain fields, with no shifts or masks.
  2. **Trim the program.** Store the index of the last non-empty step in
     `Recompile()` and stop the loop there (MDEC_CT still decrements once).
     Check the empty-step shortcut first: it still updates ACC, so trimming is
     only exact when nothing after the last real step reads ACC. Verify
     against the full loop.
  3. **SPE (G9).** The DSP is self-contained. Inputs: MIXS[16], EXTS[2],
     MPRO/COEF/MADRS (rarely change). State: TEMP, MEMS, ring buffer
     (8 K - 64 K words = 16 - 128 KB). Output: EFREG[16]. It needs a batch
     design (e.g. 32 samples of latency).
- Put the DSP behind a preset (Options -> Emulation), default OFF until
  measured. Use the existing `[aud]` and stats line to read the cost.

---

## 3. Other Minicast techniques that fit the PS3

| # | Technique | Minicast source | Fit for us |
|---|---|---|---|
| a | **ARM7 zero-run skip.** If the next 32 words at ARM PC are all 0 (`andeq r0,r0,r0`), skip 128 bytes and charge 3000 cycles instead of compiling. | commit `2874978`, `libswirl/hw/arm7/arm7_jit_virt.cpp`, `CompileCode()` | Goes in the `arm7_cached.cpp` block decoder. We already skip idle loops (`lbl_b_idle`) but not zero runs. Add a counter first: only worth it if the ARM runs over zeroed ARAM. |
| b | **Async GD-ROM.** DMA sector reads run on a worker thread (semaphore kick + atomic `fill_pending`). A mutex serializes disc access with PIO and CDDA reads. A new read command waits for the old fill (`FillReadBufferSync`). | commit `d53f4e4`, `libswirl/hw/gdrom/gdromv3.cpp` | Good for USB/HDD latency spikes. Our CDDA also reads one disc sector from inside `AICA_Sample()` on the SH4 thread (~75/s, `sgc_if.cpp:1156`). Move that to the same worker. Use PSL1GHT `sysLwMutex` / `sysSemaphore`. |
| c | **Cycle check only on backward branches.** Forward-only static blocks just subtract cycles. Only backward static branches poll `intc_sched`. Dynamic ends: Minicast skips the poll too; for us keep the check on dynamic ends (safer). Constprop-promoted static ends are flagged `static_from_constprop`. | commit `c524f7a`, `libswirl/jit/backend/arm32/rec_arm.cpp` | Small change in `ps3/ps3_driver.cpp`. Off by default in Minicast. Expect 0-3% (`Optimization.md` section 4). |
| d | **New SHIL passes**: constprop (with unit tests in `tests/constprop_test.cpp`), dead value / dead store elimination, shift-chain folding, `readm.f32` pair fusion, `ftrv` with known w=1. | commits `6e0fa3c`, `96ebdba`, `67e5949`, `1094ef2`, `8b558b7`, `19eafde`; `libswirl/hw/sh4/dyna/shil.cpp` | Host-independent. This constprop is new and tested, not the reicast one that "crashes on ip". Pair fusion is two 32-bit loads, so endian-safe on our native-word RAM. Expect 0-3%. `tests/` has a host harness worth copying. |
| e | **FP "run fast" on the TA thread.** | commit `b710850`, `libswirl/gpl/lxdream/tacore.inl` | PPE equivalent is FPSCR[NI] on our render thread. Not verified to help. Measure on hardware first. Do not set it on the SH4 thread without checking SH4 FPSCR.DN semantics. |

Not applicable: freerunning scheduler (A9 global timer), polly2 FPGA path,
NEON TA, ARM32/ARM64/x86 JIT backends, `regalloc.h` (see `Optimization.md`
section 2.5).

---

## 4. Suggested order

1. `[~]` Fix the PLFO bug (section 1.2). HW test: a game with music.
   Built 2026-09-23, HW test pending.
2. `[ ]` Port the DSP interpreter (sections 1.1, 2) with the `^2` ring-buffer
   fix, the `(s16)EFREG` read, EXTS feed and predecoded MPRO. Behind a preset.
   HW test: sound correctness, then PPE cost.
3. `[ ]` Catmull-Rom resampler (section 1.3). HW test: listen.
4. `[ ]` ARM7 zero-run skip (section 3a), only if a counter shows zero runs.
5. `[ ]` Async GD-ROM + CDDA reads off the SH4 thread (section 3b).
6. `[ ]` SH4 JIT items (sections 3c, 3d) after the renderer work in
   `Optimization.md`, per its evidence.
