# NotDC-PS3

NotDC is a Sega Dreamcast emulator for the PlayStation 3. It runs as homebrew on a PS3 with CFW or HEN. It is built
with the open-source [PSL1GHT](https://github.com/ps3dev/PSL1GHT) SDK.



NotDC is work in progress. See
[Game compatibility](#game-compatibility).

## Install and set up

1. Install `notdc_ps3.pkg` with the Package Manager on the XMB.
2. Copy `dc_boot.bin` to `/dev_hdd0/data/NotDC/data/`.
3. Optional: copy `dc_flash.bin` to the same folder. Without it, NotDC makes a flash (USA, English, NTSC).
4. Copy your disc images to `/dev_hdd0/data/NotDC/discs/` or to a USB drive.
5. Start NotDC from the Game column and select a disc.

**NotDC does not include a BIOS or any game.**

| Path | Content |
|---|---|
| `/dev_hdd0/data/NotDC/NotDC.cfg` | Settings. A new package install keeps them. |
| `/dev_hdd0/data/NotDC/cheats/` | Cheat files, `<image name>.cht`. Set the Cheats option to ON. |
| `/dev_hdd0/savedata/vmu/` | VMU saves, raw 128 KB images. |


## Game compatibility

### Check a game

- The [compatibility list](https://mayo1970.github.io/NotDC-PS3/) can be found here.
- All reports, approved or not, are in the
  [issues with the `compatibility` label](https://github.com/Mayo1970/NotDC-PS3/issues?q=label%3Acompatibility).

### Test a game

1. Search the reports for the game. If a report exists, add a comment to it, and do not open a new one.
2. Use the default settings first. If you change a setting, write it in the report.
3. Play past the intro and the menus, into the game. Try more than one level or mode if you can.
4. Write down what goes wrong, where, and if you can make it happen again.
5. Provide some images/videos if possible.

### Send a report

1. Open a [Game compatibility report](https://github.com/Mayo1970/NotDC-PS3/issues/new?template=game_compatibility.yml).
2. Fill in the form. Send one report for each game.
3. Select the status that fits best:

| Status | Meaning |
|---|---|
| Perfect | No issues found. |
| Playable | Playable, but with minor glitches (graphics, audio, slowdowns). |
| In-game | Gets into gameplay, but major problems stop you from finishing it. (heavy slowdowns) |
| Menus / Intro | Reaches the title screen, menus or intro, but not gameplay. |
| Boots | Shows something (logo, black screen with sound) and then stops. |
| Nothing | Does not boot, black screen, or crashes at once. |

Reports about games that run well help too. Send them.

**Homebrew games and ports are out of scope.** Homebrew can work, but it is
not tested or supported. Do not open issues or compatibility reports for them.

The maintainer reads each report and adds the `approved` label. Only approved reports go on the compatibility list. If
someone without write access edits a report, the label is removed until it gets approved again.

### Known limits

**Windows CE** games will run but won't reach full speed. You can find them [here](https://segaretro.org/Category:Dreamcast_Windows_CE_games).

Some games (ex. Quake 3 Arena, Dead or Alive 2) are too demanding for the PS3 thus they will most likely not reach full speed in future.

Some FMVs will suffer from slowdowns. Gameplay is unaffected.

All cases are still worth reporting nonetheless.

## Compiling

The build makes `build/notdc_ps3.pkg`. The maintainer builds in Docker with `ppu-gcc` 7.2.0.

### With Docker (recommended)

`build.sh` runs `make` in a Docker image named `ps3dev-latest:latest`. The image has the toolchain, PSL1GHT and
the libraries (FreeType, libpng, zlib). GitHub Actions uses the same image.

1. Get the prebuilt image from Docker Hub
   ([mayo2853/ps3dev-latest](https://hub.docker.com/r/mayo2853/ps3dev-latest)) and give it the local name:

   ```sh
   docker pull mayo2853/ps3dev-latest:latest
   docker tag mayo2853/ps3dev-latest:latest ps3dev-latest:latest
   ```

2. In the NotDC-PS3 folder, build the package:

   ```sh
   ./build.sh pkg
   ```

On Windows, run `build.sh` from Git Bash with Docker Desktop.

#### Make the image yourself

Do this only if you cannot use the prebuilt image.

1. Clone [ps3dev](https://github.com/ps3dev/ps3dev):

   ```sh
   git clone https://github.com/ps3dev/ps3dev.git
   cd ps3dev
   ```

2. Save this file as `Dockerfile` in that folder:

   ```dockerfile
   FROM ubuntu:24.04

   ENV DEBIAN_FRONTEND=noninteractive
   ENV PS3DEV=/usr/local/ps3dev
   ENV PSL1GHT=$PS3DEV
   ENV PATH=$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin

   RUN apt-get update && apt-get install -y autoconf automake bison \
       bzip2 ca-certificates clang curl flex g++ gcc git libelf-dev \
       libgmp3-dev libncurses5-dev libssl-dev libtool libtool-bin \
       make patch pkg-config python-is-python3 python3 python3-dev \
       subversion tar texinfo wget xz-utils zlib1g-dev \
       && rm -rf /var/lib/apt/lists/*

   WORKDIR /opt/ps3dev-src
   COPY build-all.sh ./
   COPY scripts ./scripts
   COPY utils ./utils

   # Windows checkouts use CRLF, which breaks bash scripts.
   RUN sed -i 's/\r$//' build-all.sh scripts/*.sh utils/*.sh \
       && chmod +x build-all.sh scripts/*.sh

   RUN ./build-all.sh && rm -rf build

   WORKDIR /src
   ```

3. Build the image. This builds the toolchain, PSL1GHT and the libraries (FreeType, libpng, zlib), so it takes a
   long time:

   ```sh
   docker build -t ps3dev-latest:latest .
   ```

4. In the NotDC-PS3 folder, run `./build.sh pkg`.

#### Build commands

| Command | Result |
|---|---|
| `./build.sh` | `build/notdc_ps3.elf` |
| `./build.sh self` | `build/EBOOT.BIN` |
| `./build.sh pkg` | `build/notdc_ps3.pkg` |
| `./build.sh clean` | Deletes `build/` |

| Variable | Effect |
|---|---|
| `LOG_IP=<PC IP>` | The PC that gets the UDP log, port 18194. Example: `./build.sh pkg LOG_IP=192.168.1.20` |
| `DEBUG=1` | Adds `-g` and `-DPS3_DEBUG`. |

Run `./build.sh clean` before you build again with another image or another variable. Object files from two
toolchains link without an error, but the program then fails on the PS3.

### With a local toolchain

1. Install the PS3 toolchain, PSL1GHT and the libraries as the [ps3dev](https://github.com/ps3dev/ps3dev) README
   says (Linux, macOS, or WSL on Windows).
2. Set `PS3DEV`, `PSL1GHT` and `PATH` as that README says.
3. In the NotDC-PS3 folder, run `make pkg`.

### Shaders

The RSX shaders are prebuilt in `ps3/ps3gl/`. A normal build does not need the Cg Toolkit.

### Builds from GitHub Actions

Each push builds a package in GitHub Actions. Download it from the Actions tab. The artifact name is
`notdc-ps3-build_<first 8 characters of the commit>`.

## AI disclosure

NotDC-PS3 is an AI-assisted project. The emulator itself was tested on real PS3 hardware.

## Credits

- [Flycast](https://github.com/flyinghead/flycast)
- [skmp](https://github.com/skmp): nullDC, Reicast and Minicast, as well as providing the required documentation to get the emulation in the state it is today.
- [BenoitAdam](https://github.com/BenoitAdam/nullDC4Wii): the nullDC4Wii port, which NotDC started from.

## License

NotDC-PS3 is free software, licensed under the
[GNU General Public License, version 3](https://www.gnu.org/licenses/gpl-3.0.html) (GPL-3.0). You can
use, share and change it under the terms of that license. It comes with no warranty. See [LICENSE](LICENSE).
If you share a package built from NotDC-PS3, you must also give its full source code under the GPL-3.0.