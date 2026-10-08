#!/bin/sh
# Git Bash: builds the spe_tex SPU image as the Makefile does, with symbols, for misc/spu_sim (sim.sh).
cd "$(dirname "$0")/../.." || exit 1
mkdir -p misc/spetex_check/out
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src ps3dev-latest:latest sh -c '
O=misc/spetex_check/out
$PS3DEV/spu/bin/spu-gcc -O2 -g -Wall -ffunction-sections -fdata-sections -I$PS3DEV/spu/include -Ips3/spu \
  -Wl,--gc-sections ps3/spu/spe_tex.c -L$PS3DEV/spu/lib -lsputhread -o $O/spe_tex_sim.elf &&
$PS3DEV/spu/bin/spu-objdump -dl $O/spe_tex_sim.elf > $O/spe_tex_sim.dis &&
$PS3DEV/spu/bin/spu-size $O/spe_tex_sim.elf'
