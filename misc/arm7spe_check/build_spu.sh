#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B2 (Git Bash): spe_arm7 as the Makefile builds it, with symbols, for misc/spu_sim.
# SRC=<file> builds another copy of spe_arm7.cpp (mutate.sh).
set -e
cd "$(dirname "$0")/../.."
mkdir -p misc/arm7spe_check/out
SRC=${SRC:-ps3/spu/spe_arm7.cpp}
MSYS_NO_PATHCONV=1 docker run --rm -e SRC=$SRC -v "$(pwd -W):/src" -w /src ps3dev-latest:latest sh -c '
S=$PS3DEV/spu/bin
O=misc/arm7spe_check/out
$S/spu-g++ -O2 -Wall -ffunction-sections -fdata-sections -I$PSL1GHT/spu/include -Ips3/spu -fno-exceptions -fno-rtti \
  -DCAIQUE_RAM_HOOKS -Iplugs/caiqueAICA -Wl,--gc-sections $SRC plugs/caiqueAICA/aica_model.cpp \
  -L$PSL1GHT/spu/lib -lsputhread -o $O/spe_arm7_sim.elf &&
$S/spu-nm $O/spe_arm7_sim.elf | grep -E " _ZL3cpu$| _ZL2ac$| _ZL5s_now$" > $O/syms.txt &&
$S/spu-size $O/spe_arm7_sim.elf && cat $O/syms.txt'
