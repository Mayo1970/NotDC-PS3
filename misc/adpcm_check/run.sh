#!/bin/sh
# OPTIMIZATION_PLAN 6.9 B (WSL): the ADPCM Loop row. One voice against Flycast's rule (adpcm_check), then run() against
# advance() with the row on and live (misc/spu_sim diff_adv). SRC=<file>: another copy of aica_model.cpp (mutate.py).
cd "$(dirname "$0")"
M=../../plugs/caiqueAICA
SRC=${SRC:-$M/aica_model.cpp}
S=../spu_sim
CXX="g++ -std=c++17 -O2 -Wall -Wno-unused-function"
$CXX -I$M "$SRC" adpcm_check.cpp -o /tmp/adpcm_check || exit 1
$CXX -DHAVE_ADVANCE -I$M -c "$SRC" -o /tmp/adpcm_m.o && $CXX -DHAVE_ADVANCE -DPFX=cur_ -I$M -c $S/model_wrap.cpp -o /tmp/adpcm_w.o || exit 1
make -s -C $S out/base_model.o out/base_wrap.o > /dev/null || exit 1
$CXX -o /tmp/adpcm_diff $S/diff_adv.cpp $S/out/base_model.o $S/out/base_wrap.o /tmp/adpcm_m.o /tmp/adpcm_w.o || exit 1
fail=0
for s in 1 2 3 4 5 6 7 8; do
	/tmp/adpcm_check $s ${CASES:-300} || fail=1
done
# diff_adv: the old model (the row has no say there: RESET must be the old rule), then the tree's run() with RESTORE and live
for a in 0 1 2; do
	for s in 21 22 23; do
		out=$(ADPCM=$a /tmp/adpcm_diff ${EPI:-120} $s) || fail=1
		echo "diff_adv ADPCM=$a seed $s: $out" | cut -c1-150
	done
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
