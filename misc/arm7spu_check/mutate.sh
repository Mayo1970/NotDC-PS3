#!/bin/sh
# OPTIMIZATION_PLAN 6.5 / P44 (WSL): each mutation of arm7_spu.h's race and reset code must make the B0 check fail.
cd "$(dirname "$0")"
V=../../plugs/vbaARM
H=../../ps3/spu/arm7_spu.h
FLAGS="-std=c++17 -O2 -fno-strict-aliasing -w -Istub -I$V -I../.."
mkdir -p /tmp/a7mut
sed 's#"../../ps3/spu/arm7_spu.h"#"/tmp/a7mut/arm7_spu.h"#' harness.cpp > /tmp/a7mut/harness.cpp
caught=0
total=0
try() {
	total=$((total + 1))
	sed "$2" $H > /tmp/a7mut/arm7_spu.h
	if cmp -s /tmp/a7mut/arm7_spu.h $H; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	g++ $FLAGS -I. $V/arm7.cpp $V/arm7_cached.cpp /tmp/a7mut/harness.cpp -o /tmp/a7mut/chk || { echo "BUILD FAILED: $1"; return; }
	if /tmp/a7mut/chk 120 3 > /tmp/a7mut/run.log 2>&1; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try "extension of 40 ticks" 's/const int32_t lim = a->budget - 50;/const int32_t lim = a->budget - 40;/'
try "no race check after STR" 's/^\tA7_RACE(a, ticks);$/\t;/'
try "no race check after an STM word" 's/^\t\tA7_RACE(a, ticks);$/\t\t;/'
try "no race check after SWP" 's/A7_RACE(a, \&ticks);/;/'
try "race check before the store's charge moved" 's/^\tA7_RACE(a, ticks);$/\t*ticks += 3; A7_RACE(a, ticks); *ticks -= 3;/'
try "extensions without limit" 's/^\t\ta->race_done = 1;$/\t\t;/'
try "the limit kept across slices" 's/^\ta->race_done = 0;$/\t;/'
try "unsigned slice-end test" 's/if (ticks >= (int32_t)budget)/if ((uint32_t)ticks >= budget)/'
try "reset stays in System mode" 's/a->mode = a->reset_svc ? 0x13 : 0x1F;/a->mode = 0x1F;/'
try "reset in SVC always" 's/a->mode = a->reset_svc ? 0x13 : 0x1F;/a->mode = 0x13;/'
echo "$caught of $total mutations caught"
