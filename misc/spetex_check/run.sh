#!/bin/sh
# 5.12 #3 (WSL): ps3/spu/spe_tex_hash.h on SPU shims against the hash code of ps3_tex.cpp. Args: scenes (600).
# SPETEX_SOURCE: a mutated copy of spe_tex_hash.h (mutate.py); SPETEX_OUT: the build directory (/tmp/spetex).
set -e
cd "$(dirname "$0")/../.."
OUT=${SPETEX_OUT:-/tmp/spetex}
mkdir -p $OUT
tr -d '\r' < ps3/ps3_tex.cpp > $OUT/tex.cpp
awk '/misc\/spetex_check compiles the code from here/{f=1} f{print} /\/\/ spetex_check: end/{f=0}' $OUT/tex.cpp > $OUT/region.inc
grep -q 'spetex_check: end' $OUT/region.inc || { echo "FAIL: no reference region in ps3_tex.cpp"; exit 1; }
# The PPE's u64 VRAM load goes through BePtr (x86 is little-endian); the line must be there exactly once.
LD='const u64\* p = (const u64\*)&PVRPARAMS.vram\[addr\];'
[ "$(grep -c "$LD" $OUT/region.inc)" = 1 ] || { echo "FAIL: the u64 VRAM load line changed in ps3_tex.cpp"; exit 1; }
sed -i "s/$LD/BePtr p(\&PVRPARAMS.vram[addr]);/" $OUT/region.inc
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
gcc -O2 -g $SAN -Imisc/spetex_check/shim -Ips3/spu -I. -w \
	${SPETEX_SOURCE:+-DSPETEX_SOURCE="\"$SPETEX_SOURCE\""} -c misc/spetex_check/spu_side.c -o $OUT/spu_side.o
g++ -O2 -g $SAN -Imisc/spetex_check/stub -I$OUT -Ips3/spu -I. -w -c misc/spetex_check/spetex_check.cpp -o $OUT/drv.o
g++ $SAN $OUT/spu_side.o $OUT/drv.o -o $OUT/spetexcheck
$OUT/spetexcheck "${1:-600}"
