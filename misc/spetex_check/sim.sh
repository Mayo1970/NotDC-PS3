#!/bin/sh
# 5.12 #3 (WSL): spe_tex in misc/spu_sim (image from build_sim.sh). Args: params textures kb seed requests.
# HOT=fn prints that function's hottest instruction words.
set -e
cd "$(dirname "$0")/../.."
OUT=/tmp/spetexsim
mkdir -p $OUT
tr -d '\r' < ps3/ps3_tex.cpp > $OUT/tex.cpp
awk '/misc\/spetex_check compiles the code from here/{f=1} f{print} /\/\/ spetex_check: end/{f=0}' $OUT/tex.cpp > $OUT/region.inc
LD='const u64\* p = (const u64\*)&PVRPARAMS.vram\[addr\];'
[ "$(grep -c "$LD" $OUT/region.inc)" = 1 ] || { echo "FAIL: the u64 VRAM load line changed in ps3_tex.cpp"; exit 1; }
sed -i "s/$LD/BePtr p(\&PVRPARAMS.vram[addr]);/" $OUT/region.inc
gcc -O2 -g -c misc/spu_sim/spusim.c -o $OUT/spusim.o
g++ -O2 -g -Imisc/spetex_check/stub -I$OUT -Imisc/spu_sim -Ips3/spu -I. -w \
	misc/spetex_check/sim_tex.cpp $OUT/spusim.o -o $OUT/sim_tex
$OUT/sim_tex misc/spetex_check/out/spe_tex_sim.elf "$@"
