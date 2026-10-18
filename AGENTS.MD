# AGENTS.MD

Instructions for AI coding agents that work on NotDC-PS3. Read all of it before the first change.
What the user says in the chat always comes before this file.

## Rules of work

- Do only what the user asks. Do not add work, refactors or "fixes" that nobody asked for.
  If you find a problem outside the task, report it and stop.
- Reply short and direct. No emojis.
- A change is done only after a hardware (HW) test passes.
- Speed comes before accuracy. If a fix costs speed, say so and offer a faster option.
- If the task is not clear, ask. Do not guess.

## Project

NotDC is a Sega Dreamcast emulator for the PS3. It runs as homebrew (CFW or HEN) and builds with the open-source
PSL1GHT SDK (ppu-gcc and spu-gcc 7.2.0). It started from nullDC4Wii (nullDC-v1 core). License: GPL-3.0.

- SH4: interpreter plus a PPC JIT on the PPE (`dc/sh4/rec_v2/` front end, `ps3/ps3_driver.cpp` back end).
- PVR2: TA decode in `plugs/drkPvr/`, draw in `ps3/ps3_rend.cpp` through ps3gl (GL 1.1 on the RSX).
- AICA, ARM7, texture decode, texture hash, vertex build: SPE programs in `ps3/spu/`.
- Title ID `NOTDC0000`. Data folder `/dev_hdd0/data/NotDC/` (settings in `NotDC.cfg`, `[ps3]` section).

## Commands

Run from Git Bash in the repo root, with Docker Desktop on.

| Command | Result |
|---|---|
| `./build.sh` | `build/notdc_ps3.elf` |
| `./build.sh pkg` | `build/notdc_ps3.pkg`, the file for HW tests |
| `./build.sh self` | `build/EBOOT.BIN` |
| `./build.sh clean` | Deletes `build/` |
| `./build.sh pkg LOG_IP=<PC IP>` | Sends the UDP log to that PC, port 18194 |
| `./build.sh pkg DEBUG=1` | Adds `-g -DPS3_DEBUG` |

