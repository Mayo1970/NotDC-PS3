#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B2 (WSL): out/spe_arm7_sim.elf (build_spu.sh first) against the x86 reference, 8 seeds.
# Seed % 4 = Arm7Batch - 1; seed bit 2 = Idle Skip SAFE; seed % 4 == 3: SH4 write storm, the sound SPU lags 1000 batches.
cd "$(dirname "$0")"
mkdir -p out
gcc -O2 -c ../spu_sim/spusim.c -o out/spusim.o || exit 1
g++ -O2 -std=c++17 -Wall -Wno-unused-function -DCAIQUE_RAM_HOOKS -I../spu_sim -I../../plugs/caiqueAICA -I../../ps3/spu \
	harness.cpp ../../plugs/caiqueAICA/aica_model.cpp out/spusim.o -o out/harness || exit 1
B=${BATCHES:-6000}
fail=0
for s in 1 2 3 4 5 6 7 8; do
	./out/harness out/spe_arm7_sim.elf out/syms.txt $s $B || fail=1
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
