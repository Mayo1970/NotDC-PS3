#!/bin/sh
# Plan 5.41: each planted bug in a copy of ps3/ps3_framehold.h must make framehold_check fail.
cd "$(dirname "$0")/../.."
caught=0
total=0
mutant() {
	total=$((total + 1))
	rm -rf /tmp/fh_mut && mkdir -p /tmp/fh_mut/ps3
	sed "$1" ps3/ps3_framehold.h > /tmp/fh_mut/ps3/ps3_framehold.h
	if cmp -s ps3/ps3_framehold.h /tmp/fh_mut/ps3/ps3_framehold.h; then
		echo "NOT APPLIED: $2"
		return
	fi
	g++ -std=c++14 -O2 -I/tmp/fh_mut -I. -o /tmp/fh_mut/check misc/framehold_check/framehold_check.cpp || return
	if /tmp/fh_mut/check > /tmp/fh_mut/out 2>&1; then
		echo "missed: $2"
	else
		caught=$((caught + 1))
		echo "caught: $2"
	fi
}
mutant 's/(m + p - 1) \/ p/m \/ p/' "absolute mapping rounds down"
mutant 's/(dm + p \/ 2) \/ p/dm \/ p/' "step count truncates"
mutant 's/if (prev_k)/if (0)/' "no step mapping"
mutant 's/e >= -p \/ 8 \&\& e < p + p \/ 8/e >= 0 \&\& e < p/' "no hysteresis"
mutant 's/const int64_t m = (int64_t)(mark - tb0);/const int64_t m = (int64_t)(mark - tb0) - p;/' "grid one refresh off"
mutant 's/k = kr;/k = kr + 1;/' "step mapping one vsync late"
echo "$caught of $total mutants caught"
[ "$caught" -eq "$total" ]
