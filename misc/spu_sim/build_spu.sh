#!/bin/sh
# Git Bash: builds the spe_aica SPU image like the Makefile does, but keeps the symbols (for the profile).
cd "$(dirname "$0")/../.." || exit 1
mkdir -p misc/spu_sim/out
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src ps3dev-latest:latest sh -c '
F="-O2 -g -Wall -ffunction-sections -fdata-sections -I$PS3DEV/spu/include -Ips3/spu"
O=misc/spu_sim/out
$PS3DEV/spu/bin/spu-gcc $F -c ps3/spu/spe_aica.c -o $O/spe_aica_main.o &&
$PS3DEV/spu/bin/spu-gcc $F -Os -Iplugs/caiqueAICA -c ps3/spu/spe_dsp_jit.c -o $O/spe_dsp_jit.o &&
$PS3DEV/spu/bin/spu-g++ $F -fno-exceptions -fno-rtti -DCAIQUE_RAM_HOOKS -DCAIQUE_PROF -DCAIQUE_DSP_HOOK -DCAIQUE_ADV_RENDER_ONLY -Iplugs/caiqueAICA \
  -Wl,--gc-sections $O/spe_aica_main.o $O/spe_dsp_jit.o ps3/spu/spe_aica_model.cpp plugs/caiqueAICA/aica_model.cpp \
  -L$PS3DEV/spu/lib -lsputhread -o $O/spe_aica_sim.elf &&
$PS3DEV/spu/bin/spu-objdump -dl $O/spe_aica_sim.elf > $O/spe_aica_sim.dis &&
$PS3DEV/spu/bin/spu-size $O/spe_aica_sim.elf'