TOC size. Run it after each package and report the value:

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src ps3dev-latest:latest ppu-readelf -S -W build/notdc_ps3.elf | grep ' .got '
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src ps3dev-latest:latest ppu-nm build/notdc_ps3.elf | grep -c r2off
```

PC checks. There is no unit test suite. The checks in `misc/` are the tests. They are local only: `.gitignore` keeps
`misc/` out of the repo, except `misc/fsca-table.bin`. A fresh clone has no checks. Run them in WSL from the repo root:

```sh
sh misc/<name>_check/run.sh          # build and run one check
python3 misc/<name>_check/mutate.py  # mutation run: each mutant must be CAUGHT
bash misc/spu_sim/run.sh             # the real SPU image in a PC simulator
```

`misc/spu_sim/build_spu.sh` makes that SPU image. It runs from Git Bash (Docker). Each script's header says where
it runs and what arguments it takes. A run of more than 10 minutes must run detached with a log file
(`nohup ... > /tmp/x.log 2>&1 &`), because a tool call stops at 600 s. Do not run two checks that share a `/tmp` folder
at the same time.

CI (`.github/workflows/build.yml`) builds the package on each push with the same Docker image.

## Repository map

| Path | Content |
|---|---|
| `dc/` | Dreamcast core: `sh4/`, `sh4/rec_v2/` (decoder, SHIL, block manager), `mem/`, `pvr/`, `aica/`, `gdrom/`, `maple/`, `asic/` |
| `plugs/drkPvr/` | TA, PVR registers, SPG timing |
| `plugs/caiqueAICA/` | AICA model and DSP. Also built for the SPU |
| `plugs/vbaARM/` | ARM7 core |
| `plugs/ImgReader/` | Disc images: CDI, GDI, CHD, ELF |
| `plugs/nullExtDev/` | Modem (DCNet) |
| `ps3/` | PS3 layer: `ps3_main.cpp`, menu, presets, renderer, JIT back end (`ps3_driver.cpp`, `ps3_jit_*.h`), audio, input, SPE hosts |
| `ps3/ps3gl/` | GL 1.1 on the RSX. Shaders are prebuilt |
| `ps3/spu/` | SPU programs, linked into the ELF with `bin2s` |
| `emitter/PPCEmit/` | PPC instruction encoders (`ppc_emit.h`, `ppc_emitter.h`) |
| `deps/` | chdr, LZMA, FLAC: upstream files, unchanged. Do not edit |
| `config/` | `NotDC.cfg` reader and writer |
| `misc/` | Local only: PC checks (`*_check/`), SPU simulator (`spu_sim/`). Git tracks only `fsca-table.bin` |
| `assets/`, `icon/` | Menu art and XMB icon, linked with `.incbin` |
| `.github/` | CI build, issue template, compatibility page |

Code comments name old plan items (`P1`-`P105`, `bundle N`, `G1`-`G16`). The plan files are not in the tree, and
`.gitignore` blocks them. They are in the git history (commit `95cf5d1` and before). Read them with
`git show 95cf5d1:<file>`.

## Hardware test workflow

1. Build the package. Report the `.got` size and any edit by another session (see below).
2. Give numbered HW steps: the game, where to go in the game, and what to look for.
3. List every menu option that the test needs, not only the changed one. An install keeps the old `NotDC.cfg`.
4. Stop and wait for the result. Do not start the UDP listener (port 18194) until the user asks for it.

Rules:

- Never run a build in RPCS3 or in any other emulator. Test only on real hardware.
- Tests run at SH4 Clock 200 MHz.
- Before you name a test game, prove from old logs that the game runs the changed code path.
- Do not use Soul Calibur for SH4 or JIT speed tests. It is render-bound.
- One log file can hold many boots. Split it at each `[menu] launching` line before you tie a counter to a game.
- The `[ps3] NotDC <date time>` line is the compile time of one file, not the package time. To prove which build
  runs, look for a log key that only the new build prints.
- Read the `[jitcfg]`, `[sndcfg]` and other `[...cfg]` lines before you compare speed between two runs.

## How to add a setting

New behavior goes behind a menu row. Value 0 keeps the old path. Change the default only on request, after a HW result.

1. Add `int g_<name>_preset = 0;` to `ps3/ps3_presets.cpp` and its `extern` to `ps3/ps3_presets.h`.
2. Add `{ "<Key>", &g_<name>_preset },` to `s_items` (saved in `[ps3]`), and to the list of its `[...cfg]` log line.
3. Add an `Option` row to the page array in `ps3/ps3_menu.cpp` (`s_emu`, `s_jit`, `s_renderer`, ...).
4. If the value can change while a game runs, add it to `k_live_opts`. If not, it applies at the next boot.
5. Keep the row count under `MAX_INGAME_OPTS` (256). Above it, the last rows drop without an error.

Before you add a key or a row, or move a line, grep `misc/` for the text next to it. Many checks count exact source
lines or cut regions between marker comments (`// <name>_check: begin` ... `end`). For example, `rtt_check` needs
`KeepFrame` as the last entry of `s_items`. After a change, run every `misc/*/run.sh` that names a changed file.

## PS3 traps

Each of these cost one or more HW runs. A fault kills only the faulting thread, so most look like a freeze, not a crash.

- **JIT code cache.** Code runs only from a `.text` array (`ps3/ps3_jitmem.cpp`). RW memory never executes. Sync
  128-byte lines (`dcbst`, `sync`, `icbi`, `isync`).
- **Upper 32 bits.** Pinned SH4 registers hold garbage in bits 32-63. Emit `clrldi rD,rD,32` before a
  64-bit use (host address, C call argument). Never take a 32-bit result from a record form (`and.`, `add.`): use the
  plain form, then `cmplwi rD,0`. `andi.` and `ori.` are safe.
- **ELFv1 ABI.** The build is 64-bit ELFv1. A `u64` argument or result goes in one GPR, not a 32-bit register pair.
- **RSX memory.** It is cache-inhibited. Use naturally aligned word stores (`volatile u32*`) or `memcpy`. GCC can merge
  two floats into one unaligned `std`: check the disassembly.
