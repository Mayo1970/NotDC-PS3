#!/bin/sh
# OPTIMIZATION_PLAN 6.6 (WSL): the twin's hazard map and exact test against what the render pass reads, 8 seeds.
# SRC=<file> checks another copy of aica_model.cpp (mutate.sh).
cd "$(dirname "$0")"
M=../../plugs/caiqueAICA
SRC=${SRC:-$M/aica_model.cpp}
g++ -std=c++17 -O2 -Wall -Wno-unused-function -DCAIQUE_RAM_HOOKS -I$M "$SRC" hz_check.cpp -o /tmp/hz_check || exit 1
fail=0
for s in 1 2 3 4 5 6 7 8; do
	/tmp/hz_check $s ${SAMPLES:-400000} || fail=1
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
