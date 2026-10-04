#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B0 (WSL): arm7_spu.h against the real arm7_cached.cpp; full map, then the SPU's 8192 entries.
set -e
cd "$(dirname "$0")"
V=../../plugs/vbaARM
FLAGS="-std=c++17 -O2 -g -fno-strict-aliasing -Wall -Wno-unused -Istub -I$V -I../.."
g++ $FLAGS $V/arm7.cpp $V/arm7_cached.cpp harness.cpp -o /tmp/a7chk
g++ $FLAGS -DA7_MAP_N=8192u $V/arm7.cpp $V/arm7_cached.cpp harness.cpp -o /tmp/a7chk_small
fail=0
for s in 1 2 3 4 5 6; do
	/tmp/a7chk 400 $s > /tmp/a7chk_$s.log 2>&1 || fail=1
	grep -v "Unsupported ARM mode\|Undefined ARM instruction" /tmp/a7chk_$s.log
done
echo "--- 8192-entry map: an eviction re-decodes a block from another start, so ticks may differ (counted) ---"
/tmp/a7chk_small 400 9 2>&1 | grep -v "Unsupported ARM mode\|Undefined ARM instruction\|^  \|^    " || true
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
