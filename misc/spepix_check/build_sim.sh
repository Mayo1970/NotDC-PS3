#!/bin/sh
# Git Bash: builds the spe_pix SPU image as the Makefile does, with symbols, for misc/spu_sim (sim.sh).
cd "$(dirname "$0")/../.." || exit 1
mkdir -p misc/spepix_check/out
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src ps3dev-latest:latest sh -c '
O=misc/spepix_check/out
$PS3DEV/spu/bin/spu-gcc -O2 -funroll-loops -g -Wall -ffunction-sections -fdata-sections -I$PS3DEV/spu/include -Ips3/spu \
  -Wl,--gc-sections ps3/spu/spe_pix.c -L$PS3DEV/spu/lib -lsputhread -o $O/spe_pix_sim.elf &&
$PS3DEV/spu/bin/spu-objdump -dl $O/spe_pix_sim.elf > $O/spe_pix_sim.dis &&
$PS3DEV/spu/bin/spu-size $O/spe_pix_sim.elf'