- **TOC.** `.got` is near its 64 KB limit (0xFFC8 of 0x10000 on 2026-10-18). Each new global, static or string literal
  costs 8 bytes. Use enums and numbers, not strings. Keep new state in one static struct. Build cold objects with
  `-mminimal-toc` (see the `Makefile`). Never remove `--no-multi-toc`: r2off stubs hang the PS3 at boot.
- **Compiler flags.** `-O2` only (`-O3` broke boot). With `-mno-altivec`, `jmp_buf` is 256 bytes but libc and libpng
  use 512. Do not declare a `jmp_buf`. Pass the library's own size to its setjmp API.
- **Timebase.** Use `gettime()` from `ps3/include/ps3_timebase.h`. A bare `mftb` can read 53.8 s in the past.
- **SPU threads.** `main()` gets only 3 arguments; the 4th arrives as 0. Pack extra buffers into one struct.
- **PSL1GHT librsx.** Never call `rsxSetTransferData`: it never returns. `rsxFinish` with the same ref value as the
  last call does not wait.
- **Shaders.** Never regenerate `ps3/ps3gl/ps3gl_shader_data.h`; its VP is an old build. Put new programs in their own
  prebuilt header. cgcomp sets DISABLE_PC on MUL, DP*, DIV and NRM that read a texcoord: read it through `MOV` or
  `saturate()` first.
- **Byte order.** The host is big-endian. When a value reads wrong, check the byte order first. Test it with data
  that is not uniform.
- **Build image.** Use only `ps3dev-latest:latest` through `build.sh`. After any image change, run `./build.sh clean`:
  objects from two toolchains link without an error and then fail on the PS3.

## Editing rules

- **Comments.** Only for logic that is not obvious. 1-2 lines at most, file headers too. Keep the "why".
  When you edit a file, also cut its existing comments that break this rule.
- **License headers.** A file ported from Flycast, Reicast or nullDC keeps its GPL block unchanged. The comment rule
  does not apply to it.
- **Line endings.** The tree mixes CRLF and LF files. Keep each file's own line ends. Never convert in bulk. Check with
  `git ls-files --eol <file>`. Git Bash `sed -i` changes CRLF to LF: use an exact-string edit tool or Python.
- **Git.** Never use `git stash`, `checkout` or `reset` in this tree. Read old versions with `git show HEAD:<path>`.
- **Parallel sessions.** More than one agent can work in this tree at the same time. They share `build/` and port
  18194. Before a build, list the files changed in the last hour and name edits that are not yours. Use exact-string
  edits on shared files, never whole-file rewrites.
- **Packages.** Keep one package in `build/`. Do not keep copies of old packages there. Put compare files in a temp folder.
- **Style.** Match the code around the change. PS3-side settings are `g_<name>_preset` globals. Log lines start with
  a `[tag]`. `printf` goes to the UDP log.

## References

- Core behavior: nullDC-v1 / nullDCe. Renderer (depth, texture formats, YUV): Reicast. Flycast is allowed as a reference.
- AICA DSP: caique is the authority.
- Do not use Wii code or Wii presets unless asked. Do not use the nullDC 360 port.
- Never use or emit code from Sony's proprietary SDK.

## Git and release

- Commit or push only when asked. Commit titles are short (`Network`, `Shenmue fixes`).
- No AI attribution: no `Co-Authored-By` line, no AI text in commits or PR descriptions.
- Never commit BIOS files, flash dumps, disc images or secrets.
- `.gitignore` keeps out build output, `misc/`, the old plan files, `NotDC.cfg`, BIOS and disc images, archives and
  editor files. Never force-add them (`git add -f`).
- Do not move or rename `misc/fsca-table.bin`. `ps3/ps3_fsca_table.S` links it, and CI builds from a fresh clone.
- `README.md` is for users. Write it in Simplified Technical English (ASD-STE100).
- `APP_VER` in the `Makefile` sets the XMB version.
