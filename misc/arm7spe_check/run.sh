#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B2 (WSL): out/spe_arm7_sim.elf (build_spu.sh first) against the x86 reference. Seed % 4 =
# Arm7Batch - 1, bit 2 Idle Skip SAFE, % 4 == 3 a write storm and a lagging sound SPU, bits 3-5 the 6.5/P44/P31 fixes.
SEEDS_B5="100 101 102 106 108 110 115 116 117 118 122 124 127 130 131"   # bundle 5: harness.cpp's header has the bits
SEEDS_B5="$SEEDS_B5 148 149 150 151 180 181 182 183 190"   # SPE AICA Sync at + BLOCK SYNC (148-151) and + SAMPLE SYNC
cd "$(dirname "$0")"
mkdir -p out
gcc -O2 -c ../spu_sim/spusim.c -o out/spusim.o || exit 1
g++ -O2 -std=c++17 -Wall -Wno-unused-function -DCAIQUE_RAM_HOOKS -I../spu_sim -I../../plugs/caiqueAICA -I../../ps3/spu \
	harness.cpp ../../plugs/caiqueAICA/aica_model.cpp out/spusim.o -o out/harness || exit 1
B=${BATCHES:-6000}
fail=0
for s in ${SEEDS:-1 2 3 4 5 6 7 8 9 10 17 18 33 34 56 57 58 59 60 61 62 63 $SEEDS_B5}; do
	./out/harness out/spe_arm7_sim.elf out/syms.txt $s $B || fail=1
done
# Bundle 9: ARM7 STR Align (flag 128) alone and with the 6.5/P44/P31 fixes; then the conformance binaries and the op
# tests (plugs/vbaARM/arm7_optest.h) on the SPU image, the row off and on.
if [ -z "$SEEDS" ]; then
	./out/harness out/spe_arm7_sim.elf out/syms.txt 7 $B 128 || fail=1   # seed 7 makes the most unaligned stores
	./out/harness out/spe_arm7_sim.elf out/syms.txt 9 $B 135 || fail=1
	./out/harness out/spe_arm7_sim.elf out/syms.txt optest 0 || fail=1
	./out/harness out/spe_arm7_sim.elf out/syms.txt optest 130 || fail=1
fi
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
