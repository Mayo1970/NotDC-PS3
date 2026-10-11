#!/bin/sh
# Bundle 4 (WSL): ps3_rend.cpp's draw rules over a recording GL, against misc/ta4_check/ref_ps3_rend.inc (rows off)
# and a model (rows on). Args: scenes (4000), seed (0). REND_SOURCE: a mutated ps3_rend.cpp (mutate.py).
set -e
cd "$(dirname "$0")/../.."
OUT=${R4_OUT:-/tmp/rend4chk}
D=misc/rend4_check
rm -rf $OUT
mkdir -p $OUT/new $OUT/ref
extract() {
	tr -d '\r' < "$1" > $OUT/$2/rend.cpp
	awk '/^\/\/ Plane through the 3 background vertices/{f=1} /^\/\/ P18: the scene.s depth format and its clear value/{f=0} f{print}' \
		$OUT/$2/rend.cpp > $OUT/$2/region.inc
	: > $OUT/$2/structs.inc
	for s in DcVertex DcStrip ModTri ModVolParam DrawLists; do
		awk -v s="struct $s" '$0 == s {f=1} f{print} f && /^};$/ {f=0}' $OUT/$2/rend.cpp >> $OUT/$2/structs.inc
		grep -q "struct $s" $OUT/$2/structs.inc || { echo "FAIL: struct $s not found"; exit 1; }
	done
	grep -q 'static void draw_scene' $OUT/$2/region.inc || { echo "FAIL: no draw_scene in the region"; exit 1; }
}
extract "${REND_SOURCE:-ps3/ps3_rend.cpp}" new
extract misc/ta4_check/ref_ps3_rend.inc ref
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
CXX="g++ -O1 -g -ffp-contract=off $SAN -Imisc/spevb_check/stub -Ips3/spu -I$D -I. -w"
$CXX -I$OUT/new -c $D/side.cpp -o $OUT/new.o
$CXX -DR4_REF -I$OUT/ref -c $D/side.cpp -o $OUT/ref.o
$CXX -c $D/rend4_check.cpp -o $OUT/drv.o
g++ $SAN $OUT/drv.o $OUT/new.o $OUT/ref.o -o $OUT/rend4check -lm
$OUT/rend4check "${1:-4000}" "${2:-0}"
