# Space Channel 5 - FMV backdrop - CLOSED

Fixed and confirmed on hardware, 2026-09-22.

Space Channel 5 (PAL) draws its stage backdrops as pre-rendered video. The game
decodes MPEG-1 on the SH4, streams the macroblocks through the PVR TA YUV
converter, and paints the result as one fullscreen quad at the far plane. On the
PS3 the picture was blocky, green-dominant noise with some large-scale structure
still visible.

## Root cause

**The SH4 dynarec miscompiled `tst Rm,Rn`.** The converter was never at fault.

`shop_test` in `ps3/ps3_driver.cpp` emitted `and.` (record form, Rc=1) and read
CR0.EQ. On PPC64 `and.` sets CR0 from the **full 64-bit** result. SH4 `tst` is a
32-bit test, and this JIT's pinned SH4 registers carry undefined garbage above
bit 31. So whenever the low word was zero but either upper half was dirty, CR0.EQ
came out 0 and the guest got `T=0` where the hardware gives `T=1`. A wrong `T` is
a wrong conditional branch, and the decoder's inner loops diverged.

The fix takes the condition from the low word explicitly:

```c
ppc_andx(ppc_rarg0, a1, b, 0);          // and, Rc=0
ppc_cmpli(ppc_cr0, ppc_rarg0, 0, 0);    // cmplwi rarg0,0
```

`tst #imm,R0` was never affected. It uses `andi.`, whose zero-extended 16-bit
immediate clears the upper 48 bits, so CR0 is already 32-bit correct there.
`shop_test` was the only unsafe record-form site in the emitter; `cmp/str`'s
`and.` is safe because its mask comes from `ppc_li(0x01010101)`.

Why it looked like a video bug and nothing else: `tst` is common, but the failure
needed a zero low word AND a dirty upper half at the same moment. Ordinary game
code hit that rarely enough to survive. A tight MPEG decode loop hit it every
frame.

## Two real fixes landed on the way here

Both are hardware confirmed and worth keeping, though neither cured the FMV.

### 1. The 1/W floor - `ps3/ps3_rend.cpp`, `INV_W_FLOOR`

`set_pos`, `track_w`, `emit_mod_vertex` and the background plane clamped 1/W at
1e-4, so everything beyond W=10000 collapsed onto one depth. The video quad
(DepthMode 4 = "greater" on 1/W, `GL_LESS` here) and the background plane both
landed there, compared equal, and the quad was depth-rejected - invisible, not
garbled. Lowered to a shared `1e-9`.

All four sites must move together. Lowering only `set_pos` puts the background
plane in front of the video and it stays black.

### 2. Stride clamp - `ps3/ps3_tex.cpp` `decode()`

The quad declares 512x256 while the converter writes 320x240 with rows packed at
320. The decoder read w=512 columns from a 320-texel row, stealing 192 texels
from the next row on every line. It now converts `min(w, pitch)` columns into the
w-wide output and zeroes the tail, matching reicast's `texconv(pb, vram+sa,
stride, h)` with a w-by-h buffer (glestex.cpp:349).

Do NOT "fix" this by resizing the texture to 320x240. That was tried and is
wrong: UV extrema measure [0..0.623535],[0..0.93457] - exactly 319/512 and
239/256 - so the game samples the top-left 320x240 of a 512x256 texture. Keep the
declared size and use TEXT_CONTROL as the source pitch.

## Smaller changes kept

- `YUV_init()` now re-runs on a `TA_YUV_TEX_CTRL` write as well as
  `TA_YUV_TEX_BASE`, matching reicast (`pvr_sb_regs.cpp:382`). CTRL carries the
  block grid, so writing BASE then CTRL previously left a stale grid.
- The YUV macroblock loop builds its output word as a value instead of punning a
  `u8[4]` through `*(u32*)`. Same result on either host endian, and strict
  aliasing can no longer reorder it.
- `shop_pref` and the `WriteMem32` cold path now zero-extend their `u32`
  arguments before the C call, which is what ELFv1 requires and what the rest of
  that file already did.

## How it was found

The converter had absorbed a great deal of effort on the theory that the
corruption was in the YUV path. It was not, and the evidence that settled it was
cheap:

1. **`Dynarec=0` renders the FMV perfectly.** One cfg edit. This moved the whole
   search into the JIT and retired every converter theory at once.
2. **Whole-surface plane means.** A single macroblock can legitimately sit far
   from 128, so a per-block chroma mean proves nothing - that mistake cost real
   time. Averaged over all 300 blocks of a frame, both chroma planes MUST land
   near 128. Broken runs read `mean=58/66/58`, with all three planes sharing one
   distribution. Correct runs read `mean=122/135/30`.
3. **`JitIfbMask`.** A bitmask in nullDC.cfg `[ps3]`: bit N forces every SH4
   opcode with top nibble N through the interpreter and changes nothing else.
   Five cfg-only runs narrowed 16 groups to one (`0-7`, `0-3`, `2-3`, `2`), then
   `JitIfbGroup`/`JitIfbLoMask` sub-bisected group 2 by low nibble and cleared
   the multiplies, the stores and `div0s`. Four opcodes were left, and reading
   their emitters gave the answer.

**`JitSafe` and `JitDynEnds` are useless as bisect tools.** Both also set
`OnlyDynamicEnds`, which swaps the emission path from `rec_oph` to `dec_generic`
on top of disabling static block linking - two or three variables at once.
`JitDynEnds=1` on its own stops the FMV playing at all. That is why `JitIfbMask`
exists; reach for it first next time.

Note that `cfgLoadInt` parses with `atoi`, so every mask value in the cfg file is
DECIMAL. The `[jitcfg]` log line echoes them as hex.

## Diagnostics

The temporary `[yuv]`, `[yuvtex]`, `[yuvq]` and `trdraw=` probes have been
removed. `JitIfbMask`, `JitIfbGroup` and `JitIfbLoMask` are kept - they are
cheap, off by default, and the next JIT hunt will want them.

## Build

`./build.sh` runs `ps3dev-latest:latest`; `./build.sh pkg` produces the installer.
Run `./build.sh clean` first after any image change - mixed-toolchain object files
link without error and then fail on hardware. That cost a full debugging detour on
2026-09-21.
