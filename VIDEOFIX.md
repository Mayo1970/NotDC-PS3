# Space Channel 5 - FMV backdrop investigation

Status as of 2026-09-22, ~01:30. Two real fixes landed and are confirmed on
hardware. The streaming video is still garbled. Everything below is measured on
the PS3 over the UDP log, not inferred.

Reference implementation for all renderer questions is reicast at
`E:\Users\Matteo\Desktop\Dreamcast\reicast-emulator`.

## The symptom

Space Channel 5 (PAL) draws its stage backdrops as pre-rendered video, not 3D
geometry. The video streams through the PVR TA YUV converter and is painted as
one fullscreen quad at the far plane.

Static YUV422 backgrounds (the spaceport scene) render **perfectly**. Only
TA-converter output is wrong: blocky, green-dominant noise with some large-scale
structure still visible.

## Fixed, hardware confirmed

### 1. The 1/W floor - `ps3/ps3_rend.cpp`, `INV_W_FLOOR`

`set_pos`, `track_w`, `emit_mod_vertex` and the background plane clamped 1/W at
1e-4, so everything beyond W=10000 collapsed onto one depth. The video quad
(DepthMode 4 = "greater" on 1/W, `GL_LESS` here) and the background plane both
landed there, compared equal, and the quad was depth-rejected. The backdrop was
invisible, not garbled. Lowered to a shared `1e-9`.

All four sites must move together. Lowering only `set_pos` puts the background
plane in front of the video and it stays black.

Evidence: `wclamp` went 504/window to 0; the quad `w=` went 10000 to 65536;
`near=` unchanged at 0.4995; the backdrop appeared.

### 2. Stride clamp - `ps3/ps3_tex.cpp` `decode()`

The quad declares 512x256 while the converter writes 320x240 with rows packed at
320. The decoder read w=512 columns from a 320-texel row, stealing 192 texels
from the next row on every line. It now converts `min(w, pitch)` columns into
the w-wide output and zeroes the tail, matching the reicast call
`texconv(pb, vram+sa, stride, h)` with a w-by-h buffer (glestex.cpp:349).

Do NOT "fix" this by resizing the texture to 320x240. That was tried and is
wrong: UV extrema measure [0..0.623535],[0..0.93457] - exactly 319/512 and
239/256 - so the game samples the top-left 320x240 of a 512x256 texture. Keep
the declared size and use TEXT_CONTROL as the source pitch.

Side effect: `tex decodes` went from 0/window to ~126/window, so the cache now
tracks the video every frame.

### 3. Soul Calibur (different game, same depth fix)

Its opaque list submits 1/W = +inf vertices. The old code took the near plane
from `min_w`, which those made 0. The reicast-style `max_inv_w` filter (integer
compare; rejects negatives, NaN and >= 0x49800000) plus the far plane pinned at
1/W = 0 fixed it.

## Ruled out, with the evidence

| Candidate | Evidence it is not the cause |
|---|---|
| Encoding / byte order in the converter | Producer word == consumer word, same address, at block 0 AND mid-frame block 149. `first=67C080:003F0602` vs `at(67C080)=003F0602`. |
| Macroblock placement | `midinit=0@0/300`, so no mid-surface `YUV_init` and no cursor rewind. The captured offset for block 149 is pixel (144,112) = row 7 col 9, exactly right. |
| Producer/consumer race | `RenderThread=0` (single-threaded) changes nothing. |
| Draws not reaching the RSX | `trdraw=258/258`, `298/298`, `306/306` - emitted equals attempted, always. |
| Alpha, vertex colour, texture content | With blend, alpha test, texturing and depth test all off, the missing content was still black. `blacktex=0`, `alpha0tex=0` over 126 real decodes. |
| Texture sizing / UV mapping | UV extrema match 320/512 and 240/256 exactly. |
| DMA source address (DmaFix / SAR2) | Toggling "DMA Fix" in the menu changes nothing. 115,200 bytes per call either way, no discontinuity. |
| 32-byte vs 384-byte desync | 384 is exactly twelve 32-byte units and `YUV_index` carries across calls. Byte accounting exact: 300 blocks x 384 x 125 frames = 14.4 MB/window. |
| SQ interleave, VRAM bank interleave | CH2 DMA is the ingress (one surface per call), not the store queue. The YUV branch in `TAWrite` bypasses `pvr_write_area1_32` entirely. |
| 4:2:2 path | `blk422=0` always. `ctrl=00000E13` constant: 20x15 blocks, bit 16 and bit 24 clear. |

