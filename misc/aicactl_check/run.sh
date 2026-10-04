#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B1 (WSL): runs out/ac_test.elf (build_spu.sh first) in misc/spu_sim, 8 seeds.
# Seed bits 0-2 pick aica_fast, the KYONB mode and MutePcm16.
cd "$(dirname "$0")"
mkdir -p out
g++ -O2 -Wall -I../spu_sim harness.cpp ../spu_sim/spusim.c -o out/ac_harness || exit 1
OPS=${OPS:-4000}
fail=0
for s in 1 2 3 4 5 6 7 8; do
	./out/ac_harness out/ac_test.elf $s $OPS || fail=1
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
