#!/bin/sh
# 5.12 #2 (WSL): spe_vb in misc/spu_sim (image from build_sim.sh). Args: op_strips tr_strips seed builds.
# HOT=fn prints that function's hottest instruction words.
set -e
cd "$(dirname "$0")/../.."
OUT=/tmp/spevbsim
mkdir -p $OUT
tr -d '\r' < ps3/ps3_rend.cpp > $OUT/rend.cpp
awk '/misc\/spevb_check compiles the code from here/{f=1} f{print} /\/\/ spevb_check: end/{f=0}' $OUT/rend.cpp > $OUT/region.inc
: > $OUT/structs.inc
for s in DcVertex DcStrip ModTri ModVolParam DrawLists; do
	awk -v s="struct $s" '$0 == s {f=1} f{print} f && /^};$/ {f=0}' $OUT/rend.cpp >> $OUT/structs.inc
done
gcc -O2 -g -c misc/spu_sim/spusim.c -o $OUT/spusim.o
g++ -O2 -g -ffp-contract=off -Imisc/spevb_check/stub -I$OUT -Imisc/spu_sim -Ips3/spu -I. -w \
	misc/spevb_check/sim_vb.cpp $OUT/spusim.o -o $OUT/sim_vb -lm
$OUT/sim_vb misc/spevb_check/out/spe_vb_sim.elf "$@"