## Two traps that cost hours

**Never clear a byte order using uniform data.** Macroblock 0 is black
(U=128, Y0=0, V=129, Y1=0). Uniform words are invariant under byte permutation,
so a black block decodes perfectly under a broken byte order. The "encoding is
proven correct" conclusion rested entirely on block 0 and was worthless. Always
sample a mid-frame, non-uniform value.

**A region mean cannot test byte order.** The `^3` stays inside a 4-byte word,
and every 64-byte boundary is also a 4-byte boundary, so no XOR can change a
region byte multiset. Only a cross-region effect moves a mean: a wrong source
address, a phase error, or data already bad upstream.

Related: "chroma must average 128" is FALSE for a strongly coloured block.
`mean=` was measured swinging 45..152 frame to frame, which is normal. That
argument was used to justify removing `host_ptr_xor` from `GetY420` and
`GetUV420`. It has since been RESTORED, because CH2 memcpys main RAM verbatim
and this host stores guest u32s natively, so a guest byte still lives at host
offset ^3.

## Diagnostics currently in the tree

All temporary. Remove before any release.

- `[yuv]`, once per 5 s from `rth_print_stats`: inits, calls, bytes, blk420,
  blk422, dest, size, ctrl, `first=off:word`, `midinit=n@blk/total`,
  `inUVY=U V Y`, `mean=a/b/c`. Counters live in `dc/pvr/pvr_if.cpp` and are
  exported through `yuv_stats()`.
- `[yuvtex]` in `ps3/ps3_tex.cpp` `decode()`: address, size, pitch, raw VRAM
  words, `row1`, `mid(x,y)` raw and decoded, and `at(off)` - a read-back of the
  address the producer last captured.
- `[yuvq]` in `ps3/ps3_rend.cpp` `dump_fmv_strip`: per big textured strip - TCW,
  format flags, address, declared size, TEXT_CONTROL, bbox, W, `conv=` from
  `yuv_conv_lookup`, and UV extrema.
- `trdraw=emitted/attempted` on the `[rend]` line, counted in
  `ps3/ps3gl/ps3gl_vertices.c` `submit_ranges` (`ps3gl_draw_attempt` and
  `ps3gl_draw_emit`). Note that `submit_ranges` returns silently on a ring-alloc
  failure, so the renderer own strip count never proved a draw happened.

## Next step

The source-to-temp identity probe. One log line for macroblock 149:

    SAR2, SAR2 & 31, ReadMem32(SAR2 + 149*384), ReadMem8(...+0..3),
    the raw host bytes at that offset, and YUV_tempdata[0]

It separates the three remaining candidates outright:

1. Source word == temp word, with the raw bytes reversed against the ReadMem8
   values -> byte-lane interpretation; the swizzle placement is the answer.
2. Temp word matches a different source offset -> the copy is displaced.
3. Source and temp agree but the guest bytes are already implausible -> the
   corruption is upstream in the SH4 decode and the YUV converter is innocent.

Candidate 3 has never been checked. Nothing so far has looked at the data
BEFORE the DMA, and the SH4 runs a JIT on a big-endian host.

Also unresolved and cheap to try: this port re-runs `YUV_init` only on a
`TA_YUV_TEX_BASE` write, while reicast re-inits on BASE or `TA_YUV_TEX_CTRL`.
That cannot cause extra resets but could cause a missing one.

## Build

`./build.sh` runs `ps3dev-latest:latest`. Run `./build.sh clean` first after any
image change - mixed-toolchain object files link without error and then fail on
hardware. That cost a full debugging detour on 2026-09-21.
