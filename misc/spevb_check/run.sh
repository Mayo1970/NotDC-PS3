#!/bin/sh
# 5.12 #2 (WSL): ps3/spu/spe_vb_build.h on SPU shims against the merge code of ps3_rend.cpp. Args: scenes (3000).
# SPEVB_SOURCE: a mutated copy of spe_vb_build.h (mutate.py); REND_SOURCE: of ps3_rend.cpp (misc/bump_check/mutate.py).
set -e
cd "$(dirname "$0")/../.."
OUT=/tmp/spevb
mkdir -p $OUT
tr -d '\r' < "${REND_SOURCE:-ps3/ps3_rend.cpp}" > $OUT/rend.cpp
awk '/misc\/spevb_check compiles the code from here/{f=1} f{print} /\/\/ spevb_check: end/{f=0}' $OUT/rend.cpp > $OUT/region.inc
: > $OUT/structs.inc
for s in DcVertex DcStrip ModTri ModVolParam DrawLists; do
	awk -v s="struct $s" '$0 == s {f=1} f{print} f && /^};$/ {f=0}' $OUT/rend.cpp >> $OUT/structs.inc
done
grep -q 'spevb_check: end' $OUT/region.inc || { echo "FAIL: no reference region in ps3_rend.cpp"; exit 1; }
for s in DcVertex DcStrip ModTri ModVolParam DrawLists; do
	grep -q "struct $s" $OUT/structs.inc || { echo "FAIL: struct $s not found"; exit 1; }
done
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
gcc -O2 -g -std=gnu11 -ffp-contract=off $SAN -Imisc/spevb_check/shim -Ips3/spu -I. -w \
	${SPEVB_SOURCE:+-DSPEVB_SOURCE="\"$SPEVB_SOURCE\""} -c misc/spevb_check/spu_side.c -o $OUT/spu_side.o
g++ -O2 -g -ffp-contract=off $SAN -Imisc/spevb_check/stub -I$OUT -Ips3/spu -I. -w \
	-c misc/spevb_check/spevb_check.cpp -o $OUT/drv.o
g++ $SAN $OUT/spu_side.o $OUT/drv.o -o $OUT/spevbcheck -lm
$OUT/spevbcheck "${1:-3000}"
