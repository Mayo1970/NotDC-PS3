#!/bin/sh
# OPTIMIZATION_PLAN 6.5 (WSL): the real dc/aica/aica_if.cpp against g2_check.cpp's model, 8 configurations x 3 seeds.
# SRC=<file> checks another copy of aica_if.cpp (mutate.sh).
cd "$(dirname "$0")"
SRC=${SRC:-../../dc/aica/aica_if.cpp}
g++ -std=c++17 -O2 -Wall -Wno-unused -Istub -I../.. -I../../dc/aica "$SRC" g2_check.cpp -o /tmp/g2chk || exit 1
fail=0
for s in 1 2 3; do
	/tmp/g2chk ${RUNS:-150} ${OPS:-3000} $s || fail=1
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
